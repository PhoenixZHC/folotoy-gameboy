<p align="right"><a href="gameboy-1.8-form.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# GameBoy 1.8 release form

## English usage instructions

1. First installation: flash the GameBoy 1.8 fourteen-game full BIN to address 0x0 on AI Passport (ESP32-C3, 8 MB Flash). This replaces existing games, saves, settings and controller bonds. Restart after flashing. Preinstalled games work offline; no router or internet connection is required.
2. Connect an input device: open the first home-menu entry (Pair keyboard or controller). Use the board Up/Down buttons to select a device and OK to connect. For IINE L1241, hold Pairing + A for about 3 seconds and select IINE GAMEPAD. If an unverified-firmware prompt appears, press board OK to try or hold OK to cancel. For Xbox 1914, hold its pairing button until the indicator flashes rapidly. Only compatible Bluetooth LE devices are supported, not Bluetooth Classic or USB/2.4 GHz receivers.
3. Start playing: after connection, select a game with board/controller Up/Down and press OK or controller A. Use the D-pad/left stick and A/B in games, with Start and Select for their original functions. Xbox View/Menu map to Select/Start. Keyboard W/A/S/D map to directions, J/K to A/B, and L/I to Start/Select.
4. Pause and return: hold Select for about one second or hold board OK to open Pause, where you can resume, sync a battery save, change settings, turn the screen off or exit. The same hold returns from Settings, Controller test, Game management and game lists; when editing a setting, it ends editing first. Holding in Pause exits after saving. Release the button before another hold to prevent cascading exits. Use board buttons when no controller is connected.
5. Test and settings: the third home entry (Controller test) shows D-pad, sticks, buttons and reported trigger values; highlights clear on release. The fourth entry (Settings) adjusts volume, brightness and sound, and displays version 1.8 at the bottom.
6. Optional game management: open the second home entry, connect a phone or computer to the password-free FoloToy-GB hotspot, and keep that connection even if it reports no internet. Open http://192.168.4.1 in a browser to upload legally obtained .gb files, rename or delete games. Up to 16 games are supported, subject to the displayed free space. Leaving this page stops the hotspot. Deleting a game also removes its associated saves; preinstalled games do not return automatically.
7. Save progress: save inside the game first, then sync the battery save from Pause or exit normally. On the next launch, use the game's Continue option. These are cartridge battery saves, not instant save states; games without save support cannot save arbitrary progress. Screen-off standby retains RAM progress, but power-off does not preserve unsaved progress.
8. Troubleshooting: if a controller is missing, check its compatible BLE pairing mode, hold board OK to cancel and retry. If the web page will not open, keep the device on Game management and make sure the phone is connected to FoloToy-GB. For missing audio, check sound and volume in Settings. ROM/controller compatibility and frame rates vary. This firmware targets GB; GBA and GBC-only games are unsupported.

## English update notes

1. Added IINE L1241 private BLE support with model and firmware identification. Firmware 27.02 connects directly; other firmware on the same model requires confirmation for that connection and times out after 60 seconds.
2. Added controller navigation and A confirmation on the home menu, plus a Controller test page showing live sticks, D-pad, buttons and reported trigger values with immediate release feedback.
3. Added a one-second Select hold for gameplay pause and return across multiple pages while retaining board long-press OK. Short Select presses still reach games; release guards cover entry, resume, reconnect and page transitions.
4. Delivered a 1.8 full image retaining the original fourteen-game selection. The full image was flashed to COM14 and the user reported no issues during testing.

This feedback does not establish full playthroughs, universal controller/firmware compatibility or endurance testing. Image identity and validation scope are in the [firmware handoff record](gameboy-firmware.md).
