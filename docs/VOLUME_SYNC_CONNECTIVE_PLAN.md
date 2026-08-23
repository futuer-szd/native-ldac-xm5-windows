# V1 连接期音量同步（手机式）重新计划

更新日期：2026-08-12
状态：方向变更草案，等待用户审阅
依据：DEVELOPMENT_HISTORY section 178/193/194/195（G-B0 结论、写路径决策、
常驻方案定稿）、VOLUME_SYNC_INTEGRATION.md §3（生命周期与关键前提）、
VOLUME_SYNC_RESPIDENT_DESIGN.md（媒体期切换设计）

## 1. 目标变更

原目标：媒体期有界切换（播放期间同步，停止恢复 Microsoft）。

新目标：**连接即同步（手机式）**——XM5 与 PC 连接期间，无音频播放时也能
双向调节同步音量（耳机滑动 → PC 显示更新；PC 调音量 → XM5 实际响度变化）。

用户提出的体验参照：手机与 XM5 连接时随时可调音量。

## 2. 现状与矛盾（必须正面回答）

### 2.1 G-B0 真机证据（section 178，2026-08-09）

- XM5 空闲（无媒体）首次连接：只建立 SDP 与 AVDTP，20 秒窗口内 AVCTP
  PSM 0x0017 双向请求为零；
- Microsoft transport 依赖私有 `btampm.sys` MPM 协调（WDK 无公开接口）。

### 2.2 Native 常驻尝试的失败（section 194）

- observer/Native function-driver 在空闲时尝试 AVCTP OPEN 返回
  `0xC00000D0`，因此"直接 Native function-driver 常驻"路线被冻结。

### 2.3 现有 observer 的设计约束

- `NativeLdacAvrcpObserver`（ABI 0.11）：`BEGIN_OBSERVATION` 需要
  media-ready 后才获取 current-ACL BTH profile/channel（延迟获取）；
- 现有 `v1_avrcp_observer_probe` 只能"when media is ready"激活；
- 即：**空闲时现有工具链无法建立 AVCTP 通道**。

### 2.4 手机为何可以

手机蓝牙栈（AVRCP controller）通常在连接早期（SDP 之后）主动建立并保持
AVCTP 通道，因此无媒体时通道现成、随时可调。PC 的 Microsoft 栈空闲不建，
XM5 作为 target 空闲不主动建——这是**栈行为差异**，不是 XM5 的物理限制。

### 2.5 结论

"连接即同步"在技术上**可能**（手机为证），但 PC 侧主动建链的正确
时机/角色/前置条件**未被验证**——`0xC00000D0` 只是唯一一次尝试的结果。
必须先做可行性实验，再定架构。

## 3. 核心未知与假设

| 未知 | 假设 | 验证手段 |
|---|---|---|
| 空闲时 XM5 是否接受 controller 主动 AVCTP 建链 | 接受（手机为证） | 可行性实验 |
| 建链时机（连接后立即 / SDP 后 / 延迟）对结果的影响 | 连接早期更可能成功 | 实验变量 |
| 空闲建链后 XM5 是否响应 SetAbsoluteVolume / 发送 VolumeChanged | 响应 | 实验 |
| 无媒体时 AVCTP 通道保持多久（原"约 15 秒断开"出处存疑） | 待实测 | 实验计时 |

## 4. 可行性实验设计（第一步，决定架构方向）

### 4.1 前置开发（离线，不碰真机）

现有 observer 无法空闲激活，需要新增一个**空闲建链探针**（或给 observer
增加空闲 activation 模式）：

- 方案 1：`v1_avrcp_observer_probe` 增加 `--idle-activation` 模式：在无媒体
  时也发起 BEGIN_OBSERVATION（驱动尝试当前 ACL 的 AVCTP OPEN），输出
  建链结果与事件；驱动侧放宽"media-ready 才 activation"的门槛（仅实验
  模式，不改变生产 ABI 语义）。
- 方案 2：独立 probe 直接经 BTH profile interface 发起 PSM 0x0017 建链
  （不经过 observer 驱动），验证 XM5 对裸 AVCTP 建链的响应。

倾向方案 1（复用已验证的 observer 驱动/接口，最小改动）。

### 4.2 真机实验步骤（需用户参与）

1. 预检：系统 Microsoft 基线、Driver Store 无残留、XM5 断开；
2. 安装 observer 候选（不切换媒体，仅 probe 接口）；
3. XM5 开机连接；
4. 依次在以下时机执行空闲建链：
   - 连接后立即（ACL connected 事件后 0s）
   - SDP 完成后（约 1s）
   - 延迟 5s
   - 延迟 15s（验证"15 秒断开"说法）
5. 每次建链成功后：滑 XM5 音量 → 观察 VolumeChanged 事件；发一条
   SetAbsoluteVolume → 观察耳机响度/回显；
6. 记录建链结果（0xC00000D0 / 成功 / 超时）、通道保持时长、事件流。

### 4.3 判定标准

- 任一时机建链成功且音量命令/事件双向可用 → **连接即同步可行**，走架构 A；
- 全部时机 0xC00000D0 → 空闲建链在 PC 上不可行，回退讨论（架构 B 或维持
  媒体期方案 + 接受局限）；
- 部分时机成功 → 以成功时机为基准设计连接期接管时序。

## 5. 架构候选（可行性实验后决策）

### A：Microsoft 常驻 + observer 连接期接管（推荐候选）

- function driver 保持 Microsoft（保留 MPM bootstrap，不碰 G-B0 红线）；
- 连接建立后切到 observer（复用已验证的 stage/restart/绑定流程与
  handoff IPC/host 代码），连接断开恢复 Microsoft；
- 空闲时 observer 主动建 AVCTP 通道并保持（保持策略待实验定）；
- 复用 B2 已完成的绝大部分代码，改动点是**触发条件从"媒体期"改为
  "连接期"**（daily host 的 Reconcile/状态机/handoff 请求时机）。

### B：Microsoft 常驻 + 按需建链写

- 不切 function driver；需要写/观察时临时经观察者或 BTH profile 建链；
- 优点：无驱动切换；缺点：按需建链延迟、通道生命周期管理复杂、与
  Microsoft 并存协调未验证。

### C：完全 Native function-driver 常驻

- 仅当可行性实验推翻 0xC00000D0 结论才考虑；否则维持冻结。

## 6. 分阶段计划

1. **阶段 1（当前）**：环境恢复 + 可行性实验（探针开发 + 真机实验 + 判定）；
2. **阶段 2**：依据判定结果选架构 A/B/C，更新设计文档
   （VOLUME_SYNC_RESPIDENT_DESIGN.md 重写触发边界）；
3. **阶段 3**：架构 A 则改造 handoff 触发（媒体期 → 连接期）+ 空闲通道
   保持策略 + 冒烟（连接即同步、断开恢复各 3 轮）；
4. **阶段 4**：默认开启评审、单增益落地、UI。

## 7. 风险与待办

- 空闲建链若不可行，需向用户呈现证据并回退决策；
- "15 秒断开"出处存疑，实验一并实测，文档标注待核实；
- 连接期接管意味着 observer 持有时间大幅增加（连接期 = 常驻窗口），
  需同步评估长期持有的驱动风险（签名/更新/加载残留），可能引入常驻
  soak 验证；
- 现有 B2 handoff 冒烟（媒体期）暂停，不继续投入，避免方向变更后返工。
