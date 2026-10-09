<p align="right"><a href="gameboy-performance.md">English</a> · <strong>简体中文</strong></p>

# Game Boy 1.7 性能与虚拟机验证

## 1.7 实机复测与交付（2026-10-10）

1.7 已按明确批准仅更新 COM14 的 0x10000，写入校验、分区匹配、版本及 35 秒启动检查通过；游戏、存档、设置和配对分区未写入。240 秒观察捕获 12 个窗口，覆盖卡比和宇宙巡航机：模拟 59.6–59.8 fps、显示 14.9–29.8 fps，声音开启且输出采样递增，未报告崩溃、音频／显示丢块或输入积压丢弃。用户明确反馈“流畅度大幅改善，声音流畅也改善了，可以听到了”。这确认本轮体验改善，不是全部游戏、全部手柄或恒定显示帧率的保证。

1.6 强制内联使 CPU 解释函数膨胀到约 29 KiB，虚拟机工作耗时虽减少 19.4%，真机没有明显改善。1.7 将 CPU 解释函数恢复到约 9.2 KiB，连同 220 字节读取短路径放入指令 RAM，避免频繁从 Flash 取指；代价是约 9.6 KiB 内部 RAM。保留扫描线调色板复用、窗口背景裁剪及 2–4 帧自适应绘制。新增非敏感声音开关、音量与输出采样日志。缓存竞争是有代码体积和实测支持的解释，未用硬件计数器单独测量缓存命中率。

无声排查：1.6 读取到持久设置 volume=70、sound=0，仅记录三项非敏感设置，未保存原始 NVS。切换声音后音频生成恢复，但有声时模拟仍可降至 40.5 fps。1.7 启动确认 sound=1、volume=70，用户确认可听到声音。未改变用户设置默认值或 APU 算法，也未把音量非零等同为声音开关已开。

Build：PASS，ESP-IDF 5.5.3，正式入口 GB_SIM_TEST=OFF；应用 2,520,064 / 3,145,728 字节，8 MiB 完整镜像与独立校验通过，14 游戏 ROM 分区与 1.5 逐字节相同。

Host tests：PASS，28 组／0 失败，未变化的 6 组补充检查复用此前有效结果；14 款各 3,600 帧、旧新版配对三轮，累计画面、PCM、CPU 与完整 RAM 状态哈希一致。

Simulator：卡比和宇宙巡航机各 6 组／0 失败，均运行 1,800 帧并目视确认截图进入关卡，末尾 PCM 峰值分别 26,740 和 18,620。隔离入口不测 BLE 或真实硬件性能。

Device tests：写入／启动 PASS；本轮用户画面及声音体验确认改善。Unverified：全部游戏完整流程、全部手柄矩阵、严格同场景帧率基准、长时间内存稳定性及音频欠载仪器测量。

测试限制与失败记录：早期 Start 在开场期间被忽略，随后单次 Start 只进入宇宙巡航机选项页。最终脚本第 900 帧 Start，Gradius 测试夹具第 1080 帧再确认一次，卡比不重复 Start。第 1440 帧的另一次 500 ms PCM 捕获为静音；同一固件第 1800 帧捕获有声，不据此证明全程无静音或无欠载。保留失败目录 sim-gradius-compact、sim-gradius-compact-final、sim-1.7-gradius-gameplay。固定 CPI 不模拟真实指令缓存竞争，既有 st7789.sleep_cycle_too_fast.0x11 提示及未建模外设限制仍存在。下方 1.6 数值仅保留为历史，不作为 1.7 虚拟机提速比例。

`build/host-regression-bt783ls5/results.json`, `build/perf-all14-compact.json`, `build/sim-1.7-kirby-final`, `build/sim-1.7-gradius-final`, `build/release-1.7/com14-summary.json`, `build/release-1.7/com14-play-summary.json`.

## 历史 1.6 结果与边界（2026-10-09）

1.6 减少解释器与绘制工作，并更早响应帧预算超时。14 款本地游戏及 ROM 分区与 [1.5 经典选集](gameboy-classics-1.5.zh_CN.md)完全相同。正式固件构建及镜像验证通过；1.6 随后实机未达预期，已转向 1.7；虚拟机耗时不能换算为 ESP32-C3 帧率。

此前 1.5 完整包已烧录 COM14，写入校验、14 项目录回读及启动观察通过。随后 240 秒观察捕获卡比、宇宙巡航机运行：14 个统计窗口，模拟速度 43.9–59.7 fps，显示更新 14.7–29.5 fps，没有报告音频／显示丢块或输入积压丢弃，最大音频队列等待 13,970 微秒。用户反馈运行不流畅。队列等待不等于 I2S 欠载测量，没有丢块也不代表听感正常。证据：`build/classics-1.5/com14-play-summary.json`、`com14-play.log`。

## 修改内容

- 映射 ROM、HRAM 和普通 WRAM 读取走简短内联路径，映射器、CGB 分银行、作弊码及 I/O 行为保留原慢路径。HRAM 明确排除 `0xffff` 中断允许寄存器。卡比固定输入脚本在 3,600 帧中读取 `0xff8c` 共 4,739,615 次。
- 每扫描线只解码一次背景／精灵调色板，不绘制随后会被窗口覆盖的背景像素，去掉重复清空。保留精灵优先级、透明、窗口行计数及模拟 PPU 时序。
- 绘制负载统计由 120 个模拟帧改为 60 帧。平均工作超过 16 ms 时降低绘制频率，低于 14 ms 时恢复，间隔限定在 2–4 帧。重负载下优先维持模拟／音频节奏，可能减少画面更新次数，不保证恒定显示帧率。
- 保持原整数 APU、16 kHz 输出、CPU 跳转表及屏幕时钟。降低 CPU 负载是为了给音频生成留出更稳定的时间，未改变音频合成算法；没有直接复制 FC 专用的 NES 绘制／APU 优化。

## 验证证据

| 检查 | 结果 |
| --- | --- |
| 定向主机回归 | 28 组通过、0 失败（`build/host-regression-3pk1j7iu/results.json`），随后扩充的 RAM 边界测试通过 |
| 14 游戏差分测试 | 每款 3,600 帧，旧／新版配对运行三轮；累计帧缓冲、PCM、CPU 寄存器、WRAM、VRAM、HRAM、OAM、SRAM 哈希一致（`build/perf-all14-final.json`） |
| PassportSim 1.0.1 | 旧／新版各 6 组通过：菜单启动、异步显示／会话生命周期、卡比 1,800 帧、截图、非零 PCM 采集、机身按键注入 |
| 虚拟机 1,800 帧累计工作耗时 | 20,212,883 → 16,291,785 微秒，减少 19.4% |
| 虚拟机最终截图 | 像素 SHA-256 相同：`13d22051c88e87777c3690f8a4332555f5775feb82e10d5cff35d978e2a598d2` |
| 音频观察 | 两版在第 1,800 帧均报告输出 480,066 个采样，采集峰值均为 17,899。按墙钟截取的 WAV 不按采样对齐；合成一致性由主机 PCM 哈希验证，不直接比较这两段 WAV |

虚拟机原始回执、截图和 WAV 位于 `build/sim-baseline-final`、`build/sim-candidate-final`。核心源码／ELF 绑定记录位于 `build/perf-baseline/binding.json`、`build/perf-candidate/binding.json`。基线复制后明确触发重编译，避免 `Copy-Item` 保留旧时间戳使 Ninja 复用其他对象；此前未控制此条件的耗时比较作废，以本表为准。

开发时独立测试入口曾把 5,760 字节比较缓冲放在栈上而崩溃，已改为测试专用静态缓冲。重复 Start 的脚本曾暂停卡比、导致静音采集；最终脚本只在第 600 帧按一次 Start，第 1,200 帧后发送方向／A。这些失败尝试不计为正式固件通过。

PassportSim 仍报告既有 `st7789.sleep_cycle_too_fast.0x11` 时序提示和未建模的外设访问。固定 CPI、隔离无线的测试入口不能测量真实缓存／SPI／BLE 竞争、手柄兼容或扬声器音质。最终截图已目视确认进入卡比关卡。虚拟机测试镜像不能当作用户正式固件烧录。

## 复现方法

使用项目 ESP-IDF 5.5.3 环境，通过 `idf.py -B build/perf-sim -D SDKCONFIG=build/perf-sim/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults -D GB_SIM_TEST=ON build` 构建独立入口。用 `tools/package_gameboy_release.py` 打包用户本地 ROM 后执行：

```text
python tools/test_gameboy_simulator.py <passportsim.exe> <完整测试镜像.bin> <对应.elf> --output build/sim-tests --frames 1800
```

脚本使用工作区内独立模拟器目录，核对 ELF 绑定，按回执哈希核对截图／WAV，保留原始 JSON，结束时关闭自己的实例。它不调用模拟器的物理设备烧录命令。1.0.1 的匹配 `run` 可能在 `result=pass` 时返回 10；仅接受这一已知组合。

`tests/bench_gb_core.cpp` 使用真实 GB 头文件、`tests/stubs/sdfat_stub.cpp`、16 kHz/S16 的 minigb APU。分别针对保存的旧头文件和当前头文件构建两个程序，再执行 `tools/compare_gb_core.py <旧版.exe> <新版.exe> <本地ROM...> --output <结果.json>`。ROM 和生成产物不进入 Git。主机耗时用于筛查回退，不作为真机帧率。

正式固件单独以 `GB_SIM_TEST=OFF` 构建，输入位于 `build/release-1.6`。镜像哈希及最新设备状态以[验证记录](gameboy-validation.zh_CN.md)为准。
