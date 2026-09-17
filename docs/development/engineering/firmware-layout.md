<p align="right">
  <a href="firmware-layout.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Firmware Layout

The upstream AI Passport template targets an ESP32-C3 with 8 MB Flash. The table below describes that template, while this checkout uses the five-partition Game Boy layout in `partitions.csv`. See the [Game Boy image guide](../release/gameboy-firmware.md) for its exact layout and safe flashing instructions.

## Upstream template layout

The upstream template partition table contains exactly:

| Partition | Type/subtype | Offset | Size | Purpose |
| --- | --- | ---: | ---: | --- |
| `nvs` | data/NVS | `0x9000` | `0x6000` | ESP-IDF and application key-value storage |
| `phy_init` | data/PHY | `0xF000` | `0x1000` | PHY initialization data |
| `factory` | app/factory | `0x10000` | `0x7F0000` | The single application image; all remaining Flash |

The template has no OTA slots. This is a starting point, not a restriction on user firmware; the current Game Boy firmware does not use this three-partition table.

## Custom layouts

Users may edit `partitions.csv` to resize, move, add, or remove partitions for
their application. A custom table may use OTA slots, filesystem/resource
partitions, or other application-specific data. Keep the 8 MB device boundary,
avoid overlaps, and make sure the application image is flashed at the start of
an app partition large enough to contain it. When a derivative changes its
layout, update that project's documentation and flashing instructions.

## Enforced validation

Run:

```bash
./tools/validate.sh --firmware
```

The check builds in an isolated directory, creates the merged image, reads the
configured image offsets from `flash_args`, validates the partition-table MD5,
partition bounds, unique labels, and non-overlap, then ensures the application
offset matches an app partition large enough to contain it. The generic layout verifier does not require the upstream template partition list. The Game Boy release packager additionally enforces this checkout's five-partition layout and clean data regions. CI runs both checks.

For a Game Boy release, upload `build/FoloToy-GameBoy-8MB-clean.bin` and its `.sha256` sidecar. The shorter `build/FoloToy-AI-Passport-full.bin` contains bootloader, table and app but does not cover all 8 MiB. The app-only `build/FoloToy-AI-Passport.bin` is used at `0x10000` for a compatible existing installation.

## Flashing and stored data

> **No backup of the firmware already installed on the device is required
> before downloading (flashing) new firmware.** Do not make reading out the
> original firmware or saving a full-Flash dump a prerequisite for this
> workflow. The new firmware replaces the original firmware; this workflow
> does not retain an automatic rollback copy or promise that the original
> firmware can be restored.

Firmware and user data are different. If existing NVS settings, application
records, or files must be kept, export or otherwise save them before flashing
using a method supported by that application. Not requiring an original-firmware
backup does not guarantee data preservation or authorize a full-chip erase.

The clean Game Boy image is written from `0x0` only for first installation or an intentional reset; it overwrites the full Flash, including ROMs, saves and pairing keys. The shorter verified merged image is written from `0x0`. Because the merged file pads
the gaps between images, flashing it can reset the NVS and PHY data regions.
Use the merged image for blank-device provisioning or an intentional complete
refresh. During normal development, use segmented `idf.py flash` when existing
NVS state should be preserved; this also requires a compatible partition layout
and flash targets that do not overwrite those data regions. `idf.py erase-flash`
erases all user data. Do not add it as a routine prerequisite: use it only when
a complete erase is explicitly intended and any data that must be kept has
been saved.
