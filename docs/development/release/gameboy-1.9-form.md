<p align="right"><a href="gameboy-1.9-form.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# GameBoy 1.9 release form

## English usage instructions

1. Install: connect AI Passport with a USB data cable and flash the full image at `0x0`. Full installation replaces firmware, games, saves, settings and Bluetooth bonds. The fourteen-game edition includes preinstalled games; the clean edition has no games. For an existing compatible GB installation, an app-only update at `0x10000` preserves the other partitions.
2. Connect input: select Connect controller, put the input device into pairing mode, select it with board Up/Down and press OK. Follow the Xiaomi remote's own manual for pairing. For IINE L1241, hold Pairing+A for about three seconds and select IINE GAMEPAD; firmware other than 27.02 requires confirmation. Only supported Bluetooth LE devices work; Bluetooth Classic and USB/2.4 GHz receivers are unsupported.
3. Start: after connection the first entry becomes Game list. Select a game and press A or board OK. Release confirmation before pressing again after entering the list or returning from a game.
4. Controls: controller directions, A/B and Select/Start retain their normal roles. Keyboard W/A/S/D = Up/Left/Down/Right, J/K = A/B, I/L = Select/Start. Xiaomi remote held horizontally: physical Right/Up/Left/Down = game Up/Left/Down/Right; TV = A, Menu = B, Home = Start, Back = Select. OK, Power, Voice and Volume have no game actions. The tested RC001/FW2671 cannot reliably hold multiple keys simultaneously, limiting movement-plus-attack games.
5. Pause and save: hold board OK or Select/keyboard I/remote Back for about one second to open Pause. Existing long-press return actions also accept this gesture; release before another hold after changing pages. Save inside the game, then synchronize the battery save through Pause or exit normally. On next launch use the game's Continue option. These are cartridge battery saves, not arbitrary save states.
6. Test and configure: Controller test uses the same full layout for controllers, keyboards and remotes; unavailable controls remain zero. Hold OK or Select to return. Settings adjusts volume, brightness and sound and displays version 1.9.
7. Manage games: open Game management, connect to the password-free FoloToy-GB hotspot and visit `http://192.168.4.1`. Stay connected if the phone reports no internet. Upload compatible, legally obtained `.gb` files, rename or delete games; leaving the page stops the hotspot. Deletion removes associated saves unless an identical installed ROM still uses them. At most 16 games fit, subject to available space.
8. Troubleshoot: if a device is missing, enable pairing mode and reopen the connection page to scan again. Check Controller test for unresponsive input. Discovery alone does not establish compatibility. For a missing management page, remain on Game management and connect to FoloToy-GB. Check sound/volume for missing audio. GBA and GBC-only games are unsupported.

## English update notes

- Added identity-limited horizontal Xiaomi remote controls, with Back mapped to Select and the existing pause/return hold.
- Fixed RC001/FW2671 unresponsive input caused by the HID descriptor/report-length mismatch.
- Unified controllers, keyboards and remotes in the full test layout with press/release feedback.
- Made the first home entry reflect connection state; fixed held-confirm auto-launch on list entry/game return and empty-list handling after disconnection.

The approved 1.9 fourteen-game full image was installed on COM14. The user confirmed pairing, eight keys/releases, test-page display, long Back pause and menu protection. Reconnect, endurance and universal game/controller compatibility remain unverified. See [firmware identity](gameboy-firmware.md) and [validation](../gameboy-validation.md). This form does not publish a GitHub Release.
