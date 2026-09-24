<p align="right"><a href="gameboy-acceptance.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Game Boy baseline acceptance

## Baseline

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

The output remains 14 kHz and the APU generates native-rate blocks. Audio queue waits measure time waiting for a new block; they do not directly count I2S underruns. No queue drops and a passing tone test do not prove uninterrupted music. The user reports tempo variation remains but is substantially improved. Game-driven note changes still depend on emulated CPU progress; buffering cannot restore delayed game events. Stable original-speed music in complex scenes remains outside the accepted result.
