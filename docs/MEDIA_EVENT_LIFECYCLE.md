# V1 媒体期音量同步：全生命周期事件目录与动作合同

更新日期：2026-08-20
状态：方向定案（方案 1：媒体期同步 + 每次 ACL 连接首次 XM5 音量权威；
不做无媒体持续同步）。本文档是逐项落实"每个事件的动作"的清单与状态登记。

## 1. 原则

- 每个事件有**确定性动作**；无动作的事件明确写"不处理"（避免隐式行为）。
- 所有 AVRCP 动作必须通过 **authorized_current + ACL generation + owner
  lease** 三重校验（mapper 已内建）。
- 失败一律 **fail-closed** 并收敛到健康 Microsoft AVRCP 基线或明确
  disconnected；不自动重试轰炸。
- 每次物理 ACL 连接只采纳**一次** XM5 音量权威（headset-preferred），
  之后双向同步。
- 无媒体时不做控制通道保活；resident filter hook 随每次 ACL 连接立即开始观察，
  已存在的 Microsoft AVCTP 通道关闭后不主动重建。

## 2. 状态标记

- ✅ 已实现且真机门禁通过
- 🟡 已实现（含离线测试），真机待验证/待专项验收
- ❌ 缺口（未实现或行为未定义）
- 🎯 本阶段（方案 1 收尾）重点

## 3. 事件目录

### A. 物理连接

| 事件 | 来源 | 动作合同 | 状态 |
|---|---|---|---|
| ACL connected（XM5 开机/蓝牙开/重连） | 系统 ACL watcher → `AclConnected` | 新 ACL generation；发布 endpoint present；清空旧 filter 队列和旧 pending PLAY；立即启动 resident hook 并读取 GSMTC 当前状态 | ✅ 3 generation 综合真机通过（section 269） |
| 连接时已有 Playing 会话 | GSMTC snapshot | 只观察；Microsoft 独占媒体键执行。若 Native Render 已运行，现有音频生命周期启动 worker | ✅ generation 2 真机重连通过（section 269） |
| 连接时已有 Paused + play-enabled 会话 | GSMTC snapshot + filter pass-through | PLAY/PAUSE 手势先等待 Microsoft 200ms；已变 Playing 则抑制兜底，仍 Paused 才补一次 PLAY | 🟡 离线实现，首次双击真机待验 🎯 |
| 连接时 Stopped/Absent | GSMTC snapshot | 保持只读 hook，不注入；后续出现 Paused 会话时自动进入 PLAY 仲裁 | 🟡 离线实现 |
| 首次 XM5 absolute-volume（连接后首条） | observer/filter 观察 | 以 XM5 为权威更新 PC 一次（`headset_preferred` + `xm5_volume_seen`）；同一 generation 不再重复抢占 | ✅ 2026-08-16 方向性验收通过（PC 53.3% → 采纳 60%） |
| ACL disconnected（XM5 关机/蓝牙关） | watcher → `AclDisconnected` | 停止 worker；撤销 lease；endpoint absent；恢复 Microsoft owner（若在 observer 期）；旧 generation 全部失效 | ✅ 断开路径真机通过（policy 20/21/144/148） |
| 无媒体保持连接 | 系统 | 不保活；hook 只消费现有 Microsoft 通道事件。通道存在时可同步音量/仲裁 PLAY，关闭后不主动重建 | 🟡 新连接合同真机待验 |

### B. 媒体启动

| 事件 | 来源 | 动作合同 | 状态 |
|---|---|---|---|
| Render started（应用向 Native 端点渲染） | render tracker → `RenderStarted` → `EngineReady` → `MediaStarted` | resident hook 已在 ACL 连接时存在；PCM 以端点增益 fail-safe 启动，首次 XM5 音量采纳后原子切入单增益 | ✅ 3 generation 组合真机通过（section 269） |
| 媒体启动时 GSMTC 未就绪 | monitor | 用当前 LDAC session 的保守 Playing 快照；monitor 首次发布后覆盖 | ✅ 已实现（section 234） |
| Render started 早于 engine 就绪（pre-transport gap） | lifecycle | `tolerate_pretransport_render_gaps`：有界容忍，engine 就绪后补建 | ✅ 已实现 + 离线测试 |

### C. 播放中（同步核心）

| 事件 | 来源 | 动作合同 | 状态 |
|---|---|---|---|
| XM5 滑动音量（absolute-volume CHANGED） | observer → mapper | 更新 PC 显示（单增益，只显示）；回环抑制；不写回耳机 | ✅ 真机通过（audible sync check 12 跟随） |
| PC 调音量（滑块/键盘） | 精确 Native LDAC Core Audio callback + 500ms 身份复核 | `SendXm5Volume` 写耳机；回环容差；节点重建后按 endpoint ID 重绑；绑定丢失先恢复端点增益 | ✅ 49 条写入及重连后双向真机通过（section 269） |
| XM5 播放/暂停/上下曲手势（pass-through） | Microsoft AVRCP + resident filter 观察 | 正常媒体期由 Microsoft 唯一执行，filter 仅记录，不再 SendInput；只有 Paused PLAY 的 200ms 未处理兜底可以注入一次 | ✅ 14 条 Microsoft 单一执行，零 fallback（section 269） |
| 切歌/短暂停顿导致 PCM 暂时无数据 | PCM source → worker | Render/snapshot 仍 active 时容忍最多 2 秒；数据恢复后重置 pacing 和 encoder，发送启动边界并淡入。inactive 则进入 AVDTP SUSPEND | 🟡 离线实现，真机待验 🎯 |
| 快速连续调音量 / 0% / 100% / 静音 | mapper gate | absolute-volume gate 限速；0/100/静音语义正确 | 🟡 离线实现，真机验收待做（计划 3.3.2）🎯 |
| 写事务忙 | observer host | 只保留最新状态/音量；收到匹配 `WRITE_RESPONSE` 后立即发送，不使用 1 秒盲重试 | ✅ 离线 + 独立真机串行化通过 |
| 应用把输出切到别的设备 | render tracker → `RenderStopped` | 停止同步；回到无媒体状态 | ✅ 设计；真机 gate 有覆盖 🟡 |
| **系统把默认播放设备切到 XM5 Hands-Free**（实测：连接后约 17±3 秒自动发生） | Windows 行为（纯系统，非实验流程触发） | 测试：settle 等待 + 守卫；产品：检测到后按 RenderStopped 处理，是否主动恢复默认设备待决策 | 🎯 已确认纯 Windows 行为 |
| **HFP 链路被自动激活**（与上述切换同时；耳机出现底噪） | Windows 激活 Hands-Free 端点/SCO | **产品级风险**：HFP 激活可能压制/降级 A2DP-LDAC 路径，且不需要应用打开麦克风——与"HFP 阶段由麦克风激活才让出 LDAC"的设计冲突，需检测"虚假 HFP 激活" | ❌ 新发现，待产品决策 |

### D. 暂停 / 继续

| 事件 | 来源 | 动作合同 | 状态 |
|---|---|---|---|
| 播放器暂停（GSMTC Paused；render 停止） | monitor + worker PCM 状态 | 在原 AVDTP 会话发送 `SUSPEND`，立即停止全部媒体包并释放 PCM consumer；保留 signaling/media L2CAP，不发送 LDAC 静音，也不执行 `CLOSE` | ✅ 最新综合真机 6 次通过（section 269） |
| 暂停中 XM5 控制 | Microsoft AVRCP/filter | AVCTP 仍存在时可以同步；不通过静音媒体包保活。通道存在时 XM5 PLAY 由 Microsoft 处理 | ✅ 本轮约 112/132 秒暂停后 XM5 双击恢复通过（section 269） |
| PC 继续播放（GSMTC Playing；render 恢复） | worker 等待 Render PCM | 重新取得 PCM consumer，在同一已打开的 AVDTP 会话发送 `START`，短启动静音与 500ms 淡入后恢复真实 PCM；不重新打开 signaling/media L2CAP，规避同 ACL `OPEN` Win32 121 | ✅ 真机连续 5 次通过（section 263） |

### E. 停止 / 退出

| 事件 | 来源 | 动作合同 | 状态 |
|---|---|---|---|
| 播放停止/退出（`RenderStopped`） | render tracker | 播放中执行 `SUSPEND -> CLOSE`；若已经因暂停处于 SUSPEND，则只发送 `CLOSE`；释放 PCM 与控制 lease | ✅ 原正常停止通过；暂停态 CLOSE 组合待验 🟡 🎯 |
| GSMTC Stopped/Absent | monitor | lease 结束；媒体键 fail-closed；不注入 | ✅ 离线实现 |
| 媒体会话期间故障（`MediaFailed`） | lifecycle | 有界重试（3-4 次/代）；超限恢复基线 | ✅ 实现 + 部分真机 |

### F. 断开与异常

| 事件 | 来源 | 动作合同 | 状态 |
|---|---|---|---|
| 播放中物理断连（XM5 关机/蓝牙关） | StreamStopped/ACL 竞态 | 有界收敛；新 generation 重建；旧代写权全部撤销 | ✅ 真机通过（section 137/138/144） |
| 睡眠/唤醒 | ACL 断开+重连 | 按重连处理，generation 重建 | ✅ 部分真机（section 28）；完整验收（计划阶段七）🟡 |
| 宿主崩溃/重启 | 登录任务 | 自动重启；状态文件只存可恢复信息；不自动删驱动 | ✅ 设计；验收 🟡 |
| 蓝牙 radio 关闭/开启 | ACL 事件 | 与断连/重连同路径 | ✅ 覆盖；50 次开关验收待做（阶段七）🟡 |
| HFP 麦克风激活（未来阶段） | HFP capture monitor | `HfpSuspendLdac` 让出；结束稳定 2s 后恢复 | 🟡 离线（section 233-238）；真机属 HFP 阶段，不在本阶段 |

## 4. 本阶段重点缺口（按优先级）

1. 🎯 **暂停期行为真机证据**（D 组）：决定"暂停期间双向同步"是否成立。
   需实测：暂停时 AVDTP 是否 CLOSE、AVCTP 通道保持多久、暂停中滑耳机能否
   同步、继续后是否重复权威抢占。若暂停=无媒体（通道 15s 断），则
   "暂停期同步"需改为"暂停后首次同步"或保持静音流（后者违背方案 1）。
2. 🎯 **媒体启动/停止的完整 daily 组合真机**（B/E 组）：handoff 触发、
   lease 激活、停止后恢复 Microsoft 的完整流程一次跑通（写路径 gate 与
   audible check 是真机单点证据，缺"一次完整播放周期"的组合证据）。
3. 🟡 **媒体键与暂停/继续手势 daily host 周期**（独立 executor 已通过，完整
   C/D 组合仍待验）。
4. ✅ **首次音量权威专项验收**（A 组，2026-08-16 通过）：方向性断言
   （PC 53.3% → 首条跟随 60%）；标准运行不再要求预设方向，mapper/驱动
   改动后复验。
5. 🟡 0%/100%/静音/快速连续音量真机验收。

## 5. 下一步动作

1. ✅ 离线小工具已完成（2026-08-14）：audible sync check 增加
   `-PauseObservationSeconds` 与 `-ResumeObservationSeconds`（默认关闭），
   暂停=停媒体流后继续观察（记录 `sync_survived`），继续=重启媒体流确认
   `sync_resumed`；策略测试与 CTest 199/199 通过。
2. 🎯 真机跑一次"完整播放周期"：连接 → 播放（双向同步）→ 暂停（观察）→
   继续 → 停止 → 确认恢复 Microsoft；
3. 🎯 根据暂停期证据决定 lease 合同是否需要修订；
4. 🎯 随后按 4.2-4.5 逐项真机验收，并把每项结果登记回本文档状态列。
