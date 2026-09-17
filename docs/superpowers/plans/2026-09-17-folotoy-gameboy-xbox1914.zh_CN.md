<p align="right"><a href="2026-09-17-folotoy-gameboy-xbox1914.md">English</a> · <strong>简体中文</strong></p>

# FoloToy AI Passport + Xbox 1914 Game Boy 实施计划

> 历史计划，保留制定时的假设和待办。当前功能与验收状态以[产品说明](../../../README.zh_CN.md)及[验收记录](../../development/gameboy-acceptance.zh_CN.md)为准。

> 执行说明：使用 `superpowers:executing-plans` 逐项实施并记录证据。遵守用户提供的 AGENTS.md；不自动创建分支或 worktree，不自动提交、推送或刷机。本文件是实施计划，不代表已经开始实现。

**目标：** 在现有 FoloToy AI Passport 上运行原版 Game Boy 游戏，通过 Xbox Wireless Controller 1914 的 BLE 连接进行操作，并支持可靠的游戏存档。

**架构：** 以 AI Passport 的 ESP-IDF/BSP 为硬件基础，移植 SUMI 的 GB 核心，独立适配显示、ROM 读取、存档和时间接口。使用 Bluepad32/BTstack 验证并接入手柄；游戏期间不运行 Wi-Fi。采用压缩灰度帧缓冲和小块 RGB565 DMA 缓冲，避免彩色全屏双缓冲。

**技术栈：** ESP32-C3、ESP-IDF 5.5.3、C/C++、FreeRTOS、esp_lcd/ST7789P3、Bluepad32/BTstack、内部 Flash、ES8311（音频阶段）。

**需求依据：** 本次对话中用户提出“移植 SUMI 的 Game Boy 模拟器到现有硬件，通过蓝牙 Xbox 手柄游玩”，随后确认硬件仓库为 FoloToy/ai-passport、手柄型号为 1914。下文包含本计划的完整范围和验收，不依赖未创建的设计文档。

## 1. 已确认事实与尚未验证的条件

### 已确认

- 硬件：ESP32-C3，8 MB Flash，无 PSRAM。
- 屏幕：ST7789P3，240×320，RGB565，SPI2 40 MHz。沿用 BSP 专用初始化、反色、字节序和背光配置。
- 输入：GPIO0 ADC 电阻梯三键；用于菜单、配对和暂停，不承担完整 GB 游戏控制。
- 音频：ES8311，I2S PCM 播放和录音接口，内置扬声器。
- 当前公开硬件接口没有 SD 卡；ROM 和存档采用内部 Flash。
- 原 BSP 提供 `bsp_display_panel()`，LVGL 是可选接入层；直接绘图无需让游戏逐帧通过 LVGL。
- 原蓝牙示例是 NimBLE peripheral/broadcaster，Central 和 Observer 关闭，并非手柄主机。
- 用户手柄型号为 1914。Bluepad32 兼容表列出该型号的 v5.15 或更新固件；用户实际固件版本尚未核实。
- SUMI 使用 Arduino/PlatformIO，其 GB 核心有 SdFat 和时间等平台依赖，不能原样作为 ESP-IDF 组件直接使用。
- 已检查的 SUMI 核心输出为 160×144、2 bit 灰度，声音仅为占位实现，默认卡带 SRAM 上限为 8 KB；必须分别处理声音和大容量存档问题。

### 当前工作区与工具链

- 工作区：`E:\code\folotoy-gameboy`，本计划编写前为空目录，无本地 AGENTS.md、CONTEXT.md 或代码。
- 已检查 `E:\AGENTS.md` 和 `E:\code\AGENTS.md`，未发现文件；适用规则为用户在对话中提供的 AGENTS.md。
- 本机常用路径 `C:\esp\v5.5.4\esp-idf\export.ps1` 存在，`C:\esp\v5.5.3\esp-idf\export.ps1` 不存在；这不等于本机其他位置没有 5.5.3。
- 实施时先查找可用的 5.5.3 环境并验证版本，缺少时按上游环境文档准备。不能静默使用 5.5.4，也不在本次计划任务中安装环境。

### 证据基线

- AI Passport：本次查询的 Gitee main 提交为 `cd73a8a6f1f95e010bfd83a08e2b915e38408308`。
- SUMI：前序核查固定提交为 `1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb`。
- Bluepad32：实施第一阶段选取与 ESP-IDF 5.5.3、C3 BLE 兼容的版本，验证后固定提交及 BTstack 子模块/补丁版本，不使用浮动 main 构建交付物。
- 上述是源码和官方文档证据，不是本机编译、上板或游戏兼容性结果。

## 2. 范围与完成标准

### 首版范围

1. 独立游戏固件：启动菜单、选择 ROM、配对状态和错误信息。
2. Xbox 1914 单手柄连接、配对持久化、重连和完整按下/松开状态。
3. 原版 GB/DMG；先 160×144 原尺寸，性能允许后增加 240×216 保持比例的缩放。
4. 板载 Flash 中的 ROM，电脑端 USB 串口工具导入；首轮原型可先打包一个合法测试 ROM。
5. 卡带电池 SRAM 存档；MBC3 游戏需要正确的 RTC 处理和适用限制说明。
6. 暂停、恢复、退出，以及手柄断线时清空输入并暂停。
7. 明确显示不支持的 ROM/存档条件，不用截断、错误银行映射或静默降级掩盖问题。

首版无声音是本计划建议的分阶段边界，不表示用户放弃声音需求。音频在第七阶段单独处理；若实施前确认声音必须随首版交付，应先执行该阶段的核心选择评估，再固化核心架构。

本计划不把完整彩色 GBC、即时存档、Wi-Fi 传游戏、OTA、多手柄、震动或 AI 功能共存作为首版要求。

### 首版验收门槛

- 实际 1914 手柄可配对、重连，方向+A/B 等组合键及松开状态正确；断线不产生卡键。
- 对选定的测试 ROM，目标为接近原机速度：60 秒稳态窗口内模拟速度达到理论值的 95%–105%；正常 1× 模式不主动加速游戏。
- 先验收原尺寸显示。240×216 显示约 30 FPS 是优化目标，不能替代模拟速度要求，也不能未实测便承诺。
- 若原尺寸已满足模拟速度、放大未满足，交付原尺寸并明确标注放大模式未通过；若模拟速度不满足，不称为“流畅版已完成”。
- 单次连续游戏 30 分钟、20 次进出游戏、10 次手柄重连无崩溃、看门狗或持续堆下降；记录热身后的堆变化范围，不仅记录平均值。
- 正常保存并重启可恢复；存档写入中断时可恢复旧的或新的完整版本，不能加载半份存档。允许丢失尚未成功保存的最近进度，需在界面和说明中明确。
- 支持范围按实际 ROM 清单记录，不宣称所有 GB/GBC 游戏兼容。

## 3. 模块与拟定文件

以下是准备创建或修改的目标路径，不表示文件已经存在。引入硬件基线时保留本计划；不得用整目录覆盖或清理方式导入。

| 路径 | 责任 |
| --- | --- |
| `components/bsp/` | 复用上游显示、按键、音频、电量驱动；引脚只定义在 `bsp_pins.h` |
| `components/gb_core/` | 提取并保留来源的模拟器核心及最小平台适配 |
| `components/gb_core/include/gb_port.h` | 核心公共接口、ROM 读取回调、输入和帧缓冲约定 |
| `main/gamepad.c`、`main/gamepad.h` | Bluepad32 事件、连接状态、配对管理 |
| `main/gb_input.c`、`main/gb_input.h` | 与协议栈无关的按键映射和状态快照 |
| `main/gb_display.c`、`main/gb_display.h` | 灰度转换、缩放、LCD DMA 和帧提交 |
| `main/gb_storage.c`、`main/gb_storage.h` | ROM 清单/分区访问、存档验证及恢复 |
| `main/gb_runtime.cpp`、`main/gb_runtime.h` | 模拟循环、节拍、暂停、生命周期、性能统计 |
| `main/main.c`、`main/game_menu.c` | 启动、最小菜单和三键操作 |
| `tools/pack_roms.py`、`tools/import_roms.ps1` | ROM 容器打包、校验及经批准的串口写入 |
| `tests/test_gb_input.c` | 按键映射、组合键、断线释放 |
| `tests/test_gb_pixels.c` | 解包、RGB565、缩放边界 |
| `tests/test_gb_storage.c` | ROM 边界、存档校验、断写恢复 |
| `tests/test_gb_timing.cpp` | 节拍累积、暂停恢复、落后时的有界处理 |
| `CMakeLists.txt`、`main/CMakeLists.txt`、组件 CMake/manifest | 依赖、语言和构建目标 |
| `sdkconfig.defaults`、`partitions.csv` | 蓝牙主机配置、运行频率及应用/数据分区 |
| `CONTEXT.md`、`README.md`、`docs/validation.md`、`THIRD_PARTY_NOTICES.md` | 状态、用法、验证证据及来源许可 |

引入上游后遵守其适用文档双语规则，维护文档时补齐 `.zh_CN.md` 对应文件。不要为此次实现改写上游历史 CHANGELOG。

## 4. 接口和资源约定

### 核心接口

以下为拟定接口；通过适配层隔离 SUMI 的 C++ 对象、Arduino 时间调用和 `FsFile`。业务层不直接操作核心内部成员。

```c
typedef struct gb_session gb_session_t;
typedef struct {
    bool right, left, up, down;
    bool a, b, select, start;
} gb_keys_t;

typedef bool (*gb_rom_read_fn)(void *ctx, uint32_t offset,
                               void *dst, size_t bytes);
typedef struct {
    gb_rom_read_fn read;
    void *ctx;
    uint32_t size;
} gb_rom_source_t;

bool gb_port_create(const gb_rom_source_t *rom, gb_session_t **out);
void gb_port_set_keys(gb_session_t *s, gb_keys_t keys);
bool gb_port_step_frame(gb_session_t *s, bool render);
const uint8_t *gb_port_framebuffer(const gb_session_t *s);
void gb_port_destroy(gb_session_t *s);
```

头文件需包含标准类型头和 `extern "C"` 保护。创建失败时 `*out=NULL`，失败原因通过单独错误查询或明确日志提供；读取失败使会话停止，不能用全零数据继续执行。帧缓冲在下一次渲染或销毁前有效。

`render=false` 只能跳过像素生成，仍推进 CPU、定时器、中断和必要 PPU 状态；从 SUMI 分批运行帧的逻辑中提取这一语义并验证。不能仅靠减少 `runFrames()` 次数控制显示帧率。

### 并发与生命周期

- 单核系统中，BLE 回调只更新受保护的输入快照并发送事件，不执行模拟、存档或刷屏。
- 模拟任务分帧运行并有界让出 CPU，避免蓝牙与系统任务饥饿；不能靠长期关闭看门狗解决阻塞。
- 显示使用一个 240×20 RGB565 DMA 缓冲起步，完成回调后才能复用；游戏和菜单共用同一个显示所有者。
- 首版不启动 LVGL；同时从组件依赖/配置确认是否仍链接其静态内存池，不能把“未调用初始化”当作“内存已经释放”。
- 退出顺序：暂停输入消费与模拟 → 等待 LCD DMA → 执行存档并检查结果 → 释放 ROM/核心 → 返回菜单。失败初始化也要释放本次取得的资源。
- 游戏时不运行 Wi-Fi、麦克风录音和未使用的音频服务。

### Flash 规划草案

先核对实际固件大小，再确定分区。下列布局仅用于预算；不足时停下来重算，不越界写入：

| 分区 | 起始地址 | 大小 | 用途 |
| --- | --- | --- | --- |
| NVS | `0x9000` | `0x6000` | 设置、配对信息 |
| PHY | `0xF000` | `0x1000` | 射频数据 |
| factory | `0x10000` | `0x300000` | 3 MiB 应用 |
| ROM | `0x310000` | `0x400000` | 4 MiB ROM 容器，含目录和校验信息 |
| saves | `0x710000` | `0xF0000` | 960 KiB 存档文件系统 |

ROM 分区使用项目自定义 data subtype；存档优先使用 ESP-IDF 已有 SPIFFS，初始化/挂载失败不得自动格式化。经验证需要调整时，先确定数据迁移与受影响范围，再请求相应刷写批准。

ROM 读取先采用固定 16 KiB bank 缓存和分区读取，按测量增加缓存或改映射。缓存总量受预算控制；大 ROM 不整份复制进 RAM。ROM 写入仅在维护模式发生，禁止游戏执行时写 ROM。

## 5. 分阶段任务

### 阶段一：建立可复现基线并验证 Xbox 1914

**依赖：** 无。**文件：** BSP、构建配置、`gamepad.*`、`gb_input.*`、`test_gb_input.c`、版本与验证记录。

- [ ] 非破坏性引入固定提交的 AI Passport 基线，保留本计划；读取引入后的 AGENTS.md 及当前相关配置。
- [ ] 找到/准备 ESP-IDF 5.5.3，执行 `idf.py --version` 并记录；核对目标为 esp32c3，核对 Flash 配置。新目标配置使用 `idf.py set-target esp32c3`，随后 `idf.py build`。
- [ ] 记录用户实际 1914 固件版本。若不在已支持范围，说明兼容要求，由用户完成手柄固件操作后复测，不擅自更新手柄。
- [ ] 选取并固定 Bluepad32/BTstack 版本、子模块及官方所需补丁，按其 ESP-IDF 集成方式构建最小 BLE 示例；禁用原 NimBLE host，保证只有一个蓝牙主机栈拥有控制器。
- [ ] 在显示/串口输出连接状态与按钮状态，首版仅一只手柄；连接状态与输入状态分别保存，断线时原子清空所有按键。
- [ ] 测试纯逻辑输入映射，至少覆盖方向+A/B 同按、释放、斜向、相反方向归零及断线；随后完成经批准的上板连接验证。
- [ ] 实测首次配对、手柄关机、板卡重启后重连及重新配对，确认配对存储实际使用的接口与 NVS 键范围；清配对仅清本应用记录。

输入层接口与最小测试示例：

```c
typedef struct {
    bool connected;
    bool right, left, up, down, a, b, view, menu;
} pad_sample_t;
gb_keys_t gb_input_map(pad_sample_t sample);

pad_sample_t p = {.connected=true, .right=true, .a=true};
gb_keys_t k = gb_input_map(p);
assert(k.right && k.a && !k.b);
p.connected = false;
k = gb_input_map(p);
assert(!k.right && !k.left && !k.up && !k.down);
assert(!k.a && !k.b && !k.select && !k.start);
```

**产出/门槛：** 可编译的板级基线与手柄验证程序；实际 1914 稳定输入。若连接受阻，仅暂停依赖手柄的验证，继续显示与核心独立测试；不能把“能扫描到手柄”当作配对成功。

### 阶段二：实现低内存显示路径

**依赖：** 阶段一的硬件基线；不依赖手柄验证通过。**文件：** `gb_display.*`、`test_gb_pixels.c`，必要时小范围调整显示 BSP。

- [ ] 初始化已有 LCD，先显示红绿蓝白黑和四级灰度图，保留原初始化和反色参数。
- [ ] 实现每字节四像素、低两位在左的 SUMI 帧解包，并在条带缓冲中转换 RGB565。
- [ ] 实现 160×144 居中，坐标 `(40,88)`；等待每次 DMA 完成后复用缓冲，超时返回错误并停止提交，不能覆盖在途缓冲。
- [ ] 测试边界和字节序后增加 240×216 模式，坐标 `(0,52)`，按 `src_x = dst_x * 160 / 240`、`src_y = dst_y * 144 / 216` 最近邻取样；上下黑边只在布局变化时清理。
- [ ] 使用固定测试图仅在独立验证入口测吞吐，记录传输时间、帧率和 DMA 内存，不将测试图接入正式游戏入口。

```c
static uint8_t shade_at(const uint8_t *fb, int x, int y) {
    return (fb[y * 40 + x / 4] >> ((x % 4) * 2)) & 3;
}
uint8_t fb[5760] = {0};
fb[0] = 0xE4;
assert(shade_at(fb, 0, 0) == 0);
assert(shade_at(fb, 1, 0) == 1);
assert(shade_at(fb, 2, 0) == 2);
assert(shade_at(fb, 3, 0) == 3);
assert((239 * 160 / 240) == 159);
assert((215 * 144 / 216) == 143);
```

**产出/门槛：** 两种布局正确、无越界和在途缓冲覆盖。SPI 理论纯传输时间：原尺寸约 9.2 ms/帧，放大约 20.7 ms/帧；实测需要额外计入命令、转换及调度，不能套用理论值报告实机帧率。

### 阶段三：提取 GB 核心，跑通一个 ROM

**依赖：** 阶段二；接手柄时需要阶段一。**文件：** `components/gb_core/`、`gb_runtime.*`、构建配置、第三方声明。

- [ ] 提取固定提交的核心及实际引用文件，核对每个来源的许可证和版权声明，保留第三方源码归属。
- [ ] 通过 `gb_port.h` 替换 SdFat 访问、Arduino 时间和平台日志；移除阅读器/墨水屏宿主依赖，默认禁用针对墨水屏的游戏 ROM 补丁和长时间预跑。
- [ ] 从卡带头读取 ROM 类型、容量、SRAM 容量；核对文件最小长度、声明容量和支持类型。CGB-only、未实现映射器或无法分配的 RAM 明确拒绝运行。兼容 DMG 的双色卡带在首版固定运行 DMG 模式，移除上游见到 CGB 标志便自动分配额外内存的路径。
- [ ] 先使用可合法分发的 GB 测试 ROM 或 homebrew；用户游戏 ROM 仅保存在本地导入目录，加入忽略规则，不提交或打包到公开代码仓库。
- [ ] 将核心输出接到阶段二原尺寸显示，接入 `gb_input_map()` 结果；每帧取一次一致的输入快照。
- [ ] 建立固定节拍与统计。DMG 基准为 4,194,304 cycles/s、70,224 cycles/frame，约 59.73 模拟帧/s；以单调时钟的累积截止时间调度，避免整数毫秒截断漂移。
- [ ] 模拟落后时优先省略像素渲染，设追赶上限并记录落后量；禁止无限追赶饿死 BLE，也不能通过无记录的重置时间基准伪装满速。
- [ ] 验证暂停期间不推进 CPU；恢复时重设节拍起点，避免补跑整个暂停时间。RTC 走独立时间策略，不等同于游戏 CPU 暂停。

节拍计算的测试基准：

```cpp
static int64_t frame_deadline_us(int64_t start, int64_t n) {
    return start + n * 70224LL * 1000000LL / 4194304LL;
}
assert(frame_deadline_us(0, 1) == 16742);
assert(frame_deadline_us(0, 4194304) == 70224000000LL);
```

**产出/门槛：** 实际 1914 控制的单 ROM 无声运行；分别记录模拟速度、显示帧率、BLE 连接状态、最小堆和最大连续块。先取得这一证据，再开发完整游戏管理，避免把性能问题留到最后。

### 阶段四：Flash ROM 管理和 USB 导入

**依赖：** 阶段三确认资源预算可行。**文件：** `gb_storage.*`、`partitions.csv`、`pack_roms.py`、`import_roms.ps1`、`test_gb_storage.c`。

- [ ] 依据固件真实大小、链接内存报告和增长余量核对第 4 节布局，验证所有分区对齐、不重叠且结束地址不超过 `0x800000`。
- [ ] 定义版本化 ROM 容器：magic、格式版本、条目数；每条目包含名称、偏移、长度与 SHA-256。目录大小有上限，逐条检查 ROM 范围和重叠。
- [ ] 打包器拒绝容量溢出、重叠或无效 ROM；导入前打印设备、分区地址、镜像长度、ROM 清单及受影响内容。
- [ ] 导入工具从已验证分区表解析地址，不把临时地址写死；使用项目环境中的 esptool，经用户同意后仅更新 ROM 分区，不擦除 NVS/存档。
- [ ] 导入后验证分区写入结果；启动时校验目录，选择游戏时流式校验该 ROM 内容，不把整份 ROM 放入 RAM。
- [ ] 游戏 ID 使用 ROM 内容哈希，避免同名文件互相覆盖存档。ROM 读取回调检查 `offset <= size && bytes <= size-offset`，失败使游戏停止并显示原因。
- [ ] 测试截断容器、损坏目录、整数溢出、末尾精确读取、越界读取和超大条目数；维护模式写入中断后显示损坏错误，不把残缺 ROM 当有效游戏。

```c
static bool valid_range(uint32_t size, uint32_t offset, uint32_t bytes) {
    return offset <= size && bytes <= size - offset;
}
assert(valid_range(32768, 32767, 1));
assert(!valid_range(32768, 32767, 2));
assert(!valid_range(32768, UINT32_MAX, 2));
```

**产出/门槛：** 可重复导入多个容量允许的 ROM，选择和切换正确；普通 ROM 更新不影响已有存档和配对记录。容量不足明确报错，不自动重分区。

### 阶段五：可靠存档与 RTC 边界

**依赖：** 阶段四。**文件：** `gb_storage.*`、核心 SRAM/RTC 导入导出适配、存储测试及验证记录。

- [ ] 移除固定 8 KB SRAM 假设，按受支持卡带类型和头字段分配实际需要容量，保留 MBC2 等特殊映射语义；内存不足则拒绝该游戏，不截断数据。
- [ ] 第一版只实现卡带电池存档，不直接继承 SUMI 二进制即时状态格式。存档绑定 ROM SHA-256，并包含格式版本、SRAM 长度、序号、RTC 数据和校验值。
- [ ] 使用 A/B 两份文件：保留当前有效文件，覆盖较旧副本，写完关闭并重读校验后才视为保存成功；加载时在有效副本中选择较新序号。
- [ ] 文件系统挂载失败时显示存档不可用并阻止依赖存档的正常启动；不调用自动格式化，也不假装保存成功。
- [ ] 暂停和退出时保存；游戏中对脏 SRAM 采用合并写入策略（首版以 30 秒为最短自动保存间隔），暂停模拟进行同步快照，保存后再恢复。记录 Flash 写入带来的卡顿，不能在 BLE 回调执行。
- [ ] RTC：运行期间用独立时间源推进；断电经过时间需要可信墙钟。通过电脑维护工具设置时间并存储同步信息，验证 C3 重启/断电后的有效性；没有可信墙钟时明确提示“断电期间时钟不推进”，不推算虚假离线时间。
- [ ] 通过主机故障注入测试短写、截断、CRC 错误、ROM ID 不符、容量不符和两份均损坏；最后在经批准的设备测试中验证写入中断恢复，不能只凭文件名切换宣称原子性。

**产出/门槛：** 正常恢复与中断恢复均有证据；原始 SRAM、RTC 的范围及限制在游戏兼容清单中可追溯。两个副本都坏时保留证据并报告，未经用户确认不覆盖为新档。

### 阶段六：菜单、性能与首版验收

**依赖：** 前五阶段。**文件：** `main.c`、`game_menu.c`、`gb_runtime.*`、相关测试和用户文档。

- [ ] 状态限定为菜单、配对、加载、运行、暂停、保存和错误；进入加载前检查 ROM/存档，失败返回可读错误，不能留在无限加载画面。
- [ ] 三键映射：上/下选择，OK 确认，OK 长按进入暂停菜单。手柄 View→Select、Menu→Start，不抢占游戏的 Start/Select 组合；暂停优先用机身键。
- [ ] 断线触发输入全释放与暂停；重连后由用户确认恢复，避免连接恢复瞬间误操作。
- [ ] 完成 160×144 模式性能验收，再尝试 240×216 约 30 FPS 显示；以模拟速度和输入响应优先，保留未达标模式的明确状态。
- [ ] 至少选择 ROM-only、MBC1 和 MBC3 的合法测试材料验证实际支持范围；对其他声明支持的类型另列测试结果，未测不写“通过”。
- [ ] 测量绘图关闭、绘图开启、BLE 开启三组运行数据定位负载；只有证据指向内存/带宽时才增加缓存、优化渲染或调整任务参数。
- [ ] 完成 30 分钟连续运行、20 次游戏进退和 10 次重连；核对生命周期、堆稳定、组合键和存档恢复。
- [ ] 对修改批次自检，运行上游要求的静态、主机和固件检查。上游 `tools/validate.sh` 若需 POSIX 环境，使用明确准备的 WSL/Git Bash 运行脚本，不向 PowerShell 输入 Bash 语法；环境不可用时单独报告必需门禁未执行，不用一次 `idf.py build` 替代。
- [ ] 更新 CONTEXT、运行说明、ROM 导入说明、兼容性清单及验证记录，注明固件版本、手柄版本、测试 ROM 哈希和测试模式。

**产出/门槛：** 满足第 2 节的无声 GB 首版；用户得到可构建工程、经校验固件、导入工具和清晰的实机验收结果。

### 阶段七：游戏音频（独立后续交付）

**依赖：** 首版性能和 RAM 证据；声音若为首版必需，则将本阶段的核心评估前置。

- [ ] 评估 SUMI 核心增加 APU 的成本，与采用已有音频核心的成本；核对准确性、许可证、RAM、CPU 和适配接口，不假设 ES8311 驱动能代替 APU。
- [ ] 在确定声音方案后，先实现独立 PCM 生成和主机波形/时序验证，再调用 `bsp_audio_set_format()`、`bsp_audio_write()` 接入硬件。
- [ ] 采用小型 PCM 环形缓冲和独立工作任务，初步评估 22050 Hz/16 bit/单声道；采样率是待测设计参数，不冒充已验证硬件结果。
- [ ] 正确处理暂停、恢复、缓冲欠载、退出及 codec 格式切换，不让阻塞音频写入占住输入或显示回调。
- [ ] 重新测量模拟速度、显示帧率、堆、欠载次数和声音连续性；声音开启后不达首版速度门槛时继续处理或明确标为未完成。

**产出/门槛：** 独立报告有声版本的性能与兼容性；在此之前只称无声 GB 版本完成，不承诺完整 GBC 或全游戏兼容。

## 6. 风险和调整规则

| 风险 | 识别证据 | 范围内处理 |
| --- | --- | --- |
| 1914 固件/协议差异 | 配对日志、加密状态、实际报告解析 | 优先按 Bluepad32 官方支持路径处理；无法连接时不扩展到自研 HID 栈，先说明具体差异 |
| 内部 RAM 不足 | 链接报告、最小堆、最大块、分配失败 | 去除未使用服务和 LVGL 静态池、保持条带缓冲、缩小 ROM 缓存；不能牺牲存档容量正确性 |
| CPU 达不到全速 | 模拟速度随 BLE/渲染负载变化 | 先跳像素生成和降显示刷新；原尺寸仍不达标则如实报告需核心优化/换核心，不自动购买或更换硬件 |
| 单 bank 缓存频繁失效 | bank miss 与读取耗时统计 | 在预算内加缓存，或验证分区映射读取；不凭感觉增大内存 |
| Flash 保存卡顿/中断 | 保存耗时、故障注入和断电测试 | 暂停快照、合并写入、双副本校验，界面显示保存结果 |
| GBC/SRAM 兼容性被高估 | ROM 头、核心渲染与映射实现、测试 ROM | 首版限定 DMG，对不支持的模式明确拒绝，保留具体 ROM 兼容清单 |
| 工具链版本不符 | `idf.py --version` 和依赖锁 | 使用已验证 5.5.3 环境；若要改变基线先形成单独兼容性证据 |

## 7. 执行边界和最终交付

- 本次仅写计划，没有克隆源代码、配置工具链、编写固件、运行测试或刷机。
- 用户之后要求实施时，可进行范围内本地代码修改和非破坏性验证；创建分支/worktree、刷机、擦除、导入 ROM 覆盖设备数据和外部发布仍按用户批准范围执行。
- 首次刷机前提供具体设备、固件、分区和会改变的数据范围供批准；不把“USB 已连接”当作刷机授权。
- 不把 Flash 备份设为强制前置；但明确说明旧应用分区重排对原有内容的影响，未经授权不全片擦除。
- 不自动提交、推送、开 PR 或更新 Codex/Obsidian 永久记忆。
- 最终报告分别填写：构建、主机测试、设备测试、未验证事项；测试仅报告真实结果。

最终交付应包含：可复现的源代码及依赖版本、通过校验的固件、ROM 打包/导入工具、配置与存档说明、支持游戏清单和实机测试记录。没有实机条件时，构建可交付，但设备验收必须明确保持未完成。

## 8. 参考资料

- [AI Passport 硬件仓库](https://gitee.com/folotoy/ai-passport)
- [产品规格](https://gitee.com/folotoy/ai-passport/blob/main/docs/hardware-design/specifications.zh_CN.md)
- [硬件开发指南](https://gitee.com/folotoy/ai-passport/blob/main/docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md)
- [BSP 引脚与参数](https://gitee.com/folotoy/ai-passport/blob/main/components/bsp/include/bsp_pins.h)
- [原分区表](https://gitee.com/folotoy/ai-passport/blob/main/partitions.csv)
- [原配置](https://gitee.com/folotoy/ai-passport/blob/main/sdkconfig.defaults)
- [SUMI 核心基线](https://github.com/psychoplath9450/SUMI/blob/1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb/src/plugins/gb/gb_emulator.h)
- [SUMI 宿主适配](https://github.com/psychoplath9450/SUMI/blob/1a1c47c3fc4fa7a7c2d4ffba0f55e1e7dfdad1cb/src/plugins/SumiBoyEmulator.cpp)
- [Bluepad32 ESP-IDF 接入](https://bluepad32.readthedocs.io/en/latest/plat_esp32/)
- [Bluepad32 Xbox 兼容列表](https://bluepad32.readthedocs.io/en/latest/supported_gamepads/)
