<p align="right"><a href="gameboy-sources.md">English</a> · <strong>简体中文</strong></p>

# Game Boy 移植来源

- AI Passport BSP 与项目基线：[FoloToy/ai-passport](https://gitee.com/FoloToy/ai-passport)，提交 `cd73a8a6f1f95e010bfd83a08e2b915e38408308`。本移植更改应用、组件列表、分区及默认配置。上游硬件文档保留作参考。
- 模拟器核心：[psychoplath9450/SUMI](https://github.com/psychoplath9450/SUMI)，提交 `1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb`。复制的 `gb` 头文件经 `components/gb_core` 适配：Flash bank 访问、按卡带头分配 RAM、ROM 读失败报告及确定顺序的 16 位取指。原许可证见 `third_party_licenses/SUMI-LICENSE`。
- 音频合成器：[deltabeard/minigb_apu](https://github.com/deltabeard/minigb_apu)，提交 `344d9814c0d834faf1a26ec97b57bedddefed8aa`，MIT 许可证见 `components/minigb_apu/LICENSE`。本项目适配 Game Boy 四声道 APU，生成 14 kHz 单声道 PCM，输出到开发板 ES8311。
- 中文位图来源：[Noto Sans SC](https://github.com/notofonts/noto-cjk)，SIL Open Font License 1.1 见 `assets/fonts/OFL.txt`。`tools/generate_ui_font.py` 生成固件所用的 `main/ui_font.c` 字形子集。
- 手柄协议栈：[Bluepad32](https://github.com/ricardoquesada/bluepad32)，提交 `e9b755faabc240585da42e6d26164bb2cdd064d3`；其 [BTstack](https://github.com/bluekitchen/btstack) 依赖固定为 `5d4d8cc7b1d35a90bbd6d5ffd2d3050b2bfc861c`。已应用上游 BTstack 补丁 `0002-l2cap-allow-incoming-connection-with-not-enough.patch`。本地 BLE 启用主动扫描与 Legacy Just Works 绑定。BTstack 在不绑定时不请求密钥，并将响应方声明的密钥分发范围限制在本机请求范围内，避免等待未请求的密钥。Xbox 1914 固件 5.22.16.0 已在绑定模式下配对并发送游戏按键。许可证见 `third_party_licenses/BLUEPAD32-LICENSE` 与 `third_party_licenses/BTSTACK-LICENSE`。
- 真机测试 ROM：[wyattferguson/2048-gb](https://github.com/wyattferguson/2048-gb)，提交 `167788105db659f2049dd2d537ee12f63d87b1e7`，MIT 许可证。其 `2048.gb` 为 32,768 字节，SHA-256 `46F9AB6A5E9ACB81C76E9C2C6526DB1ED4E03B4FB6F36C23FB786E31F9659FC5`。
- 第二款真机测试 ROM：[splch/pirates-folly](https://github.com/splch/pirates-folly)，[MIT 许可证](https://github.com/splch/pirates-folly/blob/main/LICENSE)，[滚动发布的 ROM](https://github.com/splch/pirates-folly/releases/tag/latest)。下载的 `pirates_folly.gb` 为 131,072 字节，SHA-256 `8C1870D9102C875CE499C2C96E4E063DACC103AC5A6679C11A0C07C748B4CB85`；卡带头标明 MBC5 和 8 KiB 电池存档 RAM。两款游戏已打包成 `build/roms-two-games.bin`（167,936 字节，SHA-256 `212A4B73F365640708CB03D372F432ACC0EE959787857F4B4B46D9A56F8CBDC7`），写入 COM14 的 `0x310000` ROM 分区并逐字节回读确认。ROM 文件留在被忽略的 `build/` 中，没有提交。真机游戏和存档兼容性另记于 `gameboy-validation.zh_CN.md`。
- 用户提供的 ROM：`pokemon_blue.gb`，1,048,576 字节，SHA-256 `A02956BF1B3A2FC78E191347DEFDEBDFBF6F8FB7C12375A3D1417AC21E25F921`。已检查的卡带头为 `POKEMON BLUE`、MBC3+RAM+BATTERY（`0x13`）、1 MiB ROM、32 KiB RAM，头部校验和一致。它与 2048 一起替换先前的 ROM 镜像中的 Pirate's Folly。早期 `build/roms-2048-pokemon.bin` 为 1,085,440 字节，SHA-256 `FC0FFC48ABF16CE7B13863324EB257C513B880547C3285E6FE88C8215567CE99`；COM14 Flash 回读一致。用户提供的 ROM 字节只保存在被忽略的 `build/`，没有提交或重新分发。真机游玩和存档结果另记于 `gameboy-validation.zh_CN.md`。

当前集成目标为 ESP-IDF 5.5.3。代码来源与编译成功都不能证明 Xbox 1914 的真机配对或游戏兼容性。
