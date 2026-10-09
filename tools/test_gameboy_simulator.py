"""Exercise isolated GB_SIM_TEST firmware in PassportSim; no real device access."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('simulator',type=Path)
    parser.add_argument('image',type=Path)
    parser.add_argument('elf',type=Path)
    parser.add_argument('--output',type=Path,default=ROOT/'build/sim-tests')
    parser.add_argument('--frames',type=int,default=1800)
    parser.add_argument('--audio-frame',type=int,help='Capture at a known audible point (multiple of 120), rather than the final frame')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    if args.frames<120 or args.frames%120:parser.error('--frames must be a positive multiple of 120')
    if args.audio_frame and (args.audio_frame<240 or args.audio_frame>args.frames or args.audio_frame%120):
        parser.error('--audio-frame must be a multiple of 120 between 240 and --frames')
    home=out/'state';config=home/'config/corpus.toml';config.parent.mkdir(parents=True,exist_ok=True)
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    corpus='gb-'+digest(args.image)[:12]
    config.write_text(f'[{corpus}]\nbin = "{args.image.resolve().as_posix()}"\nelf = "{args.elf.resolve().as_posix()}"\n',encoding='utf-8')
    env=dict(os.environ,PASSPORTSIM_HOME=str(home));commands=[];passed=0;instance=None
    def call(label,*command):
        proc=subprocess.run([str(args.simulator.resolve()),'--output','json',*command],env=env,capture_output=True,timeout=60)
        (out/(label+'.json')).write_bytes(proc.stdout)
        data=json.loads(proc.stdout)
        commands.append(dict(command=label,exit_code=proc.returncode))
        if proc.returncode and not(proc.returncode==10 and data.get('result')=='pass'):
            raise RuntimeError(f'{label}: exit {proc.returncode}: {data.get("message",data.get("status"))}')
        if data.get('code') or data.get('receipt',{}).get('hle',{}).get('tripwires_hit'):
            raise RuntimeError(f'{label}: simulator fault')
        return data
    def artifact(data,section=None):
        value=data[section] if section else data
        receipt=next(item for item in data['artifacts'] if item['path']==value['path'])
        matches=[p for p in (home/'data/artifacts').glob('**/'+instance+'/'+value['path'])
                 if p.stat().st_size==receipt['bytes'] and digest(p)==receipt['sha256']]
        if not matches:raise RuntimeError('No artifact matches receipt hash')
        target=out/matches[0].name;shutil.copy2(matches[0],target);return target.name
    try:
        data=call('start','start',corpus,'--boot','none');instance=data['instance']
        assert data['receipt']['binding']['app_elf_sha256']==digest(args.elf),'Wrong ELF'
        for label,marker in [('theme','GB_SIM_THEME_READY'),('lifecycle','GB_SIM_LIFECYCLE_OK'),('frames','GB_SIM_FRAMES=120')]:
            data=call(label,'run','serial:/'+marker+'/','--instance',instance,'--timeout','20s','--wall-budget-ms','25000')
            assert data['status']=='matched',label
            passed+=1
        audio=None
        for frame in range(240,args.frames+1,120):
            data=call('frames-'+str(frame),'run','serial:/GB_SIM_FRAMES='+str(frame)+'/','--instance',instance,'--timeout','20s','--wall-budget-ms','25000')
            assert data['status']=='matched',frame
            print(f'Simulated {frame} frames',flush=True)
            if frame==args.audio_frame:
                audio=call('audio','audio_capture','--instance',instance,'--duration-ms','500','--wav')
        screen=call('screen','screenshot','--instance',instance)
        assert screen['w']==240 and screen['h']==320 and screen['panel']['display_on'] and not screen['panel']['sleep_in']
        screen_path=artifact(screen);passed+=1
        if audio is None:
            audio=call('audio','audio_capture','--instance',instance,'--duration-ms','500','--wav')
        assert audio['analysis']['peak']>0,'Silent PCM capture'
        wav_path=artifact(audio,'wav');passed+=1
        call('button','input','ok','click','--instance',instance,'--then-run','300ms')
        serial=call('serial','serial','read','--instance',instance,'--cursor','0')
        assert 'GB_SIM_BUTTON=2' in json.dumps(serial)
        assert not any(s in json.dumps(serial) for s in ('Guru Meditation','assert failed','Task watchdog got triggered'))
        passed+=1
        report=dict(passed=passed,failed=0,scope='Isolated test entry; no BLE, no real hardware timing or listening acceptance',
                    firmware_sha256=digest(args.image),elf_sha256=digest(args.elf),screen=screen_path,audio=wav_path,
                    audio_analysis=audio['analysis'],frames=args.frames,
                    audio_frame=args.audio_frame or args.frames,
                    timing_lint=screen['receipt'].get('timing_lint',[]))
    except Exception as error:
        report=dict(passed=passed,failed=1,error=str(error))
        if instance:
            try:call('failure-serial','serial','read','--instance',instance,'--cursor','0')
            except Exception as serial_error:report['serial_error']=str(serial_error)
    finally:
        if instance:
            try:call('stop','stop',instance)
            except Exception as error:
                report['cleanup_error']=str(error);report['failed']+=1
    report['commands']=commands
    (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False));return bool(report['failed'])

if __name__=='__main__':raise SystemExit(main())
