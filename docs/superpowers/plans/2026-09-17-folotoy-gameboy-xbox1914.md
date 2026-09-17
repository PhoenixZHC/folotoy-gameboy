<p align="right"><a href="2026-09-17-folotoy-gameboy-xbox1914.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Historical plan: AI Passport Game Boy and Xbox 1914

> This is an archived planning snapshot, not the current implementation checklist. Its original detailed text is preserved in the [Chinese peer](2026-09-17-folotoy-gameboy-xbox1914.zh_CN.md). For implemented behavior and actual acceptance, use the [product README](../../../README.md) and [acceptance record](../../development/gameboy-acceptance.md). Toolchain and workspace observations below reflect the date of planning.

**Goal:** Run original Game Boy games on the FoloToy AI Passport with an Xbox Wireless Controller 1914 over BLE and reliable cartridge saves.

**Architecture:** Keep the AI Passport ESP-IDF/BSP hardware layer, adapt SUMI's Game Boy core behind display, ROM, save and timing interfaces, and use Bluepad32/BTstack for the controller. Do not run Wi-Fi during play. Prefer a compact two-bit grayscale framebuffer and small RGB565 DMA strips over full-screen double buffers. Audio was planned as a later stage, not waived.

## 1. Known facts and conditions to verify

The board has an ESP32-C3, 8 MB Flash, no PSRAM, a 240×320 RGB565 SPI panel, three ADC-ladder buttons on GPIO0, an ES8311 codec and no documented SD-card interface. The original BSP exposes the panel directly; its NimBLE demo was a peripheral/broadcaster, not a controller host. SUMI used Arduino/PlatformIO, SdFat and time dependencies, a 160×144 two-bit image, placeholder audio and an initial 8 KiB SRAM limit, so it could not be used unchanged. The controller model was known to be 1914; its firmware was not yet checked at planning time.

The workspace was initially empty. The author had not found ESP-IDF 5.5.3 at the expected path and explicitly rejected silently substituting 5.5.4. The source baselines were AI Passport `cd73a8a6f1f95e010bfd83a08e2b915e38408308` and SUMI `1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb`. Source observations were not build or device results.

## 2. Original scope and acceptance gates

The first stage called for a GB/DMG firmware with menu, ROM selection, pairing/error states, one bonded controller, reconnect and complete press/release tracking, internal-Flash ROM loading, battery SRAM saves, pause/resume/exit and safe input release on disconnect. Start at 160×144; try proportional 240×216 after measuring speed. Explicitly reject unsupported ROM/save conditions. Full-color GBC, save states, Wi-Fi ROM transfer, OTA, multiple controllers, rumble and AI coexistence were outside the initial scope.

The proposed acceptance target was 95%–105% of original emulation speed in a steady 60-second window for chosen ROMs. Roughly 30 displayed frames/s at 240×216 was only an optimization target. The plan also called for 30 minutes continuous play, 20 game entry/exit cycles, 10 controller reconnects, save restoration and interrupted-write recovery, plus a per-ROM compatibility record. These were targets, not claims of current acceptance.

## 3. Planned modules and files

Reusable hardware belonged in `components/bsp`, core adaptation in `components/gb_core`, and controller input, pixel conversion/display, ROM catalog/saves, runtime and menu in `main/`. ROM import tooling was assigned to `tools/pack_roms.py` and `tools/import_roms.ps1`; host tests would cover input, pixels, storage and timing. `sdkconfig.defaults` and `partitions.csv` carried configuration. The detailed file list in the Chinese original was prospective, not an inventory of the current tree.

## 4. Interface and resource design

The planned `gb_port` interface separated the core from Arduino objects and exposed bounded ROM reads, keys, one-frame stepping, framebuffer access and destruction. A failed ROM read would stop the session instead of supplying false zero bytes. Skipping rendering still had to advance CPU, timers, interrupts and required PPU state. On this single-core board, BLE callbacks should only update a protected input snapshot. Emulation should yield to system tasks, DMA buffers should be reused only after completion, and exit should stop input/emulation before draining display work, saving and releasing resources.

The planning budget placed NVS at `0x9000`/`0x6000`, PHY at `0xF000`/`0x1000`, a 3 MiB app at `0x10000`, a 4 MiB ROM partition at `0x310000`, and 960 KiB SPIFFS saves at `0x710000`, ending at `0x800000`. These addresses later became the current layout, but the plan did not establish build fit. ROMs were to be streamed or mapped, not copied in full to RAM; saves must not be automatically formatted on mount failure.

## 5. Seven proposed stages

1. **Baseline and controller:** Import pinned source without overwriting the plan, verify ESP-IDF and Flash target, pin Bluepad32/BTstack, use only one Bluetooth host stack, and test actual pairing, input, disconnect, reboot and reconnect. Seeing an advertisement alone was not pairing success.
2. **Low-memory display:** Verify panel colors and grayscale, decode four two-bit pixels per byte, show 160×144 at `(40,88)`, then test nearest-neighbor 240×216 at `(0,52)` without reusing in-flight DMA strips. Theoretical SPI transfer estimates of about 9.2 and 20.7 ms per frame excluded overhead and were not device FPS.
3. **Core and first ROM:** Adapt the pinned core; honor cartridge header/type/RAM bounds; reject CGB-only and unsupported types; use a legal test ROM; pace at about 4,194,304 cycles/s and 70,224 cycles/frame; measure simulation FPS, display FPS, BLE state and heap limits.
4. **Flash ROM management:** Validate partition fit, define a bounded versioned ROM directory with name/offset/length/SHA-256, import only the ROM partition with readback verification, identify saves by ROM content, and test corrupt or overflowing entries.
5. **Saves and RTC:** Allocate RAM by cartridge header, use two versioned checked save copies keyed by ROM hash, block battery games if the filesystem fails, test interrupted writes, and state that power-off RTC time cannot advance without a trusted wall clock.
6. **Menu, performance and first-stage acceptance:** Implement menu/pairing/loading/running/paused/saving/error states, key mapping, disconnect pause, mapper compatibility checks, load measurements with rendering/BLE on and off, endurance tests and separate build/host/device reports.
7. **Audio:** Evaluate an APU and its license, generate/test PCM independently, use a small buffered codec worker, handle pause/underrun/exit, then remeasure speed, memory and audible continuity. The suggested 22,050 Hz was a candidate, not a measured hardware value.

## 6. Risks and adjustment rules

The plan identified controller protocol differences, scarce internal RAM, insufficient CPU speed, ROM bank-cache misses, Flash-save stalls or torn writes, overclaimed GBC/SRAM support and toolchain mismatch. It called for measurements or explicit errors rather than hiding unsupported cases or treating theoretical performance as board evidence. A different core or hardware was to be considered only after bottlenecks were demonstrated.

## 7. Execution and delivery boundary at planning time

Writing this plan did not itself clone code, configure tools, build, test or flash. Later implementation required the then-current authorization for flashing, erasing, importing ROMs and publication. The expected handoff was reproducible source and dependencies, checked firmware, ROM tools, save/compatibility documentation and separate build, host and device evidence. Historical consent wording does not supersede current instructions or user approvals.

## 8. Source references

- [AI Passport hardware repository](https://gitee.com/folotoy/ai-passport)
- [SUMI core baseline](https://github.com/psychoplath9450/SUMI/blob/1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb/src/plugins/gb/gb_emulator.h)
- [Bluepad32 ESP-IDF integration](https://bluepad32.readthedocs.io/en/latest/plat_esp32/)
- [Bluepad32 supported controllers](https://bluepad32.readthedocs.io/en/latest/supported_gamepads/)
