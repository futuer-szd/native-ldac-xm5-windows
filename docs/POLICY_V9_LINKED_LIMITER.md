# Policy v9 linked-limiter comparison

Policy v8 已完成 60 秒 transport，并由用户报告为 `generally-clear` 与 `muffled-bass`。Policy v9 不复跑 v8；它把下列已收口证据固定为唯一前置条件：

- transaction：`artifacts\v1-stability-burst\trial\transaction-20260726-144127-295.json`
- `transport_passed=true`
- 60,000 ms、unity gain、0.25 输出上限
- ConsumerLease 获取/释放配平，START/SUSPEND/CLOSE 成功
- 用户枚举报告 `generally-clear + muffled-bass`

## 唯一变量

Policy v9 只把 policy v8 的独立逐 sample hard clamp 替换为 `linked-stereo-block` limiter v1。左右声道共享同一个 frame gain，block 间保留 release 状态；0.25（−12 dBFS）仍是不可突破的最终输出上限。Windows post-volume PCM 的额外 gain 仍为 1.0。

Transport 合同不变：真实 ACL generation 与 RenderDemand 授权、120 秒 PCM wait、60 秒媒体时限、SUSPEND/CLOSE、最多四次 OPEN。只有 `OpenSignaling / Win32 71 / zero exchange` 可按既有 15/30/45 秒策略恢复；任何协议、configuration、media 或已发生 signaling exchange 的失败都立即停止。

## 强制证据

最终 session 除 policy v8 的 limiter、epoch 和 ConsumerLease 证据外，必须显式提供：

- `limiter_algorithm = linked-stereo-block`
- `limiter_algorithm_version = 1`
- `limiter_release_ms = 50.0`
- `limiter_minimum_gain`，范围严格位于 0 与 1 之间
- `limiter_gain_reduced_frames > 0`
- `limiter_gain_reduced_samples`，必须与 stereo frame 数量一致
- `limiter_fallback_clamp_count = 0`

fallback clamp 非零意味着 linked envelope 没有独立守住 ceiling，本轮音质比较直接判失败，不能把 hard-clamped 输出当作 linked-limiter 成功。

## 当前开发边界

PowerShell candidate、gate、evidence 与 policy tests 已独立定义。C++ worker/telemetry 接线及 CMake target 完成并提交后，才可从 clean commit 构建冻结 candidate。当前阶段不运行真机 gate，不安装或回滚驱动，不重启，不切换蓝牙，不修改 endpoint、LinkState 或默认输出。
