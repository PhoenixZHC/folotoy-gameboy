<p align="right"><a href="gameboy-firmware.md">English</a> · <strong>简体中文</strong></p>

# Game Boy 固件镜像与 GitHub 提交

本产品采用仓库根目录 `partitions.csv` 中的五个分区，不采用上游三分区模板。清洁版镜像长度**恰好为 8,388,608 字节（8 MiB）**：`0x0` 为 ESP32-C3 引导程序，`0x8000` 为分区表，`0x10000` 为应用，`0x710000` 为可重复生成的初始 SPIFFS 镜像；其余字节全部为 `0xFF`。尤其是 NVS（`0x9000`–`0xEFFF`）、PHY（`0xF000`–`0xFFFF`）及整个 ROM 区（`0x310000`–`0x70FFFF`）不包含设置、手柄绑定或游戏。初始存档文件系统只包含公开的 `assets/initial_saves/README.txt`，没有游戏进度。

## 构建与校验

激活 ESP-IDF 5.5.3，以 `esp32c3` 为目标构建。上游完整门禁为 `./tools/validate.sh`；其中固件模式还会生成 `build/FoloToy-GameBoy-8MB-clean.bin` 及 `.sha256` 校验文件。在 Windows 上执行 `idf.py build` 后，可用以下命令生成同样的镜像：

```powershell
& 'C:\Users\User\.espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe' tools/package_gameboy_release.py
& 'C:\Users\User\.espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe' tools/package_gameboy_release.py --verify
Get-FileHash -Algorithm SHA256 -LiteralPath 'artifacts\releases\FoloToy-GameBoy-8MB-clean.bin'
```

打包工具在写出前会核对固定分区、源码镜像、重新生成的清洁 SPIFFS、准确的 8 MiB 长度，以及未使用区、ROM、NVS 均为擦除态。本地产物为 `artifacts/releases/FoloToy-GameBoy-8MB-clean.bin`，其 SHA-256 应与同目录 `.sha256` 文件一致。这两个本地产物均被 Git 忽略。不要把商业或用户提供的 ROM、设备 Flash 回读、NVS 转储、存档、配对数据加入源码仓库或 GitHub Release。较小的 `FoloToy-AI-Passport-full.bin` 是 ESP-IDF 的合并固件，只到应用末尾，**并非**整片 8 MiB 镜像。

## 首次安装与日常更新

**仅用于首次安装或明确要清空全部数据时**，从 Flash `0x0` 烧录清洁版镜像。选定实际串口后，可用 ESP-IDF 的 esptool：

```powershell
& python.exe -m esptool --chip esp32c3 -p COM14 -b 460800 write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB 0x0 'artifacts\releases\FoloToy-GameBoy-8MB-clean.bin'
```

此操作覆盖整片 8 MiB，包括所有游戏、电池存档、设置和手柄配对密钥。清洁版启动后游戏列表为空；进入“游戏管理”，连接 `FoloToy-GB` 热点，再上传自己有权使用的 ROM。已有安装应**只更新应用分区**：将 `build/FoloToy-AI-Passport.bin` 写到 `0x10000`，保留兼容的原有分区表、ROM、存档和 NVS；不能拿清洁版镜像当作普通升级包。

## 仓库与发布

提交源码、中英配对文档、测试、脚本、配置及第三方许可证记录。`build/`、`artifacts/releases/`、`artifacts/baselines/`、`sdkconfig`、`managed_components/`、Python 缓存、ROM、设备回读和用户数据不进入 Git。推送发布标签后，工作流从已提交源码构建清洁镜像，并把 `.bin` 和校验文件附到 GitHub Release。编译和镜像布局通过不等于空白设备启动已实测；真机验收与剩余性能限制见[基准验收](../gameboy-acceptance.zh_CN.md)。
