# V1 音量同步常驻集成设计

更新日期：2026-08-08
依据：`docs/VOLUME_SYNC_INTEGRATION_PLAN.md`（Phase A 产出物）
状态：Phase B 常驻产品化暂停；先执行 Microsoft/Native 首次连接 bootstrap 对照诊断。

2026-08-10 更新：upper filter 观察面已真机验证（policy 6 门禁通过），成为常驻
观察的可选来源之一；写路径评审见 `docs/VOLUME_SYNC_WRITE_PATH_REVIEW.md`。
同日更新：G-B0 已完成（2026-08-09 Microsoft 对照 + 静态导入审计，见
DEVELOPMENT_HISTORY section 178）；常驻方案改为“Microsoft 常驻 + 媒体期有界
切换 observer 写”，直接 Native function-driver 常驻路线冻结。常驻服务化
设计（组件、切换编排状态机、IPC、B2 slice、真机验证计划）见
`docs/VOLUME_SYNC_RESPIDENT_DESIGN.md`。

## 1. 目标

把已验证的 AVRCP 双向音量同步从试验门禁变为常驻产品能力，并与现有 V1 daily host 共存；同时落地单增益音频模型（实际响度只由 XM5 绝对音量决定），消除 PC 端点与耳机增益相乘的双衰减。

## 2. 组件与边界

### 现有组件（冻结/复用）

- `NativeLdacAudio`（ROOT\MEDIA\0001）：WaveRT 虚拟端点；拓扑音量节点存在但不对 PCM tap 生效；`native_pcm_source` 在用户态对 PCM 应用端点音量（当前双衰减来源）。
- `LdacNative`（A2DP transport）：每 ACL generation 单 OPEN，RenderDemand 后才建立媒体会话。
- `v1_presence_agent.exe --daily`：常驻进程（登录计划任务，policy 31），观察 ACL/Render/Engine 生命周期，按需启动 `v1_transport_daily_worker.exe`；质量 IPC 仅接受 HQ/SQ/MQ。
- `NativeLdacAvrcpObserver`（ABI 0.11）：只匹配 XM5 0x110E PDO；PnP 启动只发布接口和可激活状态，`BEGIN_OBSERVATION` 在媒体就绪后才获取当前物理 ACL 的 BTH profile/channel 并触发本 observation 唯一的 AVCTP OPEN；唯一写 IOCTL仍是 `SEND_COMMAND`（SetAbsoluteVolume、播放状态回写）。
- `v1_avrcp_action_mapper/executor`：纯逻辑映射与执行（授权状态机 + 音量/媒体键/AVRCP 写）。

### 2A. upper filter 观察面（2026-08-10 新证据）

`NativeLdacAvrcpIoFilter`（设备级 Extension upper filter）在保留
`microsoft_bluetooth_avrcptransport.inf` 为 function driver 的前提下，对精确
XM5 `0x110E` PDO 的 device-control IRP 做只读捕获，`v1_avrcp_filter_decoder`
把两种真实布局（Direct / Microsoft 8 字节私有头）解码为共享
`avrcp_observer_event` 词汇；`v1_avrcp_action_executor` 的
`V1AvrcpRunFilterReplay` 已把 decoded trace 回放进 volume-sync reducer
（headset 侧事件），`v1_avrcp_action_executor.exe --replay-filter` 提供带
`--apply` 双门控的离线回放。该面不依赖 Native 常驻绑定，绕开常驻 observer
AVCTP `0xC00000D0` 阻塞；PC→XM5 写方向是否由 Microsoft 自行完成待决策实验
（见写路径评审）。

### 新增/改造

- `v1_presence_agent --daily` 内新增 **AVRCP volume-sync 模块**（不新增常驻进程）：
  - 打开 observer 设备接口（0x110E PDO 出现即枚举）；
  - 事件消费：复用 mapper/executor 逻辑（在进程内实现实时事件源，不再依赖门禁工具链）；
  - 授权租约：媒体会话持有 volume-sync 租约，会话结束后吊销（沿用 gate 语义）；
  - 配置驱动：音量同步开关、端点音量开关、回声容差。
- `v1_transport_daily_worker` / PCM 源：在音量同步启用时 `apply_endpoint_volume=false`（单增益）。
- 安装/回滚：当前只保留已验证的 resident install/rollback 工具；常驻默认绑定等待 bootstrap 诊断结论。

## 3. 生命周期（原常驻方案，等待 bootstrap 诊断）

2026-08-10 更新：G-B0 结论已定（section 178）：XM5 空闲首次连接只建立 SDP 与
AVDTP，20 秒内无任何方向 AVCTP `0x0017`；Microsoft transport 依赖私有
`btampm.sys` MPM 协调（WDK 无公开接口），直接 Native function-driver 常驻
冻结。常驻生命周期改为：

```
XM5 ACL connect
  -> 0x110E PDO 由 Microsoft AVRCP 常驻提供（保留 MPM bootstrap）
  -> Render RUN -> media session 建立（单 OPEN）-> MediaStarted
     （此时 Windows endpoint 增益保持 fail-safe 有效）
  -> 精确 PDO owner 切换 -> 重绑 endpoint presence lease
  -> BEGIN_OBSERVATION -> 单次 outbound AVCTP OPEN
  -> CHANNEL_OPEN + VOLUME_SUPPORTED + OBSERVING + 首次 XM5 音量
  -> 原子切入 XM5 单一硬件增益，volume-sync 租约激活
       XM5 滑动 -> VolumeChanged -> PC 显示更新
       PC 音量变化 -> 轮询 -> SEND_COMMAND SetAbsoluteVolume -> XM5
       媒体键 -> 注入 + PlaybackStatus 回写
 -> Render STOP / 物理断开 -> 租约吊销，先恢复 Microsoft AVRCP 再释放 signaling
```

观察与写分离：Microsoft 持有期用 filter 观察（已产品化）；写方向只在媒体期
短窗口内由 observer 承担；切换失败或写失败按 gate 语义回滚 Microsoft 基线。

当前 Phase B 增量已把上述 observer 接口枚举、ABI 0.11 校验、post-media
激活、队列消费和媒体作用域租约放进 `v1_presence_agent --daily`。该增量
故意保持 mapper 为 ObserveOnly：Core Audio 写、媒体键注入和 `SEND_COMMAND`
写仍留在下一段 B2 接线，避免在 G-B 尚未确认常驻时序前扩大运行时副作用。

三次常驻真机结果已经确认，上述生命周期目前停在 AVCTP OPEN：post-media
activation、current-ACL profile 获取和媒体 START 后精确 PDO restart 均返回
`0xC00000D0`。临时 gate 在 Microsoft 完成物理连接后再绑定 Native 时可以建立
同一 AVCTP 通道。因此当前先捕获两种 owner 下从物理开机开始的 HCI/L2CAP
序列，确认 SDP、PSM `0x0017` 建链方向和连接响应，再修改常驻驱动。

关键前提（真机证据）：**XM5 只在 A2DP 媒体会话存在时发送音量事件并保持 AVCTP 通道**；无媒体时约 15 秒断开（**“15 秒”出处待核实，阶段 1 空闲建链可行性实验将实测通道保持时长**）。因此“播放期间同步”是默认策略；若要求“连接即同步”，需要常驻静音会话（见决策 2，且“连接即同步”方向已由 `docs/VOLUME_SYNC_CONNECTIVE_PLAN.md` 重新规划）。

## 4. 单增益音频模型

- 启用条件：音量同步服务激活（配置 `volume_sync.enabled=true`）。
- 实现：worker 默认保留端点增益；只有 observer 三项 ready 且首次 XM5 音量已采纳时，父进程才通过 manual-reset readiness event 调用 `native_pcm_source_set_apply_endpoint_volume(false)`。readiness 丢失立即恢复端点增益。
- 系统滑块仍作为输入+显示：PC 调音量 → Core Audio 回调直接唤醒 → `SetAbsoluteVolume` 写耳机；耳机滑动 → `VolumeChanged` → 写滑块（仅显示）。稳定媒体期实际响度 = XM5 绝对音量，无叠增益。
- 静音：映射到 XM5 绝对音量 0。
- 边界：若音量同步未启用，保持现有行为（agent 应用端点音量），不改变现状。

## 5. 失败与恢复

- observer 驱动问题码/接口缺失：服务健康检查报告并降级（不崩溃）；修复脚本可恢复 Microsoft AVRCP（沿用门禁 Restore-Baseline 逻辑）。
- 媒体会话 fault：沿用 daily host 既有恢复（fresh ACL generation、有界、零跨代）。
- `BEGIN_OBSERVATION` 只允许在每个 observation generation 请求一次；profile/channel 不跨 observation 或物理 ACL 保留，句柄清理或远端断开后才重置为下一代；过早、重复或失败不自动重试。
- 写事务 busy：暂存最新状态/音量；匹配 `WRITE_RESPONSE` 到达后立即发送最新值，
  不使用定时盲重试。控制通道 10 秒未 ready 则保留端点增益、恢复 Microsoft 并
  将该 generation 标记为 fail-closed。
- 崩溃/重启：登录任务重启服务；状态文件只保留可恢复信息；绝不自动删除驱动包。
- 贯穿不变量：BTHport BRB 在 profile 存活期释放；每代单 OPEN；写 IOCTL 单一且双重门控；headset-preferred 初始采纳。

## 6. 配置（并入 daily config，schema v1 扩展）

```json
{
  "volume_sync": {
    "enabled": true,
    "headset_preferred": true,
    "media_session_requirement": "playback",
    "echo_tolerance_units": 1,
    "apply_endpoint_volume": false
  },
  "media_keys": { "routed": true }
}
```

## 7. 需要评审的决策点（默认建议）

1. **绑定策略**：采用 Microsoft-preserving resident upper-filter；旧 Native handoff 路线已退役，不再作为运行时兜底。
2. **媒体会话前提**：默认“播放期间同步”（推荐，保持 RenderDemand-only 生命周期）；备选“连接时常驻静音会话”（功耗/通话影响需评估）。
3. **服务归属**：并入 `v1_presence_agent --daily`（推荐，避免双进程仲裁）；备选独立 `v1_avrcp_host`。
4. **端点音量**：agent 不应用端点音量（推荐），滑块作输入+显示。
5. **回声容差**：默认 1 档（现状），配置可调为 2 档。
6. **自有 UI**：Phase D 在现有 UI 增加音量显示（托盘/曲线），Phase C 前不阻塞。

## 8. 验证 gate

- G-A（离线）：CTest（mapper/executor/observer/protocol/queue/policy）全通过；WDK 0 警告/0 错误；daily manifest 校验扩展。
- G-B0（首次连接诊断）：XM5 从关机基线连接，分别捕获 Microsoft/Native owner 下目标 ACL handle 的 SDP、AVCTP 与 AVDTP L2CAP 序列；采集前后 owner 不变，analytic channel 状态恢复。
- G-B（常驻真机）：连接 → 播放 → 双向音量同步；停止播放 → 同步停止且无残留；断开 → 干净回收。
- G-C（单增益听感）：PC 调音量 → XM5 实际响度变且无叠增益；XM5 调 → PC 显示跟随；回读标量对照。
- G-D（恢复）：重启/崩溃后服务自愈；observer 问题码可回退 Microsoft AVRCP；无残留。

## 9. Phase B/C 实施顺序

1. G-B0：恢复健康 Microsoft AVRCP，执行首次连接 capture；根据结果决定是否需要 Native 对照和最小 SDP/profile 初始化实验；
2. Phase B：bootstrap 门禁通过后恢复 observer 常驻绑定工作，并执行 G-B；
3. Phase C：`apply_endpoint_volume=false` 接线（会话激活时）+ G-C 听感裁决；
4. 视听感结果决策回声容差；
5. Phase D：UI、回滚演练、文档同步、Gate 3 收尾。
