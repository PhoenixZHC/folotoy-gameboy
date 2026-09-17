<p align="right"><a href="gameboy-firmware.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Game Boy firmware image and GitHub handoff

This product uses the five partitions in the repository-root `partitions.csv`, not the three-partition upstream template. The clean release image is **8,388,608 bytes (8 MiB)**. It is assembled from the ESP32-C3 bootloader at `0x0`, partition table at `0x8000`, application at `0x10000`, and a reproducibly generated initial SPIFFS image at `0x710000`. Every other byte is `0xFF`. In particular, NVS (`0x9000`–`0xEFFF`), PHY (`0xF000`–`0xFFFF`) and the entire ROM region (`0x310000`–`0x70FFFF`) contain no settings, controller bonds or games. The initialized saves filesystem contains only the public `assets/initial_saves/README.txt`, no game progress.

## Build and verify

Activate ESP-IDF 5.5.3 and build for `esp32c3`. The upstream complete gate runs `./tools/validate.sh`; its firmware mode also produces `build/FoloToy-GameBoy-8MB-clean.bin` and a `.sha256` sidecar. On Windows, after `idf.py build`, generate the same image with:

```powershell
& 'C:\Users\User\.espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe' tools/package_gameboy_release.py
& 'C:\Users\User\.espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe' tools/package_gameboy_release.py --verify
Get-FileHash -Algorithm SHA256 -LiteralPath 'artifacts\releases\FoloToy-GameBoy-8MB-clean.bin'
```

The packaging tool checks the fixed partition layout, source images, a freshly regenerated clean SPIFFS image, exact 8 MiB length, and erased unused/ROM/NVS bytes before writing the file. The local output is `artifacts/releases/FoloToy-GameBoy-8MB-clean.bin`; compare its SHA-256 with the adjacent `.sha256` file. Both local outputs are ignored by Git. Do not add commercial or user-supplied ROMs, device Flash readbacks, NVS dumps, saves, or pairing data to source control or a GitHub Release. The smaller `FoloToy-AI-Passport-full.bin` is an ESP-IDF merged firmware image that stops after the application; it is **not** an 8 MiB Flash image.

## Installing and updating

**Only for first installation or an intentional full reset:** write the clean image from Flash offset `0x0`, for example with ESP-IDF's esptool after selecting the actual serial port:

```powershell
& python.exe -m esptool --chip esp32c3 -p COM14 -b 460800 write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB 0x0 'artifacts\releases\FoloToy-GameBoy-8MB-clean.bin'
```

This overwrites the full 8 MiB, including all installed games, battery saves, settings and controller pairing keys. The clean image boots with an empty game list; use Game management and its `FoloToy-GB` Wi-Fi page to upload ROMs you may legally use. For an existing installation, update **only the application partition** at `0x10000` with `build/FoloToy-AI-Passport.bin` and keep the current compatible partition table, ROM, saves and NVS. Do not flash the clean image as an update.

## Repository and release

Commit the source, paired English/Chinese documentation, tests, scripts, configuration and third-party license records. Keep `build/`, `artifacts/releases/`, `artifacts/baselines/`, `sdkconfig`, `managed_components/`, Python caches, ROMs, readbacks and user data out of Git. The tag workflow builds the clean image from committed source and attaches the `.bin` and checksum to the GitHub Release. A successful build and image-layout check do not prove a fresh-device boot; device acceptance and remaining performance limits are recorded in [baseline acceptance](../gameboy-acceptance.md).
