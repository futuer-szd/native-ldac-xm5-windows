# Native LDAC virtual audio endpoint

这个目录是独立 Windows render endpoint 的 M3 实现。它基于微软官方 Simple Audio Sample 的 PortCls/WaveRT 实现，并按 MS-PL 保留版权和许可。

当前版本只暴露一个端点：

```text
Native LDAC - WH-1000XM5
44.1 / 48 / 88.2 / 96 kHz
16-bit or 24-valid-bit PCM (32-bit container) / stereo PCM
WaveRT event-driven render
```

它与 `driver/LdacNative.sys` 是两个不同的驱动：

- `NativeLdacAudio.sys` 是根枚举的 MEDIA 设备，接收 Windows Audio Engine 的 PCM；
- `LdacNative.sys` 绑定 XM5 A2DP Sink service PDO，负责 BthPort signaling/media transport。

WaveRT render stream 消费的数据会写入驱动内的 48,000-byte 有界 PCM ring buffer（默认 48 kHz/16-bit 时为 250 ms）。PCM ABI v2 动态上报当前 Windows stream 的采样率、位深和 block alignment；用户态通过 WaveSpeaker 的只读 KS property 获取格式/状态并批量读取 16-bit 或 24-valid-bit（32-bit container）样本。缓冲溢出时丢弃最旧数据并累计 discontinuity，避免延迟无限增长。`audio_endpoint_probe.exe` 用于独立验证这条路径；`transport_probe.exe --play-endpoint` 已把该读取接口接入 LDAC 编码和 Bluetooth 传输循环。

## 构建

使用现有 Visual Studio 2022 + WDK 26100：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\build-audio-endpoint.ps1
```

暂存包输出到：

```text
artifacts\audio-endpoint\package
```

PCM probe 输出到：

```text
artifacts\audio-endpoint\audio_endpoint_probe.exe
```

安装或更新驱动后，可以先在不连接蓝牙的情况下检查 ABI：

```powershell
.\artifacts\audio-endpoint\audio_endpoint_probe.exe --info
.\artifacts\audio-endpoint\audio_endpoint_probe.exe --monitor 10
```

`--monitor` 期间需要让 Windows 应用向 `Native LDAC - WH-1000XM5` 播放音频；它会显示 PCM 字节、峰值、RMS、缓冲占用和丢弃量。

完整桥接测试使用：

```powershell
.\artifacts\driver-test\transport_probe.exe --play-endpoint --quality auto
```

桥接进程和 `audio_endpoint_probe --monitor` 都会消费同一个 PCM ring，测试时不要同时运行两者。样例拓扑把主音量声明为硬件控制但没有处理 PCM；当前 bridge 会读取 Windows `IAudioEndpointVolume` 的主音量/静音，并在 LDAC 编码前应用对应增益。

Native LDAC topology 将音量范围声明为 `-96..0 dB`，步长为 `1/32 dB`。较细的硬件步长避免 Windows 媒体音量键或 MouseInc 边缘滚轮连续调用 `VolumeStepUp/VolumeStepDown` 时，相邻系统音量步被归一化到同一个值并显示 `+0/-0`。`endpoint_volume_probe.exe --info` 会只读输出当前 scalar/dB、范围和 Windows logical step，`--monitor` 会记录实际变化。

## 安全边界

- 该驱动使用独立硬件 ID `ROOT\NativeLdacAudio`，不会替换 AX211、XM5 PDO 或当前 `LdacNative`；
- topology physical bridge pin 通过只读 `KSPROPERTY_JACK_CONTAINERID` 返回构建时从 XM5 A2DP service PDO 验证得到的真实 Container ID；Endpoint Builder 可以据此把独立 ROOT audio adapter 生成的 MMDevice endpoint 归入 XM5 容器，而无需改写 PnP 的只读 `DEVPKEY_Device_ContainerId`；
- `build-audio-endpoint.ps1` 只在找到恰好一个唯一 XM5 Container ID 时自动生成 `nativeldac_remote_container.h`；没有目标设备或结果不唯一时拒绝构建，也可在人工核验后显式传入 `-RemoteContainerId`；
- 安装会新增一个 Windows 音频输出设备，属于系统级变更；
- 只有在用户明确确认后才运行安装脚本；
- 当前包仅用于 TESTSIGNING 环境和本机验证。

## 上游

- Source: https://github.com/microsoft/Windows-driver-samples/tree/main/audio/simpleaudiosample
- Revision: `2ee527bfeb0aeb6be11f0a8b6dce4011b358ce89`
- License: [Microsoft Public License](MS-PL.txt)
