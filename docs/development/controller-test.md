<p align="right"><a href="controller-test.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Controller support and live test

## Global Select return (2026-10-12)

Every existing long-OK back/exit action also accepts a one-second Select hold: Settings (finish editing first), Controller test, Game management, game/empty lists, and Pause (exit after a successful save). Pairing cancel/retry uses the same event, but board buttons remain necessary when no controller input is available before connection/confirmation. Entry and return from child pages require release before another hold, preventing cascading exits. In Controller test, a short Select press is displayed; a one-second hold returns. The GameBoy 1.8 fourteen-game full image was flashed to COM14 on 2026-10-12; the user subsequently reported no issues during testing. Acceptance is limited to the exercised behavior, not all games, controllers or long-term stability.


## Current build identity

Application SHA-256: `cd567b0217eb9f01cdd7789b9c2283afe6cd6d3fe8bc0dccbcbc5d73ee79d519`. The GameBoy 1.8 fourteen-game full image was flashed to COM14 on 2026-10-12; the user subsequently reported no issues during testing. Acceptance is limited to the exercised behavior, not all games, controllers or long-term stability.

## Select pause shortcut

During gameplay, hold Select for about one second to open Pause; board long-press OK remains available. Short Select presses and input before the threshold still reach the game. Select held on entry, resume or reconnect must be released before another long press. Keyboard I shares this mapping. The controller test page accepts board long-press OK or a one-second Select hold.

## Connect and inspect input

Connected controllers can navigate the home screen with D-pad / left-stick up and down, and confirm with A. Board buttons remain usable. Inputs already held on entry or reconnection do not immediately trigger an action. Short controller presses are display-only inside the test page; hold Select for one second or long-press board OK to exit.

For IINE L1241, hold Pairing + A for about three seconds and select IINE GAMEPAD. Firmware 27.02 is the verified protocol baseline. An eight-byte identity matching VID 0710 / PID 0220 but a different firmware version requires explicit board OK confirmation. The screen displays the version and an unverified warning. Long-press OK cancels; 60 seconds without approval disconnects. Approval is not persisted across reconnects, cancellation or reboot. Return to the pairing screen to confirm another connection. Other model identities, malformed identity replies and invalid packets remain rejected.

After connecting a controller, open Home → Controller test, directly below Game management. Settings no longer contains this entry. The page shows both sticks and coordinates, D-pad, A/B/X/Y, shoulders, triggers, stick clicks, Select/Start and the input report count. Standard BLE HID and the IINE private adapter use the same normalized input snapshot, before the emulator's dead zone and button reduction. Held buttons are highlighted; release restores the original color on the next frame. There is no calibration or remapping.

Short controller presses are display-only on this page. Hold Select for one second or long-press board OK to return, preserving the connection. Disconnection immediately clears inputs and shows a disconnected message; a keyboard gets a keyboard-specific message. GameBoy's display comparison cache is invalidated before drawing, so resuming gameplay cannot reuse test-page pixels. Drawing reuses the existing LCD stripe buffer.

D-pad directions use arrows. Select / Start sit at the bottom. There are no separate L3 / R3 buttons: clicking a stick changes its circular background, and release restores it immediately without the 600 ms marker. Home and Capture are not shown. Trigger bars show reported values; the measured L1241 sends digital endpoints, so the bars do not establish analog-trigger support. The page does not test vibration, gyro, Turbo configuration or every hardware function.

## Why this pairing mode?

The [manufacturer guide](https://iine.store/pages/pocket-controller-ns-guide-manual) specifies Pairing+A for Android, Pairing+Y for iOS and USB for PC. It does not document the transport used by a PC wireless mode.

The [contributor's protocol record](https://github.com/1ikeMcFlurry/esp32c3-iine-gamepad/blob/codex/main/docs/PROTOCOL.md) describes a private IINE GAMEPAD BLE configuration interface with a temporary input-test channel and keepalive. On 2026-10-12 the user confirmed with the developer that only Android mode was tested; PC wireless remains untested. This clarification narrows the validation claim. Both firmware projects already parse standard Xbox BLE HID from the actual report descriptor, so a compatible PC profile may work directly. The name Xbox Wireless Controller alone does not establish the transport or compatibility; no PC-wireless hardware validation is claimed.

## Source and validation

The private adapter originated in [PR #1](https://github.com/PhoenixZHC/folotoy-gameboy/pull/1), merged as bdc10f2. The live-controls layout is adapted from the same author's gamepad_render.c; attribution and MIT terms are retained in third_party_licenses/iine-gamepad-MIT.txt. The firmware's existing green palette, fonts, settings and LCD transport are used.

Local host checks cover verified-version auto-connect, unknown-version approval, rejection of other identities, no input or mode writes before approval, cancellation, timeout, reconnect, input retention, highlight expiry, clock wrap and drawing bounds. Chinese glyph coverage and native-renderer previews were checked. Build and packaging are checked separately. The Linux validation entry point remains unavailable because WSL lacks virtualization support; Windows native checks are used, and actionlint passed in the final verification.

Device tests: the 1.8 fourteen-game full image was flashed to COM14 and the user reported no issues. The report does not enumerate every controller firmware, reconnection, wake or endurance scenario; those require separate evidence.
