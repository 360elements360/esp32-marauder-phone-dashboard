"""Guided tasks use the exact device workflow/target contract, without a radio."""
import json
import re
import unittest
from urllib.parse import parse_qs, urlparse
import test_browser as browser_tests
ROOT = browser_tests.ROOT


def job(**fields):
    return dict(id=0, kind='', state='idle', seconds=0, elapsed=0, remaining=0,
                target='', targetName='', channel=0, file='', error='', count=0,
                rssi=0, **fields) if not fields else {**job(), **fields}


class WorkflowBrowserTest(unittest.TestCase):
    setUpClass = classmethod(browser_tests.DashboardBrowserTest.setUpClass.__func__)
    tearDownClass = classmethod(browser_tests.DashboardBrowserTest.tearDownClass.__func__)
    tearDown = browser_tests.DashboardBrowserTest.tearDown
    login = browser_tests.DashboardBrowserTest.login

    def setUp(self):
        self.workflow = job()
        self.boot = 42
        self.starts = []; self.cancels = []; self.target_requests = []
        self.reject = 0; self.lose_receipt = False
        self.targets = [{'id':'12:34:56:78:90:AB', 'name':'Studio Wi-Fi',
                         'rssi':-48, 'channel':6, 'security':'WPA2',
                         'packets':11, 'lastSeen':9000}]
        browser_tests.DashboardBrowserTest.setUp(self)

    def route(self, route):
        if route.request.headers.get('x-device-key') != 'test-password':
            return browser_tests.DashboardBrowserTest.route(self, route)
        path = urlparse(route.request.url).path
        if path == '/api/status':
            self.api_calls += 1
            return route.fulfill(json=dict(version='v1.19.2', board='ESP32-S3 N16R8',
                bootId=self.boot, uptime=123, heap=180000, psram=8000000,
                flash=16777216, mode=0, channel=1, remaining=0, ssid='Marauder-TEST',
                clients=1, workflow=self.workflow))
        if path == '/api/targets':
            query = parse_qs(urlparse(route.request.url).query)
            self.target_requests.append(query)
            page = int(query.get('page', ['0'])[0])
            return route.fulfill(json=dict(kind=query['kind'][0], total=len(self.targets),
                targets=self.targets[page*40:(page+1)*40], limit=40, page=page))
        if path == '/api/workflow':
            payload = route.request.post_data_json
            if payload['action'] == 'cancel':
                self.cancels.append(payload)
                if payload['id'] != self.workflow['id']:
                    return route.fulfill(status=409, json={'error':'This job is no longer active'})
                self.workflow = {**self.workflow, 'state':'cancelled', 'remaining':0}
                return route.fulfill(json=self.workflow)
            self.starts.append(payload)
            if self.reject:
                return route.fulfill(status=self.reject, json={'error':'Target is no longer available'})
            self.workflow = job(id=self.workflow['id'] + 1, state='queued',
                kind=payload['kind'], seconds=payload['seconds'], remaining=payload['seconds'],
                channel=payload['channel'], target=payload.get('target', ''))
            if self.lose_receipt: return route.abort()
            return route.fulfill(status=202, json=self.workflow)
        if path == '/api/file':
            return route.fulfill(body=b'capture data', content_type='application/octet-stream')
        return browser_tests.DashboardBrowserTest.route(self, route)

    def refresh(self):
        self.page.evaluate("window.dispatchEvent(new Event('online'))")

    def choose(self, title):
        self.page.get_by_role('button', name=title, exact=True).click()

    def review(self, seconds=12):
        duration = self.page.get_by_label('Duration in seconds')
        if duration.is_visible(): duration.fill(str(seconds))
        self.choose('Review task')

    def test_default_home_groups_and_mobile_layout(self):
        self.login()
        self.assertTrue(self.page.get_by_role('heading', name='Choose your next task').is_visible())
        for name in ['Wi-Fi', 'Bluetooth', 'Captures']:
            self.assertTrue(self.page.locator('#tab-tasks').get_by_role('heading', name=name, exact=True).is_visible())
        self.page.set_viewport_size({'width':390, 'height':844})
        self.assertTrue(self.page.evaluate('document.documentElement.scrollWidth <= innerWidth'))
        self.page.screenshot(path=str(ROOT / '.cache/tasks-mobile.png'), full_page=True)
        self.assertEqual(self.errors, [])

    def test_discovery_review_and_explicit_run(self):
        self.login(); self.choose('Discover Wi-Fi networks'); self.review()
        self.assertEqual(self.starts, [])
        self.choose('Run task')
        self.page.get_by_role('heading', name='Task in progress').wait_for()
        self.assertEqual(self.starts, [{'action':'start', 'kind':'wifi_discover', 'seconds':12, 'channel':0}])

    def test_target_identity_and_snapshot_results(self):
        self.login(); self.choose('Inspect a Wi-Fi network')
        self.choose('Studio Wi-Fi')
        self.review(); self.choose('Run task')
        self.page.get_by_role('heading', name='Task in progress').wait_for()
        self.assertEqual(self.starts[0]['target'], '12:34:56:78:90:AB')
        self.workflow = {**self.workflow, 'state':'completed', 'targetName':'Studio Wi-Fi', 'rssi':-52, 'count':11}
        self.refresh()
        self.page.get_by_role('heading', name='Task complete').wait_for()
        self.assertIn('-52 dBm', self.page.locator('#flow-job').inner_text())

    def test_empty_discovery_resumes_picker_after_reload(self):
        self.targets = []
        self.login(); self.choose('Track a Bluetooth signal')
        self.page.get_by_text('No devices found yet.', exact=True).wait_for()
        self.choose('Discover devices first'); self.review(5); self.choose('Run task')
        self.page.get_by_role('heading', name='Task in progress').wait_for()
        self.page.reload()
        self.page.get_by_role('heading', name='Task in progress').wait_for()
        self.targets = [{'id':'AA:BB:CC:DD:EE:FF', 'name':'Desk sensor', 'rssi':-70,
                         'channel':0, 'security':'', 'packets':1, 'lastSeen':10000}]
        self.workflow = {**self.workflow, 'state':'completed', 'count':1}
        self.refresh()
        self.choose('Desk sensor'); self.review(8); self.choose('Run task')
        self.page.get_by_role('heading', name='Task in progress').wait_for()
        self.assertEqual(len(self.starts), 2)
        self.assertEqual(self.starts[1]['kind'], 'ble_signal')
        self.assertEqual(self.starts[1]['target'], 'AA:BB:CC:DD:EE:FF')

    def test_uncertain_receipt_never_reposts_and_resumes(self):
        self.login(); self.choose('Discover Wi-Fi networks'); self.review()
        self.lose_receipt = True; self.choose('Run task')
        self.page.get_by_text('Receipt not confirmed.', exact=False).wait_for()
        self.refresh()
        self.page.get_by_role('heading', name='Task in progress').wait_for()
        self.page.reload()
        self.page.get_by_role('heading', name='Task in progress').wait_for()
        self.assertEqual(len(self.starts), 1)

    def test_cancel_uses_exact_job_and_cancelled_result(self):
        self.workflow = job(id=77, kind='wifi_discover', state='running', seconds=30, remaining=20)
        self.login(); self.choose('Stop task')
        self.page.get_by_role('heading', name='Task stopped').wait_for()
        self.assertEqual(self.cancels, [{'action':'cancel', 'id':77}])
        self.assertEqual(self.commands, [])

    def test_failed_capture_has_no_download_and_completed_capture_downloads(self):
        self.login(); self.choose('Record Wi-Fi traffic'); self.review()
        self.page.get_by_text('entire selected radio channel', exact=False).wait_for()
        self.choose('Run task')
        self.page.get_by_role('heading', name='Task in progress').wait_for()
        self.workflow = {**self.workflow, 'state':'failed', 'error':'Recording is disabled'}
        self.refresh()
        self.page.get_by_role('heading', name='Task failed').wait_for()
        self.assertEqual(self.page.get_by_role('button', name='Download capture').count(), 0)
        self.workflow = {**self.workflow, 'state':'completed', 'file':'/capture.pcap', 'error':''}
        self.refresh()
        with self.page.expect_download() as result:
            self.choose('Download capture')
        self.assertEqual(result.value.suggested_filename, 'capture.pcap')

    def test_restart_clears_job_without_resubmitting(self):
        self.login(); self.choose('Discover Wi-Fi networks'); self.review(); self.choose('Run task')
        self.page.get_by_role('heading', name='Task in progress').wait_for()
        self.boot = 99; self.workflow = job()
        self.page.reload()
        self.page.get_by_text('Device restarted.', exact=False).wait_for()
        self.assertEqual(len(self.starts), 1)

    def test_duration_validation_and_stale_target(self):
        self.login(); self.choose('Track a Wi-Fi signal'); self.choose('Studio Wi-Fi')
        self.review(301)
        self.assertEqual(self.page.get_by_role('button', name='Run task').count(), 0)
        self.review(4); self.reject = 404; self.choose('Run task')
        self.page.get_by_text('Target is no longer available', exact=False).wait_for()
        self.assertEqual(len(self.starts), 1)
        self.choose('Choose another target')
        self.choose('Studio Wi-Fi')
        self.assertEqual(len(self.target_requests), 2)

    def test_paginated_targets_and_untrusted_names(self):
        self.targets = [{**self.targets[0], 'id':f'AA:BB:CC:DD:EE:{i:02X}', 'name':f'Network {i}'} for i in range(41)]
        self.targets[-1]['name'] = '<img src=x onerror=alert(1)>'
        self.login(); self.choose('Track a Wi-Fi signal'); self.choose('Load more targets')
        self.choose('<img src=x onerror=alert(1)>')
        self.assertEqual(self.page.locator('#tab-tasks img').count(), 0)
        self.review(); self.choose('Run task')
        self.page.get_by_role('heading', name='Task in progress').wait_for()
        self.assertEqual(self.starts[0]['target'], 'AA:BB:CC:DD:EE:28')

    def test_completed_result_does_not_pull_user_back_from_task_home(self):
        self.login(); self.choose('Discover Wi-Fi networks'); self.review(); self.choose('Run task')
        self.page.get_by_role('heading', name='Task in progress').wait_for()
        self.workflow = {**self.workflow, 'state':'completed'}; self.refresh()
        self.page.get_by_role('heading', name='Task complete').wait_for()
        self.choose('Choose another task'); self.refresh()
        self.page.wait_for_timeout(250)
        self.assertTrue(self.page.get_by_role('button', name='Discover Wi-Fi networks', exact=True).is_visible())

    def test_unconfirmed_stop_does_not_claim_success(self):
        self.workflow = job(id=12, kind='wifi_discover', state='running', seconds=30, remaining=20)
        self.login()
        self.page.route('**/api/workflow', lambda route: route.abort())
        self.choose('Stop task')
        self.page.get_by_text('Stop could not be confirmed.', exact=False).wait_for()
        self.assertTrue(self.page.get_by_role('heading', name='Task in progress').is_visible())
        self.assertEqual(self.page.get_by_role('heading', name='Task stopped').count(), 0)
        self.assertIn('automatic timer', self.page.locator('#flow-notice').inner_text())

    def test_wifi_capture_target_uses_its_actual_channel(self):
        self.login(); self.choose('Record Wi-Fi traffic'); self.choose('Choose a network instead')
        self.choose('Studio Wi-Fi'); self.review(); self.choose('Run task')
        self.page.get_by_role('heading', name='Task in progress').wait_for()
        self.assertEqual(self.starts, [{'action':'start', 'kind':'wifi_capture', 'seconds':12,
                                       'channel':6, 'target':'12:34:56:78:90:AB'}])

    def test_running_job_reconnects_without_resending(self):
        self.login(); self.choose('Discover Wi-Fi networks'); self.review(); self.choose('Run task')
        self.page.get_by_role('heading', name='Task in progress').wait_for()
        self.page.unroute('**/api/**'); self.page.route('**/api/**', lambda route: route.abort())
        self.refresh(); self.page.locator('#connection').filter(has_text=re.compile('^Disconnected$')).wait_for()
        self.page.unroute('**/api/**'); self.page.route('**/api/**', self.route)
        self.refresh(); self.page.locator('#connection').filter(has_text=re.compile('^Connected$')).wait_for()
        self.page.reload(); self.page.get_by_role('heading', name='Task in progress').wait_for()
        self.choose('Stop operation'); self.page.get_by_role('heading', name='Task stopped').wait_for()
        self.assertEqual(len(self.starts), 1)
        self.assertEqual(self.cancels, [{'action':'cancel', 'id':1}])

    def test_busy_response_is_readable_and_never_retried(self):
        self.login(); self.choose('Discover Wi-Fi networks'); self.review()
        self.page.route('**/api/workflow', lambda route: route.fulfill(status=409, json={'error':'Another operation is active'}))
        self.choose('Run task')
        self.page.get_by_text('Another operation is active', exact=True).wait_for()
        self.assertTrue(self.page.get_by_role('heading', name='Ready to run?').is_visible())
        self.assertEqual(self.starts, [])

    def test_mobile_wizard_target_setup_review_and_progress(self):
        self.page.set_viewport_size({'width':320, 'height':740})
        self.targets[0]['name'] = 'A very long network name with <unsafe> text'
        self.login(); self.choose('Inspect a Wi-Fi network')
        self.page.get_by_test_id('flow-target').wait_for()
        self.assertTrue(self.page.evaluate('document.documentElement.scrollWidth <= innerWidth'))
        self.choose(self.targets[0]['name'])
        self.assertTrue(self.page.evaluate('document.documentElement.scrollWidth <= innerWidth'))
        self.review()
        self.assertTrue(self.page.evaluate('document.documentElement.scrollWidth <= innerWidth'))
        self.choose('Run task'); self.page.get_by_role('heading', name='Task in progress').wait_for()
        self.assertTrue(self.page.evaluate('document.documentElement.scrollWidth <= innerWidth'))
        self.page.screenshot(path=str(ROOT / '.cache/task-progress-mobile.png'), full_page=True)

    def test_stop_acknowledgement_clears_pending_notice(self):
        self.workflow = job(id=3, kind='ble_discover', state='running', seconds=30, remaining=20)
        self.login(); self.choose('Stop task')
        self.page.get_by_role('heading', name='Task stopped').wait_for()
        self.assertTrue(self.page.locator('#flow-notice').is_hidden())

    def test_unconfirmed_request_with_no_job_stays_visible_after_reload(self):
        posts = []
        def no_receipt(route):
            posts.append(route.request.post_data_json); route.abort()
        self.login(); self.choose('Discover Wi-Fi networks'); self.review()
        self.page.route('**/api/workflow', no_receipt)
        self.choose('Run task'); self.page.get_by_text('Receipt not confirmed.', exact=False).wait_for()
        self.page.reload()
        self.page.get_by_role('heading', name='Checking task receipt').wait_for()
        self.choose('Check device status')
        self.assertEqual(len(posts), 1)

    def test_inspection_collects_no_duration_and_uses_snapshot(self):
        self.login()
        for task in ['Inspect a Wi-Fi network', 'Inspect a Bluetooth device']:
            self.choose(task); self.choose('Studio Wi-Fi')
            self.assertEqual(self.page.get_by_label('Duration in seconds').count(), 0)
            self.page.get_by_text('Inspection reads the latest discovery snapshot.', exact=False).wait_for()
            self.review(); self.choose('Run task')
            self.page.get_by_role('heading', name='Task in progress').wait_for()
            self.assertEqual(self.starts[-1]['seconds'], 1)
            self.workflow = {**self.workflow, 'state':'completed'}; self.refresh()
            self.page.get_by_role('heading', name='Task complete').wait_for()
            self.choose('Choose another task')


if __name__ == '__main__': unittest.main(verbosity=2)
