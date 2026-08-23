# V1 音量同步常驻集成开发计划

更新日期：2026-08-08
当前分支：`codex/v1-volume-sync`
状态：首次连接 bootstrap 对照诊断进行中；常驻产品化暂停

2026-08-10 更新：G-B0 已完成并定论（DEVELOPMENT_HISTORY section 178）：
Microsoft 对照（`avrcp-bootstrap-microsoft-20260809-160534-307`）证明 XM5 空闲
首次连接只建 SDP/AVDTP、无 AVCTP `0x0017`；静态导入审计证明 Microsoft transport
依赖私有 `btampm.sys` MPM 协调。直接 Native function-driver 常驻路线冻结；常驻
方案改为“Microsoft 常驻 + 媒体期有界切换 observer 写”（observer gate 已验证的
顺序产品化），upper filter 作为 Microsoft 持有期的只读观察面。写路径验证分层见
`docs/VOLUME_SYNC_WRITE_PATH_REVIEW.md` section 7。

## 1. 背景与已验证状态

AVRCP 观察支线已完成真机验证：

- 独立 `NativeLdacAvrcpObserver`（ABI 0.11）：只匹配 XM5 0x110E PDO；`BEGIN_OBSERVATION` 在媒体会话就绪后获取当前 ACL 的 BTH profile，并触发本 observation 唯一 AVCTP OPEN；单一写 IOCTL `SEND_COMMAND` 继续只允许 SetAbsoluteVolume 与播放状态回写；
- XM5→PC：侧滑音量 → PC 滑块跟随（精确标量回读逐次变化）；
- PC→XM5：PC 音量变化 → 轮询检测 → `send-xm5-volume (sent)` 写回耳机（`9169b6e` 修复反馈分发后生效）；
- 媒体键：双击（PLAY/PAUSE 交替）、上下曲注入生效；播放状态回写已实现待真机复核；
- 初始同步：headset-preferred（连接时采纳耳机音量，不推 PC 值）；
- 单增益模型准备：`native_pcm_source_set_apply_endpoint_volume` 开关已就绪（默认关）。

现状仍是“试验 harness”：observer 驱动由门禁临时绑定，executor 在门禁窗口内实时消费。音频管线仍对 PCM 应用端点音量，双衰减未消除。

2026-08-08 的三次常驻实机验证补充了关键约束：ABI 0.10 的 post-media activation、ABI 0.11 的 current-ACL profile 获取，以及媒体 START 后精确 PDO restart 均无法建立 AVCTP，最终返回 `0xC00000D0`。临时 gate 继续在“Microsoft 完成物理连接后、媒体就绪时绑定 Native”的顺序下成功。当前先对 Microsoft 与 Native 的首次物理连接执行 HCI/L2CAP 对照，确认 SDP、AVCTP 建链方向和 L2CAP结果；Phase B 的常驻实现等待该证据。

## 2. 目标

把双向音量同步从试验变为常驻产品路径，并完成单增益音频模型，最终与 Gate 3（常驻交付）收尾合并。

## 3. 贯穿性不变量（任何阶段不得破坏）

1. BTHport 异步请求 BRB 必须在 profile 存活期释放并置空（UAF 修复不变量）；
2. 每个 observation generation 只允许一次 outbound AVCTP OPEN：只能由媒体就绪后的 `BEGIN_OBSERVATION` 触发，BTH profile 不跨物理 ACL 保留，无自动启动和无重试；
3. 写 IOCTL 保持单一、授权 + `--apply` 双门控；
4. headset-preferred 初始采纳；
5. 单增益未落地前，不得宣称“听感正确”；
6. 失败路径必须可回滚到 Microsoft AVRCP / 既有驱动，不留残留。

## 4. 阶段划分

### Phase A：常驻集成设计（文档评审，无代码/真机）

产出 `docs/VOLUME_SYNC_INTEGRATION.md`：

- 组件与边界：observer 驱动、`v1_avrcp_host` 服务、executor 逻辑、daily host/agent 共存；
- 绑定策略：observer 何时/如何常驻绑定 XM5 0x110E PDO，Microsoft AVRCP 回退与恢复；
- 生命周期：ACL connect/disconnect、媒体会话、授权租约、崩溃/重启恢复；
- 配置与失败策略：音量同步开关、端点开关启用时机、日志；
- 安全评审：ABI 0.11 的 current-ACL profile + post-media activation 契约、写面收敛、policy 更新。

用户介入：设计评审与下述决策点确认。
门禁：文档 + 评审通过。

### Phase B：observer 常驻化（等待 bootstrap 诊断）

- 前置门禁：已完成（G-B0 结论见上），不再执行 Native 常驻绑定实验；
- 驱动：维持 Microsoft 常驻；observer 只在媒体期经精确 PDO restart 有界切换
  （每次会话单次 outbound AVCTP OPEN，失败即恢复 Microsoft）；
- 服务：常驻服务化设计已定稿（`docs/VOLUME_SYNC_RESPIDENT_DESIGN.md`）：
  普通用户 daily host（媒体触发/租约/写授权）+ 提升 handoff host（唯一执行
  PDO 切换与恢复）+ 命名事件/状态文件 IPC；
- B2 slice：Windows 写 sink 库化（SEND_COMMAND/虚拟键/端点音量回读）、
  `V1AvrcpObserverHost` 写模式、daily host 配置接线（默认关闭）；
- 安装/回滚：媒体期切换脚本（复用 observer gate 顺序）、卸载无残留、日志轮转；
- 离线：WDK 0 警告/0 错误、CTest、policy；
- 真机：组合实验——filter 与 observer 同栈，媒体期切换写路径，三层验证
  （filter IOCTL raw prefix 核对 pdu=`0x50`/params；observer INTERIM 回显；
  音量行为确认），随后恢复 Microsoft 基线。

用户介入：安装确认、一次真机冒烟。
门禁：常驻服务真机冒烟通过。

### Phase C：单增益音频模型落地

- agent/`native_pcm_source`：音量同步服务激活时启用 `apply_endpoint_volume=false`；
- 端点显示：滑块仍写端点（仅显示），实际响度只由 XM5 绝对音量决定；
- 真机验证：PC 调音量 → XM5 变；XM5 调 → PC 变；回读标量对照；听感确认无叠增益（0.2×0.2 消除）；
- 若 PC→XM5 出现跳动/竞争，引入“≥2 档才回写”容差（用户决策）。

用户介入：听感验证（关键裁决点）。
门禁：双方向数值 + 听感 gate 通过。

### Phase D：日常化与 Gate 3 收尾

- 回声容差策略定稿（默认保留或 ≥2 档容差）；
- 自有 UI：逻辑音量控制、连接状态、质量/格式选择（沿用现有 UI 基座）；
- 与 daily host 合并收尾：回滚演练、无残留检查、签名前检；
- 文档同步：更新 `STATUS.md`、`ROADMAP.md`；
- Gate 4：正式签名、退出 TESTSIGNING（外部依赖，另行计划）。

用户介入：UI 评审、最终安装确认。
门禁：Gate 3 收尾 + Gate 4 前置检查通过。

## 5. 需要用户决策的点（Phase A 评审时）

1. **绑定策略**：等待 bootstrap 对照结果；媒体期临时切换保留为已验证兜底，尚未达到日常无感标准；
2. **逻辑音量 UI**：端点滑块锁定 100% 后，系统滑块不再反映音量；逻辑音量显示放现有 UI/托盘，还是保留“写端点仅显示”方案？
3. **端点开关启用方式**：跟随音量同步服务激活（推荐），还是默认全局关闭；
4. **回声容差**：保留当前 1 档对齐，还是“≥2 档才回写”；
5. **常驻服务归属**：独立 `v1_avrcp_host`，还是并入现有 daily host。

## 6. 预估投入（相对）

- Phase A：0.5 天（文档 + 评审）
- Phase B：2–3 天（驱动常驻 + 服务 + 真机冒烟）
- Phase C：1–2 天（音频开关 + 真机听感）
- Phase D：2–3 天（UI + 收尾 + 文档）
- Gate 4：外部依赖，另计

## 7. 成功标准

- 常驻服务下：连接 XM5 → 自动同步；两侧任意调节音量始终收敛到同一增益；听感无叠增益；
- 播放、切歌、双击、音量调节与现有透明音频路径共存；
- 断连/崩溃/重启后自动恢复且无残留；
- Gate 3 收尾与文档同步完成，等待正式签名。
