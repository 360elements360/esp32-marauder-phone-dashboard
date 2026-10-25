"""Reproducible local N16R8 build; does not modify global Arduino packages."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse
import io
import json
import os
import shutil
import subprocess
import sys
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
LIBRARIES = {
    "ArduinoJson": ("bblanchon/ArduinoJson", "v6.18.2"),
    "LinkedList": ("ivanseidel/LinkedList", "v1.3.3"),
    "NimBLE-Arduino": ("h2zero/NimBLE-Arduino", "2.3.8"),
    "ESPAsyncWebServer": ("ESP32Async/ESPAsyncWebServer", "v3.8.1"),
    "AsyncTCP": ("ESP32Async/AsyncTCP", "v3.4.8"),
    "ESP32Ping": ("marian-craciunescu/ESP32Ping", "1.6"),
    "MicroNMEA": ("stevemarple/MicroNMEA", "v2.0.6"),
    "EspSoftwareSerial": ("plerup/espsoftwareserial", "8.1.0"),
}
FQBN = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc,PartitionScheme=custom"

def dependency(item):
    name, (repo, tag) = item
    target = ROOT / ".deps" / name
    if (target / ".version").exists() and (target / ".version").read_text() == tag:
        return
    url = f"https://codeload.github.com/{repo}/zip/refs/tags/{tag}"
    request = urllib.request.Request(url, headers={"User-Agent": "MarauderDashboardBuilder"})
    with urllib.request.urlopen(request, timeout=90) as response:
        data = response.read()
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        prefix = archive.namelist()[0].split("/")[0] + "/"
        for info in archive.infolist():
            relative = info.filename.removeprefix(prefix)
            if not relative or info.is_dir():
                continue
            path = target / relative
            if not path.resolve().is_relative_to(target.resolve()):
                raise ValueError("Unsafe dependency archive")
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(archive.read(info))
    (target / ".version").write_text(tag)
    print(f"Installed {name} {tag}", flush=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--deps-only", action="store_true")
    parser.add_argument("--cli")
    args = parser.parse_args()
    with ThreadPoolExecutor(max_workers=6) as pool:
        list(pool.map(dependency, LIBRARIES.items()))
    if args.deps_only:
        return
    candidates = [args.cli, os.environ.get("ARDUINO_CLI"), shutil.which("arduino-cli")]
    cli = next((x for x in candidates if x and Path(x).is_file()), None)
    if not cli:
        raise SystemExit("Set ARDUINO_CLI or pass --cli to the Arduino CLI executable. Install esp32:esp32@3.3.11 first.")
    installed = json.loads(subprocess.check_output([cli, 'core', 'list', '--format', 'json'], text=True))
    version = next((platform.get('installed_version') for platform in installed.get('platforms', []) if platform.get('id') == 'esp32:esp32'), None)
    if version != '3.3.11':
        raise SystemExit(f'Expected esp32:esp32@3.3.11; installed: {version}. Install the pinned core before building.')
    subprocess.run([sys.executable, str(ROOT / "scripts/pack_assets.py")], check=True)
    command = [cli, "compile", "--fqbn", FQBN, "--libraries", str(ROOT / ".deps"),
               "--build-path", str(ROOT / ".cache/build"), "--output-dir", str(ROOT / "artifacts"),
               "--build-property", "compiler.cpp.extra_flags=-DMARAUDER_S3_N16R8 -DMARAUDER_DASHBOARD -fno-exceptions",
               "--build-property", "compiler.c.extra_flags=-DMARAUDER_S3_N16R8 -DMARAUDER_DASHBOARD",
               "--build-property", "compiler.c.elf.extra_flags=-Wl,-zmuldefs",
               "--warnings", "none", "--jobs", "8", str(ROOT / "firmware/esp32_marauder")]
    subprocess.run(command, check=True)
    metadata = {"upstream": "be4f42811538177f0f18e6ef6f5f681c297942e5", "core": "3.3.11", "fqbn": FQBN}
    (ROOT / "artifacts/build-info.json").write_text(json.dumps(metadata, indent=2) + "\n")

if __name__ == "__main__":
    main()
