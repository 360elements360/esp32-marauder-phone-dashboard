"""Catalog-driven tools exercise forms and device-owned execution without RF."""
import json
import unittest
from urllib.parse import urlparse
import test_workflows_browser as workflows
job = workflows.job

TOOLS = [
 dict(id='wifi_probe', title='Watch probe requests', description='Watch Wi-Fi requests.', group='Wi-Fi', available=True, timed=True, fields=[dict(name='filter',label='Network name filter',type='text',maxLength=32)],commands=['sniffprobe']),
 dict(id='lan_ping', title='Ping a network host', description='Check a discovered host.',group='Network',available=True,timed=False,fields=[dict(name='host',label='Network host',type='target',kind='ip',required=True,discover='lan_discover'),dict(name='count',label='Ping count',type='integer',default=4,min=1,max=10,required=True)],commands=['ping']),
 dict(id='lan_discover', title='Discover network hosts', description='Discover connected hosts.',group='Network',available=True,timed=False,fields=[],commands=['scanlocal']),
 dict(id='ble_watch', title='Watch Bluetooth advertisements',description='Observe BLE data.',group='Bluetooth',available=True,timed=True,fields=[dict(name='mode',label='Advertisement type',type='select',required=True,default='all',options=[dict(value='all',label='All advertisements'),dict(value='vendor',label='Vendor advertisements')])],commands=['sniffbt']),
 dict(id='device_setting',title='Change a device setting',description='Apply a setting.',group='Device',available=True,timed=False,fields=[dict(name='enabled',label='Enable recording',type='boolean',default=False),dict(name='password',label='Network password',type='password',maxLength=64)],commands=['settings']),
 dict(id='gps',title='GPS information',description='Read GPS.',group='Hardware',available=False,reason='GPS receiver is not attached.',timed=False,fields=[],commands=['gps'])
]

class ToolBrowserTest(unittest.TestCase):
    setUpClass = classmethod(workflows.WorkflowBrowserTest.setUpClass.__func__)
    tearDownClass = classmethod(workflows.WorkflowBrowserTest.tearDownClass.__func__)
    tearDown = workflows.WorkflowBrowserTest.tearDown
    login = workflows.WorkflowBrowserTest.login
    choose = workflows.WorkflowBrowserTest.choose
    review = workflows.WorkflowBrowserTest.review
    refresh = workflows.WorkflowBrowserTest.refresh
    # Inherited regression methods are run separately by test_workflows_browser.
    def setUp(self):
        workflows.WorkflowBrowserTest.setUp(self)
        self.page.route('**/tools.js',lambda route:route.fulfill(body='window.MARAUDER_TOOLS='+json.dumps(TOOLS)+';',content_type='text/javascript'))
        self.page.reload()

    def route(self,route):
        if urlparse(route.request.url).path=='/api/workflow' and route.request.method=='POST':
            payload=route.request.post_data_json
            if payload.get('kind')=='tool':
                self.starts.append(payload)
                if self.reject: return route.fulfill(status=self.reject,json={'error':'Target is no longer available'})
                self.workflow=job(id=self.workflow['id']+1,kind='tool',tool=payload['tool'],state='queued',seconds=payload['seconds'],remaining=payload['seconds'],logStart=0)
                if self.lose_receipt:return route.abort()
                return route.fulfill(status=202,json=self.workflow)
        return workflows.WorkflowBrowserTest.route(self,route)

    def test_catalog_search_and_unavailable_reason(self):
        self.login()
        self.page.get_by_label('Search tasks').fill('GPS')
        self.choose('GPS information')
        self.page.get_by_text('GPS receiver is not attached.',exact=True).wait_for()
        self.assertEqual(self.page.get_by_role('button',name='Run task',exact=True).count(),0)
        self.assertEqual(self.starts,[])

    def test_typed_ble_form_and_explicit_review(self):
        self.login();self.choose('Watch Bluetooth advertisements')
        self.page.get_by_label('Advertisement type').select_option('vendor')
        self.review(6)
        self.assertEqual(self.starts,[])
        self.choose('Run task');self.page.get_by_role('heading',name='Task in progress').wait_for()
        self.assertEqual(self.starts,[dict(action='start',kind='tool',tool='ble_watch',inputs={'mode':'vendor'},seconds=6,channel=0)])

    def test_ip_target_prerequisite_resumes_original_form_after_reload(self):
        self.targets=[];self.login();self.choose('Ping a network host')
        self.choose('Choose Network host');self.choose('Discover devices first');self.review();self.choose('Run task')
        self.page.get_by_role('heading',name='Task in progress').wait_for();self.page.reload()
        self.targets=[dict(id='192.168.4.8',name='<img src=x onerror=alert(1)>',rssi=0,channel=0)]
        self.workflow={**self.workflow,'state':'completed'};self.refresh()
        self.choose('<img src=x onerror=alert(1)>')
        self.page.get_by_label('Ping count').fill('7');self.review();self.choose('Run task')
        self.page.get_by_role('heading',name='Task in progress').wait_for()
        self.assertEqual(self.starts[-1]['tool'],'lan_ping')
        self.assertEqual(self.starts[-1]['inputs'],{'host':'192.168.4.8','count':7})
        self.assertEqual(self.target_requests[-1]['kind'],['ip'])
        self.assertEqual(self.page.locator('#tab-tasks img').count(),0)

    def test_management_masks_secret_and_shows_bounded_output(self):
        self.login();self.choose('Change a device setting')
        self.page.get_by_label('Enable recording').check();self.page.get_by_label('Network password').fill('private-value')
        self.review()
        self.assertNotIn('private-value',self.page.locator('#tab-tasks').inner_text())
        self.choose('Run task');self.page.get_by_role('heading',name='Task in progress').wait_for()
        self.assertEqual(self.starts[-1]['inputs'],{'enabled':True,'password':'private-value'})
        self.workflow={**self.workflow,'state':'completed'};self.refresh()
        self.page.get_by_role('heading',name='Task complete').wait_for()
        self.page.get_by_label('Task output').get_by_text('Ready',exact=False).wait_for()
        self.assertEqual(self.page.locator('#tab-tasks img').count(),0)

    def test_wifi_text_form_and_validation(self):
        self.login();self.choose('Watch probe requests')
        self.page.get_by_label('Network name filter').fill('Studio Wi-Fi')
        self.review(301)
        self.assertEqual(self.starts,[])
        self.page.get_by_text('Choose a whole-number duration',exact=False).wait_for()
        self.review(9);self.choose('Run task')
        self.page.get_by_role('heading',name='Task in progress').wait_for()
        self.assertEqual(self.starts[-1],dict(action='start',kind='tool',tool='wifi_probe',inputs={'filter':'Studio Wi-Fi'},seconds=9,channel=0))

    def test_generic_uncertain_receipt_reload_never_resends(self):
        self.login();self.choose('Watch Bluetooth advertisements');self.review(5)
        self.lose_receipt=True;self.choose('Run task')
        self.page.get_by_text('Receipt not confirmed.',exact=False).wait_for()
        self.page.reload();self.page.get_by_role('heading',name='Task in progress').wait_for()
        self.assertEqual(len(self.starts),1)
        self.assertIn('Watch Bluetooth advertisements',self.page.locator('#flow-job').inner_text())

    def test_setup_draft_preserves_inputs_without_sending(self):
        self.login();self.choose('Watch probe requests')
        self.page.get_by_label('Network name filter').fill('Persist this name')
        self.page.get_by_label('Duration in seconds').fill('17')
        self.page.reload()
        self.page.get_by_label('Network name filter').wait_for()
        self.assertEqual(self.page.get_by_label('Network name filter').input_value(),'Persist this name')
        self.assertEqual(self.page.get_by_label('Duration in seconds').input_value(),'17')
        self.assertEqual(self.starts,[])

    def test_server_rejection_preserves_review_without_retry(self):
        self.login();self.choose('Watch probe requests');self.review()
        self.reject=409;self.choose('Run task')
        self.page.get_by_text('Target is no longer available',exact=True).wait_for()
        self.page.get_by_role('heading',name='Ready to run?').wait_for()
        self.refresh();self.page.wait_for_timeout(100)
        self.assertEqual(len(self.starts),1)

    def test_production_catalog_every_card_opens_guided_surface(self):
        self.page.unroute('**/tools.js');self.page.reload();self.login()
        rows=self.page.evaluate('window.MARAUDER_TOOLS')
        self.assertEqual(self.page.locator('[data-tool]').count(),len(rows))
        self.assertGreater(len(rows),100)
        self.page.set_viewport_size({'width':320,'height':740})
        self.assertTrue(self.page.evaluate('document.documentElement.scrollWidth <= innerWidth'))
        self.page.screenshot(path=str(workflows.ROOT/'.cache/all-tools-mobile.png'),full_page=False)
        for row in rows:
            self.page.locator('[data-tool="'+row['id']+'"]').click()
            if row.get('view')=='files':
                self.page.get_by_role('button',name='Tasks',exact=True).click()
                continue
            self.page.get_by_test_id('flow-home').wait_for()
            if not row['available']:
                self.page.get_by_text(row['reason'],exact=True).wait_for()
                self.assertEqual(self.page.get_by_role('button',name='Run task',exact=True).count(),0)
            self.assertTrue(self.page.evaluate('document.documentElement.scrollWidth <= innerWidth'),row['id'])
            self.page.get_by_test_id('flow-home').click()
        self.assertEqual(self.starts,[])
        self.assertEqual(self.errors,[])

    def test_production_channel_and_management_payloads(self):
        self.page.unroute('**/tools.js');self.page.reload();self.login()
        self.page.locator('[data-tool="wifi_beacons"]').click()
        self.page.get_by_label('Radio channel',exact=True).select_option('6')
        self.review(3);self.choose('Run task')
        self.page.get_by_role('heading',name='Task in progress').wait_for()
        self.assertEqual(self.starts[-1],dict(action='start',kind='tool',tool='wifi_beacons',inputs={},seconds=3,channel=6))
        self.workflow={**self.workflow,'state':'completed'};self.refresh()
        self.page.get_by_role('heading',name='Task complete').wait_for();self.choose('Choose another task')
        self.page.locator('[data-tool="device_info"]').click();self.review();self.choose('Run task')
        self.page.get_by_role('heading',name='Task in progress').wait_for()
        self.assertEqual(self.starts[-1],dict(action='start',kind='tool',tool='device_info',inputs={},seconds=1,channel=0))

    def test_production_csi_side_control_preserves_running_receipt(self):
        self.workflow=job(id=81,kind='tool',tool='wifi_csi',state='running',seconds=30,remaining=20,logStart=0)
        self.page.unroute('**/tools.js');self.page.reload();self.login()
        def side(route):
            payload=route.request.post_data_json;self.starts.append(payload)
            if self.lose_receipt:return route.abort()
            return route.fulfill(json={'workflow':self.workflow,'side':True,'logStart':0})
        self.page.route('**/api/workflow',side)
        self.page.get_by_text('Task controls',exact=True).click();self.choose('Read CSI motion status')
        self.review();self.choose('Run task')
        self.page.get_by_text('Read CSI motion status command accepted.',exact=False).wait_for()
        self.assertEqual(self.page.evaluate("JSON.parse(sessionStorage.getItem('marauder-flow')).id"),81)
        self.assertEqual(self.starts[-1]['tool'],'csi_status')
        self.page.get_by_text('Task controls',exact=True).click();self.choose('Read CSI motion status');self.review()
        self.lose_receipt=True;self.choose('Run task')
        self.page.get_by_text('Control request receipt was not confirmed.',exact=False).wait_for()
        self.page.reload();self.page.get_by_role('heading',name='Task in progress').wait_for()
        self.assertEqual(self.page.evaluate("JSON.parse(sessionStorage.getItem('marauder-flow')).id"),81)
        self.assertEqual(len(self.starts),2)

    def test_production_multiple_targets_use_mac_identities(self):
        self.page.unroute('**/tools.js');self.page.reload();self.login()
        self.page.locator('[data-tool="wifi_pmkid_active"]').click()
        self.choose('Choose Wi-Fi network')
        self.page.get_by_label('Studio Wi-Fi',exact=False).check()
        self.choose('Use selected targets');self.review(2);self.choose('Run task')
        self.page.get_by_role('heading',name='Task in progress').wait_for()
        self.assertEqual(self.starts[-1]['inputs'],{'target':['12:34:56:78:90:AB']})
        self.assertEqual(self.starts[-1]['tool'],'wifi_pmkid_active')
        self.assertEqual(self.commands,[])

    def production_tool(self, tool_id):
        self.page.unroute('**/tools.js');self.page.reload();self.login()
        self.page.locator('[data-tool="'+tool_id+'"]').click()

    def test_name_filter_uses_friendly_match_controls(self):
        self.production_tool('select_filter')
        self.page.get_by_label('Match',exact=True).select_option('equals')
        self.page.get_by_label('Network name',exact=True).fill('Studio Wi-Fi')
        self.review();self.choose('Run task')
        self.page.get_by_role('heading',name='Task in progress').wait_for()
        self.assertEqual(self.starts[-1]['inputs'],{'filter':'equals Studio Wi-Fi'})

    def test_html_upload_chunks_utf8_and_preserves_auth(self):
        chunks=[]
        def upload(route):
            payload=route.request.post_data_json;chunks.append(payload)
            self.assertEqual(route.request.headers.get('x-device-key'),'test-password')
            self.assertLessEqual(len(route.request.post_data.encode('utf-8')),512)
            self.assertLessEqual(len(payload['data'].encode('utf-8')),256)
            return route.fulfill(json={'path':payload['path'],'offset':payload['offset']+len(payload['data'].encode('utf-8')),'complete':payload['final']})
        self.page.route('**/api/template',upload)
        self.production_tool('portal_template')
        html='\ufeff<html>'+('😀"\\\n' * 130)+'<script>window.uploadExecuted=true</script></html>'
        self.page.get_by_label('Upload HTML page',exact=True).set_input_files({'name':'custom.html','mimeType':'text/html','buffer':html.encode('utf-8')})
        self.page.get_by_text('custom.html is ready.',exact=False).wait_for()
        self.assertEqual(''.join(chunk['data'] for chunk in chunks),html)
        self.assertEqual(chunks[-1]['total'],len(html.encode('utf-8')))
        self.assertTrue(chunks[-1]['final'])
        offset=0
        for chunk in chunks:
            self.assertEqual(chunk['offset'],offset);offset+=len(chunk['data'].encode('utf-8'))
        self.assertIsNone(self.page.evaluate('window.uploadExecuted'))
        self.assertEqual(self.page.get_by_label('HTML filename',exact=True).input_value(),'custom.html')
        self.review();self.choose('Run task')
        self.page.get_by_role('heading',name='Task in progress').wait_for()
        self.assertEqual(self.starts[-1]['inputs'],{'file':'custom.html'})

    def test_html_upload_failure_never_retries_or_selects_partial_file(self):
        chunks=[]
        def upload(route):
            chunks.append(route.request.post_data_json)
            if len(chunks)==2:return route.abort()
            payload=chunks[-1]
            return route.fulfill(json={'path':payload['path'],'offset':payload['offset']+len(payload['data'].encode('utf-8')),'complete':False})
        self.page.route('**/api/template',upload);self.production_tool('portal_template')
        self.page.get_by_label('Upload HTML page',exact=True).set_input_files({'name':'partial.html','mimeType':'text/html','buffer':b'x'*700})
        self.page.get_by_text('Upload not confirmed.',exact=False).wait_for()
        self.assertEqual(len(chunks),2)
        self.assertEqual(self.page.get_by_label('HTML filename',exact=True).input_value(),'index.html')
        self.assertEqual(self.starts,[])
        self.review()
        self.assertIn('index.html',self.page.locator('#tab-tasks').inner_text())

    def test_html_upload_rejects_oversize_before_post(self):
        chunks=[];self.page.route('**/api/template',lambda route:chunks.append(route.request.post_data_json))
        self.production_tool('portal_template')
        self.page.get_by_label('Upload HTML page',exact=True).set_input_files({'name':'large.html','mimeType':'text/html','buffer':b'x'*8193})
        self.page.get_by_text('Choose an HTML file from 1 to 8192 bytes.',exact=True).wait_for()
        self.assertEqual(chunks,[])
        self.assertEqual(self.page.get_by_label('HTML filename',exact=True).input_value(),'index.html')

    def test_html_upload_blocks_review_until_final_acknowledgement(self):
        pending=[]
        self.page.route('**/api/template',lambda route:pending.append(route))
        self.production_tool('portal_template')
        with self.page.expect_request('**/api/template'):
            self.page.get_by_label('Upload HTML page',exact=True).set_input_files({'name':'ready.HTML','mimeType':'text/html','buffer':b'<html>Ready</html>'})
        self.assertTrue(self.page.get_by_role('button',name='Review task',exact=True).is_disabled())
        self.assertEqual(self.starts,[])
        self.page.wait_for_timeout(50)
        payload=pending[0].request.post_data_json
        pending[0].fulfill(json={'path':'/'+payload['path'],'offset':len(payload['data'].encode('utf-8')),'complete':True})
        self.page.get_by_text('ready.html is ready.',exact=False).wait_for()
        self.assertTrue(self.page.get_by_role('button',name='Review task',exact=True).is_enabled())

    def test_html_upload_mismatched_acknowledgement_stops(self):
        chunks=[]
        def upload(route):
            payload=route.request.post_data_json;chunks.append(payload)
            return route.fulfill(json={'path':payload['path'],'offset':1,'complete':False})
        self.page.route('**/api/template',upload);self.production_tool('portal_template')
        self.page.get_by_label('Upload HTML page',exact=True).set_input_files({'name':'wrong.html','mimeType':'text/html','buffer':b'x'*700})
        self.page.get_by_text('Upload acknowledgement did not match the file.',exact=False).wait_for()
        self.assertEqual(len(chunks),1)
        self.assertEqual(self.page.get_by_label('HTML filename',exact=True).input_value(),'index.html')

    def test_terminal_output_drains_all_log_pages_then_stops(self):
        from urllib.parse import parse_qs
        text='BEGIN:'+('x'*2300)+':FINAL RESULT'
        cursors=[]
        def log(route):
            cursor=int(parse_qs(urlparse(route.request.url).query)['cursor'][0]);cursors.append(cursor)
            chunk=text[cursor:cursor+1024]
            return route.fulfill(json={'cursor':cursor+len(chunk),'text':chunk,'lost':False})
        self.page.route('**/api/log?*',log)
        self.workflow=job(id=31,kind='tool',tool='device_setting',state='completed',seconds=1,logStart=0)
        # A previously tracked receipt reopens a completed result on reconnect.
        self.page.evaluate("sessionStorage.setItem('marauder-flow',JSON.stringify({boot:42,id:31,kind:'tool',tool:'device_setting'}))")
        self.page.reload();self.login()
        self.page.get_by_label('Task output',exact=True).get_by_text(':FINAL RESULT',exact=False).wait_for()
        self.assertEqual(self.page.get_by_label('Task output',exact=True).text_content(),text)
        self.refresh();self.page.wait_for_timeout(150)
        self.assertEqual(cursors.count(len(text)),1)
        self.assertLessEqual(len(cursors),7)
        self.assertEqual(self.starts,[])

    def test_disconnected_lan_guides_join_and_resumes_after_reload(self):
        joined=[False]
        def workflow(route):
            payload=route.request.post_data_json
            if payload.get('tool')=='lan_arp' and not joined[0]:
                self.starts.append(payload)
                return route.fulfill(status=409,json={'error':'Join Wi-Fi before this tool'})
            return self.route(route)
        self.page.route('**/api/workflow',workflow)
        self.production_tool('lan_arp');self.review(3);self.choose('Run task')
        self.page.get_by_text('Join Wi-Fi before this tool',exact=True).wait_for()
        self.choose('Connect to Wi-Fi first')
        self.choose('Choose Wi-Fi network');self.choose('Studio Wi-Fi')
        self.page.get_by_label('Network password',exact=True).fill('private-password')
        self.review();self.assertNotIn('private-password',self.page.locator('#tab-tasks').inner_text())
        self.choose('Run task');self.page.get_by_role('heading',name='Task in progress').wait_for()
        self.page.reload();self.page.get_by_role('heading',name='Task in progress').wait_for()
        joined[0]=True;self.workflow={**self.workflow,'state':'completed'};self.refresh()
        self.page.get_by_role('heading',name='Discover hosts with ARP',exact=True).wait_for()
        self.assertEqual(len(self.starts),2)
        self.assertEqual(self.page.get_by_label('Duration in seconds').input_value(),'3')
        self.review(3);self.choose('Run task');self.page.get_by_role('heading',name='Task in progress').wait_for()
        self.assertEqual([row['tool'] for row in self.starts],['lan_arp','network_join','lan_arp'])

    def test_lan_target_join_chain_survives_wifi_discovery_and_reload(self):
        self.targets=[];self.production_tool('lan_ports')
        self.choose('Choose Network host');self.choose('Discover devices first')
        self.choose('Connect to Wi-Fi first');self.choose('Choose Wi-Fi network')
        self.choose('Discover devices first');self.review(2);self.choose('Run task')
        self.page.get_by_role('heading',name='Task in progress').wait_for();self.page.reload()
        self.page.get_by_role('heading',name='Task in progress').wait_for()
        self.targets=[{'id':'12:34:56:78:90:AB','name':'Studio Wi-Fi','rssi':-48,'channel':6,'security':'WPA2'}]
        self.workflow={**self.workflow,'state':'completed'};self.refresh();self.choose('Studio Wi-Fi')
        self.page.get_by_label('Network password',exact=True).fill('private-password')
        self.review();self.choose('Run task');self.page.get_by_role('heading',name='Task in progress').wait_for()
        self.page.reload();self.page.get_by_role('heading',name='Task in progress').wait_for()
        self.targets=[];self.workflow={**self.workflow,'state':'completed'};self.refresh()
        self.page.get_by_role('heading',name='Choose Network host',exact=True).wait_for()
        self.assertEqual(len(self.starts),2)
        self.choose('Discover devices first');self.review(2);self.choose('Run task')
        self.page.get_by_role('heading',name='Task in progress').wait_for()
        self.targets=[{'id':'192.168.1.8','name':'Office host','rssi':0,'channel':0}]
        self.workflow={**self.workflow,'state':'completed'};self.refresh();self.choose('Office host')
        self.review(2);self.choose('Run task');self.page.get_by_role('heading',name='Task in progress').wait_for()
        self.assertEqual([(row['kind'],row.get('tool')) for row in self.starts],[('wifi_discover',None),('tool','network_join'),('tool','lan_arp'),('tool','lan_ports')])
        self.assertEqual(self.starts[-1]['inputs'],{'target':'192.168.1.8'})

if __name__=='__main__':unittest.main(verbosity=2)
