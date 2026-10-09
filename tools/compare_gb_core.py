"""Compare two compiled bench_gb_core binaries on local ROMs, without distributing ROMs."""
import argparse
import hashlib
import json
import statistics
import subprocess
from pathlib import Path

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline',type=Path)
    parser.add_argument('candidate',type=Path)
    parser.add_argument('roms',type=Path,nargs='+')
    parser.add_argument('--frames',type=int,default=3600)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();results=[]
    for rom in args.roms:
        runs=[[],[]]
        for repeat in range(3):
            for i,exe in enumerate((args.baseline,args.candidate)):
                proc=subprocess.run([str(exe.resolve()),str(rom.resolve()),str(args.frames)],capture_output=True,check=True,timeout=120)
                runs[i].append(json.loads(proc.stdout.decode().splitlines()[-1]))
        expected={key:runs[0][0][key] for key in ('frames','video','audio','state')}
        assert all(all(run[key]==value for key,value in expected.items()) for group in runs for run in group),f'Output changed: {rom}'
        timings=[{key:statistics.median(run[key] for run in group) for key in ('draw_ns','skip_ns','audio_ns')} for group in runs]
        results.append(dict(rom=rom.name,sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),output=expected,timings=timings,runs=runs))
        print(rom.name,json.dumps(timings),flush=True)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(dict(passed=len(results),failed=0,scope='Host timing only; not ESP32 FPS',results=results),indent=2),encoding='utf-8')

if __name__=='__main__':main()
