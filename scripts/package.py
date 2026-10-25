"""Verify the built images contain this exact dashboard, then create a flash package."""
from pathlib import Path
import gzip
import hashlib
import json
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'artifacts'

def package():
    app = (OUT / 'esp32_marauder.ino.bin').read_bytes()
    merged = (OUT / 'esp32_marauder.ino.merged.bin').read_bytes()
    table = (OUT / 'esp32_marauder.ino.partitions.bin').read_bytes()
    packed = gzip.compress((ROOT / '.cache/dashboard.html').read_bytes(), compresslevel=9, mtime=0)
    assert len(app) <= 0x300000, 'Application exceeds its actual 3 MB partition'
    assert len(merged) == 0x1000000, 'Factory image must be exactly 16 MB'
    assert app[0] == 0xe9 and merged[0] == 0xe9, 'Invalid ESP image header'
    assert merged[0x10000:0x10000 + len(app)] == app, 'Merged application does not match'
    assert merged[0x8000:0x8000 + len(table)] == table, 'Merged partition table does not match'
    assert merged[0xe000:0xe004] == bytes([1,0,0,0]), 'OTA data is not initialized to app0'
    assert packed in app, 'Stale firmware: compiled application does not contain the current dashboard'
    partitions = {}
    for offset in range(0, len(table), 32):
        entry = table[offset:offset + 32]
        if entry[:2] != bytes([0xaa,0x50]): break
        _, kind, subtype, address, size, label, flags = struct.unpack('<HBBII16sI', entry)
        name = label.split(bytes([0]))[0].decode('ascii')
        assert address + size <= 0x1000000, 'Partition extends beyond flash'
        partitions[name] = {'offset':address, 'size':size}
    assert partitions['app0'] == {'offset':0x10000, 'size':0x300000}
    assert partitions['spiffs'] == {'offset':0x610000, 'size':0x9e0000}
    files = sorted(OUT.glob('*.bin')) + [OUT / 'build-info.json']
    manifest = {'board':'ESP32-S3 N16R8', 'dashboardGzipBytes':len(packed),
                'applicationBytes':len(app), 'partitions':partitions,
                'files':{path.name:{'bytes':path.stat().st_size, 'sha256':hashlib.sha256(path.read_bytes()).hexdigest()} for path in files}}
    manifest_path = OUT / 'dashboard-manifest.json'
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    target = OUT / 'marauder-dashboard-n16r8.zip'
    with zipfile.ZipFile(target, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in files + [manifest_path]: archive.write(path, path.name)
        archive.write(ROOT / 'README.md', 'README.md')
    print(f'PASS: {len(app):,}-byte application, {len(packed):,}-byte flash page, 16 MB merged image, partition bounds, initialized OTA, exact embedded asset and SHA-256 manifest')
    print(f'Package: {target} ({target.stat().st_size:,} bytes)')

if __name__ == '__main__': package()
