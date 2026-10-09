<p align="right"><a href="gameboy-performance.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Game Boy 1.7 performance and simulator checks

## 1.7 device retest and delivery (2026-10-10)

With explicit approval, only COM14 address 0x10000 was updated to 1.7. Write/hash, partition compatibility, version and 35-second startup checks passed; game, save, settings and pairing partitions were not written. A 240-second observation captured 12 windows across Kirby and Gradius: emulation 59.6–59.8 fps, display 14.9–29.8 fps, audio enabled and output samples increasing, with no reported crashes, audio/display drops or input backlog discards. The user confirmed substantially improved smoothness and smoother audible sound. This accepts the observed experience, not every game/controller or a constant display frame rate.

Forced inlining in 1.6 grew the CPU interpreter to about 29 KiB. Its virtual work time improved by 19.4%, but hardware did not noticeably improve. In 1.7 the interpreter is about 9.2 KiB and, together with the 220-byte short read path, resides in instruction RAM. This avoids repeated Flash instruction fetches at a cost of about 9.6 KiB of internal RAM. Scanline palette reuse, window-background clipping and adaptive 2–4-frame drawing remain. Non-sensitive sound/volume/output-sample logs were added. Cache pressure is an explanation supported by code size and observed behavior, not an independently measured cache-hit rate.

Silence investigation: 1.6 persisted volume=70 and sound=0. Only three non-sensitive settings were recorded; raw NVS was not saved. Toggling sound restored audio generation, but audible-mode emulation still reached 40.5 fps. The 1.7 boot confirms sound=1 and volume=70, and the user confirms audible sound. User defaults and the APU algorithm were not changed; nonzero volume is distinct from the sound-enable flag.

Build: PASS, ESP-IDF 5.5.3, production GB_SIM_TEST=OFF; application 2,520,064 / 3,145,728 bytes. Full 8 MiB image and independent verification pass; fourteen-game ROM partition is byte-identical to 1.5.

Host tests: PASS, 28 groups/0 failures; six unchanged supplementary checks reuse previous valid results. Fourteen ROMs, 3,600 frames each, three baseline/candidate pairs agree on cumulative video, PCM, CPU and full RAM state hashes.

Simulator: Kirby and Gradius each pass 6 groups/0 failures over 1,800 frames, with screenshots visually confirmed in gameplay and final PCM peaks 26,740 and 18,620. The isolated entry excludes BLE and real hardware performance.

Device tests: write/startup PASS; the user confirms improved motion and sound in this retest. Unverified: full playthroughs, all-controller compatibility, strictly matched-scene FPS benchmarks, long-duration memory stability and instrumented audio underrun measurement.

Test limits and failures: early Start was ignored during the intro; a later single Start reached only Gradius options. The final fixture presses Start at frame 900 and confirms Gradius at 1080, without repeating Start for Kirby. A separate 500 ms capture at frame 1440 was silent; the same firmware was audible at frame 1800. This does not establish uninterrupted audio or no underruns. Failed directories sim-gradius-compact, sim-gradius-compact-final and sim-1.7-gradius-gameplay are retained. Fixed CPI does not model real instruction-cache competition. Existing st7789.sleep_cycle_too_fast.0x11 lint and unmodeled peripherals remain. The 1.6 values below are historical, not a claimed 1.7 virtual speedup.

`build/host-regression-bt783ls5/results.json`, `build/perf-all14-compact.json`, `build/sim-1.7-kirby-final`, `build/sim-1.7-gradius-final`, `build/release-1.7/com14-summary.json`, `build/release-1.7/com14-play-summary.json`.

## Historical 1.6 results and limits (2026-10-09)

Version 1.6 reduced interpreter and rendering work and reacted earlier to missed frame budgets. The fourteen local games and their ROM partition were unchanged from the [1.5 classic selection](gameboy-classics-1.5.md). Build and image checks passed, but the subsequent physical test did not meet expectations. It was superseded by 1.7; simulator time is not ESP32-C3 frame rate.

The preceding 1.5 full image was installed on COM14 with successful write verification, fourteen-entry directory readback and startup observation. A subsequent 240-second observation captured Kirby and Gradius: 14 reporting windows, emulation 43.9–59.7 fps, display updates 14.7–29.5 fps, no reported audio/display drops or input backlog discards, and maximum audio queue wait 13,970 microseconds. The user reported that operation was not smooth. A queue wait is not an I2S underrun measurement, and no-drop counters do not establish listening quality. Evidence: `build/classics-1.5/com14-play-summary.json` and `com14-play.log`.

## Changes

- Keep mapped ROM, HRAM and ordinary WRAM reads in a small inline path; retain mapper, CGB bank selection, cheats and I/O behavior in the existing slow decoder. HRAM excludes `0xffff` (interrupt enable). Profiling the fixed Kirby input script found 4,739,615 reads at `0xff8c` over 3,600 frames.
- Decode each background/object palette once per scanline. Skip background pixels covered by the window and remove redundant line clearing. Sprite order, transparency, window line advancement and emulated PPU timing are unchanged.
- Reassess display load every 60 emulated frames instead of 120. Increase the draw interval above 16 ms average work, recover below 14 ms, and bound it to 2–4 frames. This favors emulation/audio cadence under load and can reduce display updates; it does not promise a constant display frame rate.
- Keep the existing integer APU, 16 kHz output, CPU jump tables and panel clock. Reducing CPU load is intended to give audio production more consistent time; the audio synthesis algorithm itself is unchanged. FC-specific NES rendering/APU changes were not copied.

## Evidence

| Check | Result |
| --- | --- |
| Focused host regression | 28 groups passed, 0 failed (`build/host-regression-3pk1j7iu/results.json`); expanded RAM boundary test subsequently passed |
| Fourteen-ROM differential run | Each ROM: 3,600 frames, three paired baseline/candidate runs; accumulated framebuffer, PCM, CPU register, WRAM, VRAM, HRAM, OAM and SRAM hashes agree (`build/perf-all14-final.json`) |
| PassportSim 1.0.1 | Baseline and candidate each passed 6 groups: menu startup, asynchronous display/session lifecycle, 1,800 Kirby frames, screen capture, nonzero PCM capture and board-button injection |
| Virtual work time over 1,800 frames | 20,212,883 → 16,291,785 microseconds, a 19.4% reduction in the simulator |
| Final virtual screen | Same pixel SHA-256: `13d22051c88e87777c3690f8a4332555f5775feb82e10d5cff35d978e2a598d2` |
| Audio observation | Both reported 480,066 output samples at frame 1,800; captured peak 17,899. Wall-time captures are not sample-aligned. Exact synthesis regression is checked by the host PCM hash, not by comparing these WAV files |

Virtual receipts and artifacts are in `build/sim-baseline-final` and `build/sim-candidate-final`. Core source/ELF bindings are in `build/perf-baseline/binding.json` and `build/perf-candidate/binding.json`. The baseline build explicitly recompiles after copying the preserved source: `Copy-Item` retaining an old timestamp can otherwise make Ninja reuse an unrelated object. Earlier timing comparisons without that control are superseded.

The test entry initially overflowed its stack with a 5,760-byte local comparison buffer; this test-only buffer is now static. A repeated Start script paused Kirby and produced a silent capture; final tests press Start once at frame 600 and apply direction/A after frame 1,200. These failed development attempts are not counted as passing production tests.

PassportSim reports the existing `st7789.sleep_cycle_too_fast.0x11` timing lint and unmodeled peripheral accesses. Its fixed CPI and isolated, non-wireless entry cannot measure real cache/SPI/BLE contention, controller compatibility or speaker quality. The screenshot was visually inspected in a running Kirby level. No simulator image should be flashed as the user firmware.

## Reproduction

Use the project's ESP-IDF 5.5.3 environment. Build an isolated simulator entry with `idf.py -B build/perf-sim -D SDKCONFIG=build/perf-sim/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults -D GB_SIM_TEST=ON build`. Package a locally supplied ROM using `tools/package_gameboy_release.py`, then run:

```text
python tools/test_gameboy_simulator.py <passportsim.exe> <full-test-image.bin> <matching.elf> --output build/sim-tests --frames 1800
```

The script uses a workspace-local simulator home, checks the exact ELF binding, verifies artifacts against receipt hashes, retains raw JSON and always stops its instance. It never calls the simulator's physical-device flashing commands. In version 1.0.1 a matched `run` may exit 10 with `result=pass`; only that documented combination is accepted.

`tests/bench_gb_core.cpp` compiles with the real GB header, `tests/stubs/sdfat_stub.cpp`, and minigb APU at 16 kHz/S16. Build one binary against the preserved baseline header and one against the current header, then use `tools/compare_gb_core.py <baseline.exe> <candidate.exe> <local-roms...> --output <report.json>`. ROMs and generated binaries remain outside Git. Host timings are useful for screening regressions, not for device FPS claims.

Build production separately with `GB_SIM_TEST=OFF`; release inputs are in `build/release-1.6`. The authoritative image hashes and device status are in [validation](gameboy-validation.md).
