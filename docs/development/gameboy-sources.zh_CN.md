<p align="right"><a href="gameboy-sources.md">English</a> · <strong>简体中文</strong></p>

# Game Boy 移植来源

- AI Passport BSP 与项目基线：[FoloToy/ai-passport](https://gitee.com/FoloToy/ai-passport)，提交 `cd73a8a6f1f95e010bfd83a08e2b915e38408308`。本移植更改应用、组件列表、分区及默认配置。上游硬件文档保留作参考。
- 模拟器核心：[psychoplath9450/SUMI](https://github.com/psychoplath9450/SUMI)，提交 `1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb`。复制的 `gb` 头文件经 `components/gb_core` 适配：Flash bank 访问、按卡带头分配 RAM、ROM 读失败报告及确定顺序的 16 位取指。原许可证见 `third_party_licenses/SUMI-LICENSE`。
- 音频合成器：[deltabeard/minigb_apu](https://github.com/deltabeard/minigb_apu)，提交 `344d9814c0d834faf1a26ec97b57bedddefed8aa`，MIT 许可证见 `components/minigb_apu/LICENSE`。本项目适配 Game Boy 四声道 APU，生成 16 kHz 单声道 PCM，输出到开发板 ES8311。
- 中文位图来源：[Noto Sans SC](https://github.com/notofonts/noto-cjk)，SIL Open Font License 1.1 见 `assets/fonts/OFL.txt`。`tools/generate_ui_font.py` 生成固件所用的 `main/ui_font.c` 字形子集。
- 手柄协议栈：[Bluepad32](https://github.com/ricardoquesada/bluepad32)，提交 `e9b755faabc240585da42e6d26164bb2cdd064d3`；其 [BTstack](https://github.com/bluekitchen/btstack) 依赖固定为 `5d4d8cc7b1d35a90bbd6d5ffd2d3050b2bfc861c`。已应用上游 BTstack 补丁 `0002-l2cap-allow-incoming-connection-with-not-enough.patch`。本地 BLE 启用主动扫描，手柄保留 Legacy Just Works 绑定。应用为键盘选择仅显示的 IO 能力，并显示请求的配对码。本地通用键盘解析器在收到独立媒体键报告时保留按住的字母键；普通媒体键不映射，保留上游 JX-05 特殊解析器。BTstack 在不绑定时不请求密钥，并将响应方声明的密钥分发范围限制在本机请求范围内，避免等待未请求的密钥。Xbox 1914 固件 5.22.16.0 已在绑定模式下配对并发送游戏按键。许可证见 `third_party_licenses/BLUEPAD32-LICENSE` 与 `third_party_licenses/BTSTACK-LICENSE`。
- 真机测试 ROM：[wyattferguson/2048-gb](https://github.com/wyattferguson/2048-gb)，提交 `167788105db659f2049dd2d537ee12f63d87b1e7`，MIT 许可证。其 `2048.gb` 为 32,768 字节，SHA-256 `46F9AB6A5E9ACB81C76E9C2C6526DB1ED4E03B4FB6F36C23FB786E31F9659FC5`。
- 第二款真机测试 ROM：[splch/pirates-folly](https://github.com/splch/pirates-folly)，[MIT 许可证](https://github.com/splch/pirates-folly/blob/main/LICENSE)，[滚动发布的 ROM](https://github.com/splch/pirates-folly/releases/tag/latest)。下载的 `pirates_folly.gb` 为 131,072 字节，SHA-256 `8C1870D9102C875CE499C2C96E4E063DACC103AC5A6679C11A0C07C748B4CB85`；卡带头标明 MBC5 和 8 KiB 电池存档 RAM。两款游戏已打包成 `build/roms-two-games.bin`（167,936 字节，SHA-256 `212A4B73F365640708CB03D372F432ACC0EE959787857F4B4B46D9A56F8CBDC7`），写入 COM14 的 `0x310000` ROM 分区并逐字节回读确认。ROM 文件留在被忽略的 `build/` 中，没有提交。真机游戏和存档兼容性另记于 `gameboy-validation.zh_CN.md`。
- 用户提供的 ROM：`pokemon_blue.gb`，1,048,576 字节，SHA-256 `A02956BF1B3A2FC78E191347DEFDEBDFBF6F8FB7C12375A3D1417AC21E25F921`。已检查的卡带头为 `POKEMON BLUE`、MBC3+RAM+BATTERY（`0x13`）、1 MiB ROM、32 KiB RAM，头部校验和一致。它与 2048 一起替换先前的 ROM 镜像中的 Pirate's Folly。早期 `build/roms-2048-pokemon.bin` 为 1,085,440 字节，SHA-256 `FC0FFC48ABF16CE7B13863324EB257C513B880547C3285E6FE88C8215567CE99`；COM14 Flash 回读一致。用户提供的 ROM 字节只保存在被忽略的 `build/`，没有提交或重新分发。真机游玩和存档结果另记于 `gameboy-validation.zh_CN.md`。

本地 Bluepad32 在不支持或未启用 BR/EDR 时跳过 SSP 和查询过滤命令。`HCI_Set_Event_Filter` 用于 BR/EDR 查询过滤，参见[蓝牙 HCI 规范](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host-controller-interface/host-controller-interface-functional-specification.html)；因此不再向仅支持 BLE 的 ESP32-C3 发送 `0x0c05`。音频使用已安装 esp_codec_dev 1.6.2 ES8311 表中支持的 16 kHz／4.096 MHz 时钟组合。该表无 14 kHz 项，且 `es8311_set_fs()` 忽略内部配置失败；未修改受管理的驱动，应用改为对开机提示音及 APU 输出统一请求支持的采样率。参见[上游 ES8311 实现](https://github.com/espressif/esp-adf/blob/release/v2.x/components/esp_codec_dev/device/es8311/es8311.c)，本次构建以实际安装的固定版本源码为准。

LCD 开关行为依据 [Pan Docs LCD Control](https://github.com/gbdev/pandocs/blob/master/src/LCDC.md) 和 [Interrupt Sources](https://github.com/gbdev/pandocs/blob/master/src/Interrupt_Sources.md)：游戏关闭 LCD 时停止 PPU 活动并清空画面，CPU／计时器继续运行；重新开启从第零行开始，首帧保持空白。本地内核将主机帧预算与 PPU 相位分开，即使跳过像素绘制也推进窗口行计数。这仍是扫描线级近似，不代表周期精确的 PPU 验证。

可选的本地预装镜像另使用用户提供的汉化《口袋妖怪红》（1,048,576 字节）和《超级马里奥大陆》（131,072 字节），卡带信息及 SHA-256 标识见[验证记录](gameboy-validation.zh_CN.md)。这些 ROM 及生成的预装镜像不进入 Git 或公开发布流程，默认清洁镜像不含游戏。

本地 BLE 发现还使用 16 项缓存，按地址及地址类型合并广播包和扫描响应中的名称／类型／HID 服务信息；连续五秒无数据后失效，停止扫描时重置。完整名称优先于短名称，畸形 AD 字段会在更新缓存前被拒绝。应用接纳 Gamepad 和 Joystick 候选，不按名称筛选。类型缺失、为零或为通用 HID 时，允许依据完整／不完整的 16 位或蓝牙基础 128 位 UUID 列表中的 0x1812 发现设备；不覆盖明确不同的类型。专用 BLE 发现入口保留白名单、信号强度和用户选择门槛，不放宽经典蓝牙的接纳范围。仅由 UUID 发现的候选采用显示配对码的能力，让键盘可请求 PIN。在就绪前，通过有界描述符检查确认顶层 Gamepad、Joystick 或 Keyboard Application 集合包含数据 Input；纯鼠标、纯媒体遥控、畸形或支持类型冲突的描述符被拒绝。选择第一个符合要求的 HID 服务，在解析器初始化前复制其描述符，并忽略其他服务或分类完成前的报告。既有报告解析器及已知类型的配对行为保持不变。类型和 HID UUID 都缺失、不支持／存在歧义的报告描述符，以及设备信息服务失败，仍不在本次发现修复范围内。

## 反馈手柄的协议

资料核对日期为 2026-10-04。ESP32-C3 支持 BLE，不支持经典蓝牙（BR/EDR），参见 [Bluepad32 平台兼容表](https://bluepad32.readthedocs.io/en/latest/supported_gamepads/)。商品名称、蓝牙版本号及 Xbox 授权标志都不能单独确定传输协议；使用下列结论前，应核对机身型号和工作模式。

| 反馈型号 | 证据及蓝牙直连结论 |
| --- | --- |
| GameSir T3s | [T3s 认证报告](https://fcc.report/FCC-ID/2AF9ST3/5880578.pdf)记录 BR/EDR（硬件 V1.1、软件 V1.0）。该报告对应的蓝牙实现不受 ESP32-C3 支持；发现流程修复不能补上缺失的传输协议。 |
| 良值 IINE L167 | 尚未找到该型号的权威传输协议资料，不能确定协议不支持；需要说明书、机身标识或设备抓包。此前 IINE-1001 的结果不适用于 L167。 |
| 八位堂猎户座二代青春版／Ultimate 2C | 必须区分版本：[80NC 蓝牙／Switch 版报告](https://device.report/m/3f2c6ef2ff77610fc84502bcdcccf45a40efd1626343dd57dda6ccdb1e54395c)标明 Bluetooth BR，该版本不支持；[81HD 无线 PC／Android 版报告](https://fcc.report/FCC-ID/2AOWF-24GULT2C/7617829.pdf)标明 BLE 与 2.4 GHz，不能仅凭传输协议排除其 BLE 模式。仍需确认反馈设备的具体版本。 |
| 八位堂 X-Pro／Ultimate 3-mode for Xbox | [厂家 X-Pro 页面](https://www.8bitdo.cn/ultimate-3-mode-controller-xbox/)及 [81HB 认证报告](https://device.report/m/af2a2c567a495ac82ab661752c859a0cf4822a6ac4ef428b7e15f9f3da4c898f)对应产品及 BLE／2.4 GHz 无线方式，不能判为 BLE 协议明确不支持；配对及 HID 输入兼容性仍待实测。 |

这四款反馈手柄均未取得实物测试。具备 BLE 传输能力不等于配对、报告解析及重连已通过。本固件不支持 USB／2.4 GHz 接收器；公开资料仅适用于所标识的版本，不能扩展到所有同名产品。

当前集成目标为 ESP-IDF 5.5.3。代码来源与编译成功都不能证明实体键盘兼容性或具体游戏验收通过，实机结果单独记录。
