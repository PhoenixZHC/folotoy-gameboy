<p align="right"><a href="controller-test.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Controller support and live test


## Xiaomi remote and menu update (2026-10-14)

Version 1.9 ports the FC remote input and menu fixes. Keyboard HID devices with VID/PID `2717:32B8` use horizontal controls: physical Right/Up/Left/Down become Up/Left/Down/Right; TV is A, Menu is B, Home is Start, and Back is Select. Hold Back for one second for the existing pause/return action. Complete names `MI RC` and `Xiaomi RC` can appear as pending pairing candidates; the actual HID descriptor and identity still determine input support. The RC001/FW2671 six-byte descriptor versus eight-byte keyboard payload mismatch has a narrowly scoped compatibility decoder. Ordinary keyboards keep their existing mapping.

All input devices share the full Controller test layout; mapped keyboard/remote inputs are displayed and unavailable controls remain zero. The home entry shows **Connect controller** while disconnected and **Game list** while connected. Entering the list or returning from a game resets the held-key baseline and stale board events, requiring release and a new confirmation before launch. Empty lists also return on disconnection.

On 2026-10-14 the approved 1.9 fourteen-game full image was flashed to COM14 at `0x0`, with the write hash verified. A 45-second startup/input observation confirmed version 1.9, matching ELF, Xiaomi RC001/FW2671 (`2717:32B8`), the report compatibility decoder and Kirby loading; no panic/abort/watchdog or invalid-HID-report markers were seen. The user confirmed pairing, eight keys/releases, test-page display, long Back pause and held-confirm menu protection. COM14 was released. Acceptance is limited to those actions; reconnect, extended play and all-game compatibility remain unverified. FC's RC001/FW2671 capture and user test showed only one simultaneous key; constructed combination tests do not establish physical rollover support. Historical 1.8 images do not contain these changes. The firmware was flashed after separate approval; Source synchronization is recorded in Git; binary artifacts remain local.

## Version 1.9 validation

Build: PASS (ESP-IDF 5.5.3). Host tests: PASS (40 groups, 0 failures); the extended empty-list regression was rerun after adding its cases. Repository checks and actionlint 1.7.12: PASS. Windows native checks were used; the complete Bash gate entry point was NOT RUN. Device tests: PASS for the user-confirmed pairing, eight keys/releases, pause/return, menu protection and test-page display. Reconnect and extended play remain unverified.

Artifacts are in `build/xiaomi-validation/`: app `FoloToy-AI-Passport.bin` (2,532,848 bytes, SHA-256 `af0da2418cb4bdfc4142ee84b9927fe935c773b7544fafe24d1241a7e01fe7f1`), matching ELF SHA-256 `c954b0bd6b2e18c6c07e50191e6909498a618e4ec2cbfad4c6ad5a48708cc736`, clean `FoloToy-GameBoy-1.9-8MB-clean.bin`, and `FoloToy-GameBoy-1.9-14games-full.bin` (8,388,608 bytes, SHA-256 `7619395c66001712431000fa45cf9738b3b37805db4bf82c78a213370a76d47c`). ROM names/hashes match the old 1.8 manifest. Full installation at `0x0` replaces firmware, games, saves, settings and bonds; app-only at `0x10000` requires a compatible GB installation. COM14 now runs the approved GB 1.9 image; it replaced FC 1.5 and its stored data. Old images remain unchanged.

## Global Select return (2026-10-12)

Every existing long-OK back/exit action also accepts a one-second Select hold: Settings (finish editing first), Controller test, Game management, game/empty lists, and Pause (exit after a successful save). Pairing cancel/retry uses the same event, but board buttons remain necessary when no controller input is available before connection/confirmation. Entry and return from child pages require release before another hold, preventing cascading exits. In Controller test, a short Select press is displayed; a one-second hold returns. The GameBoy 1.8 fourteen-game full image was flashed to COM14 on 2026-10-12; the user subsequently reported no issues during testing. Acceptance is limited to the exercised behavior, not all games, controllers or long-term stability.


## Historical 1.8 build identity

Application SHA-256: `cd567b0217eb9f01cdd7789b9c2283afe6cd6d3fe8bc0dccbcbc5d73ee79d519`. The GameBoy 1.8 fourteen-game full image was flashed to COM14 on 2026-10-12; the user subsequently reported no issues during testing. Acceptance is limited to the exercised behavior, not all games, controllers or long-term stability.

## Select pause shortcut

During gameplay, hold Select for about one second to open Pause; board long-press OK remains available. Short Select presses and input before the threshold still reach the game. Select held on entry, resume or reconnect must be released before another long press. Keyboard I shares this mapping. The controller test page accepts board long-press OK or a one-second Select hold.

## Connect and inspect input

Connected controllers can navigate the home screen with D-pad / left-stick up and down, and confirm with A. Board buttons remain usable. Inputs already held on entry or reconnection do not immediately trigger an action. Short controller presses are display-only inside the test page; hold Select for one second or long-press board OK to exit.

For IINE L1241, hold Pairing + A for about three seconds and select IINE GAMEPAD. Firmware 27.02 is the verified protocol baseline. An eight-byte identity matching VID 0710 / PID 0220 but a different firmware version requires explicit board OK confirmation. The screen displays the version and an unverified warning. Long-press OK cancels; 60 seconds without approval disconnects. Approval is not persisted across reconnects, cancellation or reboot. Return to the pairing screen to confirm another connection. Other model identities, malformed identity replies and invalid packets remain rejected.

After connecting a controller, open Home → Controller test, directly below Game management. Settings no longer contains this entry. The page shows both sticks and coordinates, D-pad, A/B/X/Y, shoulders, triggers, stick clicks, Select/Start and the input report count. Standard BLE HID and the IINE private adapter use the same normalized input snapshot, before the emulator's dead zone and button reduction. Held buttons are highlighted; release restores the original color on the next frame. There is no calibration or remapping.

Short controller presses are display-only on this page. Hold Select for one second or long-press board OK to return, preserving the connection. Disconnection immediately clears inputs and shows a disconnected message; keyboards and remotes use the same full test layout. GameBoy's display comparison cache is invalidated before drawing, so resuming gameplay cannot reuse test-page pixels. Drawing reuses the existing LCD stripe buffer.

D-pad directions use arrows. Select / Start sit at the bottom. There are no separate L3 / R3 buttons: clicking a stick changes its circular background, and release restores it immediately without the 600 ms marker. Home and Capture are not shown. Trigger bars show reported values; the measured L1241 sends digital endpoints, so the bars do not establish analog-trigger support. The page does not test vibration, gyro, Turbo configuration or every hardware function.

## Why this pairing mode?

The [manufacturer guide](https://iine.store/pages/pocket-controller-ns-guide-manual) specifies Pairing+A for Android, Pairing+Y for iOS and USB for PC. It does not document the transport used by a PC wireless mode.

The [contributor's protocol record](https://github.com/1ikeMcFlurry/esp32c3-iine-gamepad/blob/codex/main/docs/PROTOCOL.md) describes a private IINE GAMEPAD BLE configuration interface with a temporary input-test channel and keepalive. On 2026-10-12 the user confirmed with the developer that only Android mode was tested; PC wireless remains untested. This clarification narrows the validation claim. Both firmware projects already parse standard Xbox BLE HID from the actual report descriptor, so a compatible PC profile may work directly. The name Xbox Wireless Controller alone does not establish the transport or compatibility; no PC-wireless hardware validation is claimed.

## Source and validation

The private adapter originated in [PR #1](https://github.com/PhoenixZHC/folotoy-gameboy/pull/1), merged as bdc10f2. The live-controls layout is adapted from the same author's gamepad_render.c; attribution and MIT terms are retained in third_party_licenses/iine-gamepad-MIT.txt. The firmware's existing green palette, fonts, settings and LCD transport are used.

Local host checks cover verified-version auto-connect, unknown-version approval, rejection of other identities, no input or mode writes before approval, cancellation, timeout, reconnect, input retention, highlight expiry, clock wrap and drawing bounds. Chinese glyph coverage and native-renderer previews were checked. Build and packaging are checked separately. The Linux validation entry point remains unavailable because WSL lacks virtualization support; Windows native checks are used, and actionlint passed in the final verification.

Device tests: the 1.8 fourteen-game full image was flashed to COM14 and the user reported no issues. The report does not enumerate every controller firmware, reconnection, wake or endurance scenario; those require separate evidence.
