# Native LDAC for Windows

面向 Sony WH-1000XM5 的 Windows 原生 LDAC 驱动方案。项目复用系统蓝牙控制器和 Windows 蓝牙栈，在用户态完成 PCM 采集、LDAC 编码、AVDTP 协商与 RTP 封包，在内核态提供受限的 Bluetooth profile transport；不接管 USB 蓝牙控制器，也不替换主板 radio 驱动。

## 核心模块

| 模块 | 作用 | 当前状态 |
| --- | --- | --- |
| `driver/` | KMDF Bluetooth profile transport，提供 signaling/media IOCTL | 已完成 ABI 0.5，AX211/XM5 真机建链通过 |
| `protocol/` | AVDTP 状态机、LDAC capability 和 RTP packetizer | 已完成，CTest 覆盖协议边界 |
| `engine/` | WaveRT PCM reader、格式转换、音量/静音处理和 libldac 输入 | 已完成 16-bit；24-bit Windows 容器路径已实现 |
| `audio-endpoint/` | PortCls/WaveRT render endpoint 与 PCM ring buffer | 已完成 44.1/48/88.2/96 kHz、16/24 valid-bit 离线路径 |
| `agent/` | 事件驱动 presence、daily worker、生命周期、VolumeSync 和清理 | 已完成主要 V1 流程 |
| `tools/` | 构建、候选包、只读探针、格式事务和回滚工具 | 已完成安全门禁与恢复脚本 |
| `ui/` | Python/Tk 控制界面和状态/码率遥测 | 已完成只读 daily UI 与质量选择 |

## 完成进度

已由真实 XM5 链路验证：

- LDAC capability 枚举、`SET_CONFIGURATION`、`OPEN`、Media L2CAP、`START`、`SUSPEND -> CLOSE`；
- MQ/SQ/HQ 质量档位及稳定链路码率观测，Auto ABR 的保守降档逻辑；
- 48 kHz/16-bit/stereo 系统音频与 WaveRT endpoint bridge；
- `44100/16/HQ/stereo + VolumeSync` 固定 daily 门禁；
- Windows 主音量、静音和 XM5 侧绝对音量的 V1 同步路径；
- endpoint PCM ABI、活动格式回读、格式设置事务和原格式恢复；
- Release 构建、PowerShell policy/parser、Python parser 和 CTest 回归。

当前明确状态：

- 24-bit 已改为 Windows 标准的 `32-bit container + 24 valid bits` 表示，用户态解码、共享模式格式控制和门禁校验已更新；最新 endpoint 驱动尚未取得新的 24-bit XM5 真机 PASS，因此发布状态仍为“实现完成、硬件复测待进行”；
- 44.1/48/88.2/96 kHz、mono/dual 的协议能力和离线配置路径存在，除已记录的 16-bit 基线外，逐项真机证据仍需补齐；
- 常驻服务安装、默认设备自动切换、睡眠唤醒和长期压力测试不属于本次冻结快照的完成项。

完整开发过程和每轮证据见 [docs/DEVELOPMENT_HISTORY.md](docs/DEVELOPMENT_HISTORY.md)。长期路线见 [docs/ROADMAP.md](docs/ROADMAP.md)。公开发布不包含本机生成的构建产物、原始真机日志、驱动商店备份、证书私密材料或用户环境路径。

贡献者见 [CONTRIBUTORS.md](CONTRIBUTORS.md)。

## 构建协议测试

需要 Visual Studio 2022 C++ Build Tools、CMake、Windows SDK/WDK 和 PowerShell 7：

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

构建只产生本地测试文件，不安装驱动、不修改 PnP、不重启系统。驱动安装和 endpoint 更新属于单独的管理员操作，必须先阅读 [driver/README.md](driver/README.md)、[audio-endpoint/README.md](audio-endpoint/README.md) 和相关工具说明。

## 只读探针与播放

驱动已安装并绑定后，可先检查 transport 和 endpoint：

```powershell
.\build\protocol\Release\transport_probe.exe --info
.\build\protocol\Release\audio_endpoint_probe.exe --scan-interfaces
.\build\protocol\Release\audio_endpoint_probe.exe --format
```

在管理员 PowerShell 7、目标耳机已连接且用户确认输出设备后，固定质量门禁一次只运行一个档位：

```powershell
& .\artifacts\v1-daily-host\candidate\run-v1-daily-quality-gate.ps1 `
    -Quality hq `
    -VolumeSync
```

门禁只有在实际 transport result 同时记录目标 PCM 格式、真实 `START`、非零媒体包、质量/码率匹配和正常清理时才算通过；“Applied format”本身不是播放证据。

## UI

UI 使用 Python 标准库 Tk 8.6，不需要第三方 Python 包：

```powershell
pwsh -NoProfile -File .\tools\run-ui.ps1
```

UI 的停止路径通过命名 Event 通知 worker，等待 `SUSPEND -> CLOSE` 完成，不强杀媒体进程。

## 发布范围

本快照面向源码审阅、离线构建和后续开发交接。公开文档中的设备地址、主机名、用户名、安装生成的 INF 名称、证书指纹和绝对路径均已移除或改为运行时发现/占位符。技术协议中的 `Bluetooth A2DP` 仅作为协议名称保留。

## 许可

本项目原创代码采用 Apache-2.0，见 [LICENSE](LICENSE)。Bluetooth transport 和 WaveRT endpoint 中使用的 Microsoft sample 衍生部分按 MS-PL 保留，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。libldac/ldacBT 的来源、固定 revision 和许可证见同一文件。
