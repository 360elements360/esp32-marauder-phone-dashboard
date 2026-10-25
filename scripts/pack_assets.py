"""Pack one self-contained HTML page into deterministic, flash-resident gzip."""
from pathlib import Path
import gzip
import json
import re
from tool_catalog import generate as generate_tools

ROOT = Path(__file__).resolve().parents[1]

def catalog():
    header = (ROOT / "firmware/esp32_marauder/CommandLine.h").read_text(encoding="utf-8-sig")
    entries = [{"command":"help", "syntax":"help", "group":"SYSTEM"},
               {"command":"dashboard", "syntax":"dashboard recover", "group":"SYSTEM"}]
    for line in header.splitlines():
        match = re.match(r'^const char PROGMEM HELP_\w*CMD\w*\[\] = ("(?:[^"\\]|\\.)*");', line)
        if not match: continue
        syntax = json.loads(match[1]).split("\r")[0].split(" - copy")[0].split(" - inspect")[0].split(" - restore")[0]
        command = syntax.split()[0]
        group = "SYSTEM"
        if command.startswith(("sniff", "scan", "attack", "foxhunt", "ssid", "join", "list", "select", "recon", "remoteid", "channel", "karma", "evilportal", "pingscan", "portscan", "arpscan")): group = "WI-FI / RADIO"
        if command.startswith(("ble", "findmy", "spoofat", "sniffbt", "sniffskim")): group = "BLUETOOTH"
        if command.startswith(("gps", "nmea", "geofence", "wardrive")): group = "GPS · REQUIRES PERIPHERAL"
        if command in ("led", "brightness"): group = "REQUIRES PERIPHERAL"
        if command in ("ls", "backupspiffs", "backupstatus", "restorespiffs", "save", "load"): group = "STORAGE"
        entries.append({"command":command,"syntax":syntax,"group":group})
    return entries

def compact_script(source):
    # Preserve newlines (including ASI) and every character inside string literals.
    # Multiline templates/escaped continuations opt out of indentation removal.
    if '`' in source or re.search(r'\\[ \t]*$', source, re.M):
        return source
    return '\n'.join(line.lstrip(' \t') for line in source.splitlines())

def pack():
    generate_tools()
    entries = catalog()
    definitions = (ROOT / 'firmware/esp32_marauder/WiFiScan.h').read_text(encoding='utf-8-sig')
    modes = {0:'Idle'}
    for name, value in re.findall(r'^#define ((?:WIFI_|BT_|REMOTE_ID_|GPS_)[A-Z_]+) (\d+)\s*$', definitions, re.M):
        value = int(value)
        if value == 0 or value in modes: continue
        label = name.replace('WIFI_', 'Wi-Fi · ').replace('BT_', 'Bluetooth · ').replace('_', ' ').lower()
        label = label.replace('wi-fi', 'Wi-Fi').replace('bluetooth', 'Bluetooth').replace(' ap', ' AP').replace(' gps', ' GPS')
        modes[value] = label
    catalog_js = 'window.MARAUDER_COMMANDS=' + json.dumps(entries, separators=(',', ':'), ensure_ascii=True) + ';\n'
    catalog_js += 'window.MARAUDER_MODES=' + json.dumps(modes, separators=(',', ':')) + ';\n'
    (ROOT / "web/catalog.js").write_text(catalog_js, encoding="utf-8")
    page = (ROOT / "web/index.html").read_text(encoding="utf-8")
    css = (ROOT / "web/style.css").read_text(encoding="utf-8")
    page = page.replace('<link rel="stylesheet" href="style.css">', '<style>' + css + '</style>')
    pattern = r'<script src="([a-zA-Z0-9_-]+\.js)" defer></script>'
    scripts = re.findall(pattern, page)
    if not scripts or scripts[-1] != 'app.js': raise ValueError('Dashboard app must initialize after its modules')
    inline = '\n'.join(compact_script((ROOT / 'web' / name).read_text(encoding='utf-8-sig')) for name in scripts)
    # Inline defer is ineffective: initialize modules in source order after the DOM.
    page = re.sub(pattern, '', page).replace('</body>', '<script>' + inline + '</script></body>')
    raw = page.encode("utf-8")
    compressed = gzip.compress(raw, compresslevel=9, mtime=0)
    if len(compressed) >= 32768: raise ValueError('Dashboard exceeds 32 KiB flash asset budget')
    assert gzip.decompress(compressed) == raw
    header = '#pragma once\n#include <pgmspace.h>\nstatic const uint8_t DASHBOARD_HTML[] PROGMEM = {\n'
    header += '\n'.join(','.join(str(byte) for byte in compressed[i:i+24]) + ',' for i in range(0, len(compressed), 24))
    header += '\n};\n'
    (ROOT / "firmware/esp32_marauder/DashboardAssets.h").write_text(header, encoding="utf-8")
    (ROOT / ".cache").mkdir(exist_ok=True)
    (ROOT / ".cache/dashboard.html").write_bytes(raw)
    print(f'PASS: {len(entries)} command entries; dashboard {len(raw):,} bytes -> {len(compressed):,} bytes gzip; round-trip verified')

if __name__ == '__main__': pack()
