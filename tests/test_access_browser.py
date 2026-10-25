"""Phone onboarding and password rotation use the production page."""
import json
import sys
import test_browser as base

class AccessBrowserTest(base.DashboardBrowserTest):
    # Inherited tests exercise the same fixtures; limit discovery to this class's additions.
    def setUp(self):
        self.secret = 'test-password'
        self.setup_required = False
        self.access_posts = []
        self.drop_receipt = False
        self.never_received = False
        self.reject_save = False
        super().setUp()

    def route(self, route):
        path = route.request.url.split('/api/')[1].split('?')[0]
        if route.request.headers.get('x-device-key') != self.secret:
            return route.fulfill(status=401, json={'error':'Wrong device password'})
        if path == 'status':
            return route.fulfill(json=dict(version='v1.19.2',board='ESP32-S3 N16R8',bootId=42,uptime=123,heap=180000,minHeap=160000,psram=8000000,flash=16777216,mode=0,channel=1,remaining=0,queued=0,ssid='Marauder-TEST',clients=1,logCapacity=8192,setupRequired=self.setup_required))
        if path == 'access':
            self.access_posts.append(route.request.post_data_json)
            if self.never_received: return route.abort()
            if self.reject_save: return route.fulfill(status=500,json={'error':'Could not save password'})
            self.secret = route.request.post_data_json['password']
            self.setup_required = False
            if self.drop_receipt: return route.abort()
            return route.fulfill(json={'message':'Password saved'})
        # Existing fixtures expect their test credential on unrelated requests.
        if path == 'log': return route.fulfill(json={'cursor':100,'lost':False,'text':'Ready\n'})
        return super().route(route)

    def open_access(self):
        self.login()
        self.page.get_by_role('button',name='Settings',exact=True).click()
        self.page.get_by_role('button',name='Wi-Fi access',exact=True).click()

    def fill_password(self, value, confirm=None):
        self.page.get_by_label('New password',exact=True).fill(value)
        self.page.get_by_label('Confirm password',exact=True).fill(value if confirm is None else confirm)

    def test_phone_first_setup_requires_own_password(self):
        self.secret = 'marauder-setup'; self.setup_required = True
        self.page.get_by_role('button',name='Set up new device',exact=True).click()
        dialog = self.page.get_by_role('dialog')
        dialog.wait_for()
        self.page.keyboard.press('Escape')
        self.assertTrue(dialog.is_visible())
        self.fill_password('My phone pass!')
        self.page.get_by_role('button',name='Save password',exact=True).click()
        self.page.get_by_text('Connected with your new password.',exact=True).wait_for()
        self.assertEqual(self.access_posts,[{'password':'My phone pass!'}])
        self.assertEqual(self.secret,'My phone pass!')

    def test_show_password_change_validation_and_mobile(self):
        self.open_access()
        field=self.page.get_by_label('Current password',exact=True)
        self.assertEqual(field.input_value(),'test-password')
        self.assertEqual(field.get_attribute('type'),'password')
        self.page.get_by_label('Show passwords',exact=True).check()
        self.assertEqual(field.get_attribute('type'),'text')
        self.fill_password('new phone password','mismatch')
        self.page.get_by_role('button',name='Save password',exact=True).click()
        self.assertEqual(self.access_posts,[])
        self.page.get_by_text('Passwords do not match.',exact=True).wait_for()
        self.page.set_viewport_size({'width':390,'height':844})
        self.assertTrue(self.page.evaluate('document.documentElement.scrollWidth <= innerWidth'))

    def test_failed_save_keeps_old_password(self):
        self.open_access(); self.reject_save=True
        self.fill_password('new phone password')
        self.page.get_by_role('button',name='Save password',exact=True).click()
        self.page.get_by_text('Could not save password',exact=True).wait_for()
        self.assertEqual(self.page.evaluate('sessionStorage.getItem("marauder-key")'),'test-password')

    def test_lost_receipt_checks_new_password_without_resending(self):
        self.open_access();self.drop_receipt=True
        self.fill_password('new phone password')
        self.page.get_by_role('button',name='Save password',exact=True).click()
        self.page.get_by_text('Connected with your new password.',exact=True).wait_for()
        self.assertEqual(len(self.access_posts),1)

    def test_packed_catalog_roundtrips_every_metadata_field(self):
        self.page.route('**/packed',lambda route:route.fulfill(content_type='text/html',path=str(base.ROOT/'.cache/dashboard.html')))
        self.page.goto(f'http://127.0.0.1:{self.server.server_port}/packed')
        self.login()
        sys.path.insert(0,str(base.ROOT/'scripts'))
        from tool_catalog import catalog
        expected=[{k:v for k,v in row.items() if k not in ('steps','modes','select','check','secret')} for row in catalog()]
        self.assertEqual(self.page.evaluate('window.MARAUDER_TOOLS'),expected)
        self.page.get_by_role('button',name='Settings',exact=True).click()
        self.page.get_by_role('button',name='Wi-Fi access',exact=True).click()
        self.fill_password('packed phone password')
        self.page.get_by_role('button',name='Save password',exact=True).click()
        self.page.get_by_text('Connected with your new password.',exact=True).wait_for()
        self.page.reload()
        self.page.locator('#connection').get_by_text('Connected',exact=True).wait_for()
        self.assertEqual(len(self.access_posts),1)

    def test_unsent_change_recovers_previous_password_without_retry(self):
        self.open_access(); self.never_received=True
        self.fill_password('new phone password')
        self.page.get_by_role('button',name='Save password',exact=True).click()
        self.page.get_by_text('Change was not applied. Your previous password still works.',exact=True).wait_for()
        self.assertEqual(len(self.access_posts),1)
        self.assertEqual(self.page.evaluate('sessionStorage.getItem("marauder-key")'),'test-password')

    def test_change_waits_for_inflight_status_poll(self):
        self.open_access(); self.fill_password('new phone password')
        self.page.evaluate("""() => {
          const original=window.fetch; let delay=true;
          window.fetch=(url,options)=>{
            if(url==='/api/status' && delay){delay=false; window.statusInFlight=true; return new Promise(resolve=>setTimeout(()=>resolve(original(url,options)),700));}
            return original(url,options);
          };
          document.dispatchEvent(new Event('visibilitychange'));
        }""")
        self.page.wait_for_function('window.statusInFlight === true')
        self.page.get_by_role('button',name='Save password',exact=True).click()
        self.page.get_by_text('Connected with your new password.',exact=True).wait_for()
        self.assertEqual(len(self.access_posts),1)

# Avoid duplicating the inherited dashboard tests in this module.
for _name in list(base.DashboardBrowserTest.__dict__):
    if _name.startswith('test_') and _name not in AccessBrowserTest.__dict__:
        setattr(AccessBrowserTest,_name,None)
