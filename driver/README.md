# KMDF transport driver

这个目录是已通过 AX211/WH-1000XM5 真机 signaling 和 media 建链验证的 M1 transport。它能绑定到 WH-1000XM5 的 A2DP Sink service PDO、查询 `BthPort` profile interface、读取本地/远端 Bluetooth address，并向管理员进程公开 transport ABI。

signaling/media IOCTL 已实现异步 `BRB_L2CA_OPEN_ENHANCED_CHANNEL` 和 `BRB_L2CA_ACL_TRANSFER`，包括协商 MTU、一次一个 signaling read/write 与 media write、超时、取消、远端断连与文件/PnP 清理。驱动还注册固定 AVDTP PSM `0x0019` 的入站 server：XM5 在物理 ACL 建立后主动发起 signaling 时，由同一状态机接受并保存 channel；后续用户态 `OPEN_SIGNALING` 会复用该 channel，不再同时发起第二条冲突连接。ABI 的缓冲区约定见 [TRANSPORT_ABI.md](../docs/TRANSPORT_ABI.md)。

## 构建要求

- Visual Studio 2022 C++ Build Tools；
- Windows 11 SDK；
- 与 SDK 匹配的 Windows Driver Kit；
- x64 Developer Command Prompt，或显式使用 64 位 MSBuild。

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' `
    '.\driver\LdacNative.vcxproj' `
    /p:Configuration=Debug `
    /p:Platform=x64
```

不要使用 `MSBuild\Current\Bin\MSBuild.exe` 的 32 位宿主；WDK 26100 的 INF 验证步骤需要 x64 工具。当前工作机已用 WDK 10.0.26100.6584 完成 Debug/Release x64 编译、C/C++ 驱动代码分析和 signability 检查，结果为 0 错误、0 警告。ABI 0.5 在已验证的 ABI 0.4 signaling/media BRB 上仅增加只读 L2CAP OPEN 完成诊断；原有 AX211/XM5 capability、LDAC 配置、第二条 Media L2CAP、`OPEN` 和 `START` 路径保持不变。

## 暂时不要在主力系统安装

此 INF 会占用 XM5 的 A2DP Sink service PDO。安装开发包会替换该 PDO 当前的 profile function driver，使既有播放 endpoint 暂时消失；测试签名配置也可能需要重启。

进入 M1 真机阶段前必须准备：

1. 独立的测试 Windows 或可靠的恢复启动项；
2. 当前 `oem*.inf` 驱动包的导出副本；
3. BitLocker 恢复密钥与本地键鼠；
4. 开启测试签名所需的明确用户授权；
5. Driver Verifier 只选择 `LdacNative.sys`，不能全选系统驱动。

回滚目标是把 `BTHENUM\{0000110B...}` 重新绑定到安装前保存的 A2DP profile 驱动包。`tools` 目录提供带显式确认开关的备份、安装与回滚脚本；公开仓库不包含任何本机驱动包或绑定状态。使用顺序和安全边界见 [tools/README.md](../tools/README.md)。
