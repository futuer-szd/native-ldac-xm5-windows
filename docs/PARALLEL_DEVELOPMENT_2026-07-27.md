# V1 并行离线开发记录（2026-07-27）

## Resume 标记

- policy v11 normal-stop真机gate状态：`failed-once/fix-verified/awaiting-retest`。
- 首次真机已完成完整transport与清理，但用户退出播放器偏早，实际媒体仅4040 ms，低于5000 ms证据下限；同时暴露epoch-only Render STOP被误记为volume change，以及agent在完整断连后仍等待总时限的两个实现缺口。
- 用户离开期间禁止自动运行gate、连接XM5、安装/更新驱动、切换Bluetooth、修改默认输出或注册登录任务。
- 后续离线提交会改变Git HEAD，因此现有candidate只保留为构建证明。resume前必须从最终clean HEAD重新运行`build-v1-normal-stop-candidate.ps1`并复验manifest/hash，再向用户重新给出命令。
- retest操作固定为：XM5初始关机；MediaStarted后至少继续播放10秒；完全退出播放器触发Render STOP；contained engine clean stop后再关闭XM5并等待ACL disconnect。失败不得原地重试。

## 本轮并行边界

1. 主线：policy v12播放中ACL断连/local-cancel合同、final attempt证据与正反例测试。只做离线实现；policy v11未通过前不冻结可运行candidate。
2. 生命周期：多generation日常序列与旧事件隔离压力测试，不访问设备或创建真实Bluetooth child。
3. UI/证据：全部可选的stop reason、graceful/cancel、media duration、final archive和resource convergence遥测；保持legacy/V1 state ABI隔离。
4. AVRCP：transport-free ObserveOnly记录/回放；任何回放动作必须恒为0，不接Windows或Bluetooth写backend。
5. 集成：每条支线独立审查、测试和Git提交；遇到需要真机、系统权限或不明确架构选择时停止在接口/测试边界。

## 明确不做

- 不运行policy v11或任何历史真机gate；
- 不安装、回滚或重签驱动；
- 不重启Windows、Bluetooth radio、音频服务或PnP设备；
- 不恢复Direct-PDO、HFP、自动抢默认输出或播放器自动暂停；
- 不用增加重试、延时或宽松evidence掩盖生命周期问题。

## 已完成的离线成果

### policy v12 播放中断连合同

- 父agent在消费ACL disconnect前冻结ending attempt编号；reducer仍立即fail-mute、publish absent并清零generation-local attempt，child完成cancel acknowledgement与exit 0后才按冻结编号归档final cancelled session。
- evidence要求MediaStarted后至少5000 ms、local cancel、SUSPEND/CLOSE均false、remote cleanup仍required、ConsumerLease released、本地channel close成功和一次physical disconnect。graceful exchange、缺final archive、跨generation、旧ramp或limiter介入全部拒绝。
- 当前只完成common policy与正反例测试；policy v11未真机通过前不生成policy v12 candidate。提交：`d0dd141`。

### 多generation日常收敛

- 新场景覆盖ACL1 graceful stop、ACL2新媒体、旧retry/Media/Engine回调隔离，以及播放中disconnect local cancel。
- 64个generation交替graceful/cancel，每轮endpoint、engine、transport、media账本归零，合法OPEN总数严格为64。
- 测试显式建模宿主active-generation过滤；不替代真实Job/named-event provenance测试。提交：`052fe4a`。

### UI生命周期结果遥测

- 新增全部可选且严格验证的`stop_reason`、graceful/cancel计数、media duration、final archive、resources released与lifecycle outcome。
- legacy agent state和V1 snapshot ABI继续隔离；旧state无字段时UI布局不变。policy v11 Result现发布统一graceful-stop字段。提交：`832efd3`、`aa89c85`。

### AVRCP ObserveOnly记录/回放

- 固定256事件的generation-bound日志覆盖capability、Windows callback、XM5 command response/notification、lease与mode请求。
- 回放强制ObserveOnly，即使日志请求Synchronize也不会进入写模式；每一步完整gate decision必须全0，异常enum、范围、sequence、lease转换、旧generation和篡改全部fail-closed。
- snapshot schema v1仅含JSON友好普通字段；没有Windows/Bluetooth backend。提交：`9ddac5e`。

## 集成验证

- CMake集中接入上述C++与PowerShell测试：`ac4a3cb`。
- 完整MSVC Release构建成功；CTest 127/127通过，其中1项真实endpoint条件测试按设计跳过。
- UI tests 24/24与`py_compile`通过。
- 全程未运行agent、XM5 gate、Bluetooth transport或系统探测，未修改driver、service、endpoint、任务或默认输出。

## 第二批离线诊断加固

### Resume 状态判定

- 新增只读`get-v1-normal-stop-resume-status.ps1 -AsJson`，只检查Git source identity、前置evidence与candidate manifest/hash，不探测设备或系统状态。
- 状态明确区分`source-dirty`、`prerequisite-invalid`、`candidate-missing`、`candidate-invalid`、`candidate-stale`和`ready-to-resume`。当前后续提交已经使旧candidate过期，这是预期状态；policy v11继续保持`pending-resume`，用户明确resume前不重建。提交：`7e6664d`。

### 真实宿主资源压力

- `V1EngineReadyHost`新增8轮真实Job Object、named event与child process压力，交替覆盖graceful/cancel stop。
- 每轮验证旧generation ready/media/stop事件不能越过新generation，stop acknowledgement存在，child实际退出，host回到inactive且父进程handle数精确回归基线；两次独立运行共覆盖32个child。提交：`a605594`。

### V1 trial result 只读入口

- 新增独立`V1TrialResult`、`load_v1_trial_result(explicit_path)`与`format_v1_trial_summary()`；只读取调用方明确给出的单个`result.json`，不扫描目录、不启动Tk、不探测进程或系统。
- result严格复用telemetry字段验证，但不能冒充`AgentState`；legacy state、V1 observer state与trial result三种ABI继续隔离。UI tests由24项增至29项。提交：`2f02071`。

### AVRCP ObserveOnly 确定性序列化

- snapshot新增固定4096-byte栈缓冲JSON serializer，无堆分配、第三方库或locale依赖，字段顺序与枚举字符串固定。
- buffer不足、非法schema/enum/range/generation/lease/计数或非ObserveOnly/非零动作合同均fail-closed；失败不修改调用方buffer或`bytes_written`。提交：`1a16482`。

### 第二批集成验证

- 集成提交：`7d67de5`。
- 完整MSVC Release构建成功；CTest 129/129通过，其中1项真实endpoint条件测试按设计跳过。
- UI tests 29/29、`py_compile`与`git diff --check`通过；真实host压力套件额外连续运行两次通过。
- 本批同样未运行agent、XM5 gate或Bluetooth transport，未修改driver、service、endpoint、任务、默认输出或其他系统状态。

## 第三批离线边界加固

### 生命周期事件幂等性

- 增加normal-stop在ack前重复Render STOP、ack后迟到ready/media/exit/retry、重复ACL disconnect，以及playback-disconnect后旧generation事件跨新generation idle/streaming阶段的确定性场景。
- 所有重复与迟到事件均不得产生第二次OPEN或错误的graceful/cancel；当前generation的重复ready/media/retry也保持media账本不变。审计未发现需要修改生产reducer的缺口。提交：`b0c7b57`。

### AVRCP ObserveOnly边界

- 回放在发布snapshot前核对事件流与日志缓存的generation/current/owner lease元数据；篡改时fail-closed且不修改输出。
- JSON serializer拒绝owner lease非零、但acquire不大于revoke的不可能状态；新增256事件满容量、`UINT64_MAX`、generation半范围与回绕、精确buffer/NUL、隐藏payload非法值和重复序列化逐字节一致测试。仍为ObserveOnly、零action、零token且无backend。提交：`5d41cf2`。

### 显式多结果比较

- 新增`V1TrialComparison`、`compare_v1_trial_results(paths)`与`format_v1_trial_comparison()`，只比较调用方明确给出的非空、无重复路径序列；不接受单个字符串、目录或glob，也不扫描同目录文件。
- 汇总pass/fail/unknown、graceful/cancel、media duration、stop reason、lifecycle outcome和archive/resource三态；任一显式文件非法或属于legacy ABI时整组失败。提交：`f95a265`。

### 宿主压力诊断

- 集成复验曾出现一次未能复现的真实host压力失败，原聚合错误无法区分新child过早ready、old host事件失败或new host收到意外事件。没有增加timeout或放宽断言。
- 将该判断拆为精确错误阶段并在意外transport event时输出枚举值。随后单项连续两次、独立单项一次和最终全量套件一次均通过；该历史波动继续保留在日志中供未来复现定位。提交：`d48c156`。

### 第三批集成验证

- 完整MSVC Release构建成功；CTest 129/129通过，其中1项真实endpoint条件测试按设计跳过。
- UI tests 33/33与整个`ui`目录`compileall`通过；`git diff --check`通过。
- 本批未运行agent、XM5 gate、Bluetooth transport或系统探测，未修改driver、service、endpoint、任务、默认输出或其他系统状态。

## 第四批离线发布边界

### policy v11 candidate identity

- resume与真机gate现要求candidate必须为`Release`，manifest prerequisite必须是仓库固定的policy v10 transaction，且prerequisite source commit必须与已验证transaction一致；Debug、替换证据路径或不匹配证据source均不能进入`ready-to-resume`。
- candidate SHA-256改用标准.NET实现，避免依赖PowerShell模块自动加载。首次全量CTest暴露其fixture在CTest环境找不到`Get-FileHash`；改动后单项与全量均通过。提交：`4ef7a10`、`258fac4`。

### policy v12 evidence故障矩阵

- state/session/retry/final archive的必需字段在读取前统一检查；缺字段稳定返回false，不再因StrictMode抛异常。
- final archive必须与最终session完整一致；generation、OPEN/action、graceful/cancel、Render STOP、ConsumerLease、endpoint/render/engine失败计数、PCM/fade block、volume min/last/max和resource convergence的矛盾证据全部拒绝。
- 审查时按实际producer修正两项测试假设：PCM模式会启用configuration层并发布一次capability；内部`consumer_lease_held`不进入JSON，公开证据改由acquired/released与计数收敛证明。policy v11未通过前仍不生成v12 candidate。提交：`4282cd5`。

### 只读结果CLI

- 新增`ui/v1_trial_summary.py`，只接受一个或多个显式`result.json`路径；禁止glob、目录扫描和重复路径，不启动Tk或探测系统。
- 单文件输出稳定摘要，多文件输出aggregate加按输入顺序的逐项摘要；`--json`提供排序稳定的机器可读结果。已用policy v10真实Result做只读smoke test。提交：`a10d7d5`。

### AVRCP消费者合同

- 新增独立Python ObserveOnly snapshot parser，严格镜像C++ producer的25字段schema、4096-byte上限、enum/range/count/generation/lease/mode关系，并拒绝重复、缺失或额外JSON字段。
- parser强制`enforced_replay_mode=observe_only`且`emitted_action_count=0`，不接Tk、Windows/Bluetooth backend或写动作。提交：`afeedbc`。

### 第四批集成验证

- 完整MSVC Release构建成功；修复上述CTest环境哈希依赖后，CTest 129/129通过，其中1项真实endpoint条件测试按设计跳过。
- UI tests 48/48、整个`ui`目录`compileall`与`git diff --check`通过。
- policy v11状态仍为`pending-resume/candidate-stale`；本批未重建candidate，未运行agent、XM5 gate、Bluetooth transport或系统探测，也未修改任何系统状态。

## policy v11 首次真机结果与修复

- 首次session为`artifacts/v1-normal-stop/trial/session-20260727-154805-611`。前三次OpenSignaling均为允许的Win32 71、zero-exchange有界恢复，第四次START成功并发送696 packets、468408 bytes；SUSPEND/CLOSE、ConsumerLease release与ACL disconnect全部完成。
- 真机结果仍判失败：MediaStarted到Render STOP只有4040 ms，低于policy要求的5000 ms。用户没有听到声音；transport packet delivery不能证明声学成功，因此无声现象保留为下一次复测项，没有以延长重试或放宽evidence掩盖。
- Render STOP使WaveRT stream epoch从25切到28，PCM sample rate、bit depth、mute和volume scalar/dB均未变化。核心现把epoch-only变化识别为graceful stop，不再增加`volume_change_count`或把`volume_stable`置false；真实音量/格式变化仍按原策略立即停止。
- presence agent新增terminal PCM lifecycle收敛出口：媒体已开始、无media failure、stop acknowledgement与OPEN配平、child start/exit配平、presence absent、render idle、generation attempt清零且engine inactive时立即结束。这样XM5物理断连后不再等待完整`DurationSeconds`。
- 新增epoch-only graceful-stop单元测试与presence-agent静态合同。完整MSVC Release build成功；CTest 129/129通过（1项真实endpoint条件测试按设计跳过），UI tests 48/48、`compileall`和`git diff --check`通过。
- 下一次只允许从本修复的clean HEAD重建candidate后复测。看到MediaStarted后应继续播放至少10秒再完全退出播放器；断连后agent应快速结束。Win32 71不是必经步骤，只是仅在zero-exchange OpenSignaling阶段允许的有界恢复。

## policy v11 signaling acquisition 修复

- 多轮真机反复在第3或第4次OpenSignaling才成功，旧PCM profile的15/30/45秒退避最坏需要90秒；它只能证明失败attempt安全关闭，不能作为无感启动方案。normal-stop真机复测因此再次暂停，先修连接 acquisition。
- ABI 0.5驱动本来已经保存最近一次L2CAP OPEN的I/O、BRB、Bluetooth status、PSM、remote Response与ResponseStatus，但V1 worker在失败后关闭handle时丢弃了这些字段。backend现会在关闭前只读GET_OPEN_DIAGNOSTICS并缓存，PCM/silence session与每份attempt JSON完整发布诊断。
- retry分类从笼统`OpenSignaling/Win32 71/zero exchange`收紧为：诊断必须available、remote response valid、operation signaling、PSM 0x0019、Response 4 `NO_RESOURCES`、ResponseStatus 0。PSM不支持、安全拒绝、本地失败或无有效远端响应均立即停止，不能消耗retry窗口。
- normal-stop gate通过显式`--pcm-fast-signaling-acquisition`选择独立1/2/4秒deadline，总退避由90秒降为7秒；其他PCM调用方与旧discovery/configuration policy的历史合同不被静默改写。所有deadline仍由主event loop驱动，Render STOP、ACL disconnect与generation变化立即取消，不使用worker Sleep。
- evidence要求每个可恢复attempt同时证明完整诊断与remote-no-resources分类；新增永久remote response拒绝、诊断传递、快速delay和mock JSON合同测试。该改变只能保证不再盲等90秒；XM5是否能在7秒内释放资源仍必须由一次修复版真机运行验证。
- 完整MSVC Release构建成功；CTest 129/129通过（1项真实endpoint条件测试按设计跳过），UI tests 48/48、`compileall`与`git diff --check`通过。全程未运行agent、Bluetooth OPEN或真机gate，也未修改driver、service、endpoint或系统设置。

## policy v11 OPEN诊断回查加固

- `session-20260727-211306-521`在第一次OpenSignaling/Win32 71停止；非静音PCM、格式与ConsumerLease均正常，zero exchange/zero media packet证明问题不在PCM或encoder。由于ABI 0.5诊断不可用，strict policy没有执行第二次OPEN。
- backend现保留诊断query次数、错误和字节数；失败handle读取不到时，关闭后以fresh interface handle只读回查驱动保存的最后一次OPEN记录。该fallback不提交Bluetooth、AVDTP或media请求，并恢复原始OPEN错误。
- gate失败文本发布query attempts/Win32/bytes；只读`transport_probe --open-diagnostics`可在管理员上下文独立查看最近记录。evidence继续只接受完整确认的remote `NO_RESOURCES`，并新增query字段与不可读诊断反例。实现提交：`05ac804`。
- 完整MSVC Release构建成功；CTest 129/129通过（1项真实endpoint条件测试按设计跳过），UI tests 48/48及`compileall`通过。未运行真机gate或修改系统状态；状态为`fix-verified/awaiting-retest`。

## policy v11 driver adapter转发修复

- `session-20260728-134611-164`发布query attempts/error/bytes `0/0/0`，同时确认非静音PCM peak `0.01461592`、zero exchange与lease配平。该组合把问题定位为诊断调用未进入底层backend，与用户点击播放速度无关。
- PCM实际使用的silence driver adapter及configuration adapter遗漏`GetLastOpenDiagnostics` override，继承的默认实现会清零结果；现两者均显式转发到底层backend。normal-stop policy测试锁定两份生产adapter的转发合同。提交：`cece4a6`。
- 完整Release构建成功；已记录的host generation压力测试首轮波动一次，随后单项连续三次和最终全量CTest 129/129通过。没有改timeout或断言，未运行Bluetooth OPEN或修改系统状态；状态继续为`fix-verified/awaiting-retest`。

## policy v11 signaling collision诊断

- `session-20260728-135943-623`的四次attempt均完整确认remote L2CAP `NO_RESOURCES`：sequence 1–4、query `1/0/48`、PSM `0x0019`、Response 4/Status 0、zero exchange/packet与lease释放。1/2/4秒窗口耗尽，快速退避本身不能解决连接。
- 只读inventory确认同机A2DP PDO仅有`LdacNative`，`BthA2dp`与`AltA2dpSVC`均停止。当前保留两项待区分原因：手机/第二设备占用，或XM5入站AVDTP与当前outbound-only driver发生建链碰撞。
- 新增一次性HCI/L2CAP capture gate与离线CID 1 parser；捕获物理ACL启动到四次OPEN，解析双向PSM `0x0019` Connection Request及Response 4，并恢复analytic channel状态。内核驱动尚未修改。提交：`8ee9c50`。
- 完整Release构建和CTest 129/129通过。下一步只运行collision diagnostic，不再直接重跑normal-stop。

## policy v11 incoming signaling根因修复

- 用户确认不存在多点连接。capture `capture-20260728-144023-075`证明XM5在ACL启动时先发PSM 25/SCID 66的入站请求，Windows旧路径只回PENDING且没有SUCCESS；本机出站request为0。当前驱动缺少incoming server使XM5唯一signaling资源被悬空占用，随后outbound OPEN才得到NO_RESOURCES。
- HCI summary parser已兼容无`EventData`的analytic错误事件并成功重解析原始capture；本次无需重跑。嵌套normal-stop实际在PCM Prepare/volume状态失效处停止且OPEN为0，失败分类已修正。
- LdacNative新增固定PSM `0x0019` server、入站response与channel复用；用户态OPEN优先复用或等待正在完成的入站channel，不再建立第二条碰撞连接。固定PSM不使用仅支持动态范围的`BRB_REGISTER_PSM`。只读file handle关闭会保留未claimed入站channel，transport owner退出才清理；diagnostics发布`INBOUND_CHANNEL`方向位。PnP注销、async cancel/wait、remote disconnect和用户handle cleanup均有静态合同。
- ABI 0.5新增ready bit `0x8`，新候选要求总flags `0xF`。WDK Release代码分析、ApiValidator、Inf2Cat、签名均0警告/0错误；完整Release build、CTest 130/130（1项条件跳过）、UI 48/48、`compileall`和`git diff --check`通过。未安装驱动或修改系统状态。

## policy v12 inbound handoff部署边界

- 新candidate/prepare/rollback链先导出当前`oem122`类published package，再尝试同服务更新；只有新driver未加载时才重启精确XM5 A2DP service PDO。ready flags必须从`0x7`变为`0xF`。Windows reboot、Bluetooth radio toggle和A2DP service状态修改均被禁止，失败自动恢复旧INF。
- 首测不使用PCM或播放：只读等待incoming OPEN成功，执行一次`--discover --open-attempts 1`，HCI必须证明inbound PSM25最终SUCCESS、outbound PSM25 request为0、NO_RESOURCES为0。SET_CONFIGURATION、媒体OPEN/START/packet全部为0。
- parser新增按identifier配对的PENDING/SUCCESS/rejection与pending-without-success字段；新增静态policy锁定单次DISCOVER、零媒体、自动rollback和无重启边界。最终Release build、CTest 131/131（1项条件跳过）、UI 48/48、`compileall`和`git diff --check`通过。当前只完成离线实现，尚未安装新driver或运行真机gate。

## policy v12 首次真机与CRLF修正

- `oem123`无重启加载并达到ready flags `0xF`。HCI明确证明incoming server成功：XM5 PSM25/SCID66，Windows PENDING后约53微秒SUCCESS，configuration完成；outbound PSM25 request和remote NO_RESOURCES均为0。
- gate没有进入DISCOVER，因为native diagnostics的CRLF行尾未匹配只接受LF的正则，五秒后形成假失败。XM5断开后自动回滚已精确恢复`oem122`。Windows蓝牙UI在真实ACL disconnect前短暂保留“已连接”，HCI最终disconnect正常。
- diagnostics正则改为CRLF/LF兼容并增加fixture；失败时保存最后一次diagnostics文本。driver tree不变，旧candidate作废，下一次必须从新clean HEAD重建。

## policy v12 入站handoff返回合同修复

- 第二次真机已到达入站channel复用，但同步`OPEN_SIGNALING`完成时返回长度为0；probe在发送DISCOVER前以invalid channel data停止。transport owner退出拆除channel后XM5再次入站，因此HCI出现2次request，根因不在XM5重连或多点占用。
- driver dispatcher现为同步成功路径返回完整`LDAC_NATIVE_CHANNEL_INFO`长度，测试锁定该合同。异步completion没有改变。
- gate在ACL disconnect event后继续有界等待Windows公开`fConnected`收敛，记录耗时和轮询；只有精确exit 10/disconnected才允许失败回滚。未收敛时禁止PnP rollback。
- 开/关机提示改由connection probe在notification注册与二次初态检查完成后输出并flush，gate实时流式捕获；消除旧流程“先提示、后布防”可能漏掉快速ACL transition的窗口。
- WDK Release与完整MSVC Release构建成功，CTest 131/131通过（1项条件跳过）。当前旧`oem123`和rollback-required事务仍在，未擅自修改系统；下一步先显式回滚旧事务，再从新clean HEAD构建candidate。
