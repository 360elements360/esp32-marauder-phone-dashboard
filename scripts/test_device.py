"""Exercise the installed board. Join its Wi-Fi first; --radio adds a passive scan.

Credentials and evidence stay under ignored .cache; no password is printed.
"""
from pathlib import Path
import argparse
import gzip
import http.client
import json
import re
import time

ROOT = Path(__file__).resolve().parents[1]
HOST = '192.168.4.1'

def credentials(port):
    import serial
    device = serial.Serial(None, 115200, timeout=0.1, write_timeout=2)
    # Set BEFORE opening: asserted Windows DTR/RTS can return USB-JTAG to ROM download.
    device.dtr = False
    device.rts = False
    device.port = port
    captured = bytearray()
    with device:
        device.write(b'dashboard\n')
        until = time.monotonic() + 6
        while time.monotonic() < until:
            captured.extend(device.read(max(1, device.in_waiting)))
            found = re.search(r'Dashboard Wi-Fi: ([^\r\n]+)\r?\nPassword: ([^\r\n]+)', captured.decode('utf-8', errors='replace'))
            if found:
                result = {'ssid':found[1].strip(), 'password':found[2].strip(), 'address':'http://' + HOST, 'port':port}
                (ROOT / '.cache/device-credentials.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
                (ROOT / '.cache/DEVICE-ACCESS.txt').write_text(
                    f"Dashboard: {result['address']}\nWi-Fi network: {result['ssid']}\n"
                    f"Device and Wi-Fi password: {result['password']}\nUSB port: {port}\n",
                    encoding='utf-8')
                return result
    raise RuntimeError('Board did not answer the USB dashboard command')

class Board:
    def __init__(self, secret): self.secret = secret; self.cursor = 0

    def request(self, path, body=None, key=True, extra=None, expected=200):
        connection = http.client.HTTPConnection(HOST, timeout=5)
        headers = {'Connection':'close'}
        if key: headers['X-Device-Key'] = self.secret
        if body is not None:
            headers['Content-Type'] = 'application/json'
            if not isinstance(body, (str, bytes)): body = json.dumps(body)
        headers.update(extra or {})
        try:
            connection.request('POST' if body is not None else 'GET', path, body=body, headers=headers)
            response = connection.getresponse()
            data = response.read()
            assert response.status == expected, f'{path}: expected HTTP {expected}, got {response.status}: {data[:160]!r}'
            return data, dict(response.getheaders())
        finally: connection.close()

    def json(self, path, **options): return json.loads(self.request(path, **options)[0])

    def command(self, command, seconds=30):
        self.json('/api/command', body={'command':command, 'seconds':seconds}, expected=202)

    def logs(self):
        cursor, text = self.cursor, ''
        for _ in range(12):
            part = self.json('/api/log?cursor=' + str(cursor))
            text += part['text']
            if part['cursor'] == cursor: break
            cursor = part['cursor']
        self.cursor = cursor
        assert self.secret not in text, 'Device password leaked into mirrored log'
        return text

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM9')
    parser.add_argument('--radio', action='store_true', help='Run an 8-second passive beacon capture and test timed AP recovery')
    parser.add_argument('--browser', action='store_true', help='Verify real dashboard in headless Chrome and save screenshots')
    parser.add_argument('--reconnect', action='store_true', help='Reconnect the saved Windows device Wi-Fi profile after a timed scan')
    args = parser.parse_args()
    (ROOT / '.cache').mkdir(exist_ok=True)
    identity = credentials(args.port)
    print('PASS: USB command response; device password stored privately', flush=True)
    board = Board(identity['password'])
    status = board.json('/api/status')
    assert status['board'] == 'ESP32-S3 N16R8' and status['flash'] == 16777216
    assert status['psram'] > 6000000 and status['ssid'] == identity['ssid']
    boot_id = status['bootId']
    checks = ['USB command response', 'hardware identity', '16 MB flash', '8 MB OPI PSRAM initialized']
    board.request('/api/status', key=False, expected=401)
    board.request('/api/status', extra={'X-Device-Key':'wrong-password'}, expected=401)
    board.request('/api/command', body={'command':'help','seconds':30}, extra={'Origin':'http://different.invalid'}, expected=403)
    board.request('/api/command', body={'command':'help\nreboot','seconds':30}, expected=400)
    # Headers alone must be rejected before a body is read/allocated. Sending an
    # unread oversized body can make TCP reset swallow the rejection response.
    board.request('/api/command', body='', extra={'Content-Length':'513'}, expected=413)
    board.request('/api/file?storage=spiffs&path=/settings.json', expected=400)
    checks += ['HTTP authentication', 'cross-origin rejection', 'multiline command rejection', 'oversized body rejection', 'private settings protection']
    page, headers = board.request('/')
    assert headers.get('Content-Encoding') == 'gzip'
    unpacked = gzip.decompress(page)
    assert unpacked == (ROOT / '.cache/dashboard.html').read_bytes()
    checks.append('device serves exact compressed dashboard')
    board.logs()  # Advance past previous runs; assertions below must use fresh output.
    board.command('help')
    time.sleep(0.4)
    log = board.logs()
    assert 'Commands' in log and 'settings' in log
    board.command('settings')
    time.sleep(0.4)
    log = board.logs()
    assert 'SavePCAP' in log or 'ChanHop' in log
    checks += ['real command dispatch and output', 'read settings', 'password absent from mirrored console']
    initial_files = {file['path'] for file in board.json('/api/files?storage=spiffs')['files']}

    if args.radio:
        started = time.monotonic()
        board.command('sniffbeacon', seconds=8)
        print('Testing an 8-second passive beacon capture and automatic control-Wi-Fi recovery...', flush=True)
        time.sleep(9)
        until = time.monotonic() + 40
        while True:
            try:
                after = board.json('/api/status')
                if after['mode'] == 0 and after['remaining'] == 0: break
            except (OSError, http.client.HTTPException):
                if args.reconnect:
                    from windows_wifi import connect_profile
                    def ready():
                        try: board.json('/api/status'); return True
                        except (OSError, http.client.HTTPException): return False
                    connect_profile(identity['ssid'], ready=ready)
            if time.monotonic() > until: raise RuntimeError('Timed radio operation did not restore HTTP; USB dashboard recover is available')
            time.sleep(1)
        assert after['bootId'] == boot_id, 'Device rebooted instead of restoring the AP'
        files = board.json('/api/files?storage=spiffs')['files']
        created = [file for file in files if file['path'] not in initial_files and file['path'].endswith('.pcap')]
        assert created, 'Passive capture was not saved to internal SPIFFS'
        from urllib.parse import quote
        sample = created[0]
        pcap, _ = board.request('/api/file?storage=spiffs&path=' + quote(sample['path'], safe=''))
        assert len(pcap) >= 24 and pcap[:4] == bytes([0xd4,0xc3,0xb2,0xa1]), 'Invalid saved PCAP header'
        (ROOT / '.cache/device-smoke-capture.pcap').write_bytes(pcap)
        board.json('/api/file/delete', body={'path':sample['path']})
        assert sample['path'] not in {file['path'] for file in board.json('/api/files?storage=spiffs')['files']}
        checks += ['passive beacon capture', 'timed AP recovery without reboot', 'internal SPIFFS recording', 'capture download', 'test-capture deletion']
        status = after
        print(f'PASS: timed recovery in {time.monotonic() - started:.1f}s; downloaded {len(pcap)}-byte capture', flush=True)

    if args.browser:
        from playwright.sync_api import sync_playwright
        chrome = Path('C:/Program Files/Google/Chrome/Application/chrome.exe')
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch(executable_path=str(chrome) if chrome.exists() else None, headless=True)
            page = browser.new_page(viewport={'width':1440,'height':1000})
            failures = []
            page.on('pageerror', lambda error: failures.append(str(error)))
            page.goto('http://' + HOST, wait_until='domcontentloaded', timeout=15000)
            page.get_by_label('Device password').fill(identity['password'])
            page.get_by_role('button', name='Unlock dashboard').click()
            page.locator('#connection').filter(has_text=re.compile('^Connected$')).wait_for(timeout=15000)
            assert 'ESP32-S3 N16R8' in page.locator('#board').inner_text()
            page.screenshot(path=str(ROOT / '.cache/dashboard-device-desktop.png'), full_page=True)
            page.set_viewport_size({'width':390, 'height':844})
            assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
            page.screenshot(path=str(ROOT / '.cache/dashboard-device-mobile.png'), full_page=True)
            assert not failures, failures
            browser.close()
        checks += ['real browser login/status', 'real mobile layout']

    report = {'outcome':'PASS', 'ssid':identity['ssid'], 'checks':checks, 'status':status, 'physicalPortalMode':'not exercised'}
    (ROOT / '.cache/device-test-report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))

if __name__ == '__main__': main()
