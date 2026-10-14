<p align="right"><a href="gameboy-firmware.md">English</a> · <strong>简体中文</strong></p>

# Game Boy 固件镜像与 GitHub 提交


## 当前 1.9 十四游戏镜像（2026-10-14）

`artifacts/releases/FoloToy-GameBoy-1.9-14games-full.bin` 为本地归档并获准从 `0x0` 安装到 COM14 的镜像：8,388,608 字节，SHA-256 `7619395c66001712431000fa45cf9738b3b37805db4bf82c78a213370a76d47c`。十四款 ROM 名称和 SHA-256 与历史 1.8 清单一致。应用：`build/xiaomi-validation/FoloToy-AI-Passport.bin`，2,532,848 字节，SHA-256 `af0da2418cb4bdfc4142ee84b9927fe935c773b7544fafe24d1241a7e01fe7f1`；匹配 ELF SHA-256 `c954b0bd6b2e18c6c07e50191e6909498a618e4ec2cbfad4c6ad5a48708cc736`。相邻 manifest／校验文件保留身份。清洁镜像：`artifacts/releases/FoloToy-GameBoy-1.9-8MB-clean.bin`，SHA-256 `b459d74dce98bd4bfa4c67ae62da5b33dcd26be7635f1825f66533c85c3fd6a7`。

完整写入已覆盖原 FC 1.5、游戏、存档、设置和配对。写入哈希、启动版本及 ELF 匹配，用户遥控器／菜单专项验收通过。构建及 40 组主机检查通过；使用 Windows 原生检查，未运行完整 Bash 入口。重连、耐久、全部游戏兼容和断电存档仍待验证。原始日志和含 ROM 镜像只在本地保留，不进入 Git 或公开 Release 附件。见[验证记录](../gameboy-validation.zh_CN.md)及[1.9 使用方法／更新表单](gameboy-1.9-form.zh_CN.md)。下方历史镜像保留。

## 1.8 十四游戏完整包烧录（2026-10-12）

已按用户要求将 `artifacts/releases/FoloToy-GameBoy-1.8-14games-full.bin` 从 `0x0` 写入 COM14，覆盖原 FC 游戏、存档、设置及配对信息。镜像为 8,388,608 字节，包含原选集十四款 GB 游戏，各 ROM SHA-256 与 1.7 清单一致。完整包 SHA-256：`b7aeab13ae5215b286d714c42a942b88ac3af01bacf45d13bc87165ef5b99b82`；应用 SHA-256：`cd567b0217eb9f01cdd7789b9c2283afe6cd6d3fe8bc0dccbcbc5d73ee79d519`。同目录 manifest 保存应用、ELF 和游戏身份。

本次 ESP-IDF 5.5.3 增量构建、完整包生成及复验、烧录写入哈希检查通过。30 秒串口观察有 BLE 扫描活动，未见 panic/watchdog 标记，但未捕获完整启动身份行；用户随后反馈“测试下来没问题”；未提供逐游戏通关、手柄固件版本或耐久测试清单，不扩大验收范围。提交前复验：38 组主机检查通过、0 失败（32 组定向检查及 6 组补充检查），actionlint 通过。复用本会话已验证且未变化的 ESP-IDF 5.5.3 应用；十四款源 ROM、完整包及组件一致性重新核验通过。完整 Bash 门禁在本机不可用，原生检查不宣称其入口已通过。下文“1.8 未烧录”为本次之前的历史记录。

## 全页面 Select 返回（2026-10-12）

所有原有长按 OK 返回/退出操作均支持长按 Select 1 秒，机身 OK 保留：设置（编辑时先结束编辑）、手柄测试、游戏管理、游戏列表/空列表、暂停菜单（保存成功后退出游戏）。配对取消/重试也复用该事件，但未连接或确认前没有可用手柄输入时仍需机身按键。进入页面及从子页面返回时重新建立持键基线，必须释放后再次长按，避免连续跨页退出。测试页 Select 短按用于观察，持续 1 秒返回。 GameBoy 1.8 十四游戏完整包已于 2026-10-12 烧录到 COM14，用户随后反馈“测试下来没问题”。本次确认限于用户实际操作范围，不外推为全部游戏、全部手柄或长期稳定性验收。


## 2026-10-12 手柄更新

GameBoy 1.8 十四游戏完整包已于 2026-10-12 烧录到 COM14，用户随后反馈“测试下来没问题”。本次确认限于用户实际操作范围，不外推为全部游戏、全部手柄或长期稳定性验收。

本产品采用仓库根目录 `partitions.csv` 中的五个分区，不采用上游三分区模板。清洁版镜像长度**恰好为 8,388,608 字节（8 MiB）**：`0x0` 为 ESP32-C3 引导程序，`0x8000` 为分区表，`0x10000` 为应用，`0x710000` 为可重复生成的初始 SPIFFS 镜像；其余字节全部为 `0xFF`。尤其是 NVS（`0x9000`–`0xEFFF`）、PHY（`0xF000`–`0xFFFF`）及整个 ROM 区（`0x310000`–`0x70FFFF`）不包含设置、手柄绑定或游戏。初始存档文件系统只包含公开的 `assets/initial_saves/README.txt`，没有游戏进度。

## 1.7 累计更新说明（2026-10-10）

- 预装经典游戏由 2 款扩充至 14 款：超级马里奥大陆、星之卡比、口袋妖怪红、魂斗罗、恶魔城漆黑前奏曲、忍者龙剑传、忍者蛙、龙珠 Z 悟空飞翔传、宇宙巡航机、超级机器人大战、哆啦 A 梦赛车、异型战机、坦克大战、大力工头阿源君。名单和 ROM 哈希见[经典选集](../gameboy-classics-1.5.zh_CN.md)。
- 新增标准 BLE HID 解析，适配常见 8/12 键及 Android 布局；保持分报告按键状态，支持左摇杆方向，使用真实 BLE 描述符，扩展广播识别并改善连接失败及旧绑定恢复。
- 改进多按键输入缓存，减少保持方向时连续短按 A/B 的遗漏；暂停、退出或断连时清空积压，进入和恢复游戏时等待松键。
- 将紧凑的 CPU 解释器及常用读取路径放入指令 RAM，减少重复背景／精灵绘制及窗口覆盖区域的无效计算，并按负载调整画面更新频率。
- 卡比、宇宙巡航机实测画面和声音明显更流畅；本轮模拟速度为 59.6–59.8 fps，显示更新为 14.9–29.8 fps。不同游戏表现仍有差异，不保证恒定满帧或全部游戏／手柄兼容。

预装游戏只包含在本地十四游戏完整安装包中；仅应用升级不会新增游戏。完整安装从 0x0 写入会覆盖原有数据；当前兼容分区的应用升级只写 0x10000。GitHub 只提交源码、测试和文档，不上传 ROM、预装固件或设备日志。

## 构建与校验

激活 ESP-IDF 5.5.3，以 `esp32c3` 为目标构建。上游完整门禁为 `./tools/validate.sh`；其中固件模式还会生成 `build/FoloToy-GameBoy-8MB-clean.bin` 及 `.sha256` 校验文件。在 Windows 上执行 `idf.py build` 后，可用以下命令生成同样的镜像：

```powershell
& 'C:\Users\User\.espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe' tools/package_gameboy_release.py
& 'C:\Users\User\.espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe' tools/package_gameboy_release.py --verify
Get-FileHash -Algorithm SHA256 -LiteralPath 'artifacts\releases\FoloToy-GameBoy-8MB-clean.bin'
```

打包工具在写出前会核对固定分区、源码镜像、重新生成的清洁 SPIFFS、准确的 8 MiB 长度，以及未使用区、ROM、NVS 均为擦除态。本地产物为 `artifacts/releases/FoloToy-GameBoy-8MB-clean.bin`，其 SHA-256 应与同目录 `.sha256` 文件一致。这两个本地产物均被 Git 忽略。不要把商业或用户提供的 ROM、设备 Flash 回读、NVS 转储、存档、配对数据加入源码仓库或 GitHub Release。较小的 `FoloToy-AI-Passport-full.bin` 是 ESP-IDF 的合并固件，只到应用末尾，**并非**整片 8 MiB 镜像。

## 可选预装游戏

历史 1.7 经典版为 `artifacts/releases/FoloToy-GameBoy-1.7-14games-full.bin`，使用 `build/release-1.7` 的正式应用（`GB_SIM_TEST=OFF`）。14 款清单和分区容量沿用[1.5 选集](../gameboy-classics-1.5.zh_CN.md)，ROM 分区逐字节相同。`FoloToy-GameBoy-1.7-app.bin` 用于兼容分区的仅应用更新（`0x10000`）。1.7 已通过仅应用写入／启动检查，用户确认画面与声音明显改善。旧包保留，下方为历史双游戏打包示例。

通过重复的 `--rom` 参数，将本地游戏放入普通可写 ROM 分区。原文件名过长时，按相同顺序为每个 ROM 指定一个 `--rom-name`（各不超过 31 个 UTF-8 字节）；不改名、不修改源文件。不指定 `--output` 时，预装模式生成 `artifacts/releases/FoloToy-GameBoy-8MB-preloaded.bin`；不传 `--rom` 仍生成 CI 使用的无游戏清洁版。

```powershell
$redRom = 'E:\BaiduNetdiskDownload\GB ROM\口袋妖怪红(精灵宝可梦红) (简)(修正版+完美LOGO)(sss888+RickyL1213+fciq)(8Mb).gb'
$marioRom = 'E:\BaiduNetdiskDownload\GB ROM\超级马里奥大陆(简)(v2.0)(星夜之幻)(0.5Mb).gb'
$packageArgs = @('tools/package_gameboy_release.py', '--build-dir', 'build/release-1.5', '--rom', $redRom, '--rom-name', '口袋妖怪红', '--rom', $marioRom, '--rom-name', '超级马里奥大陆', '--output', 'artifacts/releases/FoloToy-GameBoy-1.5-full.bin')
& python.exe @packageArgs
& python.exe @packageArgs --verify
```

两款本地文件分别为 1,048,576 和 131,072 字节；马里奥文件名中的容量后缀与实际长度不符，因此按卡带头及实际字节校验。预装镜像仍恰好为 8 MiB，NVS、PHY 与初始存档均为空。打包工具校验两款 ROM 哈希，并为 ROM 分区末尾两份可写目录保留擦除态空间。游戏进入普通列表、占用普通容量，可通过“游戏管理”改名或删除；删除后重启不恢复，空出的空间可供新上传游戏使用。没有开机自动补装或应用内的第二份副本，只有重新安装完整预装镜像才恢复出厂游戏。

这是本地首次安装镜像。以上命令只生成文件，不烧录、不发布；`--verify` 需要相同的 ROM 和名称参数，核对完整镜像及校验文件。不带 ROM 参数执行清洁版校验时会拒绝预装镜像。用户提供的 ROM 及生成镜像不进入源码仓库或自动公开发布流程。

## 首次安装与日常更新

**仅用于首次安装或明确要清空全部数据时**，从 Flash `0x0` 烧录清洁版镜像。选定实际串口后，可用 ESP-IDF 的 esptool：

```powershell
& python.exe -m esptool --chip esp32c3 -p COM14 -b 460800 write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB 0x0 'artifacts\releases\FoloToy-GameBoy-8MB-clean.bin'
```

此操作覆盖整片 8 MiB，包括所有游戏、电池存档、设置和手柄配对密钥。清洁版启动后游戏列表为空；进入“游戏管理”，连接 `FoloToy-GB` 热点，再上传自己有权使用的 ROM。已有安装应**只更新应用分区**：将 `build/FoloToy-AI-Passport.bin` 写到 `0x10000`，保留兼容的原有分区表、ROM、存档和 NVS；不能拿清洁版镜像当作普通升级包。

预装版同样在首次安装时从 `0x0` 写入，也会覆盖所有游戏、存档、设置及绑定，启动后的列表包含所选游戏。它不是仅应用升级包：只刷 `FoloToy-AI-Passport.bin` 不会安装这些游戏。在已有设备上使用任何完整镜像前，都需另行批准。

## 仓库与发布

提交源码、中英配对文档、测试、脚本、配置及第三方许可证记录。`build/`、`artifacts/releases/`、`artifacts/baselines/`、`sdkconfig`、`managed_components/`、Python 缓存、ROM、设备回读和用户数据不进入 Git。推送发布标签后，工作流从已提交源码构建清洁镜像，并把 `.bin` 和校验文件附到 GitHub Release。编译和镜像布局通过不等于空白设备启动已实测；真机验收与剩余性能限制见[基准验收](../gameboy-acceptance.zh_CN.md)。
