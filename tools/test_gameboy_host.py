"""Run focused Game Boy checks natively; this does not replace the upstream gate."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    (ROOT / 'build').mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='host-regression-', dir=ROOT / 'build'))
    results = []
    cc, cxx = os.environ.get('CC', 'gcc'), os.environ.get('CXX', 'g++')
    includes = ['-Itests/stubs', '-Imain', '-Icomponents/gb_core/include',
                '-Icomponents/gb_core/gb', '-Icomponents/minigb_apu']
    cases = [
        ('gamepad_discovery', ['main/gamepad_discovery.c']),
        ('gb_input', ['main/gb_input.c']),
        ('gb_name', ['main/gb_name.c']),
        ('gb_pixels', ['main/gb_pixels.c']),
        ('gb_render', ['tests/stubs/sdfat_stub.cpp']),
        ('gb_timing', ['tests/stubs/sdfat_stub.cpp']),
        ('gb_rom_mapping', ['tests/stubs/sdfat_stub.cpp']),
        ('gbc_render', ['tests/stubs/sdfat_stub.cpp']),
        ('gb_audio', ['components/minigb_apu/minigb_apu.c']),
        ('gb_pacing', ['main/gb_audio_pacing.c', 'main/gb_frame_pacing.c']),
        ('gb_saves_cleanup', []),
        ('gb_saves_recovery', []),
        ('gb_storage', ['main/gb_name.c']),
        ('gb_boot', ['main/gb_boot.c', 'main/gb_pixels.c']),
    ]

    def run(name, commands):
        status = True
        with (work / (name + '.log')).open('w', encoding='utf-8') as log:
            for args, cwd in commands:
                try:
                    process = subprocess.run(args, cwd=cwd, stdout=log,
                                             stderr=subprocess.STDOUT, timeout=180)
                    status = process.returncode == 0
                except (OSError, subprocess.TimeoutExpired) as error:
                    log.write(str(error))
                    status = False
                if not status:
                    break
        results.append({'name': name, 'passed': status})
        print(f'{name}: {"PASS" if status else "FAIL"}', flush=True)

    for name, sources in cases:
        cpp = any(p.endswith('.cpp') for p in sources)
        executable = work / (name + ('.exe' if os.name == 'nt' else ''))
        command = [cxx if cpp else cc, '-std=c++17' if cpp else '-std=c11',
                   '-O2', '-Wall', '-Wextra', '-Werror'] + includes
        if cpp:
            command += ['-Wno-unused-parameter']
        if name == 'gb_audio':
            command += ['-DMINIGB_APU_AUDIO_FORMAT_S16SYS=1', '-DAUDIO_SAMPLE_RATE=14000']
        command += [f'tests/test_{name}.{"cpp" if cpp else "c"}', *sources,
                    '-o', str(executable)]
        run(name, [(command, ROOT), ([str(executable)], work)])
    for name in ['ui_font', 'verify_firmware', 'package_gameboy_release', 'pack_roms', 'gameboy_layout', 'gb_web_delete']:
        run(name, [([sys.executable, '-B', str(ROOT / f'tests/test_{name}.py')], ROOT)])
    summary = {'passed': sum(item['passed'] for item in results),
               'failed': sum(not item['passed'] for item in results),
               'results': results, 'scope': 'Focused host checks, not device/full upstream gate'}
    (work / 'results.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(f'Results: {work / "results.json"}')
    print(f'PASS={summary["passed"]} FAIL={summary["failed"]}')
    return bool(summary['failed'])


if __name__ == '__main__':
    sys.exit(main())
