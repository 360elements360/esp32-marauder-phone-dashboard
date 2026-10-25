"""Execute the actual bounded template upload implementation with failure injection."""
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
if not (ROOT / "firmware/esp32_marauder/DashboardTemplates.cpp").exists():
    sys.exit("FAIL: DashboardTemplates helper is not implemented")
(ROOT / ".cache" / "storage_fs").mkdir(parents=True, exist_ok=True)
output = ROOT / ".cache" / ("test_templates.exe" if sys.platform == "win32" else "test_templates")
compiler = shutil.which("g++")
command = [compiler] if compiler else [shutil.which("uv") or "uv", "run", "--with", "ziglang==0.14.1", "python", "-m", "ziglang", "c++"]
subprocess.run(command + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-fno-exceptions", "-fno-rtti", "-nostdlib++", "-I" + str(ROOT / ".deps/ArduinoJson/src"), str(ROOT / "tests/test_templates.cpp"), "-o", str(output)], check=True)
subprocess.run([str(output)], check=True, cwd=ROOT)
