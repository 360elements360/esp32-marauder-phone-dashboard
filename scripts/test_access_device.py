"""Temporarily rotate an installed board's password, then restore it.

Windows only: updates the saved device Wi-Fi profile to test both passwords.
Never prints passwords. Does not reset NVS or run radio scan/transmit tasks.
"""
from pathlib import Path
import argparse, json, secrets, subprocess, time
from xml.etree import ElementTree as ET
from test_device import Board, credentials, ROOT
from windows_wifi import connect_profile

def profile(identity):
    namespace = 'http://www.microsoft.com/networking/WLAN/profile/v1'
    ET.register_namespace('', namespace)
    def add(parent, name, text=None):
        element=ET.SubElement(parent, '{'+namespace+'}'+name)
        if text is not None: element.text=text
        return element
    xml=ET.Element('{'+namespace+'}WLANProfile')
    add(xml,'name',identity['ssid']); ssid=add(add(xml,'SSIDConfig'),'SSID');add(ssid,'name',identity['ssid'])
    add(xml,'connectionType','ESS');add(xml,'connectionMode','auto')
    security=add(add(xml,'MSM'),'security');auth=add(security,'authEncryption')
    add(auth,'authentication','WPA2PSK');add(auth,'encryption','AES');add(auth,'useOneX','false')
    shared=add(security,'sharedKey');add(shared,'keyType','passPhrase');add(shared,'protected','false');add(shared,'keyMaterial',identity['password'])
    path=ROOT/'.cache/access-test-profile.xml';ET.ElementTree(xml).write(path,encoding='utf-8',xml_declaration=True)
    try: subprocess.run(['netsh','wlan','add','profile','filename='+str(path),'user=current'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    finally: path.unlink(missing_ok=True)

def reconnect(identity):
    board=Board(identity['password'])
    def ready():
        try:return board.json('/api/status').get('setupRequired') is False
        except Exception:return False
    profile(identity);connect_profile(identity['ssid'],timeout=40,ready=ready)
    return board

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--port',default='COM9');args=parser.parse_args()
    (ROOT/'.cache').mkdir(exist_ok=True)
    original=credentials(args.port);current=dict(original);checks=[];browser=None;playwright=None
    try:
        board=reconnect(original)
        assert board.json('/api/status')['setupRequired'] is False
        board.json('/api/access',body={'password':'sample-password'},key=False,expected=401)
        board.json('/api/access',body={'password':'sample-password'},extra={'Origin':'http://example.invalid'},expected=403)
        for password in ['short','x'*64,' password','password ','marauder-setup','hello\nworld','pass\x00word','passwörd']:
            board.json('/api/access',body={'password':password},expected=400)
        checks.append('authentication, origin and password validation')
        from playwright.sync_api import sync_playwright
        playwright=sync_playwright().start();chrome=Path('C:/Program Files/Google/Chrome/Application/chrome.exe')
        browser=playwright.chromium.launch(executable_path=str(chrome) if chrome.exists() else None,headless=True)
        page=browser.new_page(viewport={'width':390,'height':844});page.set_default_timeout(15000)
        page.goto('http://192.168.4.1');page.get_by_label('Device password',exact=True).fill(original['password'])
        page.get_by_role('button',name='Unlock dashboard',exact=False).click()
        page.get_by_role('button',name='Settings',exact=True).click();page.get_by_role('button',name='Wi-Fi access',exact=True).click()
        field=page.get_by_label('Current password',exact=True)
        assert field.input_value()==original['password'] and field.get_attribute('type')=='password'
        page.get_by_label('Show passwords',exact=True).check();assert field.get_attribute('type')=='text'
        page.get_by_label('Show passwords',exact=True).uncheck()
        assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
        # Exercise the maximum supported length, internal spaces and JSON/XML symbols.
        current['password']='Phone "\\ '+secrets.token_hex(27)
        assert len(current['password'])==63
        page.get_by_label('New password',exact=True).fill(current['password']);page.get_by_label('Confirm password',exact=True).fill(current['password'])
        with page.expect_response(lambda response:'/api/access' in response.url) as response:
            page.get_by_role('button',name='Save password',exact=True).click()
        assert response.value.status==200
        time.sleep(2);board=reconnect(current)
        page.get_by_text('Connected with your new password.',exact=True).wait_for()
        Board(original['password']).json('/api/status',expected=401)
        board.logs();checks.append('real packed mobile UI, show/hide, 63-character change, Wi-Fi rejoin and old-key rejection')
        board.command('reboot',0);time.sleep(3);board=reconnect(current)
        after=credentials(args.port);assert after['password']==current['password']
        checks.append('password persists through board restart')
        print('PASS: '+ '; '.join(checks),flush=True)
    finally:
        if browser:browser.close()
        if playwright:playwright.stop()
        # Read actual credentials even if a response was lost; never guess/retry a mutation.
        actual=credentials(args.port);board=reconnect(actual)
        if actual['password']!=original['password']:
            board.json('/api/access',body={'password':original['password']});time.sleep(2)
        reconnect(original);restored=credentials(args.port)
        assert restored['password']==original['password']
        checks.append('original device password and saved Wi-Fi profile restored')
        print('PASS: original password/profile restored; no secrets printed',flush=True)
        (ROOT/'.cache/access-device-report.json').write_text(json.dumps({'checks':checks},indent=2),encoding='utf-8')

if __name__=='__main__':main()
