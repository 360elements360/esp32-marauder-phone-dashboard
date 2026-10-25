"""Auditable guided tools for the installed bare ESP32-S3 N16R8 build."""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]

def field(name, label, type='text', **values):
    return {**dict(name=name, label=label, type=type, required=True), **values}

def options(values):
    return [dict(value=a, label=b) if isinstance(item, tuple) else dict(value=item, label=item)
            for item in values for a,b in [item if isinstance(item, tuple) else (item,item)]]

def choice(name, label, values, default=None):
    result = field(name, label, 'select', options=options(values))
    result['default'] = result['options'][0]['value'] if default is None else default
    return result

def target(kind='wifi', label='Target', name='target', discover=None, multiple=False):
    result = field(name, label, 'target', kind=kind)
    if discover: result['discover'] = discover
    if multiple: result['multiple'] = True
    return result

def catalog():
    rows = []
    def add(id, title, group, steps=(), fields=(), modes=(), description='', **extra):
        commands = extra.pop('commands', list(dict.fromkeys(s.split()[0] for s in steps)))
        rows.append({**dict(id=id, title=title, description=description or title + '.', group=group,
                         available=True, reason='', fields=list(fields), timed=bool(modes),
                         commands=commands, steps=list(steps), modes=list(modes)), **extra})
    wifi = lambda **kw: target('wifi', 'Wi-Fi network', discover='wifi_discover', **kw)
    station = lambda **kw: target('station', 'Client device', discover='wifi_discover', **kw)
    ble = lambda **kw: target('ble', 'Bluetooth device', discover='ble_discover', **kw)

    # Preserve the installed seven task contracts and accessible names.
    for id,title,group,command in [
        ('wifi_discover','Discover Wi-Fi networks','Wi-Fi','scanall'),
        ('wifi_inspect','Inspect a Wi-Fi network','Wi-Fi','info'),
        ('wifi_signal','Track a Wi-Fi signal','Wi-Fi','foxhunt'),
        ('ble_discover','Discover Bluetooth devices','Bluetooth','sniffbt'),
        ('ble_inspect','Inspect a Bluetooth device','Bluetooth','list'),
        ('ble_signal','Track a Bluetooth signal','Bluetooth','foxhunt'),
        ('wifi_capture','Record Wi-Fi traffic','Captures','sniffraw')]:
        add(id,title,group,commands=[command],legacy=id)

    scans = [
        ('wifi_beacons','Record Wi-Fi beacons','sniffbeacon','WIFI_SCAN_AP'),
        ('wifi_probes','Discover probe requests','sniffprobe','WIFI_SCAN_PROBE'),
        ('wifi_deauth_rx','Monitor disconnect frames','sniffdeauth','WIFI_SCAN_DEAUTH'),
        ('wifi_sae_rx','Monitor SAE authentication','sniffsae','WIFI_SCAN_SAE_COMMIT'),
        ('wifi_pwn','Find Pwnagotchi devices','sniffpwn','WIFI_SCAN_PWN'),
        ('wifi_pineapple','Find Wi-Fi Pineapples','sniffpinescan','WIFI_SCAN_PINESCAN'),
        ('wifi_multissid','Find multi-SSID devices','sniffmultissid','WIFI_SCAN_MULTISSID'),
        ('wifi_packets','Monitor packet counts','packetcount','WIFI_SCAN_PACKET_RATE'),
        ('wifi_mac_track','Observe recurring MAC addresses','mactrack','WIFI_SCAN_DETECT_FOLLOW'),
    ]
    for id,title,command,mode in scans:
        add(id,title,'Wi-Fi',[command],modes=[mode],channel=True)
    add('wifi_pmkid','Capture passive EAPOL and PMKID','Captures',['sniffpmkid'],
        modes=['WIFI_SCAN_EAPOL'],channel=True,description='Receive authentication frames without sending disconnect traffic.')
    add('wifi_pmkid_active','Capture authentication with active test','Wi-Fi tests',
        ['sniffpmkid -d -l'],[wifi(multiple=True)],['WIFI_SCAN_ACTIVE_LIST_EAPOL'],
        channel=True,select=dict(kind='wifi',field='target'),active=True)
    add('wifi_pmkid_broadcast','Capture authentication with broad active test','Wi-Fi tests',
        ['sniffpmkid -d'],modes=['WIFI_SCAN_ACTIVE_EAPOL'],channel=True,active=True)
    add('wifi_client_signal','Track a Wi-Fi client signal','Wi-Fi',
        ['foxhunt -s {target.apIndex} {target.index}'],[station()],['WIFI_SCAN_SIG_STREN'])
    for id,title,kind,flag,discover,mode in [
        ('pineapple_signal','Track a Wi-Fi Pineapple','pineapple','-p','wifi_pineapple','WIFI_SCAN_SIG_STREN'),
        ('multissid_signal','Track a multi-SSID device','multissid','-m','wifi_multissid','WIFI_SCAN_SIG_STREN'),
        ('airtag_signal','Track a Find My device signal','airtag','-t','ble_airtags','BT_SCAN_FOX_HUNT'),
        ('flipper_signal','Track a Flipper signal','flipper','-f','ble_flippers','BT_SCAN_FOX_HUNT')]:
        add(id,title,'Bluetooth' if mode.startswith('BT_') else 'Wi-Fi',
            ['foxhunt '+flag+' {target.index}'],[target(kind,discover=discover)], [mode])
    add('wifi_csi','Detect motion with Wi-Fi CSI','Wi-Fi', ['csimotion start {target.index}'],
        [wifi()],['WIFI_SCAN_CSI_MOTION'],check='csi',description='Calibrate against a chosen network, then observe channel changes caused by movement.')
    add('csi_status','Read CSI motion status','Wi-Fi',['csimotion status'],during='csi')
    add('csi_calibrate','Recalibrate CSI motion','Wi-Fi',['csimotion recalibrate'],during='csi')
    for id,title,flavor in [('recon_wifi','Run Wi-Fi recon','wifi'),('recon_ble','Run Bluetooth recon','ble')]:
        add(id,title,'Bluetooth' if flavor=='ble' else 'Wi-Fi',['recon '+flavor],check='recon',timed=True)
    add('recon_status','Read recon status','Device',['recon status'],during='read')
    add('recon_stop','Stop recon','Device',['recon stop'],stop=True)

    # Specialized Bluetooth receiver and transmitter variants.
    for id,title,flavor,mode in [
        ('ble_airtags','Discover Find My devices','airtag','BT_SCAN_AIRTAG'),
        ('ble_ibeacons','Discover iBeacons','ibeacon','BT_SCAN_IBEACON'),
        ('ble_flippers','Discover Flipper devices','flipper','BT_SCAN_FLIPPER'),
        ('ble_flock','Discover Flock devices','flock','BT_SCAN_FLOCK'),
        ('ble_meta','Discover Meta devices','meta','BT_SCAN_RAYBAN')]:
        add(id,title,'Bluetooth',['sniffbt -t '+flavor],modes=[mode])
    add('ble_skimmers','Scan for Bluetooth skimmers','Bluetooth',['sniffskim'],modes=['BT_SCAN_SKIMMERS'])
    add('ble_findmy_sound','Play a Find My device sound','Bluetooth',['findmy -t {target.index}'],
        [target('airtag','Find My device',discover='ble_airtags')],active=True)
    add('ble_spoof','Broadcast a selected Find My identity','Bluetooth tests',['spoofat -t {target.index}'],
        [target('airtag','Find My device',discover='ble_airtags')],['BT_SPOOF_AIRTAG'],active=True)
    for flavor,title,mode in [
        ('sourapple','Sour Apple','BT_ATTACK_SOUR_APPLE'),('applejuice','Apple Juice','BT_ATTACK_APPLE_JUICE'),
        ('windows','Windows Swift Pair','BT_ATTACK_SWIFTPAIR_SPAM'),('samsung','Samsung','BT_ATTACK_SAMSUNG_SPAM'),
        ('google','Google','BT_ATTACK_GOOGLE_SPAM'),('flipper','Flipper','BT_ATTACK_FLIPPER_SPAM'),
        ('all','All supported types','BT_ATTACK_SPAM_ALL')]:
        add('ble_tx_'+flavor,title+' advertising test','Bluetooth tests',['blespam -t '+flavor],modes=[mode],active=True)

    add('remoteid_scan','Discover drone Remote IDs','Wi-Fi',['remoteid scan'],modes=['REMOTE_ID_SCAN_ALL'])
    add('remoteid_track','Track a drone Remote ID','Wi-Fi',['remoteid track {target.index}'],
        [target('remoteid','Remote ID device',discover='remoteid_scan')],['REMOTE_ID_SCAN_TARGET'])
    add('remoteid_list','View drone Remote IDs','Targets',['remoteid list'])
    add('remoteid_clear','Clear drone Remote IDs','Targets',['remoteid clear'])

    # Existing active test modes, made explicit and parameterized.
    active = [
        ('wifi_deauth','Disconnect test for selected networks','deauth','WIFI_ATTACK_DEAUTH','wifi',''),
        ('wifi_deauth_sta','Disconnect test for selected clients','deauth','WIFI_ATTACK_DEAUTH_TARGETED','station',' -c'),
        ('wifi_badmsg','Malformed frame test','badmsg','WIFI_ATTACK_BAD_MSG',None,''),
        ('wifi_badmsg_sta','Malformed frame test for clients','badmsg','WIFI_ATTACK_BAD_MSG_TARGETED','station',' -c'),
        ('wifi_sleep','Sleep frame test','sleep','WIFI_ATTACK_SLEEP',None,''),
        ('wifi_sleep_sta','Sleep frame test for clients','sleep','WIFI_ATTACK_SLEEP_TARGETED','station',' -c'),
        ('wifi_probe_tx','Probe transmission test','probe','WIFI_ATTACK_AUTH','wifi',''),
        ('wifi_sae_tx','SAE commit transmission test','sae','WIFI_ATTACK_SAE_COMMIT','wifi',''),
        ('wifi_csa_tx','Channel switch announcement test','csa','WIFI_ATTACK_CSA','wifi',''),
        ('wifi_quiet_tx','Quiet announcement test','quiet','WIFI_ATTACK_QUIET','wifi',''),
        ('wifi_beacon_rand','Random beacon advertising test','beacon','WIFI_ATTACK_BEACON_SPAM',None,' -r'),
        ('wifi_beacon_list','Saved-name beacon advertising test','beacon','WIFI_ATTACK_BEACON_LIST','ssid',' -l'),
        ('wifi_beacon_ap','Selected-network beacon advertising test','beacon','WIFI_ATTACK_AP_SPAM','wifi',' -a'),
        ('wifi_rickroll','Rick Roll beacon advertising test','rickroll','WIFI_ATTACK_RICK_ROLL',None,''),
        ('wifi_funny','Funny-name beacon advertising test','funny','WIFI_ATTACK_FUNNY_BEACON',None,''),
    ]
    for id,title,flavor,mode,kind,suffix in active:
        f = [target(kind,'Targets',discover='wifi_discover' if kind in ('wifi','station') else None,multiple=True)] if kind else []
        extra = dict(select=dict(kind=kind,field='target')) if kind else {}
        add(id,title,'Wi-Fi tests',['attack -t '+flavor+suffix],f,[mode],channel=True,active=True,**extra)
    add('wifi_deauth_mac','Disconnect test with explicit addresses','Wi-Fi tests',
        ['attack -t deauth -s {source} -d {destination}'],
        [field('source','Source MAC address',maxLength=17,format='mac'),field('destination','Destination MAC address',maxLength=17,format='mac')],
        ['WIFI_ATTACK_DEAUTH_MANUAL'],channel=True,active=True)
    add('portal_start','Host a portal','Wi-Fi tests', ['evilportal -c start -w {file}'],
        [field('file','HTML filename',default='index.html',maxLength=26,format='public_html')],
        ['WIFI_SCAN_EVIL_PORTAL'],check='portal',active=True,
        description='Host the selected HTML page. The dashboard returns when the timer ends.')
    add('portal_name','Use a discovered network name for the portal','Wi-Fi tests',
        ['evilportal -c setap {target.index}','evilportal -c start -w {file}'],
        [wifi(),field('file','HTML filename',default='index.html',maxLength=26,format='public_html')],
        ['WIFI_SCAN_EVIL_PORTAL'],check='portal',active=True)
    add('portal_probe','Host a portal for a discovered probe name','Wi-Fi tests',
        ['karma -p {target.index}'],[target('probe','Requested network name',discover='wifi_probes')],
        ['WIFI_SCAN_EVIL_PORTAL'],check='portal',active=True)
    add('portal_template','Choose the portal HTML file','Device',['evilportal -c sethtml {file}'],
        [field('file','HTML filename',default='index.html',maxLength=26,format='public_html')])

    add('network_join','Connect to a Wi-Fi network','Network',['join -a {target.index} -p {password}'],
        [wifi(),field('password','Network password','password',default='',maxLength=63,required=False)],check='joined',secret=True)
    add('network_saved','Connect using saved Wi-Fi credentials','Network',['join -s'],check='joined')
    add('lan_ping','Discover hosts with ping','Network',['pingscan'],modes=['WIFI_PING_SCAN'],requires='joined')
    add('lan_arp','Discover hosts with ARP','Network',['arpscan'],modes=['WIFI_ARP_SCAN'],requires='joined')
    add('lan_ports','Scan ports on a discovered host','Network',['portscan -a -t {target.index}'],
        [target('ip','Network host',discover='lan_arp')],['WIFI_PORT_SCAN_ALL'],requires='joined')
    services = [('ssh','SSH','WIFI_SCAN_SSH'),('telnet','Telnet','WIFI_SCAN_TELNET'),
                ('dns','DNS','WIFI_SCAN_DNS'),('http','HTTP','WIFI_SCAN_HTTP'),('smtp','SMTP','WIFI_SCAN_SMTP'),
                ('https','HTTPS','WIFI_SCAN_HTTPS'),('rdp','Remote Desktop','WIFI_SCAN_RDP')]
    for service,title,mode in services:
        add('lan_'+service,'Find '+title+' services','Network',['portscan -s '+service],modes=[mode],requires='joined')

    lists = [('wifi','Wi-Fi networks','-a'),('station','Wi-Fi clients','-c'),('ssid','Saved network names','-s'),
             ('airtag','Find My devices','-t'),('ip','Network hosts','-i'),('probe','Probe names','-p'),
             ('ble','Bluetooth devices','-b'),('flipper','Flipper devices','-f'),
             ('pineapple','Wi-Fi Pineapples','-x'),('multissid','Multi-SSID devices','-m')]
    for kind,title,flag in lists:
        add('list_'+kind,'View '+title,'Targets',['list '+flag],resultKind=kind)
    for kind,title,flag in [('wifi','networks','-a'),('station','clients','-c'),('ssid','network names','-s')]:
        add('select_'+kind,'Select '+title,'Targets',['select '+flag+' {target.index}'],
            [target(kind,'Entries to toggle',discover='wifi_discover' if kind!='ssid' else None,multiple=True)])
        add('clear_'+kind,'Clear '+title,'Targets',['clearlist '+flag])
    add('select_filter','Select networks by name','Targets',['select -a -f {filter}'],
        [field('filter','Name filter',default='contains ',maxLength=80,format='filter')])
    add('ssid_add','Add a network name','Targets',['ssid -a -n {name}'],[field('name','Network name',maxLength=32)])
    add('ssid_generate','Generate network names','Targets',['ssid -a -g {count}'],
        [field('count','Number of names','integer',default=5,min=1,max=50)])
    add('ssid_remove','Remove a network name','Targets',['ssid -r {target.index}'],[target('ssid','Network name')])
    add('target_add_ap','Add a network manually','Targets',['add -a -b {mac} -ch {radioChannel} -e {name}'],
        [field('mac','Network MAC address',maxLength=17,format='mac'),
         field('radioChannel','Radio channel','integer',default=1,min=1,max=14),field('name','Network name',maxLength=32)])
    add('target_add_client','Add a client manually','Targets',['add -c -b {mac} -ap {target.index}'],
        [field('mac','Client MAC address',maxLength=17,format='mac'),wifi()])
    add('target_info','Read detailed network information','Targets',['info -a {target.index}'],[wifi()])
    add('geofence_list','View saved geographic fences','Device',['geofence list'],
        description='View configured fences. Location-triggered operation requires GPS.')
    add('geofence_set','Save a geographic fence','Device',['geofence set {slot} {latitude} {longitude} {radius} {name}'],
        [field('slot','Fence slot','integer',default=1,min=1,max=5),
         field('latitude','Latitude','number',min=-90,max=90),field('longitude','Longitude','number',min=-180,max=180),
         field('radius','Radius in miles','number',default=0.1,min=0.1,max=1),field('name','Fence name',maxLength=32)],
        description='Save a fence configuration; GPS is required to evaluate your position against it.')
    add('geofence_clear','Remove a geographic fence','Device',['geofence clear {slot}'],
        [field('slot','Fence slot','integer',default=1,min=1,max=5)])
    add('device_info','Read device information','Device',['info'])
    for kind,title,flag in [('ap','networks','-a'),('ssid','network names','-s')]:
        add('save_'+kind,'Save '+title,'Targets',['save '+flag],check='storage')
        add('load_'+kind,'Load '+title,'Targets',['load '+flag],check='storage')

    add('files','Browse saved captures','Captures',commands=['ls'],view='files')
    add('device_channel','Set the radio channel','Device',['channel -s {radioChannel}'],
        [field('radioChannel','Radio channel','integer',default=1,min=1,max=14)])
    add('read_channel','Read the radio channel','Device',['channel'])
    add('device_settings','Read device settings','Device',['settings'])
    add('change_setting','Change a device setting','Device',['settings -s {setting} {value}'],
        [choice('setting','Setting',[('SavePCAP','Save captures'),('ChanHop','Channel hopping'),
          ('Censor','Censor displayed names'),('ForcePMKID','Force PMKID'),('ForceProbe','Force probe'),('EPDeauth','Portal disconnect mode')]),
         choice('value','Value',[('enable','Enabled'),('disable','Disabled')])])
    add('reset_settings','Restore default device settings','Device',['settings -r'])
    add('device_reboot','Restart the device','Device',['reboot'],restart=True)
    add('device_help','Read firmware help','Device',['help'])
    add('device_protocol','Read firmware protocol information','Device',['protocolinfo --machine guided'])
    add('device_radio','Read radio and task status','Device',['dashboard radio'],during='read')
    add('device_recover','Restore control Wi-Fi','Device',['dashboard recover'],stop=True)
    add('device_stop','Stop the current operation','Device',['stopscan'],stop=True)
    for id,title,command in [('mac_random_ap','Randomize the access-point MAC','randapmac'),
                             ('mac_random_sta','Randomize the station MAC','randstamac')]:
        add(id,title,'Device',[command])
    add('mac_clone_ap','Copy a network MAC to the access point','Device',['cloneapmac -a {target.index}'],[wifi()])
    add('mac_clone_sta','Copy a client MAC to the station interface','Device',['clonestamac -s {target.index}'],[station()])

    # Explicit dependency explanations rather than advertising unavailable hardware.
    unavailable = [
        ('sd_backup','Back up internal files to SD',['backupspiffs'],'Requires an SD card and an SD-enabled board configuration.'),
        ('sd_inspect','Inspect an SD backup',['backupstatus'],'Requires an SD card and an SD-enabled board configuration.'),
        ('sd_restore','Restore internal files from SD',['restorespiffs'],'Requires an SD card and an SD-enabled board configuration.'),
        ('sd_update','Install firmware from SD',['update'],'This build has no SD card; its Wi-Fi update command is not implemented. Update over USB.'),
        ('direct_upload','Upload wardriving logs',['upload'],'Requires the SD/GPS direct-upload feature, which is not enabled on this bare board.'),
        ('gps_data','Read GPS position',['gpsdata'],'Requires a connected GPS module and GPS-enabled firmware.'),
        ('gps_controls','Configure and track GPS',['gps'],'Requires a connected GPS module and GPS-enabled firmware.'),
        ('gps_poi','Record a GPS point of interest',['gpspoi'],'Requires a connected GPS module and GPS-enabled firmware.'),
        ('gps_tracker','Run the GPS tracker',['gpstracker'],'Requires a connected GPS module and GPS-enabled firmware.'),
        ('gps_nmea','Read GPS NMEA sentences',['nmea'],'Requires a connected GPS module and GPS-enabled firmware.'),
        ('gps_wardrive','Record a wardrive',['wardrive'],'Requires GPS and an SD-enabled board configuration.'),
        ('gps_wardrive_poi','Tag a wardriving location',['wardrivepoi'],'Requires an active GPS wardrive.'),
        ('led_controls','Control the LED',['led'],'This bare-board profile has no configured addressable LED.'),
        ('screen_brightness','Change screen brightness',['brightness'],'This board has no display.')]
    for id,title,commands,reason in unavailable:
        add(id,title,'Hardware',commands=commands,available=False,reason=reason)
    return rows

def generate():
    rows = catalog()
    definitions = (ROOT/'firmware/esp32_marauder/WiFiScan.h').read_text(encoding='utf-8-sig')
    modes = dict((name,int(value)) for name,value in re.findall(r'^#define\s+(\w+)\s+(\d+)\s*$', definitions,re.M))
    ids = set()
    for row in rows:
        if row['id'] in ids: raise ValueError('Duplicate tool: '+row['id'])
        ids.add(row['id'])
        row['modes'] = [modes[mode] if isinstance(mode,str) else mode for mode in row['modes']]
    header = '#pragma once\n#ifdef ARDUINO\n#include <pgmspace.h>\n#elif !defined(PROGMEM)\n#define PROGMEM\n#endif\nstruct DashboardToolDefinition { const char* id; const char* json; };\n'
    header += 'static const DashboardToolDefinition TOOL_CATALOG[] PROGMEM = {\n'
    for row in rows:
        raw = json.dumps(row,ensure_ascii=True,separators=(',',':'))
        if len(raw.encode()) > 4096: raise ValueError('Oversized definition: '+row['id'])
        header += '{'+json.dumps(row['id'])+','+json.dumps(raw)+'},\n'
    header += '};\nstatic const unsigned TOOL_CATALOG_COUNT = sizeof(TOOL_CATALOG)/sizeof(TOOL_CATALOG[0]);\n'
    (ROOT/'firmware/esp32_marauder/ToolCatalog.h').write_text(header,encoding='utf-8')
    public = [{k:v for k,v in row.items() if k not in ('steps','modes','select','check','secret')} for row in rows]
    # Omit repeated defaults on the wire; the browser restores the exact metadata.
    compact = []
    for row in public:
        defaults = dict(available=True, reason='', fields=[], timed=False, description=row['title'] + '.')
        item = {k:v for k,v in row.items() if k not in defaults or v != defaults[k]}
        if item.get('fields'):
            item['fields'] = [{k:v for k,v in f.items() if (k,v) not in (('required',True),('type','text'))} for f in item['fields']]
        compact.append(item)
    restore = ".map(t=>({available:true,reason:'',timed:false,description:t.title+'.',...t,fields:(t.fields||[]).map(f=>({type:'text',required:true,...f}))}));\n"
    (ROOT/'web/tools.js').write_text('window.MARAUDER_TOOLS='+json.dumps(compact,ensure_ascii=True,separators=(',',':'))+restore,encoding='utf-8')
    (ROOT/'.cache').mkdir(exist_ok=True)
    (ROOT/'.cache/tool-catalog.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
    print(f'PASS: generated {len(rows)} guided definitions ({sum(r["available"] for r in rows)} available)')
    return rows

if __name__=='__main__': generate()
