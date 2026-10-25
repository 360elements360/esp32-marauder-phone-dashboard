"""Exercise the actual catalog parser and recipe validator with ArduinoJson, without radios."""
from pathlib import Path
import shutil, subprocess, sys
ROOT = Path(__file__).resolve().parents[1]
output = ROOT / '.cache' / ('test_tool_runner.exe' if sys.platform == 'win32' else 'test_tool_runner')
compiler = shutil.which('g++')
command = [compiler] if compiler else [shutil.which('uv') or 'uv', 'run', '--with', 'ziglang==0.14.1', 'python', '-m', 'ziglang', 'c++']
flags = ['-std=c++17','-Os','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fno-exceptions','-fno-rtti','-DMARAUDER_DASHBOARD','-I'+str(ROOT/'.deps/ArduinoJson/src')]
obj = ROOT / '.cache/test_tool_runner.o'
# Guard the production parser's frame, independently of the larger test harness.
subprocess.run(command + flags + ['-Wframe-larger-than=1024','-c',str(ROOT/'firmware/esp32_marauder/ToolRunner.cpp'),'-o',str(obj)],check=True)
subprocess.run(command + flags + [str(ROOT/'tests/test_tool_runner.cpp'),str(obj),'-o',str(output)],check=True)
subprocess.run([str(output)],check=True)
