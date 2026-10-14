<p align="right"><a href="gameboy-acceptance.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Game Boy baseline acceptance


## Version 1.9 remote and menu acceptance (2026-10-14)

Version 1.9 ports FC's Xiaomi remote horizontal mapping, RC001/FW2671 report-length compatibility, full shared input-test layout and menu-entry held-key guards. The dedicated mapping requires keyboard HID VID/PID `2717:32B8`; candidate names do not establish compatibility. See [controls](controller-test.md) and [1.9 usage/update form](release/gameboy-1.9-form.md).

Build: PASS (ESP-IDF 5.5.3). Host tests: PASS (40 groups, 0 failures; extended menu cases rerun, unchanged results reused). Repository checks, actionlint 1.7.12, app/layout and clean/preloaded image verification: PASS. The complete Bash entry point was NOT RUN; Windows native equivalents were used. Device tests: PASS for the explicitly exercised scope: approved COM14 full-image write/hash, version 1.9/matching ELF, RC001/FW2671 recognition and compatibility decoding, and user-confirmed pairing, eight keys/releases, test-page display, long Back pause and no held-confirm auto-launch on list entry/game return. A 45-second log window showed Kirby loading and no panic/abort/watchdog/invalid-HID-report markers. The monitor closed; final enumeration no longer showed COM14.

Unverified: repeated reconnects, extended play, all-game/controller compatibility, power-loss saves and a new keyboard regression. The RC001/FW2671 single-key limitation established in FC remains; constructed combinations do not prove simultaneous physical keys. No new performance measurement was made. Earlier sections retain their historical dates; the latest artifact identity is in the [firmware handoff](release/gameboy-firmware.md).

## Global Select return (2026-10-12)

Every existing long-OK back/exit action also accepts a one-second Select hold: Settings (finish editing first), Controller test, Game management, game/empty lists, and Pause (exit after a successful save). Pairing cancel/retry uses the same event, but board buttons remain necessary when no controller input is available before connection/confirmation. Entry and return from child pages require release before another hold, preventing cascading exits. In Controller test, a short Select press is displayed; a one-second hold returns. The GameBoy 1.8 fourteen-game full image was flashed to COM14 on 2026-10-12; the user subsequently reported no issues during testing. Acceptance is limited to the exercised behavior, not all games, controllers or long-term stability.


## Controller batch (2026-10-12)

The GameBoy 1.8 fourteen-game full image was flashed to COM14 on 2026-10-12; the user subsequently reported no issues during testing. Acceptance is limited to the exercised behavior, not all games, controllers or long-term stability. Build, full-image validation and write hash verification passed. Host evidence is recorded below and in the handoff record. A 30-second serial observation showed BLE discovery without panic/watchdog markers, but did not capture complete startup identity lines. No new frame-rate measurements or per-game playthrough results were collected. See [firmware record](release/gameboy-firmware.md).

## 1.7 device retest and delivery (2026-10-10)

With explicit approval, only COM14 address 0x10000 was updated to 1.7. Write/hash, partition compatibility, version and 35-second startup checks passed; game, save, settings and pairing partitions were not written. A 240-second observation captured 12 windows across Kirby and Gradius: emulation 59.6–59.8 fps, display 14.9–29.8 fps, audio enabled and output samples increasing, with no reported crashes, audio/display drops or input backlog discards. The user confirmed substantially improved smoothness and smoother audible sound. This accepts the observed experience, not every game/controller or a constant display frame rate.

Build: PASS, ESP-IDF 5.5.3, production GB_SIM_TEST=OFF; application 2,520,064 / 3,145,728 bytes. Full 8 MiB image and independent verification pass; fourteen-game ROM partition is byte-identical to 1.5.

Host tests: PASS, 28 groups/0 failures; six unchanged supplementary checks reuse previous valid results. Fourteen ROMs, 3,600 frames each, three baseline/candidate pairs agree on cumulative video, PCM, CPU and full RAM state hashes.

Simulator: Kirby and Gradius each pass 6 groups/0 failures over 1,800 frames, with screenshots visually confirmed in gameplay and final PCM peaks 26,740 and 18,620. The isolated entry excludes BLE and real hardware performance.

Device tests: write/startup PASS; the user confirms improved motion and sound in this retest. Unverified: full playthroughs, all-controller compatibility, strictly matched-scene FPS benchmarks, long-duration memory stability and instrumented audio underrun measurement.

[Performance](gameboy-performance.md).

## 1.6 performance revision (2026-10-09)

Production build/image checks, 28 focused host groups, fourteen-ROM differential checks and 6 PassportSim groups pass. The subsequent 1.6 hardware test did not meet user expectations; see the 1.7 retest above. Fixed-CPI simulation with wireless excluded does not establish hardware acceptance. See [performance evidence](gameboy-performance.md). “Current” in the 1.5 section below refers to that delivery date.

## Version 1.5 input and display fixes (2026-10-09)

The current application is `build/release-1.5/FoloToy-AI-Passport.bin`; the latest image is `artifacts/releases/FoloToy-GameBoy-1.5-14games-full.bin`. COM14 full-write verification, fourteen-entry directory readback and 35-second startup checks passed; per-game, multi-key and audio acceptance remain separate. See the [classic package](gameboy-classics-1.5.md) and [validation](gameboy-validation.md). Historical packages remain intact; FC was not modified.

Device checks must include holding either direction while repeatedly tapping A/B, A+B with each release order, stick recentering while holding an action, and entry/pause/resume/screen-off/disconnection without leftover presses. Observe `input backlog discarded` and gameplay response together; bounded buffering can add frame latency. Check the same ROM/scene for display and audio performance. FC controller acceptance is reference evidence only, not Game Boy acceptance. The user's earlier G87 V2 keyboard feedback did not identify the firmware version or enumerate these cases.

## Archived 1.4 status (2026-10-04)

The current local application is `build/ble-discovery-fixes/FoloToy-AI-Passport.bin`; the complete package is `artifacts/releases/FoloToy-GameBoy-1.4-full.bin`. Version 1.4 adds four BLE discovery fixes: no Xbox-name requirement, Joystick admission, HID service UUID detection, and advertising/scan-response merging. It retains the 1.3 functionality and storage/display fixes. See the [controls](../../README.md#bluetooth-keyboards), [firmware guide](release/gameboy-firmware.md), [controller protocol evidence](gameboy-sources.md#reported-controller-protocols), and [build-specific validation](gameboy-validation.md).

- Build: PASS; complete-image layout and checksum verification passed.
- Host tests: PASS, 31 groups, zero failures; earlier 900-frame checks of both supplied ROMs apply to the unchanged emulator core.
- Device tests: NOT RUN for the 1.4 package. The same BLE implementation in the preceding 1.3 clean image passed full COM14 flashing and 45-second first-boot / 20-second reboot serial checks.
- Unverified: 1.4 preloaded boot/game operation, reported controllers, physical keyboards including MCHOSE, gameplay/audio after the PPU repair, real multi-client management and storage faults, and endurance/performance limits. These discovery fixes do not add Bluetooth Classic or resolve the previously reported room freeze, unexpected reboot or music-tempo fluctuations.

The sections below retain historical acceptance evidence; they do not establish that the latest binary passed every earlier device test.

## Historical baseline

The historical rollback package is `artifacts/baselines/20260917-173843-3f40fec9e627/`. App size: 2503632 bytes; SHA-256: `3f40fec9e62789d53e724f01b1598442fc59e0b5ce62e586b216bc96415df98d`. It includes source, dependencies, configuration, application and ELF, but not installed ROMs or user data. This package has a known IDLE watchdog warning discovered during acceptance; it is not an accepted stable release.

The subsequent 2503760-byte application adds a one-tick blocking opportunity at least every 250 ms of continuously late frames. This lets the lower-priority IDLE task run without disabling the watchdog. It was built, layout-verified and flashed app-only to COM14; gameplay regression remains pending. Evidence: `build/stability_acceptance.log` (original warning at 143438 ms) and `build/idle_watchdog_fix_validation.log` (retest).

Candidate archive: `artifacts/baselines/20260917-175524-13c5f6667bb4/`; app SHA-256 `13c5f6667bb4303abfaa6510fceef14f00caac7e0e7d213b3d182b98b1f8eb97`. Both packages passed checksum and source-archive integrity checks. The candidate is not yet a gameplay-accepted stable release.

## Boot animation and screen-off addition

User acceptance: the user confirmed the corrected animation speed, black/silent standby and button wake restoring the original game progress as normal. Serial evidence records screen-off at 56971 ms and wake at 60945 ms, with both display and audio returning `ESP_OK`. This closes the requested functional acceptance; long-duration standby and every individual controller button were not separately enumerated.

The boot/screen-off iteration produced a 2508736-byte application. Build, merged-image layout verification and app-only COM14 flash passed. The 19 focused host groups passed (`build/host-regression-mkj7s765/results.json`); the final chime-envelope test and Chinese glyph coverage were rerun after their corresponding edits. New tests cover logo clipping, the final logo bounds, chime silence, and wake release gating including clock wraparound. The user subsequently confirmed the visual and wake behavior on the board.

The initial full-screen boot rendering was visibly slow. Dirty-strip rendering corrected it; `build/boot_sleep_final_validation.log` records completion at 5090 ms after boot, compared with 7620 ms in the first build. The first serial capture was later interrupted by a device/port error; it does not establish continuous runtime stability. The reproduction uses header-logo pixels and synthesized two-note audio, not an original boot ROM; bit-exact timing, LCD response and speaker sound are not claimed.

The power button controls hardware power. Pause now offers screen-off standby with the emulator paused, panel/backlight off and codec suspended. BLE stays available; wake requires release of old held input, then a fresh board/controller button press. Wake input is consumed until release. Disconnected-controller wake remains on the pause page. Manual visual/audio/wake acceptance passed as reported above; no energy-consumption measurement or deep-sleep claim is made.

## Four-shade interface theme

The subsequent 2509072-byte application unifies the device's Chinese menu pages, management information, pause/settings, status and in-game hint in a DMG-style four-shade LCD palette with pixel borders and inverted selection. Battery level, navigation positions and footer guidance remain. The embedded management web page uses the same colours and squared controls. The app build, merged-image layout check, font coverage and app-only COM14 flash passed. `build/dmg_theme_validation.log` records successful boot animation completion at 5086 ms without a UI error in the captured startup interval. Browser preview was unavailable in this session. The user later confirmed that device menus were readable and other pages looked correct; the embedded web page's new theme has not been separately reviewed in a browser.

The user confirmed that the menus are readable but found the old pale background above the running game. `prepare_game_screen()` still cleared the display with `0xEF9D`; the follow-up uses the shared `GB_LCD_LIGHT` theme colour for game surrounds and boot animation, with game pixels unchanged. The revised app built, passed image layout checks and was flashed app-only to COM14. The user confirmed that the upper/lower game surround and bottom guidance now display correctly.

## Clean 8 MiB release candidate

The unchanged, user-accepted application was rebuilt with ESP-IDF 5.5.3 and packaged as the [clean first-install image](release/gameboy-firmware.md). The file is 8,388,608 bytes, SHA-256 `9d3ac5895714b67095b735896a9ffdf69141d7c0a1c88ed80e7a21fc1824d99c`. Source-image, partition-table, clean SPIFFS, erased ROM/NVS and checksum checks passed. It has not been flashed as a whole to a blank device; its first-boot behavior remains unverified. The previous app-only on-device acceptance applies to the application payload, not to the newly assembled full-Flash installation path.

## Compatibility evidence

| Installed title used in testing | Evidence | Limit |
| --- | --- | --- |
| Pokemon Blue / pokemon_blue | User confirmed display, controls and battery-save recovery | Full playthrough and all translated-ROM variants untested |
| Kirby | User reported noticeably smoother gameplay on current build; final measured windows 51.5 and 54.0 emulated FPS | User still hears variable music tempo, although improved |
| Digimon | User confirmed normal play after white-screen fix iteration | Complete-game compatibility untested; original cause not separately isolated |
| Rainbow Fighter | HBlank polling deadlock reproduced and corrected; user confirmed normal play | Complex scenes need not run at original speed |
| Konami GB Collection 1 | Host scripted-input execution and framebuffer comparison | No explicit user gameplay acceptance recorded |

2048 and Pirate's Folly passed earlier user tests; they are historical evidence, not a claim that those ROMs remain installed. Compatibility attaches to the tested ROM, not merely a title or filename.

GB and dual-mode GBC cartridges run in DMG mode. GBC-only ROMs are rejected; full GBC support is not enabled. GBA is not supported. Supported cartridge types are `00/01/02/03/05/06/0f/10/11/12/13/19/1a/1b/1c/1d/1e`, subject to header/size validation and at most 32 KiB SRAM. The current upload limit within the 4 MiB ROM partition is 4,177,920 bytes (3.984375 MiB) after metadata reservation and 16 KiB bank alignment; contiguous allocation can reduce the next upload size. These constraints are not guarantees of game compatibility.

## Regression results and remaining checks

The focused native runner `tools/test_gameboy_host.py` passed 20 groups in the release-preparation run, including the clean-image privacy and exact-size checks. It covers controller input/discovery, UTF-8 names, pixels, CPU/PPU timing, ROM banking and fetch boundaries, DMG/CGB renderer units, APU tone frequency and block timing, ROM packaging, firmware layout checks, and save deletion/recovery. Save recovery faults include interrupted writes and two invalid copies; fixtures are isolated from device data. Windows host tests do not exercise real SPIFFS durability or physical power loss. Passing CGB renderer units does not enable GBC support in the product.

```powershell
& 'C:\Users\User\.espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe' tools/test_gameboy_host.py
```

At the earlier archive-import stage, `.git` was absent. A local Git repository has since been initialized for publication. The upstream complete gate remains unrun here: the default Bash entry resolves to unusable WSL, and an alternate Bash stops with unsupported MSYS/Mingw and a missing `python3` alias. Native host checks do not replace that gate. The watchdog scheduling change passed a new firmware build and merged-image validation; host units do not validate RTOS scheduling on the board.

| Board check | Current acceptance |
| --- | --- |
| Five-minute Kirby session, disconnect/reconnect, pause/settings/resume, management hotspot enter/exit | Controller pause/settings navigation and web capacity confirmed by user on 2026-09-24; five-minute play, reconnect and hotspot exit remain pending |
| Normal battery save and reload | Previously user-confirmed; new host fault-recovery coverage passed |
| Web upload and Chinese rename | Previously user-confirmed; this pass did not delete any installed ROM |
| Deleting a disposable ROM and its saves through the web page | On-device end-to-end check pending; host save cleanup passed |
| 30-minute endurance, 20 game entry/exit cycles, 10 reconnects | Not completed; do not infer from shorter tests |

## Management fault regression

Final startup capture: `build/storage_quota_final_boot.log`; display, buttons, BLE scan and battery initialization were reached. This short boot check does not verify management-page transactions or gameplay watchdog behavior. No new user-operated regression was requested.

At that earlier stage, the subsequent 2503744-byte application fixed the missing aggregate 3 MiB upload quota. At that time, available upload size was the smaller of the remaining quota and the largest free region; the web API reported the 3 MiB quota as capacity. Existing games were not removed when already above quota. JSON list output also reserves enough space for escaped game names. Build, merged-image verification and app-only COM14 flash passed.

Seventeen host groups passed in `build/host-regression-zhbgenkc/results.json`; the subsequently added deletion-handler group passed separately, making 18 groups. Production storage code is exercised with a RAM Flash model: interrupted transfer, invalid size/CGB-only headers, duplicate names, torn directory publication, failed deletion, retry, rename, space reuse, content corruption and aggregate quota. The model uses a deterministic test digest, not real SHA-256 verification. The production HTTP deletion handler is compiled with isolated dependencies to test shared saves, invalid identifiers, origin rejection, unavailable saves and write/cleanup failures. Existing save-file tests cover actual host file removal. These layers do not establish a real device HTTP transaction or physical power-loss durability.

## Audio limit

Current output is 16 kHz, with native-rate APU blocks and a supported 4.096 MHz ES8311 master clock. This replaces the earlier unsupported 14 kHz configuration; the [validation record](gameboy-validation.md) separates that correction from older listening results. Audio queue waits measure time waiting for a new block; they do not directly count I2S underruns. No queue drops and a passing tone test do not prove uninterrupted music. The user previously reported improved but variable tempo. Game-driven note changes still depend on emulated CPU progress; buffering cannot restore delayed game events. Stable original-speed music in complex scenes remains outside the accepted result.
