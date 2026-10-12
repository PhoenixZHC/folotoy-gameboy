English | [简体中文](ble-input-fc-handoff.zh_CN.md)

# Game Boy BLE fixes and FC handoff (2026-10-09)

## 2026-10-12 controller update

The GameBoy 1.8 fourteen-game full image was flashed to COM14 on 2026-10-12; the user subsequently reported no issues during testing. Acceptance is limited to the exercised behavior, not all games, controllers or long-term stability. See [controller support](controller-test.md) for L1241, home navigation, input testing and Select holds. Historical FC results do not establish GameBoy acceptance.

## Scope and status

Only `E:/code/folotoy-gameboy` is modified here; `E:/code/folotoy-fc` is a read-only reference. The first handoff covered local fixes after 1.4. FC subsequently integrated those BLE fixes; the user confirmed FC controller testing and wrap-up in this conversation. Game Boy now ports FC's ordered input buffer and packages these cumulative changes as **1.5**. Historical packages remain unchanged. The later [fourteen-game image](gameboy-classics-1.5.md) passed authorized COM14 flashing and startup checks; gameplay acceptance is separate. The initial handoff was uncommitted; its cumulative source is now included in the 1.7 archival change. Git records identify the current synchronization state.

The code addresses split-report input loss, common standard button layouts, left-stick controls, Xbox-name descriptor replacement, and BLE failure recovery. Physical compatibility and performance remain unverified. Validation evidence is maintained in [gameboy-validation.md](gameboy-validation.md).

## BLE implementation

| Area | Files relative to the repository | Behavior |
| --- | --- | --- |
| Standard input parser | `components/bluepad32/parser/uni_hid_parser_standard.c`, `include/parser/uni_hid_parser_standard.h` under the same component | Stores state separately for up to eight input report IDs. A new report replaces only its own state; combined buttons and directions retain other reports' held inputs. Handles bitmap and array buttons, hats, signed/unsigned axes and consumer Back/Home. Unknown or truncated reports are ignored with an error log. |
| Selection and real report map | `components/bluepad32/bt/uni_bt_le.c`, `uni_hid_device.c` | Selects and copies a qualifying HID service's real descriptor before parser setup for every BLE candidate. Other services are filtered. Standard BLE Android/generic/Xbox input profiles use the new parser; keyboard and proprietary drivers stay separate. |
| Xbox-name clones | `components/bluepad32/parser/uni_hid_parser_xboxone.c` | Name-based legacy descriptor substitution is disabled on BLE. Bluetooth Classic keeps its old path. A name alone does not prove a controller's layout. |
| Connection recovery | `components/bluepad32/bt/uni_bt_le.c`, `uni_hid_device.c`, `include/uni_hid_device.h` | Checks immediate connection errors and failed connection events before using a handle. Cancels a pending connection on timeout, resumes permitted scanning, disconnects on failed pairing/GATT setup, and reports failed attempts to the UI. Re-encryption key/authentication failure can delete only that peer's old bond and retry pairing once per connection; subsequent failure disconnects. Fresh pairing authentication failure does not delete unrelated bonds. |
| Discovery | `components/bluepad32/bt/uni_bt_le_advertisement.c` | Retains address/type keyed advertising/scan-response merging, 16/128-bit UUID lists and Gamepad/Joystick admission; adds 32-bit UUID lists and 16-bit Service Data identifying HID `0x1812`. Does not accept an arbitrary device based on its name. |
| Left stick | `main/gb_input.c`, `main/gb_input.h`, `main/gamepad.c` | Maps normalized X/Y to directions outside a +/-160 dead zone. D-pad wins on each axis; stick directions participate in activity/wake/release handling. Recentering releases the stick contribution. |

`uni_hid_device_create()` now uses the device initializer so a pending link starts with invalid connection/HID handles instead of zero. The HID descriptor setter rejects null, empty and oversized maps instead of copying more than its 512-byte buffer.

### Common mappings and explicit limits

HID button numbers do not specify physical labels. The generic mapping is a convention inferred from the largest declared button usage, not proof of universal compatibility:

| Declared layout | A/B/X/Y | Select/Start |
| --- | --- | --- |
| Up to 8 buttons | 1/2/3/4 | 7/8 |
| 9-12 buttons | 1/2/3/4 | 9/10 |
| Android-style, more than 12 | 1/2/4/5 | 11/12 |

Known Xbox BLE profiles keep RX/RY as analog pedals; standard profiles support left X/Y, right Z/RZ or RX/RY, simulation Brake/Accelerator, hat and individual D-pad usages. This application's input path does not use rumble/output commands from the replaced standard profiles.

The standard parser uses the existing 256-byte per-device parser storage, with no heap allocation. Limits: 512-byte descriptor, eight input report IDs, 1-16 bits per input field, at most 255 fields per Input item and 512 payload bytes per report. Global Push/Pop and interleaved returns to an earlier report-ID definition are rejected because the vendored BTstack iterator does not interpret them correctly. Unsupported maps fail setup explicitly. Proprietary layouts and alternative physical button labels can still need a dedicated mapping. Device Information Service failures still abort that connection; this work does not claim compatibility with peers requiring that service to be bypassed.

## Ordered input buffer ported back from FC

Two separate problems need separate state retention: the HID parser merges independent report IDs; the application buffer prevents a press/release received entirely between two emulator polls from being overwritten. Ported from FC `main/gb_input.c/.h`, `main/gamepad.c/.h`, gameplay lifecycle integration and `tests/test_gb_input_buffer.c`, as read on 2026-10-09.

`set_sample()` updates both the live sample and the buffer under the existing critical section. Menus use `gamepad_snapshot()`; only the emulation loop uses `gamepad_frame_snapshot()`, once per emulated frame, regardless of display skipping. `gamepad_set_gameplay()` clears on pause/exit and rearms after release on entry/resume, including the save-error resume path. Disconnect clears pending input immediately. All session destruction paths disable the buffer.

The static queue holds 16 complete changed states; duplicate states are not queued. Keeping complete states preserves independent releases and avoids fabricating combinations from sequential taps. Overflow or an oldest event older than 100 ms discards the backlog; `input backlog discarded` reports the count outside the critical section. The latest held state remains available. Dense input may take extra emulated frames to deliver; the age limit is not a latency guarantee. No FC-only trace logging, NES input register code or core changes were copied.

The five host scenarios cover short repeated taps while holding a direction, combinations and order, entry/pause/disconnect/capacity, exact expiry boundaries, and stick/action/recentering. They do not replace Game Boy gameplay testing. FC's older on-disk pending-acceptance notes predate the user's later confirmation; no unreported controller-model matrix is inferred from that confirmation.

## What can be reused from FC rendering

Read-only reference: FC `main/gb_frame_pacing.c`, `main/gb_display.c`, and `docs/performance.md` as found on 2026-10-09.

| FC technique | Game Boy decision |
| --- | --- |
| Count from the last actual submitted draw, rather than a fixed frame modulus | Reused. `gb_frame_pacing_mark_drawn()` runs only after a frame is accepted. A missed opportunity stays due until the display can accept it. |
| Check whether the display worker is free before generating pixels | Reused through `gb_display_idle()`. CPU/APU and PPU timing continue during skipped pixel rendering. |
| Count display completions separately from emulation frames | Reused through `gb_display_completed()` and `display_fps`. A completed unchanged frame counts as a display update even if strip comparison saves all SPI transfers; this is not a count of unique visual frames. |
| Borrow the core framebuffer asynchronously | Not copied. Game Boy already copies a small 2bpp frame for its worker, so the emulator can safely advance while DMA runs. Borrowing would add buffer lifetime constraints. |
| Skip all but NES sprite 0 when no Mapper latch is needed | Not portable. Game Boy already skips `renderLine()` on non-rendered frames while preserving window/timing state; NES sprite-0 and Mapper rules do not apply. |
| Q16 NES APU and 6502 dispatch changes | Not portable to the different Game Boy CPU/APU. |
| FC adaptive thresholds and maximum skip interval | Not copied. Game Boy retains its existing 2/3-frame interval and 20/15 ms thresholds pending measurements. |

Game Boy already fetches background/window data by tile spans and skips unchanged 20-row monochrome display strips. The inspected FC `fc_draw()` still sends the full color display in strips; it is not an additional dirty-region implementation to copy. No GB/GBC core rendering, PPU priority, clock, resolution, audio format or partition layout was changed in this work. No measured frame-rate improvement is claimed.

## Original instructions for the FC implementation chat (historical)

1. Read FC's current rules, context and Git status again; it may have changed after this handoff. Do not overwrite the full FC tree with Game Boy files.
2. Compare and port the BLE files above, register `parser/uni_hid_parser_standard.c` in the component CMake list, and preserve FC-specific modifications. Port the input helper plus its single call in `controller_data()`; do not copy Game Boy's `game_main.c` over FC's application.
3. Port `tests/test_gb_gamepad_hid.c`, `tests/test_bt_le_recovery.c`, the additional advertisement cases and left-stick input cases. Bring over only the host-only stubs actually required by these tests, never into firmware include paths. The native Game Boy command wiring is in `tools/test_gameboy_host.py`; the same cases are registered in `tools/validate.sh`.
4. Preserve FC's current frame pacing, borrowed-buffer drain rules, Q16 APU, sprite optimization and measured-performance records. It already has the scheduling techniques adopted here; do not replace its pacing with Game Boy's lower maximum interval.
5. Run FC host/core/packaging checks and its ESP-IDF 5.5.3 build/layout verification. Record fresh FC evidence; Game Boy results do not validate the FC binary.
6. Obtain separate flashing approval. Check existing firmware identity and partition compatibility before choosing an app-only update. Do not write a full image or erase games/saves as part of a routine update.

## Regression and physical acceptance

The old Android parser was compiled against the split-report test and failed when the stick report cleared held A. The new parser passes that sequence and tests for individual release, simultaneous buttons, media reports, arrays, no-ID reports, signed axes, hat neutral, common 8/12/16-button layouts and malformed reports. Recovery tests execute the production BLE event handlers with radio/GATT side effects stubbed; they do not simulate the complete security protocol or physical radio.

Required device checks: known Xbox and keyboard regression; generic 8/12-button and Xbox-name BLE peers; hold A while moving/recentering the stick, release each input in both orders, Select/Start, menu navigation, wake/release, repeated disconnection/reconnection, stale peer keys, failed/cancelled PIN and pairing. For rendering, use the same ROM and scene before/after and record `emu_fps`, `display_fps`, audio waits/drops and free heap. Code tests and a successful build do not prove these physical results.

## 1.7 device retest and delivery (2026-10-10)

Only GameBoy changed in this follow-up; FC remains untouched. Performance acceptance does not expand the controller compatibility matrix. [Performance](gameboy-performance.md).
