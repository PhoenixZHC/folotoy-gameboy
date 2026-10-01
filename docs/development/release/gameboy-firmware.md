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

## Optional preinstalled games

The current local 1.3 package is `artifacts/releases/FoloToy-GameBoy-1.3-full.bin` with an adjacent `.bin.sha256` file. It contains the reviewed application from `build/review-fixes-1.3`, matching the COM14 application update, plus the two supplied games. See the [1.3 changelog](../../CHANGELOG.md) for changes and validation limits. This package has not been publicly released.

Pass repeatable `--rom` arguments to include local games in the normal writable ROM partition. If a source filename is too long, give one `--rom-name` per ROM in the same order (at most 31 UTF-8 bytes each). Source files are read without renaming or modifying them. Without `--output`, this mode writes `artifacts/releases/FoloToy-GameBoy-8MB-preloaded.bin`; omitting `--rom` still produces the empty clean image used by CI.

```powershell
$redRom = 'E:\ROMs\PokemonRed.gb'
$marioRom = 'E:\ROMs\SuperMarioLand.gb'
$packageArgs = @('tools/package_gameboy_release.py', '--build-dir', 'build/review-fixes-1.3', '--rom', $redRom, '--rom-name', 'Pokemon Red', '--rom', $marioRom, '--rom-name', 'Super Mario Land', '--output', 'artifacts/releases/FoloToy-GameBoy-1.3-full.bin')
& python.exe @packageArgs
& python.exe @packageArgs --verify
```

Replace the example paths and names with your local files; the Chinese companion contains the exact command and localized names used for this image. The two checked source files are 1,048,576 and 131,072 bytes respectively; the Mario filename's size suffix does not match its actual length, so validation uses its header and bytes. The preloaded image remains exactly 8 MiB, with empty NVS, PHY and initial saves. The packager verifies both ROM hashes and keeps the last two ROM directory sectors erased and writable. The games appear in the normal list, consume normal capacity, and can be renamed or deleted through Game management. Deletion persists through restart, and freed space can be reused by uploads; there is no boot-time restoration or second embedded copy. Only reinstalling the entire preloaded image restores the factory games.

This is a local first-install image. The command creates files but does not flash or publish anything. `--verify` requires the same ROM inputs and names and checks the complete image and checksum sidecar. A preloaded image fails clean-image verification when the ROM arguments are omitted. Keep the supplied ROMs and their generated image outside source control and the automatic public release flow.

## Installing and updating

**Only for first installation or an intentional full reset:** write the clean image from Flash offset `0x0`, for example with ESP-IDF's esptool after selecting the actual serial port:

```powershell
& python.exe -m esptool --chip esp32c3 -p COM14 -b 460800 write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB 0x0 'artifacts\releases\FoloToy-GameBoy-8MB-clean.bin'
```

This overwrites the full 8 MiB, including all installed games, battery saves, settings and controller pairing keys. The clean image boots with an empty game list; use Game management and its `FoloToy-GB` Wi-Fi page to upload ROMs you may legally use. For an existing installation, update **only the application partition** at `0x10000` with `build/FoloToy-AI-Passport.bin` and keep the current compatible partition table, ROM, saves and NVS. Do not flash the clean image as an update.

The preloaded variant is also written from `0x0` on first installation and also overwrites all games, saves, settings and bonds. It starts with the selected games in the list. It is not an application-only update: flashing only `FoloToy-AI-Passport.bin` does not install these games. Obtain separate approval before using either full image on an existing device.

## Repository and release

Commit the source, paired English/Chinese documentation, tests, scripts, configuration and third-party license records. Keep `build/`, `artifacts/releases/`, `artifacts/baselines/`, `sdkconfig`, `managed_components/`, Python caches, ROMs, readbacks and user data out of Git. The tag workflow builds the clean image from committed source and attaches the `.bin` and checksum to the GitHub Release. A successful build and image-layout check do not prove a fresh-device boot; device acceptance and remaining performance limits are recorded in [baseline acceptance](../gameboy-acceptance.md).
