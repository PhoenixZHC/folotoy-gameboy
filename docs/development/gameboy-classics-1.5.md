English | [简体中文](gameboy-classics-1.5.zh_CN.md)

# Game Boy 1.5 classic-game full image

Prepared and flashed to COM14 on 2026-10-09 at the user's request. Firmware code is unchanged from the verified 1.5 build; this is a new ROM selection/package, not another application version. The previous two-game and clean packages remain intact. All ROMs come from the user's local GB ROM folder; source files were not altered. ROMs and generated images are not committed or publicly published.

## Image and capacity

- Full image: `artifacts/releases/FoloToy-GameBoy-1.5-14games-full.bin`, exactly 8,388,608 bytes, with `.bin.sha256` and `.manifest.json` sidecars. SHA-256: `571c5eaba83feef2cb5ccb99a78011869169ebadcef7705fc5a843f7cab26fe3`.
- Game payload: 4,161,536 bytes (4,064 KiB), plus a 4 KiB initial directory. The 4 MiB partition reserves its final 8 KiB for writable directories, leaving 20 KiB before that reserved area. This cannot fit another minimum-size 32 KiB ROM; do not overwrite metadata or claim filler bytes are extra game capacity. Fourteen of sixteen game slots are used.
- Same application: SHA-256 `1df9a9002f7ee1255e0bc7589d21db05a0ce71d3c753df9bea9ac09e7f89282d`; ELF `9445b6bb2116743a91600f75210ee2ca88c72c74eaf69bef276953705d5cac05` in `build/release-1.5`.
- Full installation uses `0x0` and replaces games, saves, settings and bonds. This write was explicitly authorized for COM14; no separate whole-chip erase command was used. Ordinary compatible app upgrades still use `0x10000` and do not install these ROMs.

## Selected games

The selection favors distinct representative series and action, shooting, RPG, strategy and racing. Pokemon variants and duplicate translations are not repeated. Mario uses the 64 KiB translation from this folder rather than the previous 128 KiB translation; it was separately checked in the core. Chinese menu names were checked against the firmware bitmap font.

| Game | KiB | SHA-256 |
| --- | ---: | --- |
| Super Mario Land | 64 | `7c36d11d195fcb1de4ecb157ce48c786aa80813409a94b8c9d1f6a91870d136d` |
| Kirby | 256 | `f2c137516b31f1551fa0b14505dc8290e2fb5040f92abd4cadd4b92c9467b5d6` |
| Pokemon Red | 1024 | `cc61bab91120981f7db9d34bc55a87fc1a4904787427258cc6bceb400bc4051f` |
| Contra | 256 | `7caf08c700e95a72abec129a897ff761247c6bd2b5335c4d47dec68b57afa670` |
| Castlevania Legends | 256 | `ee6fcaee43b78b6e79adc2fd9fc3c81164d7667ab1692039c589d01e9ae42d89` |
| Ninja Gaiden Shadow | 128 | `e81143543bd8d610c924598aa158e27f97a1ad4dea2501f0cf1dc6fa05cf5d04` |
| Battletoads | 256 | `b6466b12cd492cd73decfb9b9f8c90607fadf8065bc1396d0dbf8e7df4315eb0` |
| Dragon Ball Z | 512 | `7ffd1be4b5d309c3cf7638c39fbe80c65bb8c7dd080339c609540fa10ed3186e` |
| Gradius | 128 | `93f3031918c6c2e79829ceabf71c3c3918395fdf05857cb2e7ddb91e86ef3fba` |
| Super Robot Wars | 512 | `c4fe5c08ebe52d527b344723556393c6886403a6dc1d5b479da99ac14cfe4adf` |
| Doraemon Kart | 256 | `ae0fa93fa30ebbf8af77ffc9425a76900ee20c8b55195892066b319d68e0ee1a` |
| Alien Battle | 128 | `806ea56ab42c0a852114a91d97d93ea745ee5aa4015e95233e516d93a890735b` |
| Battle City | 32 | `01b58a3f21492c210f42299a803c84940557655cd6c5efd27a6289a29a8bdd0c` |
| Hammerin Harry | 256 | `40e35ed54dc464da8e97861f6184592f84af5c40da84fed7d2a251688b3f308a` |

## Screening and validation boundaries

All 64 files were inventoried by actual header/size/hash. Two were rejected by the packer; 62 ran for 3,600 emulated frames with the current core/APU. Fifty-nine produced changing nonuniform images; three failed that screen. This broad smoke criterion is not proof of gameplay compatibility.

The final fourteen each ran for 7,200 frames with scripted controls, ROM-I/O failure checking, audio generation and sampled framebuffer inspection. Further Start-button timing checks were used where an intro/menu required them. Puyo Puyo was excluded after persistent near-black output; River City, Yoshi and the alternate Three Kingdoms candidate were not selected because the checked input scripts did not establish progress beyond menus/title. These are selection limits, not a claim that every excluded ROM is inherently unplayable. The host test cannot prove complete playthrough, real-time speed, all saves or physical input behavior.

Local evidence: `build/classics-1.5/inventory.json`, `screening.json`, `recheck-7200.json`, `selected.json`, per-ROM logs/raw frames and contact sheets. The packager and independent `--verify` both passed, matching the existing app and all selected ROM bytes.

COM14 was identified as ESP32-C3 / 8 MB; the full write passed esptool hash verification. The 4 KiB ROM directory was read back and matched all fourteen entries. A 35-second reset/startup window confirmed version 1.5, matching ELF, BLE scanning, one boot, and no crash or ROM/save initialization failure. Evidence: `com14-flash.log`, `com14-directory-read.log`, `com14-boot.log`, `com14-summary.json` under `build/classics-1.5`.

Build: PASS (unchanged 1.5 application reused). Host tests: PASS for selected ROM checks; the prior 34 code-test groups remain valid. Device tests: PASS for write/directory/startup checks only; gameplay observation and user feedback remain separate. Physical image, audio, multi-key behavior, per-game compatibility and save/load must not be inferred from clean startup logs.

## Later runtime feedback and 1.6

After the 1.5 installation, a 240-second serial observation covered Kirby and Gradius: emulation 43.9–59.7 fps, display 14.7–29.5 fps, no reported drops/backlog discards. The user nevertheless reported poor smoothness. 1.6 reuses the identical ROM partition; see [performance](gameboy-performance.md). ROM screening, startup success and no-drop counters do not establish complete-game smoothness.

## 1.7 device retest and delivery (2026-10-10)

With explicit approval, only COM14 address 0x10000 was updated to 1.7. Write/hash, partition compatibility, version and 35-second startup checks passed; game, save, settings and pairing partitions were not written. A 240-second observation captured 12 windows across Kirby and Gradius: emulation 59.6–59.8 fps, display 14.9–29.8 fps, audio enabled and output samples increasing, with no reported crashes, audio/display drops or input backlog discards. The user confirmed substantially improved smoothness and smoother audible sound. This accepts the observed experience, not every game/controller or a constant display frame rate.

[Performance](gameboy-performance.md).
