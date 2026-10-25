"""Enforce loop-stack budgets on the actual ESP32 objects after a firmware build."""
from pathlib import Path
import argparse, os, re, shutil, subprocess
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--build',type=Path,default=ROOT/'.cache/build')
parser.add_argument('--objdump')
args = parser.parse_args()
candidates = [args.objdump,shutil.which('xtensa-esp32s3-elf-objdump')]
arduino = Path(os.environ.get('LOCALAPPDATA',''))/'Arduino15/packages/esp32/tools/esp-x32'
candidates += [str(p) for p in sorted(arduino.glob('*/bin/xtensa-esp32s3-elf-objdump.exe'),reverse=True)]
objdump=next((p for p in candidates if p and Path(p).is_file()),None)
if not objdump:raise SystemExit('Set --objdump to the ESP32-S3 toolchain objdump executable')
specs = {
 'DeviceDashboard.cpp': {'DeviceDashboard::workflow()':1024},
 'ToolRunner.cpp': {'ToolRunner::prepare(':1024,'ToolRunner::revalidate(':256},
 'ToolBridge.cpp': {'dashboardToolSelect(char const*, char const*)':512,'dashboardToolRestoreSelection()':512,
                   'dashboardToolResolve(':256,'dashboardToolTargetAt(':512},
 'DashboardBridge.cpp': {'dashboardTargetAt(':512},
}
frames={}
for filename,limits in specs.items():
 source=ROOT/'firmware/esp32_marauder'/filename
 staged=args.build/'sketch'/filename
 obj=args.build/'sketch'/(filename+'.o')
 if not obj.is_file() or not staged.is_file() or source.read_text()!=re.sub(r'^#line[^\n]*\n','',staged.read_text(),count=1) or obj.stat().st_mtime<staged.stat().st_mtime:
  raise SystemExit(f'Build current firmware first: missing or stale {filename} object')
 lines=subprocess.check_output([objdump,'-dC',str(obj)],text=True).splitlines()
 for symbol,maximum in limits.items():
  matches=[]
  for i,line in enumerate(lines[:-1]):
   if re.match(r'^[0-9a-f]+ <',line) and '.literal' not in line and symbol in line and '::operator()' not in line:
    match=re.search(r'\bentry\s+a1,\s+(0x[0-9a-f]+|[0-9]+)',lines[i+1])
    if match:matches.append(int(match.group(1),0))
  if len(matches)!=1:raise SystemExit(f'Could not identify one stack frame for {symbol}: {matches}')
  frame=matches[0]
  if frame>maximum:raise SystemExit(f'Stack regression: {symbol} uses {frame}, limit {maximum}')
  frames[symbol]=frame
  print(f'PASS: {symbol} {frame} bytes (limit {maximum})')
target=sum(frames[s] for s in ['DeviceDashboard::workflow()','ToolRunner::prepare(','dashboardToolResolve(','dashboardToolTargetAt(','dashboardTargetAt('])
selection=sum(frames[s] for s in ['ToolRunner::revalidate(','ToolRunner::prepare(','dashboardToolSelect(char const*, char const*)'])
if target>3072 or selection>2048:raise SystemExit(f'Combined stack budget exceeded: target {target}/3072, selection {selection}/2048')
print(f'PASS: nested target frames {target}/3072 bytes; selection frames {selection}/2048 bytes; HTTP/SDK caller margin remains in 8192-byte loop stack')
