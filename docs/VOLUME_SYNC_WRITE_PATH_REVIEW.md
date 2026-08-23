# V1 音量同步写路径评审：upper filter 观察面

更新日期：2026-08-10
依据：policy 6 实机门禁（`avrcp-filter-20260810-180716-369`）+ 解码器扩展（section 191/192）

## 1. 结论摘要

upper filter 已证明可在保留 Microsoft AVRCP 为 function driver 的前提下双向观察
AVCTP 流量，解码器可还原 XM5 音量通知（INTERIM/CHANGED）、PASS THROUGH 和
Microsoft 的 vendor 命令（GET_CAPABILITIES、REGISTER_NOTIFICATION、
GET_PLAY_STATUS），并已验证能捕获 `SetAbsoluteVolume`（pdu `0x50`）及其音量参数。

PC→XM5 写方向当前由 Microsoft AVRCP 持有；真实 trace 中未出现 `0x50`，但观察
窗口内用户只做了耳机手势、未改 PC 音量，因此**不能归因**为 Microsoft 不推送。
这决定写 backend 的架构分叉：

- 若 Microsoft 在 PC 音量变化时自行发出 `0x50`：双向同步**无需自研写 backend**，
  filter 观察 + reducer 校验/纠偏即可（写由 Microsoft 完成）；
- 若 Microsoft 只观察不写：保留 Native observer 驱动的 `SEND_COMMAND` 写路径
  作为 backend（唯一写 IOCTL，授权 + `--apply` 双门控不变）。

## 2. 证据

### 2.1 policy 6 实机 trace（2026-08-10 18:07）

35 条 decoded 行：`vendor-command pdu=0x10/0x31/0x20`（Microsoft 侧
GET_CAPABILITIES、REGISTER_NOTIFICATION、GET_PLAY_STATUS）、XM5 音量
INTERIM `0x0F` 与 CHANGED `0x0D` 通知（42→47→51→47→42）、双击 PLAY
press/release。**无 `0x50`**。

### 2.2 解码器参数捕获（离线验证）

`v1_avrcp_filter_decoder_tests` 新增真实布局回归：Microsoft 私有头写请求
（`01 00 00 00 0E 00 00 00` + CONTROL `0x50` 命令，volume 47）与 Direct 布局
ACCEPTED 响应均解码为 `VENDOR_COMMAND`，`pdu_id=0x50`、`parameter_bytes[0]=47`。
`v1_avrcp_filter_probe` 同步在 vendor-command/write-response 行输出
`params=0xNN`。

## 3. 决策实验（下一轮真机，只读观察）

1. 当前系统为干净 Microsoft AVRCP 基线；从最终 HEAD 重建 policy 6 候选并安装；
2. 媒体 START、`XM5 ACTION WINDOW READY` 后，**PC 音量 +2 档**，再 **-2 档**；
3. 检查 trace 是否出现 `vendor-command pdu=0x50 params=0x..` 及对应响应；
4. 分类：
   - `microsoft-pushes`：出现 `0x50` 命令/响应 → 写 backend 不需要自研；
   - `microsoft-observes-only`：无 `0x50` → 需要 Native observer 写路径；
   - `inconclusive`：媒体未就绪/无通知注册/音量未变化 → 重跑。
5. 同时记录 XM5 侧滑后 Windows 公开音量标量是否被 Microsoft 回写（Core Audio
   只读采样），判断 Microsoft 是否已实现 XM5→PC 方向。

## 4. 对 reducer 与既有组件的影响

- filter 面下 XM5 音量通知已可回放进 reducer（`V1AvrcpRunFilterReplay`，
  section 191）；若 Microsoft 已实现双向同步，reducer 降级为校验/纠偏角色，
  不与 Microsoft 竞争写；
- 若需自研写 backend：既有写授权 gate（current ACL、独占 lease、Supported、
  显式 Synchronize、Windows snapshot 同时成立）与 `SEND_COMMAND` 单一写 IOCTL
  合同不变；
- 常驻 observer 的 AVCTP `0xC00000D0` 阻塞（G-B0）不影响本评审：filter 观察面
  不依赖 Native 常驻绑定。

## 5. 下一步

离线工作全部就绪（解码、回放、统计、策略）。下一步是上述实机决策实验，需要用户
参与：管理员安装确认 + 一次媒体会话中执行 PC 音量 +/-2 档操作。

## 6. 决策实验结论（2026-08-10 19:44 实机）

事务 `avrcp-filter-20260810-194421-342\result.json`：`passed=true`，媒体
START/READY 正常，30 秒观察窗口完整，结束后自动回滚（`oem9701.inf` 卸载、残留 0、
Microsoft 基线恢复）。probe 新输出 `params=0xNN` 生效。

用户确认在 `XM5 ACTION WINDOW READY` 后执行了 PC 音量 +2/-2 档，并做了两轮
侧滑手势。trace 证据：

- **窗口内没有任何 `pdu=0x50`（SetAbsoluteVolume）**——既无 Microsoft 写请求，
  也无对应响应；
- volume-changed 17 个 = 8 个 CHANGED（两轮完全同型的 `42→47→51→47→42`
  上上/下下手势）+ 9 个 INTERIM（每次 CHANGED 后 Microsoft 重新
  REGISTER_NOTIFICATION 并回读当前值）；
- XM5 音量始终在 42–51（33%–40%）区间，**未出现任何 PC 音量投影值**
  （若 PC ~18–35% 同步成功应出现 23–45 附近的新值）；
- 每次 CHANGED 后 Microsoft 都重新注册 `0x31 params=0x0D` 并收到 INTERIM
  回显——Microsoft 只观察、不推送。

判定：**`microsoft-observes-only`（已确认）**。PC→XM5 方向必须由本项目自研写
backend。该 backend 就是既有 Native observer 驱动的 `SEND_COMMAND` 路径
（2026-08-07 `f2c9836`/`9169b6e` 已在真机验证双向同步与 headset-preferred
初始采纳）。upper filter 观察面在本拓扑中的定位：

1. Microsoft 持有 AVCTP 时作为只读观察面（音量通知、媒体键、能力回查）；
2. 作为**本项目写路径的 IOCTL 层验证面**：`SEND_COMMAND` 为 METHOD_BUFFERED，
   filter 对 buffered/direct 方法记录 32 字节 raw prefix，可核对写请求结构中的
   PduId（`0x50`）、Response 与音量参数——验证命令确实到达 0x110E 驱动栈。

下一步回到常驻 observer 阻塞（AVCTP `0xC00000D0`，G-B0）：按计划执行
Microsoft/Native 首次连接 bootstrap 对照，找出 Native 常驻绑定缺少的初始化步骤；
期间 filter 观察面与 `--replay-filter` 回放可继续作为离线与诊断工具。

## 7. 写路径验证层（2026-08-10 补充）

验证自研写 backend 分三层，按可实现性排序：

1. **IOCTL 层（filter 验证）**：`SEND_COMMAND`（METHOD_BUFFERED）经过 upper
   filter 时，filter 记录 IOCTL 代码、方向、大小、完成状态与 32 字节 raw
   prefix；`NLD_AVRCP_OBSERVER_WRITE_REQUEST` 前 32 字节覆盖 Size、PduId、
   Response、ParameterSize 与 Parameters，因此可核对 pdu=`0x50` 与音量参数。
   组合实验需确认 filter（Extension upper filter）与 observer（function
   driver）同栈共存时双方的安装/回滚约束不冲突。
2. **协议回显层（observer 事件）**：XM5 对 `SetAbsoluteVolume` 返回 INTERIM
   音量回显（`0x0F`），observer 事件流可见，验证命令被耳机接受。
3. **行为层**：执行后 XM5 实际音量变化（用户确认/后续通知序列）。

空中字节（BRB → L2CAP → AVCTP）不可直接验证：`SEND_COMMAND` 由 observer 驱动
经 BTHPORT profile interface 的 ACL transfer 发出，不经过 0x110E 的
device-control IRP 队列；BTHPORT/HCI 与 /L2CAP ETW（bootstrap 工具链）只记录
L2CAP signaling（CID=1）与 HCI 短前缀（实测 BIP_DataLen ≤ 35、无 AVCTP data
帧），因此不把 ETW 作为写路径字节验证手段。
