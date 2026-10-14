[English](ble-input-fc-handoff.md) | 简体中文

# GameBoy BLE 修复与 FC 移植交接（2026-10-09）


## 1.9 FC 到 GB 遥控器移植（2026-10-14）

本地 FC 项目提供小米身份限定横握映射、实测 RC001/FW2671 描述符／报告兼容分支及菜单入页基线模式。GB 解析器与广播回归重放这些格式，固定测试页快照也接收映射后的键盘输入。这属于本地 vendored Bluepad32 修改，不是上游已支持的声明。GB COM14 启动和用户遥控器／菜单专项验收单独记录。见[手柄说明](controller-test.zh_CN.md)及[验证记录](gameboy-validation.zh_CN.md)。

## 2026-10-12 手柄更新

GameBoy 1.8 十四游戏完整包已于 2026-10-12 烧录到 COM14，用户随后反馈“测试下来没问题”。本次确认限于用户实际操作范围，不外推为全部游戏、全部手柄或长期稳定性验收。 L1241 适配、主页导航、手柄测试与 Select 长按功能见[手柄说明](controller-test.zh_CN.md)。FC 历史结果不替代 GameBoy 验收。

## 范围与状态

本任务只修改 `E:/code/folotoy-gameboy`，对 `E:/code/folotoy-fc` 仅进行只读参考。最初交接覆盖 1.4 后续本地修复；FC 随后已合入这些 BLE 修复，用户在本会话确认 FC 手柄测试并收尾。GameBoy 现回移 FC 有序输入缓存，将累计改动打包为 **1.5**。历史包保留；后续[14 游戏包](gameboy-classics-1.5.zh_CN.md)已按授权烧录 COM14 并核对启动，游戏验收另记。最初交付时未提交；累计源码现纳入 1.7 归档，当前同步状态以 Git 记录为准。

代码处理了分报告输入丢失、常见标准按键布局、左摇杆控制、Xbox 名称触发描述符覆盖，以及 BLE 失败恢复。实体兼容性和性能仍待验收。验证证据见[验证记录](gameboy-validation.zh_CN.md)。

## BLE 实现

| 项目 | 仓库内文件 | 行为 |
| --- | --- | --- |
| 标准输入解析器 | `components/bluepad32/parser/uni_hid_parser_standard.c`，同组件 `include/parser/uni_hid_parser_standard.h` | 最多按八个输入报告 ID 分别保存状态。新报告只替换自己的状态，汇总时保留其他报告仍按住的键和方向。支持位图／数组按键、帽开关、有符号／无符号轴和媒体 Back/Home。未知／截断报告记录错误后忽略。 |
| 解析器选择与真实报告描述符 | `components/bluepad32/bt/uni_bt_le.c`、`uni_hid_device.c` | 所有 BLE 候选在解析器初始化前选择符合条件的 HID 服务并复制真实描述符，过滤其他服务。标准 BLE Android／通用／Xbox 输入配置使用新解析器；键盘及专有协议保持独立驱动。 |
| 模拟 Xbox 名称的手柄 | `components/bluepad32/parser/uni_hid_parser_xboxone.c` | BLE 不再因名称匹配而替换为旧 Xbox 描述符；经典蓝牙保留旧兼容路径。名称不能证明按键布局。 |
| 连接恢复 | `components/bluepad32/bt/uni_bt_le.c`、`uni_hid_device.c`、`include/uni_hid_device.h` | 使用连接句柄前检查连接请求与完成事件的失败状态；超时取消尚未建立的连接，按扫描策略恢复发现；配对／GATT 建立失败时断开，并向 UI 报告失败。旧绑定重加密遇到密钥／认证失败时，仅针对该设备删除旧绑定并尝试一次重配，再失败则断开。首次配对认证失败不会删除其他设备的绑定。 |
| 广播发现 | `components/bluepad32/bt/uni_bt_le_advertisement.c` | 保留按地址及类型合并广播／扫描响应、16/128 位 UUID 列表和 Gamepad/Joystick 接纳，新增 32 位 UUID 列表及标识 HID `0x1812` 的 16 位 Service Data。不会只凭名称接纳任意设备。 |
| 左摇杆 | `main/gb_input.c`、`main/gb_input.h`、`main/gamepad.c` | 归一化 X/Y 超出正负 160 死区后映射方向，同轴十字键优先。摇杆方向参与活动／唤醒／松开判断，回中后释放摇杆方向。 |

`uni_hid_device_create()` 改为调用设备初始化函数，使待连接对象从无效连接／HID 句柄开始，不再使用零值。HID 描述符写入拒绝空指针、空长度和超过 512 字节的内容，避免原先按超长长度复制到固定缓冲区。

### 常见映射与明确限制

HID 按键编号不规定实体标签。通用映射按描述符声明的最大按键 usage 推断常见约定，不能证明所有手柄兼容：

| 描述符布局 | A/B/X/Y | Select/Start |
| --- | --- | --- |
| 不超过 8 键 | 1/2/3/4 | 7/8 |
| 9～12 键 | 1/2/3/4 | 9/10 |
| 超过 12 键的 Android 风格 | 1/2/4/5 | 11/12 |

已知 Xbox BLE 配置保留 RX/RY 的模拟扳机解释；标准配置支持左 X/Y、右 Z/RZ 或 RX/RY、Simulation Brake/Accelerator、帽开关和独立十字键 usage。本应用输入路径不使用被替换的标准配置中的震动／输出命令。

新解析器使用设备现有 256 字节 parser storage，不额外分配堆。限制：512 字节描述符、八个输入报告 ID、单字段 1～16 位、每个 Input 项最多 255 个字段、每个报告最多 512 字节有效载荷。全局 Push/Pop、切换后又返回较早报告 ID 的交错定义会明确拒绝，因为当前 BTstack 迭代器不能正确解释它们。不支持的描述符会使初始化失败。专有布局及不同实体标签仍可能需要单独映射。设备信息服务失败仍中止当次连接，本次不声称兼容必须绕过该服务的设备。

## 从 FC 回移的有序输入缓存

两层状态保留解决不同问题：HID 解析器合并不同报告 ID；应用缓存防止两次模拟器取键之间收到的完整按下／松开被新状态覆盖。移植依据为 2026-10-09 读取的 FC `main/gb_input.c/.h`、`main/gamepad.c/.h`、游戏生命周期接线及 `tests/test_gb_input_buffer.c`。

`set_sample()` 在原临界区内更新实时快照与缓存。菜单使用 `gamepad_snapshot()`；只有游戏循环每模拟帧调用一次 `gamepad_frame_snapshot()`，不受显示跳帧影响。`gamepad_set_gameplay()` 在暂停／退出时清空，进入／恢复时等待松键后启用，包括存档失败后的恢复路径。断连立即清除待处理输入，全部会话销毁路径禁用缓存。

静态队列保存 16 个完整变化状态，不重复缓存相同状态。保存完整状态可维持独立松开，不把先后短按拼成虚假组合。溢出或最旧事件超过 100 ms 时丢弃积压，通过临界区外的 `input backlog discarded` 日志报告数量，最新长按状态仍可读取。密集输入可能延后数个模拟帧，年龄上限不是延迟保证。未复制 FC 专用追踪日志、NES 取键寄存器或内核改动。

五类主机场景覆盖保持方向时连续短按、组合与顺序、进入／暂停／断连／容量、准确过期边界及摇杆／动作／回中，不能代替 GameBoy 游戏实测。FC 磁盘文档中较早的待验收说明早于用户本轮确认，不从该确认推导未提供的手柄型号矩阵。

## FC 绘图优化的复用结论

只读依据：2026-10-09 检查到的 FC `main/gb_frame_pacing.c`、`main/gb_display.c`、`docs/performance.md`。

| FC 方法 | GameBoy 处理 |
| --- | --- |
| 按上次实际提交绘制计数，替代固定帧号取模 | 已复用。仅帧被接收后调用 `gb_frame_pacing_mark_drawn()`，错过机会后持续保持待绘制，直到显示可接收。 |
| 生成像素前检查显示任务是否空闲 | 已复用 `gb_display_idle()`。跳过像素时仍运行 CPU/APU 与 PPU 时序。 |
| 分别统计显示完成和模拟帧数 | 已复用 `gb_display_completed()` 和 `display_fps`。画面未变、所有条带都省略 SPI 传输时，完成的显示更新仍计一次；不是独立新画面的数量。 |
| 异步借用内核帧缓冲 | 不移植。GameBoy 已复制较小的 2bpp 帧供后台显示，DMA 期间内核可继续运行；改成借用会增加缓冲生命周期约束。 |
| 无 Mapper latch 时仅检查 NES 精灵 0 | 不适用。GameBoy 非绘制帧已跳过 `renderLine()` 并保留窗口／时序推进；没有相同的 NES 精灵 0 和 Mapper 规则。 |
| NES APU Q16 与 6502 指令分派 | 不适用于不同的 GameBoy CPU/APU。 |
| FC 自适应阈值及最大跳帧间隔 | 不搬用。GameBoy 保留原先 2/3 帧间隔和 20/15 ms 阈值，等待实测再判断。 |

GameBoy 原本已按 tile 区段读取背景／窗口，并对单色显示做 20 行条带差异刷新。检查到的 FC `fc_draw()` 仍按条带传输完整彩色画面，并不存在可直接搬用的额外脏区算法。本次没有改 GB/GBC 内核绘图、PPU 优先级、时钟、分辨率、音频格式或分区，不声称已经取得实测帧率提升。

## 最初给 FC 会话的实施步骤（历史）

1. 重新读取 FC 当前规则、上下文和 Git 状态，交接后代码可能已变化。不要用 GameBoy 文件整树覆盖 FC。
2. 对照移植上述 BLE 文件，将 `parser/uni_hid_parser_standard.c` 注册到组件 CMake，保留 FC 特有改动。移植输入辅助函数及 `controller_data()` 中的一处调用；不要用 GameBoy 的 `game_main.c` 覆盖 FC 应用。
3. 移植 `tests/test_gb_gamepad_hid.c`、`tests/test_bt_le_recovery.c`，以及广播发现和左摇杆测试新增部分。只补这些测试实际需要的主机桩，不能加入生产固件 include 路径。原生命令接线参考 `tools/test_gameboy_host.py`；相同用例已加入 `tools/validate.sh`。
4. 保留 FC 当前帧调度、借用缓冲等待释放规则、Q16 APU、精灵优化和实测性能记录。FC 已有本次借鉴的调度方法，不要换成 GameBoy 更低的最大间隔。
5. 运行 FC 主机／内核／打包测试及 ESP-IDF 5.5.3 构建、分区校验，记录新的 FC 证据。GameBoy 通过不等于 FC 二进制已通过。
6. 单独取得烧录批准，先核对已安装固件身份及分区兼容性，再判断能否仅更新应用。日常更新不自动授权写入完整镜像或清除游戏／存档。

## 回归与真机验收

旧 Android 解析器在拆分报告测试中，收到摇杆报告后清掉仍按住的 A，断言失败。新解析器通过该序列，以及独立松开、组合键、媒体报告、数组、无 ID 报告、有符号轴、帽开关中立位、常见 8/12/16 键和畸形报告测试。连接恢复测试执行生产 BLE 事件处理函数，无线／GATT 副作用使用测试桩；不等于完整安全协议或实体无线模拟。

待真机检查：原有 Xbox 和键盘回归；通用 8/12 键及模拟 Xbox 名称的 BLE 手柄；按住 A 时移动／回中摇杆、不同顺序松开、Select/Start、菜单操作、唤醒后松开、多次断连重连、旧密钥失效、错误／取消 PIN 和配对。绘图需对同一 ROM、同一场景前后对照，记录 `emu_fps`、`display_fps`、音频等待／丢块与剩余堆。代码测试及构建通过不能代替这些实机结果。

## 1.7 实机复测与交付（2026-10-10）

后续只修改 GameBoy；FC 未改动。输入兼容矩阵不因性能实测自动扩展。 [Performance](gameboy-performance.zh_CN.md).
