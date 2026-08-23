# V1 并行离线开发记录（2026-07-26）

## 基线与边界

- 分支：`codex/v1-event-driven`
- 启动提交：`3b4f248ae76f53c2ce77ab291f9484cefe15b983`
- 当前真机主线：policy v10 十秒透明桥接已通过并完成用户报告收口；下一层转入正常STOP/断连/重连生命周期。
- 当前系统保持 `LdacNative` 与 V1 endpoint 已验证基线；并行开发不得安装/更新驱动、注册任务、切换蓝牙、打开 XM5 transport 或修改默认输出。
- policy v8 真机测试提醒已在用户回到电脑前后删除；没有遗留自动执行项。

## 并行线 A：生命周期压力与故障注入

状态：已完成并提交 `9ff3165`。

目标：用确定性和伪随机事件序列覆盖 ACL generation、RenderDemand、engine lease、media session、retry budget、watcher expiry 与故障收敛。必须证明无跨 generation OPEN、无 attempt 越界、远端断开不盲发 graceful command、最终状态 fail-closed。

交付：独立 C++ test target、16 个固定 seed、每个 16384 步，共 262144 个 reducer 事件。记录 685 次合法 OPEN、21395 次 ACL disconnect、20742 次 watcher expiry 和 42153 次资源收敛；MSVC `/W4 /WX` 与 CTest 通过。测试只使用纯 reducer/资源账本，不访问设备。事件枚举本身不携带来源 generation，因此“旧 worker 与新 worker 同时存在”的来源隔离仍由现有 `V1EngineReadyHost` generation-scoped event/Job 测试负责。

## 并行线 B：AVRCP Absolute Volume 模型

状态：已完成离线模型并提交 `94ceb5c`；Bluetooth backend/write 仍冻结。

目标：在不发送 Bluetooth command 的前提下实现 Windows endpoint 单一权威值、XM5 0..127 与 Windows 0..100 映射、首次会话同步、双向更新、回环抑制、静音/0%、过期 generation 拒绝与不支持 Absolute Volume 时安全退化。

交付：独立 C++ 状态机与单元测试。Windows endpoint 是唯一权威；命令响应与耳机主动通知分离，匹配 callback 抑制回环，不匹配 callback 重申 Windows 值。覆盖完整 0..100/0..127 映射、静音/0%、能力迟发现/丢失、stale generation 和 `UINT64_MAX -> 1` 回绕。本阶段没有接入 transport write。

## 并行线 C：UI 与状态遥测

状态：已完成并提交 `76dc017`。

目标：以只读、全部可选、兼容旧 state ABI 的方式解析 generation、RenderDemand、OPEN attempt/retry、PCM prepare/epoch restart、ConsumerLease acquire/release和 limiter telemetry。UI继续只负责配置和观察，不承担连接存活。

交付：冻结 `AgentTelemetry`、flat/nested 两种可选解析与严格类型检查。旧 v1/v2 state 缺少字段时全部返回 `None`/空 tuple；UI 单元测试 17/17 通过，不修改系统或启动媒体进程。

## 主代理集成责任

状态：本轮集成完成。

1. 三条子线均经主代理审查并使用独立 Git 提交。
2. `CMakeLists.txt` 已集中加入 lifecycle stress 与 AVRCP reducer target。
3. 完整 Release 构建成功；CTest 110/110 通过，其中 1 项真实 endpoint 条件测试按设计跳过；Python UI tests 17/17 通过。
4. 本轮只增加离线模型、测试与只读解析，不改变 policy v8 candidate、驱动、endpoint 或系统状态。
5. policy v8 仍是下一次唯一真机 gate；本轮并行工作不要求用户追加验证。

## 第二批纯离线加固

状态：已完成并分别提交。

- UI 遥测展示（12d6964）：旧 state 无字段时不占布局；legacy agent state 与 V1 gate state 保持双 ABI 隔离，孤立 V1 state 不能驱动后台 agent mode。可选摘要显示 policy、ACL/render、OPEN/retry、PCM epoch/ConsumerLease 和 limiter；无效/部分 JSON 被忽略。UI tests 22/22 通过。
- 宿主 generation 来源隔离（3bbe946）：旧/新 child 与 transport worker 同时存活时，旧 generation 的 ready、CapabilitiesDiscovered、MediaStarted、MediaStopped 不会进入新 host；新 generation 随后可独立 OPEN/START/STOP。没有修改生产 host。
- AVRCP 写授权闸门（39723bb）：默认 ObserveOnly。只有 current ACL generation、非零独占 owner lease、Supported capability、显式 Synchronize 和 Windows 权威快照同时成立才允许 reducer action。所有动作携带 generation/lease/authorization epoch，lease revoke、capability loss、观察模式或 ACL 换代立即使排队动作永久失效。仍没有 Bluetooth backend/write。

验证：相关 MSVC /W4 /WX targets 通过；完整 CTest 110/110 通过，其中 1 项真实 endpoint 条件测试按设计跳过。

## 暂停项

- policy v8 以外的真机 gate；
- 驱动或 endpoint 包更新；
- 登录计划任务安装；
- AVRCP 写命令与 XM5 侧滑真机同步；
- HFP、Direct-PDO、自动抢默认输出和播放器自动暂停；
- 96 kHz/24-bit 真机播放。

## 第三批：policy v8 收口与 policy v9 linked limiter

- policy v8 真机完成 60 秒、10336 packets 的稳定 transport；ConsumerLease、SUSPEND/CLOSE均配平。用户报告总体清晰、低音较闷，未明确评价的听感字段保持 `not-reported`。收口提交：`2134eed`。
- 独立 limiter 审查定位了三个集成前风险：模块未冻结0.25上限、旧release定义从实际attack恢复不足50 ms、块内晚峰可能产生瞬时gain阶跃。最终实现全部修正。
- linked limiter生产路径使用固定128-frame栈scratch，无每块堆分配；左右声道共享gain，block peak从块首预衰减，suffix约束release，fallback clamp必须为0。
- 测试覆盖44.1/48/88.2/96 kHz、首末峰、块边界reattack、exact ceiling、反相/交替声道、持续hot、2000个确定性随机块和无效输入non-mutating。
- policy v9固定unity gain、0.25 ceiling、50 ms release和60秒，复用policy v8全部ACL/retry/PCM/lease/SUSPEND/CLOSE合同。提交：`334fbab`。
- 完整Release构建通过；CTest 114/114通过，其中1项真实endpoint条件测试按设计跳过。没有运行真机gate，没有安装/回滚驱动、重启、切换蓝牙或修改默认输出。

## 第四批：policy v9 收口与 fidelity bridge

- policy v9传输通过，但用户未仔细听；结果严格收口为`not-assessed-by-user`，所有声学字段保持`not-assessed`。提交：`7ca63c9`。
- 独立架构评审确认0.89125094只能称为−1 dBFS digital sample peak；真正−1 dBTP至少需要合格4× polyphase FIR、跨block状态、lookahead、prime/flush和输出侧复验。
- sent-frame fade采用Prepare/Commit两阶段事务，只有成功WriteMedia后才推进；LDAC多个128-frame PCM block聚合为一个RTP packet时先在state副本上原子预验，再一次发布。提交：`93ad211`与`f11b173`。
- policy v10固定10秒、unity、100 ms fade-in、ceiling 0.25→0.89125094/2000 ms；ramp和fade均只按成功发送frame推进。Windows volume/mute/format/epoch在每块与写包前复查，变化时不再发送并正常SUSPEND/CLOSE。
- 测试覆盖48/96 kHz多block/packet聚合、Write失败0 commit、pacing后graceful/cancel、volume变化0 packet、旧generation和无效CLI generation。完整Release构建通过；CTest 120/120通过，其中1项真实endpoint条件测试按设计跳过。

## 第五批：policy v10 真机收口与透明默认路线

- source `0c112ae` 的policy v10在`session-20260726-195238-523`完成44.1 kHz/16-bit、10.001秒、1723 packets/1159579 bytes；PCM read/sent均为441088 frames，ConsumerLease、START/SUSPEND/CLOSE全部配平。
- Windows音量固定为−17.5 dB，5169次volume/format查询无变化；fade prepared/committed均为3446 blocks，sent-frame commit覆盖全部已发送PCM。
- pre/unlimited/post peak均为`0.03015155`；limiter attack、gain reduction、fallback clamp、fade/limiter sanitized sample均为0。因此除最初100 ms sent-frame fade外，本轮没有附加增益、EQ、压缩或动态处理。
- 用户报告整体正常、十秒播放正常、细微差别听不出来；未单独评价的bass、clarity、pumping、noise、speed和distortion保持`not-separately-assessed`。收口提交：`9acdc8d`。
- 产品默认路线锁定为transparent unity。100 ms启动fade、动态状态锁、NaN/Inf清理和通常不介入的sample安全边界保留；正常STOP不合成淡出音频，直接停止取包并执行SUSPEND/CLOSE。4× true-peak降为可选后续加固，不阻塞生命周期。
- 下一批离线工作聚焦正常Render STOP、播放中ACL断开、本地cancel、重新开机后的新generation和多轮资源收敛；在候选与证据策略审查完成前不要求用户运行真机gate。

## 第六批：透明normal-stop核心

- 新`v1_transport_normal_stop_worker`保持unity、100 ms sent-frame启动fade和固定`0.89125094` sample安全边界；移除policy v10的2秒0.25→0.89125094 ceiling ramp，避免高峰值输入在启动后继续受到开发期动态处理。媒体硬上限为60秒，只用于防止gate无限运行。
- fidelity核心现在只在`ceiling_ramp_ms=0`且起点精确等于最终ceiling时接受固定边界；0 ms ramp配较低起点会在PCM prepare前fail-closed。旧policy v10的显式2秒ramp行为保持不变。
- 单元测试证明固定边界下普通0.1峰值输入的limiter attack/gain reduction/fallback全部为0；Render STOP产生graceful disposition、丢弃未发送pending fade、SUSPEND/CLOSE成功、ConsumerLease释放。cancel/disconnect仍不盲发远端控制。
- worker提交：`a08919e`；静态policy与CMake测试提交：`c0080ba`。完整Release构建通过，CTest 122/122通过，其中1项真实endpoint条件测试按设计跳过。
- 当前只完成离线核心和policy锁；候选打包、真实artifact evidence与操作脚本尚未开放，所以用户现在不需要运行命令。

## 第七批：policy v11 normal-stop gate

- normal-stop evidence严格要求MediaStarted后至少5000 ms、未达到60000 ms硬上限、`ended_by_graceful_stop=true`、SUSPEND/CLOSE、ConsumerLease release和一次physical ACL disconnect；limiter介入、旧2秒ramp、跨generation或exchange后retry全部拒绝。
- 父agent只在graceful child acknowledgement与exit 0之后归档最终`attempt-N.json`，补齐正常提前STOP过去缺少final attempt archive的证据缺口；cancel/disconnect不会进入该分支。
- gate明确要求完全退出播放器。单纯暂停若保持WaveRT RUN，不会被误判为Render STOP；用户必须先看到contained engine clean stop，再关闭XM5。
- 完整Release构建与CTest 123/123通过，其中1项真实endpoint条件测试按设计跳过。实现提交：`36c9774`。
- 实现提交为`36c9774`。最终冻结候选必须在本轮文档提交后从当时clean HEAD重建；gate只接受manifest `source_commit`与当前HEAD完全一致。driver tree保持`b31841ff597f1c871addb750539b4f5d39d2cf7e`，构建不会启动agent、连接XM5或修改系统。
