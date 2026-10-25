"""Check complete firmware-command mapping and executable recipe metadata."""
import re
from tool_catalog import catalog, generate, ROOT
from pack_assets import catalog as command_catalog

def main():
    rows = catalog()
    by_id = {r['id']:r for r in rows}
    assert len(rows) == len(by_id), 'Duplicate tool identities'
    expected = {r['command'] for r in command_catalog()}
    represented = {c for row in rows for c in row['commands']}
    assert expected <= represented, 'Commands missing guided coverage: '+str(sorted(expected-represented))
    assert represented <= expected, 'Recipes cite nonexistent commands: '+str(sorted(represented-expected))
    legacy = {'wifi_discover','wifi_inspect','wifi_signal','ble_discover','ble_inspect','ble_signal','wifi_capture'}
    assert by_id['select_filter']['steps'] == ['select -a -f {filter}'], 'Firmware name filtering requires the AP flag'
    assert {r['legacy'] for r in rows if 'legacy' in r} == legacy
    modes = set(re.findall(r'^#define\s+(\w+)\s+\d+\s*$',
        (ROOT/'firmware/esp32_marauder/WiFiScan.h').read_text(encoding='utf-8-sig'),re.M))
    kinds = {'wifi','ble','station','airtag','flipper','pineapple','multissid','probe','ip','ssid','remoteid'}
    checks = {'capture','joined','portal','csi','recon','storage'}
    for row in rows:
        assert row['title'] and row['group'] and row['description'], row['id']
        assert len(row['id']) <= 31, row['id']
        assert not row.get('reason') if row['available'] else bool(row.get('reason')), row['id']
        if not row['available']: continue
        assert row.get('steps') or row.get('legacy') or row.get('view'), row['id']
        fields = {f['name']:f for f in row['fields']}
        assert len(fields) == len(row['fields']) and len(fields) <= 8, row['id']
        for f in fields.values():
            assert f['type'] in ('text','password','integer','number','boolean','select','target'), (row['id'],f)
            if f['type'] == 'target':
                assert f['kind'] in kinds, (row['id'],f)
                if f.get('discover'): assert f['discover'] in by_id, (row['id'],f)
            if f['type'] == 'select':
                opts = [o['value'] for o in f['options']]
                assert len(opts) == len(set(opts)) and f.get('default') in opts, (row['id'],f)
        for step in row['steps']:
            assert step.split()[0] in expected and '\n' not in step and '\r' not in step, (row['id'],step)
            for key in re.findall(r'\{([^}]+)\}', step):
                name, _, suffix = key.partition('.')
                assert name in fields or name in ('channel','seconds'), (row['id'],key)
                if suffix:
                    assert name in fields and fields[name]['type']=='target' and suffix in ('index','apIndex'), (row['id'],key)
        for mode in row['modes']: assert mode in modes if isinstance(mode,str) else 0 <= mode <= 255, (row['id'],mode)
        if row.get('check'): assert row['check'] in checks, row['id']
        if row.get('select'):
            s=row['select']; assert s['field'] in fields and fields[s['field']]['kind']==s['kind'], row['id']
    generate()
    print(f'PASS: all {len(expected)} firmware command families / {len(command_catalog())} catalog entries mapped; '
          f'{sum(r["available"] for r in rows)} available guided tools, '
          f'{sum(not r["available"] for r in rows)} explicit limitations')

if __name__ == '__main__': main()
