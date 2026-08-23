# Native LDAC UI

UI 使用 Python 标准库 `tkinter`，不需要额外安装第三方包。未检测到登录 agent 时，它仍可直接启动暂存的 `transport_probe.exe --play-endpoint`。检测到正在运行的已安装 agent 后，UI 自动切换为后台控制模式，不再创建第二个 probe。

当前 V1 daily host 的第一阶段接入为**只读状态 + 受限质量配置**：UI 从
`%LOCALAPPDATA%\NativeLdac\V1\state\daily-state.json` 读取当前连接、播放、
统一音量、HFP 监视和故障状态，并读取最近一次 transport result 显示格式与码率。
daily host 运行时，格式和启停控件禁用；质量只允许 HQ/SQ/MQ，并经由
`NativeLdac.V1.Config.<instance>` 命名管道在下一次安全 worker 边界应用。关闭 UI 只关闭窗口，
不会停止后台、worker、LDAC 或音量同步；格式/EQ 写配置仍不会扩展这个 JSON adapter。

安装版入口：

```powershell
& "$env:ProgramFiles\NativeLdac\V1\run-v1-daily-ui.ps1"
```

开发调试可以通过 `NATIVE_LDAC_V1_STATE_PATH` 和
`NATIVE_LDAC_V1_RESULT_PATH` 显式指定单个状态/结果文件；UI 不扫描 trial 目录。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\run-ui.ps1
```

使用时打开 XM5，在界面中选择质量、声道、采样率和位深并点击“开始 LDAC”，然后把 Windows 或播放器的输出设备选为 `Native LDAC - WH-1000XM5`。界面会显示实际 LDAC/RTP 码率、当前质量、BthPort 写入延迟、最近 60 秒曲线，以及虚拟端点的 active/idle、PCM 缓冲、系统音量、驱动丢弃和静音填充。

后台控制模式使用 `%LOCALAPPDATA%\NativeLdac\config.json` ABI v3，包含 `enabled/quality/channel_mode/sample_rate/bits_per_sample` 和递增 `revision`，并兼容读取 v1/v2。UI 以临时文件加原子替换保存；agent 每 500 ms 检查一次。任一媒体设置变化都会先让当前 probe 正常执行 `SUSPEND -> CLOSE`，再由新 generation 设置 WaveRT 首选格式、通知 Windows 重开音频流并重新协商 LDAC。UI 同时读取原子 `logs\state.json` 和增量跟随 `logs\probe.log`，显示 `waiting_device/settling_device/reconfiguring/disabled` 等后台状态与实时码率。若 legacy agent state 额外发布 transport policy、ACL/render、OPEN retry、PCM epoch/ConsumerLease 或 limiter 遥测，设备状态下方会显示一行只读摘要；旧 state 缺少这些可选字段时界面保持原样，部分写入或无效快照只会被忽略。

legacy agent state 的 `version/quality/agent_pid/config_revision` ABI 与 V1 bounded observer 的 `schema_version/mode/physical_presence` ABI 仍是两种不同合同。遥测解析器可只读提取两者共有的 V1 诊断字段并有独立测试，但 UI 只允许带进程身份的 legacy state 决定后台 agent 模式；孤立或陈旧的 V1 gate state 不会启动、停止或冒充常驻 agent，也不会驱动界面控制状态。

XM5 刚开机或 Windows 蓝牙 ACL 正在恢复时，BthPort 可能暂时拒绝新的 AVDTP signaling 连接。probe 会对连接数上限、设备忙、尚未就绪/连接和超时等瞬时状态每秒自动重试，形成约 20 秒的连接获取窗口；无需连续点击“开始 LDAC”。

“安全停止”和关闭窗口都会设置一个命名 Windows Event。probe 收到事件后执行 `SUSPEND -> CLOSE`；UI 不会用 `TerminateProcess` 强制结束正在播放的会话。

每次打开 UI 都会把完整输出保存到 `artifacts\ui-logs\ldac-ui-YYYYMMDD-HHMMSS.log`，并为每次点击开始和子进程退出加入时间、质量与 exit code 标记，便于分析窗口里已经滚走的冷启动失败。

运行配置与遥测解析测试：

```powershell
conda run --no-capture-output -n codex_py312 python -m unittest discover -s .\ui\tests -v
```
