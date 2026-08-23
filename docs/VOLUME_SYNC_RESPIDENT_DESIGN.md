# V1 音量同步常驻服务化设计

更新日期：2026-08-10
依据：`docs/VOLUME_SYNC_INTEGRATION.md`（组件与生命周期）、
`docs/VOLUME_SYNC_WRITE_PATH_REVIEW.md`（写路径判定与验证分层）、
DEVELOPMENT_HISTORY section 178/193/194（G-B0 结论与常驻方案定稿）
状态：设计定稿，等待实现（B2 slice 代码 + 提升编排器）

## 1. 目标与范围

把已验证的“Microsoft 常驻 + 媒体期有界切换 observer 写”从门禁工具链变为
日常自动路径：播放时自动完成切换、双向音量同步与媒体键路由，停止时自动恢复
Microsoft AVRCP；全程无需用户操作，失败自动回滚到 Microsoft 基线。

本设计只覆盖 AVRCP 控制面（音量/媒体键）。音频面（LDAC 播放）沿用 V1 daily
host 既有生命周期，与切换编排通过媒体会话状态对齐。

## 2. 组件边界

两个进程、一个受管包：

1. **`v1_presence_agent.exe --daily`（普通用户登录任务，已有）**：
   - 生命周期权威：ACL/Render/Engine/MediaSession；
   - PC 已报告播放意图且 Render/engine 已就绪、A2DP MediaStarted 尚未发生时触发切换请求；MediaStarted 后激活 observer，媒体停止时触发恢复请求；
   - 切换完成后激活 `V1AvrcpObserverHost` 写模式（B2 slice 改造现有
     ObserveOnly 集成）；
   - 不持有管理员能力，不做 PnP/Driver Store 操作。
2. **`v1_avrcp_handoff_host.exe`（新增，提升计划任务，SYSTEM 或管理员）**：
   - 唯一执行驱动切换的进程：stage observer 候选、精确 `0x110E` PDO
     restart、验证绑定、恢复 Microsoft；
   - 常驻等待命名事件，无媒体时不活动（无轮询、无 PnP 操作）；
   - 每次会话最多 1 次进入 restart + 1 次恢复 restart（沿用 gate 上限语义）。
3. **`NativeLdacAvrcpObserver` 候选包（受管，已有）**：媒体期临时 function
   driver；包由 handoff host stage/卸载，Driver Store 平时无残留。

upper filter（`NativeLdacAvrcpIoFilter`）不在常驻主路径：Microsoft 持有期与
observer 持有期的写/读验证均可用它，但观察与写以 observer 事件流为主
（进程内、低延迟）；filter 保留为诊断与 IOCTL 层验证面。

## 3. 切换编排状态机（handoff host）

```
MicrosoftHeld（常驻默认）
  │  PC playback intent + Render/engine ready, before MediaStarted
  ▼
HandoffPending
  │  stage 候选（pnputil add，不带 /install）→ 精确 PDO restart（1/1 上限）
  ▼
ObserverReady
  │  ABI 版本校验、status OK、Problem Code 0
  │  → 通知 daily host（命名事件）→ 等待 A2DP MediaStarted
  ▼
ObserverActive
  │  MediaStarted 后 daily host BeginMediaSession
  │    （BEGIN_OBSERVATION 单次 outbound OPEN）
  │  OPEN accepted → 写窗口打开（mapper 授权 gate）
  ▼
RestorePending
  │  MediaStopped（daily host）→ 先 EndMediaSession（通道已释放）
  │  → 精确 PDO restart（恢复 Microsoft INF/service）→ 验证
  ▼
MicrosoftHeld
```

失败路径（任何状态，均以恢复 Microsoft 为终态）：
- stage/restart 失败或超时（默认 10 s）→ 不再追加 restart，直接进入
  RestorePending 的回滚分支；
- OPEN `0xC00000D0` 等协议失败 → daily host 结束租约，handoff host 恢复
  Microsoft；同一媒体会话不再重试切换；
- 恢复 restart 失败 → 保持 observer 绑定但标记 degraded，日志记录，
  下一会话前只读验证（沿用“失败不循环试验”纪律，需人工 rollback 工具）。

## 4. 进程间同步（IPC）

沿用项目既有模式（命名 Event + 原子 JSON，无新框架）：

- `\Events\NativeLdac\AvrcpHandoffRequest`（daily host → handoff host）
- `\Events\NativeLdac\AvrcpHandoffDone`（handoff host → daily host，含
  result 字段；done 后 daily host 读取状态文件确认）
- `\Events\NativeLdac\AvrcpRestoreRequest` / `\Events\NativeLdac\AvrcpRestoreDone`
- 状态文件：`%LOCALAPPDATA%\NativeLdac\avrcp-handoff-state.json`
  （原子写，schema v1：state、generation、restart_count、error、timestamps）
- 身份约束：handoff host 只操作状态文件中记录的精确 `0x110E` instance id
  与受管包哈希（沿用 filter/observer gate 的校验函数）。

## 5. daily host 写模式（B2 slice）

现有 `ReconcileAvrcpObserver` 的 ObserveOnly 改造为：

1. `V1AvrcpObserverIo` 增加写打开模式（GENERIC_READ|GENERIC_WRITE）与
   `SendCommand`（`IOCTL_NLD_AVRCP_OBSERVER_SEND_COMMAND`，METHOD_BUFFERED）；
2. Windows 动作 sink 从 `tools/v1_avrcp_action_executor.cpp` 提升为库
   （`agent/v1_avrcp_windows_sink.*`）：SEND_COMMAND 写、虚拟键注入、
   `IAudioEndpointVolume` 读写回——工具与 daily host 共用，消除双实现；
3. `V1AvrcpObserverHost` 在租约有效时把授权动作发给 sink；写授权沿用
   mapper/gate 语义（current ACL、独占 lease、Supported、Synchronize、
   Windows snapshot 同时成立）；
4. 配置（并入 daily config schema v1 扩展）：
   `volume_sync.enabled`（默认 false，避免未验证路径自动生效）、
   `volume_sync.handoff.enabled`（默认 false）、`apply_endpoint_volume=false`
   （单增益，仅 volume_sync 激活时生效）。

## 6. 离线验证（B2 slice）

- Windows 写 sink 库化后：CTest 覆盖（sink 契约 + observer host 写模式 +
  handoff 状态机单测）；
- handoff host 的编排逻辑拆出纯状态机（可单测）+ 薄 PnP 壳；
- PowerShell 静态策略测试：handoff host 不得包含
  `Disable-PnpDevice`/`Set-Service`/radio toggle/默认端点写入；restart 上限
  1；候选哈希校验；恢复顺序固定（先租约后 restart）。

## 7. 真机验证计划（Phase B 组合实验，需用户参与）

1. 先用现有工具链手工组合（不等 handoff host 完成）：
   filter gate 安装 + observer gate 切换 + `--apply` 写，验证三层证据
   （filter IOCTL raw prefix 见 `SEND_COMMAND` pdu=0x50/params；observer
   INTERIM 回显；音量行为）；
2. handoff host + daily host 冒烟：播放自动切换/停止自动恢复各 3 轮，
   Microsoft 基线前后不变；
3. 断连/失败注入：媒体期 XM5 关机、restart 超时，验证恢复与回滚。

## 8. 交付顺序

1. B2 slice：Windows 写 sink 库化 + observer host 写模式 + CTest；
2. handoff host：状态机 + PnP 壳 + 计划任务包装 + 静态策略；
3. daily host 配置接线（默认关闭）；
4. 组合真机验证（上节 1–3）；
5. 默认开启评审 → 单增益落地（Phase C）→ UI（Phase D）。
