# V1 current daily UI 运行合同

更新日期：2026-08-20
状态：current daily 质量配置 IPC v1 已实现（仅 HQ/SQ/MQ）；格式/EQ 写入仍关闭；旧 AVRCP handoff 路线已退役

## 1. 权限边界

- `v1_presence_agent.exe --daily` 继续独立运行，UI 不是生命周期 owner。
- UI 只读取原子 state JSON 和最近一次 transport result，不打开驱动、不设置
  Event、不调用 PnP、不修改系统音量或默认端点。
- UI 关闭只销毁窗口，不停止 daily host、worker、LDAC 或统一音量桥。
- current daily 模式下启停、格式和 EQ 控件禁用；质量仅允许 HQ/SQ/MQ，并通过独立
  的受限 IPC 提交。旧 legacy probe 控制代码不获得 current daily host 的写权限。

## 2. 文件 ABI

默认运行根目录为 `%LOCALAPPDATA%\NativeLdac\V1`：

- `state\daily-state.json`：schema 1、`mode=daily`、`daily_mode=true`；
- `results\latest-session.json`：最近一次 transport result；
- `logs\ui`：普通用户 UI 日志。

state 新增 `host_process_id`。UI 必须同时验证：

1. JSON schema/mode 正确；
2. 必需字段类型和值域正确；
3. 文件不超过 64KiB；
4. PID 当前存在，且映像名严格为 `v1_presence_agent.exe`。

任何条件失败都不得采用该快照。开发调试只允许通过
`NATIVE_LDAC_V1_STATE_PATH`/`NATIVE_LDAC_V1_RESULT_PATH` 显式指定单个文件；
UI 不扫描 trial 或 artifacts 目录寻找“最新”结果。

## 3. 第一阶段展示

UI 从 state 派生：

- XM5 物理连接与 ACL generation；
- 播放、暂停、Render 和 worker 状态；
- 统一音量 ready 或 Windows 增益 fail-safe；
- HFP capture/bridge 只读状态；
- engine/media/observer/endpoint/Render 失败计数；
- 最近一次 transport 的采样率、位深、声道和实际传输码率。

最近 transport 数据必须明确标记为“最近会话”，不能冒充实时码率。当前 daily worker
质量状态区分 requested/applied，实际档位来自最近 transport result。

## 4. 未来写配置 IPC 的硬边界

格式、EQ 等后续可写功能不得通过扩展 state JSON 或直接写驱动实现。当前质量 IPC
已经独立、版本化并受限，仍必须满足：

- UI 保持普通用户权限；提升宿主验证调用者身份和消息大小；
- 命令使用固定 allowlist、schema version、revision 和完整值域检查；
- UI 不能提交路径、可执行文件、PnP instance、驱动包或任意系统命令；
- 设置只在 host 的安全媒体边界应用，失败保留最后一次有效配置并发布错误；
- PnP、Driver Store、默认端点和系统级恢复继续由明确授权的机器级路径负责；
- IPC 关闭或 UI 崩溃不能停止正在工作的 LDAC 会话。

当前质量 IPC 合同：实例级本地命名管道 `NativeLdac.V1.Config.<instance>`，固定 28 字节 request /
40 字节 response；调用者必须是同一用户、同一会话；只接受 HQ/SQ/MQ 和严格递增
revision。配置以 last-known-good 二进制快照原子保存；正在播放时只发布 requested，
下一次安全 worker 启动前才 applied，不做播放中热切换。非法、过期或持久化失败请求
只增加 rejected/error 遥测，不改变正在工作的配置。
