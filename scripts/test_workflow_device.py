"""Verify guided tasks on the installed device; all radio operations are passive."""
from pathlib import Path
import argparse
import http.client
import json
import re
import time
from urllib.parse import quote
from test_device import Board, credentials

ROOT = Path(__file__).resolve().parents[1]
ACTIVE = ('queued','running','recovering')

def browser_check(identity, target_id):
    from playwright.sync_api import sync_playwright
    chrome = Path('C:/Program Files/Google/Chrome/Application/chrome.exe')
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(executable_path=str(chrome) if chrome.exists() else None, headless=True)
        page = browser.new_page(viewport={'width':1440,'height':1000})
        errors = []
        page.on('pageerror', lambda error: errors.append(str(error)))
        page.goto(identity['address'], wait_until='domcontentloaded', timeout=15000)
        page.get_by_label('Device password').fill(identity['password'])
        page.get_by_role('button', name='Unlock dashboard').click()
        page.locator('#connection').filter(has_text=re.compile('^Connected$')).wait_for(timeout=15000)
        # A retained last result may be shown after login; the home link is explicit.
        home = page.get_by_test_id('flow-home')
        if home.count() and home.is_visible(): home.click()
        page.get_by_role('button', name='Inspect a Wi-Fi network', exact=True).click()
        page.locator('[data-testid="flow-target"][data-target="' + target_id + '"]').click()
        page.get_by_role('button', name='Review task', exact=True).click()
        page.get_by_role('button', name='Run task', exact=True).click()
        page.get_by_role('heading', name='Task complete', exact=True).wait_for(timeout=15000)
        assert target_id.lower() in page.locator('#flow-job').inner_text().lower()
        page.screenshot(path=str(ROOT / '.cache/guided-device-result.png'), full_page=True)
        page.get_by_test_id('flow-home').click()
        page.screenshot(path=str(ROOT / '.cache/guided-device-desktop.png'), full_page=True)
        page.set_viewport_size({'width':390, 'height':844})
        assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
        page.screenshot(path=str(ROOT / '.cache/guided-device-mobile.png'), full_page=True)
        assert not errors, errors
        browser.close()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM9')
    parser.add_argument('--seconds', type=int, default=8)
    parser.add_argument('--reconnect', action='store_true')
    parser.add_argument('--browser', action='store_true', help='Exercise a real target-selection task in Chrome')
    args = parser.parse_args()
    assert 2 <= args.seconds <= 30
    identity = credentials(args.port)
    board = Board(identity['password'])
    initial = board.json('/api/status')
    assert initial['mode'] == 0 and initial['workflow']['state'] not in ACTIVE, 'Stop current activity before testing'
    boot_id = initial['bootId']
    checks = []

    def read():
        try: return board.json('/api/status')
        except (OSError, http.client.HTTPException):
            if not args.reconnect: raise
            from windows_wifi import connect_profile
            def ready():
                try: board.json('/api/status'); return True
                except (OSError, http.client.HTTPException): return False
            connect_profile(identity['ssid'], ready=ready)
            return board.json('/api/status')

    def start(kind, seconds=1, channel=0, target=''):
        response = board.json('/api/workflow', body={'action':'start','kind':kind,'seconds':seconds,'channel':channel,'target':target}, expected=202)
        job = response.get('workflow', response)
        assert job['id'] > 0 and job['kind'] == kind and job['state'] in ACTIVE, job
        return job['id']

    def finish(job_id, wait=0.4, expected='completed'):
        time.sleep(wait)
        until = time.monotonic() + 45
        while time.monotonic() < until:
            status = read()
            assert status['bootId'] == boot_id, 'Device rebooted while running a workflow'
            job = status['workflow']
            assert job['id'] == job_id, 'Another job replaced the running workflow'
            if job['state'] not in ACTIVE:
                assert job['state'] == expected, job
                assert status['mode'] == 0, status
                return job
            time.sleep(.5)
        raise AssertionError('Workflow did not terminate and restore access')

    def targets(kind):
        data = board.json('/api/targets?kind=' + kind + '&page=0')
        assert data['kind'] == kind and len(data['targets']) <= 40
        assert data['total'] >= len(data['targets'])
        return data['targets']

    board.request('/api/workflow', body={'action':'start','kind':'wifi_discover','seconds':0,'channel':0}, expected=400)
    board.request('/api/workflow', body={'action':'start','kind':'not_a_task','seconds':8,'channel':0}, expected=400)
    board.request('/api/workflow', body={'action':'start','kind':'wifi_inspect','seconds':1,'channel':0,'target':'02:00:00:FE:FD:FC'}, expected=404)
    checks.append('workflow input and stale-target rejection')

    job_id = start('wifi_discover', args.seconds)
    # Deliberately do not poll: the board must complete without browser orchestration.
    discovery = finish(job_id, wait=args.seconds + 1)
    wifi = targets('wifi')
    assert wifi, 'No Wi-Fi beacons found; target-selection hardware test needs a nearby AP'
    checks += ['autonomous multi-channel Wi-Fi discovery', 'structured selectable targets']
    print(f'PASS: discovery returned {len(wifi)} selectable Wi-Fi targets', flush=True)

    selected = next((item for item in wifi if 1 <= item['channel'] <= 14), None)
    assert selected
    inspect = finish(start('wifi_inspect', target=selected['id']))
    assert inspect['target'].lower() == selected['id'].lower()
    assert inspect['targetName'] == selected['name']
    checks.append('MAC-selected network inspection')

    signal = finish(start('wifi_signal', args.seconds, target=selected['id']), wait=args.seconds + 1)
    assert signal['target'].lower() == selected['id'].lower()
    assert -128 <= signal['rssi'] <= 0
    checks.append('targeted signal task with automatic stop/recovery')

    capture = finish(start('wifi_capture', args.seconds, channel=selected['channel'], target=selected['id']), wait=args.seconds + 1)
    assert capture['file'].endswith('.pcap'), capture
    payload, _ = board.request('/api/file?storage=spiffs&path=' + quote(capture['file'], safe=''))
    assert len(payload) >= 24 and payload[:4] == bytes([0xd4,0xc3,0xb2,0xa1])
    (ROOT / '.cache/workflow-test-capture.pcap').write_bytes(payload)
    board.json('/api/file/delete', body={'path':capture['file']})
    checks += ['guided timed channel capture', 'real result download and test-file deletion']

    ble_id = start('ble_discover', args.seconds)
    finish(ble_id, wait=args.seconds + 1)
    ble = targets('ble')
    checks.append('autonomous Bluetooth discovery')
    if ble:
        inspected = finish(start('ble_inspect', target=ble[0]['id']))
        assert inspected['target'].lower() == ble[0]['id'].lower()
        checks.append('MAC-selected Bluetooth inspection')
        tracked = finish(start('ble_signal', args.seconds, target=ble[0]['id']), wait=args.seconds + 1)
        assert tracked['target'].lower() == ble[0]['id'].lower()
        assert -128 <= tracked['rssi'] <= 0
        checks.append('Bluetooth signal task with automatic stop/recovery')

    cancel_id = start('ble_discover', 10)
    board.request('/api/command', body={'command':'help','seconds':30}, expected=409)
    board.request('/api/workflow', body={'action':'cancel','id':cancel_id + 999}, expected=409)
    board.request('/api/workflow', body={'action':'cancel','id':cancel_id}, expected=202)
    finish(cancel_id, wait=1, expected='cancelled')
    checks += ['manual-command busy arbitration', 'stale cancellation protection', 'explicit cancellation and recovery']

    if args.browser:
        browser_check(identity, selected['id'])
        checks += ['real browser target selection/review/run/result', 'real guided mobile layout']

    report = {'outcome':'PASS','checks':checks,'wifiTargets':len(wifi),'bleTargets':len(ble),
              'captureBytes':len(payload),'final':board.json('/api/status')}
    (ROOT / '.cache/workflow-device-report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2), flush=True)

if __name__ == '__main__': main()
