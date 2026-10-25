"""Check the production credential state without Wi-Fi or NVS hardware."""
from pathlib import Path
import shutil, subprocess, sys
ROOT = Path(__file__).resolve().parents[1]
(ROOT / '.cache').mkdir(exist_ok=True)
output = ROOT / '.cache' / ('test_access.exe' if sys.platform == 'win32' else 'test_access')
compiler = shutil.which('g++')
command = [compiler] if compiler else [shutil.which('uv') or 'uv', 'run', '--with', 'ziglang==0.14.1', 'python', '-m', 'ziglang', 'c++']
subprocess.run(command + ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-fno-exceptions', '-fno-rtti', '-nostdlib++', str(ROOT / 'tests/test_access.cpp'), '-o', str(output)], check=True)
subprocess.run([str(output)], check=True)
