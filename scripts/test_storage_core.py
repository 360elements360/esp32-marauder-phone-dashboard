"""Validate the allocation-free bounded target-list record codec."""
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
if not (ROOT / "firmware/esp32_marauder/DashboardStorageFormat.h").exists():
    sys.exit("FAIL: DashboardStorageFormat codec is not implemented")
(ROOT / ".cache").mkdir(exist_ok=True)
output = ROOT / ".cache" / ("test_storage_core.exe" if sys.platform == "win32" else "test_storage_core")
compiler = shutil.which("g++")
command = [compiler] if compiler else [shutil.which("uv") or "uv", "run", "--with", "ziglang==0.14.1", "python", "-m", "ziglang", "c++"]
subprocess.run(command + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-fno-exceptions", "-fno-rtti", "-nostdlib++", str(ROOT / "tests/test_storage_core.cpp"), "-o", str(output)], check=True)
subprocess.run([str(output)], check=True, cwd=ROOT)
