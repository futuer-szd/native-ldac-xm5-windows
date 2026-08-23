# Policy v10 fidelity bridge

Policy v10 的唯一前置证据是已收口的 policy v9 transaction：

`artifacts\v1-linked-limiter\trial\transaction-20260726-154646-723.json`

前置条件只证明 policy v9 的 transport 与 linked-limiter telemetry 通过。用户没有仔细听，因此 quality 状态是 `not-assessed-by-user`；policy v10 不得把它改写为音质成功或失败。

## 峰值术语

Policy v10 的目标上限 `0.89125094` 表示数字 PCM sample peak 的 −1 dBFS。本文和所有 manifest/result 字段只使用：

- `peak_measurement = digital-sample-peak`
- `peak_unit = dBFS`
- `sample_peak_dbfs = -1.0`

它不是 oversampled true-peak 测量，不能写作 true-peak，也不能使用 dBTP。当前 gate 不提供 inter-sample peak 保证。

## 有界输出桥

- Windows post-volume gain：1.0。
- 媒体时限：10,000 ms。
- sent-frame fade-in：100 ms。
- ceiling：从 0.25 开始，只随成功发送并提交的 frame，在 2,000 ms 内升到 0.89125094。
- linked stereo sample-peak limiter 的 fallback clamp 必须为 0。
- 同一 ACL generation 最多四次 OPEN；只有 `OpenSignaling / Win32 71 / zero exchange` 可重试。
- 10 秒结束后必须 AVDTP SUSPEND/CLOSE，并释放 ConsumerLease。

## 动态锁与证据

PCM prepare 后锁定 Windows 音量、mute、sample rate、bit depth 和 WaveRT stream epoch。媒体期间每个 PCM block 查询当前值；任何查询失败或任何字段变化都立即终止。成功证据要求：

- session generation 等于 presence agent 的 ACL generation，所有 attempt 也相同。
- `volume_query_count > 0`、`volume_stable=true`、`volume_change_count=0`。
- volume scalar/dB 的 minimum、maximum、last 相等。
- fade algorithm/version、duration frames、minimum/last gain 完整。
- `fade_committed_sent_frames = pcm_frames_sent`。
- prepared/committed fade block 数配平，commit failure 与 sanitized sample 均为 0。
- ceiling ramp 最终精确达到 0.89125094。
- 原有 PCM、transport retry、packet pacing、ConsumerLease、SUSPEND/CLOSE 边界继续成立。

## 当前边界

本阶段只定义 candidate、gate 与 evidence 合同。完成 C++/CMake 集成并从 clean commit 构建冻结 candidate 后才可运行真机 gate。现在不运行 gate，不安装或回滚驱动，不重启，不切换蓝牙，不修改 endpoint、LinkState 或默认输出。
