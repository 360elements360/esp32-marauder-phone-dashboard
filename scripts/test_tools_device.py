"""Exercise complete guided controls using passive radio and management only."""
from pathlib import Path
import argparse
import http.client
import json
import time
from urllib.parse import quote
from test_device import Board, credentials, ROOT

ACTIVE = ('queued','running','recovering')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='COM9')
    parser.add_argument('--reconnect',action='store_true')
    parser.add_argument('--browser',action='store_true')
    args=parser.parse_args()
    identity=credentials(args.port);board=Board(identity['password']);checks=[]
    initial=board.json('/api/status');boot=initial['bootId']
    assert initial['mode']==0 and initial['workflow']['state'] not in ACTIVE

    def ready():
        try: board.json('/api/status');return True
        except (OSError,http.client.HTTPException):return False
    def status():
        try:return board.json('/api/status')
        except (OSError,http.client.HTTPException):
            if not args.reconnect:raise
            from windows_wifi import connect_profile
            connect_profile(identity['ssid'],ready=ready)
            return board.json('/api/status')
    def request(tool,inputs=None,seconds=1,channel=0,expected=202):
        return board.json('/api/workflow',body=dict(action='start',kind='tool',tool=tool,inputs=inputs or {},seconds=seconds,channel=channel),expected=expected)
    def finish(job,expected='completed'):
        until=time.monotonic()+55
        while time.monotonic()<until:
            s=status();assert s['bootId']==boot,'Unexpected device reboot'
            w=s['workflow'];assert w['id']==job['id'],w
            if w['state'] not in ACTIVE:
                assert w['state']==expected,w
                assert s['mode']==0,s
                return w
            time.sleep(.25)
        raise AssertionError('Task did not finish and restore access')
    def output(job):
        cursor=job['logStart'];parts=[]
        for _ in range(12):
            part=board.json('/api/log?cursor='+str(cursor));parts.append(part['text'])
            if part['cursor']==cursor:break
            cursor=part['cursor']
        text=''.join(parts);assert identity['password'] not in text
        return text
    def run(tool,inputs=None,seconds=1,channel=0):
        response=request(tool,inputs,seconds,channel);job=response['workflow']
        job=finish(job);assert job['tool']==tool,job
        return job,output(job)
    def targets(kind):return board.json('/api/targets?kind='+kind+'&page=0')['targets']

    request('not_a_tool',expected=404)
    request('gps_data',expected=503)
    request('change_setting',{'setting':'SavePCAP','value':'not_a_choice'},expected=400)
    request('target_info',{'target':0},expected=400)
    request('lan_arp',expected=409)
    checks.append('unknown/unavailable/typed/target/prerequisite rejection')
    _,text=run('device_info');assert text
    _,text=run('device_radio');assert 'apSsid' in text and 'scanMode' in text,text
    checks+=['guided device information','browser-visible radio diagnostics']
    print('PASS: generic validation, device information and browser radio output',flush=True)

    # Original discovery is a prerequisite for actual MAC pickers and CSI.
    response=board.json('/api/workflow',body=dict(action='start',kind='wifi_discover',seconds=5,channel=0),expected=202)
    time.sleep(6);finish(response['workflow']);wifi=targets('wifi');assert wifi
    chosen=max(wifi,key=lambda t:t['rssi'])
    _,text=run('target_info',{'target':chosen['id']});assert text
    job=request('wifi_csi',{'target':chosen['id']},seconds=5)['workflow']
    time.sleep(6);job=finish(job);assert 'CSI motion started' in output(job)
    job=request('ble_ibeacons',seconds=10)['workflow']
    time.sleep(1)
    # Read-only side controls must keep this running job and its deadline.
    side=request('device_radio');assert side.get('side') and side['workflow']['id']==job['id'],side
    assert output({'logStart':side['logStart']}),side
    stopped=request('device_stop');assert stopped['workflow']['id']==job['id'],stopped
    finish(job,expected='cancelled')
    checks+=['structured Wi-Fi target inspection','timed CSI startup and radio recovery',
             'running Bluetooth side status and stop without replacing the job']
    print('PASS: target inspection, CSI recovery, running side control and cancellation',flush=True)

    existing={f['path'] for f in board.json('/api/files?storage=spiffs')['files']}
    reserved={'/APs_0.log','/SSIDs_0.log'}
    assert not existing.intersection(reserved),'Existing target backups must be preserved; skip this destructive fixture section'
    name='Dashboard verification';mac='02:00:00:fe:ed:01';client='02:00:00:fe:ed:02'
    run('target_add_ap',{'mac':mac,'name':name})
    duplicate=request('target_add_ap',{'mac':mac,'name':name})['workflow']
    finish(duplicate,expected='failed')
    run('target_add_client',{'mac':client,'target':mac})
    run('select_filter',{'filter':'equals '+name})
    _,text=run('list_wifi');assert any(name in line and '(selected)' in line for line in text.splitlines()),text
    run('save_ap');run('clear_wifi');assert not targets('wifi')
    run('load_ap');assert any(t['id'].lower()==mac for t in targets('wifi'))
    assert any(t['id'].lower()==client for t in targets('station'))
    run('ssid_add',{'name':name});run('save_ssid');run('clear_ssid');assert not targets('ssid')
    run('load_ssid');assert any(t['name']==name for t in targets('ssid'))
    for path in reserved:
        data,_=board.request('/api/file?storage=spiffs&path='+quote(path,safe=''));assert data
        (ROOT/'.cache'/('tool-test-'+path[1:])).write_bytes(data)
        board.json('/api/file/delete',body={'path':path})
    run('clear_wifi');run('clear_ssid')
    checks+=['typed AP/client creation and duplicate failure','friendly name selection','SPIFFS AP/station and SSID save/clear/load/download']
    print('PASS: management, selection and SPIFFS target-list round trips',flush=True)

    html='<h1>Dashboard upload ☃</h1>\n<p>UTF-8 preserved</p>'
    raw=html.encode('utf-8');path='dashboard-test.html'
    result=board.json('/api/template',body=dict(path=path,offset=0,data=html,final=True,total=len(raw)))
    assert result['offset']==len(raw) and result['complete'] and result['path']=='/'+path,result
    board.request('/api/template',body=dict(path='../settings.json',offset=0,data='x',final=True,total=1),expected=400)
    run('portal_template',{'file':path})
    checks+=['authenticated UTF-8 template upload and path rejection','guided portal template selection without starting a portal']
    print('PASS: UTF-8 HTML upload, private-path protection and template selection',flush=True)
    run('portal_template',{'file':'index.html'})

    if args.browser:
        from playwright.sync_api import sync_playwright, TimeoutError as BrowserTimeout
        chrome=Path('C:/Program Files/Google/Chrome/Application/chrome.exe')
        with sync_playwright() as p:
            browser=p.chromium.launch(executable_path=str(chrome) if chrome.exists() else None,headless=True)
            page=browser.new_page(viewport={'width':1440,'height':1000});errors=[]
            page.on('pageerror',lambda e:errors.append(str(e)))
            page.goto(identity['address'],wait_until='domcontentloaded')
            page.get_by_label('Device password').fill(identity['password'])
            page.get_by_role('button',name='Unlock dashboard').click()
            page.get_by_label('Search tasks').wait_for()
            page.get_by_label('Search tasks').fill('Read device information')
            page.locator('[data-tool="device_info"]').click()
            page.get_by_role('button',name='Review task',exact=True).click()
            page.get_by_role('button',name='Run task',exact=True).click()
            until=time.monotonic()+50
            while True:
                try:
                    page.get_by_role('heading',name='Task complete',exact=True).wait_for(timeout=3000)
                    break
                except BrowserTimeout:
                    if time.monotonic()>until:raise
                    if args.reconnect:
                        from windows_wifi import connect_profile
                        connect_profile(identity['ssid'],ready=ready)
                    page.evaluate("window.dispatchEvent(new Event('online'))")
            page.get_by_label('Task output').filter(has_text='Hardware:').wait_for(timeout=15000)
            page.screenshot(path=str(ROOT/'.cache/complete-tools-device-result.png'),full_page=True)
            page.get_by_test_id('flow-home').click();page.get_by_label('Search tasks').fill('')
            page.set_viewport_size({'width':390,'height':844});assert page.evaluate('document.documentElement.scrollWidth<=innerWidth')
            page.screenshot(path=str(ROOT/'.cache/complete-tools-device-mobile.png'),full_page=True)
            assert not errors,errors
            browser.close()
        checks+=['real generic browser review/run/output','real expanded mobile layout']
    report=dict(outcome='PASS',checks=checks,final=status(),activeTransmitModes='software validation only; not transmitted in hardware tests')
    (ROOT/'.cache/complete-tools-device-report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
