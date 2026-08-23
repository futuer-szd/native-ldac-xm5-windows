# Native LDAC 代码与架构审查（2026-07-19）

## 审查范围与结论

本次为只读静态审查，覆盖当前双驱动架构、Bluetooth transport、WaveRT 虚拟端点、用户态 agent/probe、direct-PDO 原型与安装脚本。没有运行 Driver Verifier、内核故障注入或长时间压力测试。

审查时工作区包含尚未提交的 direct-PDO 抢占/诊断开发代码，因此本报告是当时工作树的快照。

总体结论：架构已证明可以通过 Windows 原生 BthPort/AX211 向 XM5 发送真实 LDAC 音频，功能链路成立；但目前仍是测试原型。发现一个高风险本地提权问题、多个访问控制和内核生命周期问题。在这些问题修复并完成 Driver Verifier 前，不适合作为长期、自动启动的稳定版本。

## 按严重程度排列的问题

### P0：登录计划任务从用户可写目录执行高权限程序

`tools/install-agent-autostart.ps1` 将 `ldac_agent.exe` 和 `transport_probe.exe` 安装到 `%LOCALAPPDATA%\NativeLdac\bin`（约第 55、85-87 行），随后创建 `RunLevel Highest` 的登录计划任务（约第 89-112 行）。

`agent/ldac_agent.cpp` 的 `RunAgent` 默认选择与 agent 同目录的 `transport_probe.exe`（约第 655-669 行），`StartProbe` 通过 `CreateProcessW` 直接执行该路径（约第 480-550 行）。

普通权限的同用户进程能够替换 LocalAppData 中的 EXE。计划任务下一次运行时，替换后的程序可能在不再次弹出 UAC 的情况下以最高权限执行。这是明确的本地提权/持久化边界漏洞。

修复要求：

- 安装到 `%ProgramFiles%\NativeLdac` 或另一个仅 SYSTEM/Administrators 可写的目录；
- 普通用户只拥有读取和执行权限；
- 计划任务只执行固定受保护路径；
- 安装模式禁止任意 `--probe` 路径；
- 启动前验证 Authenticode，或至少验证受保护安装清单中的 SHA-256；
- 修复前不要正式启用登录自动启动任务。

### P1：WaveRT 自定义 KS 属性对所有本地进程开放

`audio-endpoint/Source/Main/NativeLdacAudio.inx:97` 的设备安全描述符向 `WD`（Everyone）和 `RC`（Restricted Code）授予 `GRGWGX`。

`audio-endpoint/Source/Filters/speakerwavtable.h:227-243` 暴露以下自定义属性：

- PCM Info GET；
- PCM Read GET；
- LinkState GET/SET。

`audio-endpoint/Source/Main/minwavert.cpp:607-717` 的 PCM Read 会从唯一 ring buffer 中移除数据，是破坏性的单消费者读取。另一个进程可先于真正 agent 读取数据，造成丢音、补静音或拒绝服务。由于设备 ACL 允许 Everyone，这也形成跨进程音频数据读取面。

`minwavert.cpp:722-783` 的 LinkState SET 仅验证结构大小、版本和值域，不验证调用者。其他进程能够伪造 connected/heartbeat/stopping 状态，使动态 Jack 与 Windows 连接显示失真。

修复要求：

- 收紧设备 ACL；
- 使用专用 service SID 或受保护 broker 独占打开控制接口；
- 引入单消费者租约和随机 session token；
- 拒绝第二个 PCM reader 和非 owner 的 LinkState SET；
- UI 通过带 ACL 的 IPC 访问 broker，不直接写 KS 控制状态。

### P1：旧版 Bluetooth transport 未释放 BthPort profile interface

`driver/sys/device.c:135-143` 使用 `WdfFdoQueryForInterface` 获取 `GUID_BTHDDI_PROFILE_DRIVER_INTERFACE`，但 `driver/sys` 中没有找到对应的 `InterfaceDereference`。

这会导致设备解绑、更新、删除时泄漏接口引用。查询成功后，如果后续 `WdfRequestCreate`、queue 创建或 device interface 创建失败，也没有统一回滚。

修复要求：

- 增加明确的 `ProfileInterfaceReferenced` 状态；
- 查询成功后的所有失败路径统一回滚；
- 在 device cleanup/release 中保证只调用一次 `InterfaceDereference`；
- 对照 direct-PDO 当前已经实现的 profile/interface 生命周期合同。

### P1：旧版 transport shutdown 缺少完整 rundown 和串行化

`driver/sys/device.c:152-160` 使用 `WdfIoQueueDispatchParallel`。文件 cleanup 与 SelfManagedIo cleanup 都调用 `LdacNativeConnectionShutdown`（`device.c:179-187`）。

`driver/sys/connection.c:1219-1284` 会取消 signaling read/write、media write 和两个 open 请求，但只等待 signaling/media open 完成；没有等待 read、write、media-write completion 退出，随后立即关闭 Media/Signaling channel。

同时，同一个 `InitializationRequest` 被同步 BRB 操作复用（`connection.c:105-163`），而代码中没有全局 PASSIVE_LEVEL operation mutex。可能出现：

- file cleanup 与 PnP cleanup 重复关闭；
- transfer completion 与 channel close 并发；
- 同步 BRB request 被并发复用；
- PnP teardown 与 profile interface 使用重叠。

修复要求：

1. 进入 shutdown 时设置 `ShuttingDown`；
2. 停止或暂停默认 queue，并拒绝新请求；
3. 在统一 PASSIVE_LEVEL mutex/rundown 下执行关闭；
4. 取消并等待 open、read、write、media-open、media-write 全部退出；
5. 关闭 Media 和 Signaling channel；
6. 最后释放 BthPort interface。

### P1：PCM Read 使用 4 KiB 内核栈数组

`audio-endpoint/Source/Main/minwavert.cpp:613` 声明：

```cpp
BYTE pcmScratch[NATIVE_LDAC_PCM_MAX_READ_BYTES] = { 0 };
```

`NATIVE_LDAC_PCM_MAX_READ_BYTES` 在 `audio-endpoint/Source/Inc/nativeldac_pcm_abi.h:16` 中为 4096。PortCls/KS 调用链本身会消耗内核栈，额外 4 KiB 局部数组增加压力测试和 Driver Verifier 下的栈耗尽/bugcheck 风险。

修复要求：在 miniport 初始化时预分配 NonPagedPoolNx scratch buffer，或使用 lookaside list；不要在属性处理调用栈上放置 4 KiB 缓冲区。

### P2：direct-PDO 取消后存在无限等待

`direct-pdo/src/nativeldac_bth_request.c:99-118` 在同步请求超时或等待失败后调用 `IoCancelIrp`，然后无限等待 completion。

这样做可以防止 IRP 和栈 context 被过早释放，内存生命周期方向正确；但 lower driver/BthPort 如果没有完成取消，PnP 停止或卸载可能永久卡住。`nativeldac_bth_signaling.c` 的部分 stop/close 路径也存在类似无限等待。

不能通过“超时后直接释放 IRP”修复。需要用 Driver Verifier、故障注入和拔设备压力测试确认取消合同，并加入等待时间和状态遥测。

### P2：架构文档存在过期内容

`docs/ARCHITECTURE.md:50` 仍称 PCM ring 尚未接入，约第 114-115 行仍称 endpoint 枚举和 ring buffer 待实现；这些内容已与 `STATUS.md` 和当前代码不一致。`docs/TRANSPORT_ABI.md` 中部分“尚未验证”描述也落后于真机测试历史。

错误文档会增加后续驱动升级、回滚和所有权迁移时的操作风险。建议明确区分：

- 已真机工作的 legacy 双驱动路径；
- 仍不可安装的 direct-PDO 目标路径；
- 已验证、仅主机测试、尚未测试三个状态。

## 架构评价

当前可工作的数据路径为：

```text
Windows Audio
  -> NativeLdacAudio WaveRT endpoint
  -> elevated user-session agent / transport_probe / libldac
  -> LdacNative Bluetooth transport
  -> Windows BthPort
  -> AX211
  -> WH-1000XM5
```

这一结构功能上成立，但由于 transport device 只允许 Administrators/SYSTEM，整个 agent、协议解析、LDAC 编码、日志和子进程管理都被迫运行在高完整性级别，攻击面偏大。

建议最终安全架构：

- 内核驱动只保留最小的 BthPort、端点与有界传输功能；
- 使用安装在 Program Files、拥有专用 service SID 的小型 broker/service 持有驱动句柄；
- 编码和 UI 尽量保持普通用户权限；
- UI 使用带 ACL 的 named pipe/ALPC 与 broker 通信；
- LDAC 编码器和复杂 AVDTP 解析不要移入内核。

direct-PDO 可以减少两个驱动分别维护连接状态的复杂度，但它增加了 PortCls 与 BthPort 在同一内核组件中的生命周期耦合。目前原型仍不可安装、没有外部控制调用面，真实 START/Media 尚未接入，因此继续保持 fail-closed 是正确的。

## 已确认的良好设计

- legacy transport interface 默认只允许 SYSTEM/Administrators；
- transport handle 为 exclusive，并限制 signaling/media transfer 大小；
- BRB 传输使用 NonPagedPoolNx 中间缓冲区；
- AVDTP capability/SEP/vendor/长度解析有显式边界检查；
- RTP/LDAC packetizer 检查 MTU、输出容量与整数上限；
- agent 使用单实例、命名停止 Event、有界日志和原子状态文件；
- probe 先以 suspended 状态创建，加入 kill-on-close Job Object 后再恢复；
- 已进入 START 的路径优先使用 `SUSPEND -> CLOSE` 正常释放；
- 安装和回滚脚本具有管理员检查、专用确认和精确设备识别；
- direct-PDO 使用 generation、owner、arbiter 和 contract tests 管理异步生命周期；
- direct-PDO 原型保持不可安装且无外部触发面。

静态审查没有在当前协议解析器、RTP packetizer 或 IOCTL 长度检查中发现明显的裸缓冲区越界，但这不能替代 Driver Verifier 和 fuzzing。

## 建议修复顺序

1. 修复 `%LOCALAPPDATA% + RunLevel Highest` 提权漏洞；
2. 收紧 WaveRT/KS ACL，加入认证和单消费者租约；
3. 修复 legacy transport 的 interface dereference；
4. 为 shutdown 添加完整 request rundown 和串行化；
5. 移除 PCM Read 的 4 KiB 内核栈数组；
6. 更新架构和 ABI 文档；
7. 再进行 Driver Verifier 与真机压力测试。

建议的 Driver Verifier 项目：Special Pool、Force IRQL Checking、I/O Verification、Deadlock Detection、Security Checks、DDI Compliance Checking。

建议压力场景：

- Agent 启动/停止循环；
- XM5 开机、关机、远端断连与立即重连；
- signaling read/write 和 media write 进行中取消；
- Windows 睡眠/唤醒；
- endpoint active/idle 快速切换；
- 驱动更新、禁用、启用和卸载；
- Agent 强制结束后的 heartbeat 过期；
- 多进程同时打开 PCM/LinkState 接口。

## 修复复核（2026-07-19）

本节记录对上述静态审查结论的代码复核和本轮处理结果。原始问题描述保留，便于追踪当时风险。

### 已在源码中修复

- **P0 登录计划任务提权边界**：安装目录迁移到 `%ProgramFiles%\NativeLdac\bin`，使用显式 SID ACL 仅允许 SYSTEM/Administrators 写入、Users 读取执行；计划任务启用 `--installed`，安装模式固定同目录 probe，拒绝路径/日志/状态/限时参数覆盖，并在启动前用 BCrypt 校验受保护 `transport_probe.sha256`。卸载脚本同时兼容旧 LocalAppData 安装目录。
- **P1 WaveRT 本地访问与单消费者所有权**：INF 不再向 Everyone/Restricted Code 授权，仅保留 SYSTEM、Administrators 与 LocalService 所需访问；新增以 requestor PID、随机 64-bit session、30 秒租约组成的 owner contract。只有 owner 且链路为 Connected 时可以进行一个 destructive PCM read；非 owner LinkState SET、重复 reader 和错误 session 均被拒绝。
- **P1 BthPort profile interface 泄漏**：legacy transport 记录 interface reference 状态，在 WDF device cleanup 中精确调用一次 `InterfaceDereference`，包含查询成功后后续初始化失败的清理路径。
- **P1 transport shutdown rundown**：新增 PASSIVE_LEVEL operation lock 与 `ShuttingDown` 门控。shutdown 会取消并等待 signaling open/read/write、media open/write 全部 completion，再关闭 channel。默认 queue 保持 parallel，因为 AVDTP 必须先挂起 read、再并行提交 write；每个方向已有独立单 pending 约束。真机首次验证曾错误改成 sequential，导致 write 被 pending read 阻塞并收到对端主动 `DISCOVER`，现已纠正。
- **P1 4 KiB 内核栈数组**：PCM scratch 改为 miniport 初始化时预分配的 4 KiB `NonPagedPoolNx` 缓冲区，并由单 reader owner contract 串行使用。
- **P2 文档陈旧**：更新架构、transport ABI、工具说明和状态文档，明确 legacy 真机路径、direct-PDO 离线路径和尚未验证项。

### 有意保留、需要动态验证

- **direct-PDO 取消后的无限 completion 等待**暂不修改。当前等待保证 IRP/栈 context 不会在 lower driver 仍可能访问时被释放；简单加入超时并释放对象会制造更严重的 use-after-free。后续应在隔离环境用 Driver Verifier、故障注入、PnP stop/remove 和拔设备压力测试验证 BthPort 的取消完成合同，并增加等待耗时遥测后再决定是否需要分层 watchdog。

### 本轮验证边界

- CMake/MSVC Release 构建和完整 CTest：38/38 通过；
- `NativeLdacAudio`：WDK Release `/W4 /WX`、ApiValidator、Inf2Cat/signability、测试签名，0 警告、0 错误；
- legacy `LdacNative`：WDK Release `/W4 /WX`、Driver Minimum Rules Code Analysis、ApiValidator、Inf2Cat/signability、测试签名，0 警告、0 错误；
- 两个 agent 安装/卸载脚本通过 PowerShell parser；
- 没有安装或运行本轮生成的驱动，没有注册计划任务，也没有修改当前系统设置。ACL、owner/rundown 与自动启动加固仍需下一轮真机回归和 Driver Verifier。

### 真机回归追加发现

首次 queue 回归已经证明 AVDTP signaling 必须保留 parallel dispatch；修正后 capability discovery 和 60 秒 HQ 播放恢复正常。随后重复关闭/开启 XM5 的重连压力测试发现另一项独立风险：media write 失败后，旧 agent 的 2 秒重启和 probe 内部最多 20 次 OPEN 叠加，导致连接尝试覆盖耳机关机窗口。日志与耳机短暂无法重新开机、Windows 持续显示连接的现象一致。

当前登录任务继续冻结，禁止重复真机开关测试。用户态修复把 agent 子进程固定为一次 OPEN，并在 probe exit 5（媒体/协议会话失败）后至少静默 30 秒；手工诊断模式仍可显式选择 1–20 次。Release 构建、参数边界和 agent integration tests 已纳入完整 CTest 40/40。该修复尚未要求用户重新做硬件压力验证。
