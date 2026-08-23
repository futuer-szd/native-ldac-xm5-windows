# Transport ABI 0.5

这个 ABI 只面向本机管理员服务。设备接口由 `GUID_DEVINTERFACE_LDAC_NATIVE_TRANSPORT` 标识，设备对象通过 SDDL 限制为 SYSTEM 和 Administrators，当前只允许一个文件句柄。

## 生命周期

1. `GET_VERSION`，要求 major 为 `0`、minor 至少为 `3`；
2. `GET_DEVICE_INFO`，确认 profile、远端地址和本地地址三个 ready flag；
3. `OPEN_SIGNALING`；
4. 交替提交 `WRITE_SIGNALING` 与 `READ_SIGNALING`；
5. AVDTP `OPEN` 被接受后调用 `OPEN_MEDIA`；
6. AVDTP `START` 被接受后调用 `WRITE_MEDIA`；
7. L2CAP OPEN 或传输出错时分别调用 `GET_OPEN_DIAGNOSTICS` 或 `GET_TRANSFER_DIAGNOSTICS`；
8. `CLOSE_CHANNELS`。

打开、读取和写入都可以使用 overlapped `DeviceIoControl`。调用方可以用 `CancelIoEx` 取消。open/write 具有驱动侧超时；read 按 BthEcho 模式保持挂起，直到数据到达或调用方取消，避免 BthPort 在超时边界丢弃已消费数据。AVDTP 必须允许先挂起 read、再并行提交对应 write，因此默认 IOCTL queue 保持 parallel；每个 signaling 方向及 media write 各自只允许一个 pending request。进程退出、句柄关闭或 PnP stop 时，驱动在 PASSIVE_LEVEL operation lock 下拒绝新请求，取消并等待 signaling open/read/write 与 media open/write 全部完成，再关闭 channel。设备 cleanup 最后且仅一次释放 BthPort profile interface 引用。

## 缓冲区约定

### `IOCTL_LDAC_NATIVE_OPEN_SIGNALING`

- `lpInBuffer`：可选的 `LDAC_NATIVE_OPEN_SIGNALING_REQUEST`；传空使用 10 秒和 672-byte MTU；
- `lpOutBuffer`：`LDAC_NATIVE_CHANNEL_INFO`；
- 方法：`METHOD_BUFFERED`。

成功后返回 signaling PSM `0x0019` 以及 BthPort 实际协商的 inbound/outbound MTU。

### `IOCTL_LDAC_NATIVE_WRITE_SIGNALING`

- `lpInBuffer`：`LDAC_NATIVE_SIGNALING_TRANSFER_REQUEST`；
- `lpOutBuffer`：待发送的 AVDTP packet；
- 方法：`METHOD_IN_DIRECT`。

这里的 Win32 “output buffer” 是提供给 BthPort 读取的 direct-I/O MDL，并不表示数据方向是耳机到主机。packet 不能超过协商的 outbound MTU；同一时刻只允许一个 signaling write。

### `IOCTL_LDAC_NATIVE_READ_SIGNALING`

- `lpInBuffer`：`LDAC_NATIVE_SIGNALING_TRANSFER_REQUEST`；
- `lpOutBuffer`：接收 AVDTP packet 的缓冲区，大小至少为协商的 inbound MTU；
- 方法：`METHOD_OUT_DIRECT`。

成功时 `lpBytesReturned` 是收到的实际字节数。同一时刻只允许一个 signaling read。

read/write BRB 使用驱动拥有的 `NonPagedPoolNx` 中间缓冲区；完成后再在仍有效的 direct-I/O 映射与该缓冲区之间复制。

### `IOCTL_LDAC_NATIVE_OPEN_MEDIA`

- `lpInBuffer`：可选的 `LDAC_NATIVE_OPEN_SIGNALING_REQUEST`；传空使用 10 秒和 672-byte MTU；
- `lpOutBuffer`：`LDAC_NATIVE_CHANNEL_INFO`；
- 方法：`METHOD_BUFFERED`。

要求 signaling channel 已连接。成功后返回第二条 AVDTP Media L2CAP channel 的实际 inbound/outbound MTU；同一文件句柄只允许一条 media channel。

### `IOCTL_LDAC_NATIVE_WRITE_MEDIA`

- `lpInBuffer`：`LDAC_NATIVE_SIGNALING_TRANSFER_REQUEST`；
- `lpOutBuffer`：待发送的 RTP/LDAC media packet；
- 方法：`METHOD_IN_DIRECT`。

单个 packet 不能超过 media outbound MTU 或 4096 bytes；同一时刻只允许一个 media write。驱动把 direct-I/O 数据复制到自有 `NonPagedPoolNx` 缓冲区后提交 BthPort BRB。

### `IOCTL_LDAC_NATIVE_GET_TRANSFER_DIAGNOSTICS`

- `lpInBuffer`：无；
- `lpOutBuffer`：`LDAC_NATIVE_TRANSFER_DIAGNOSTICS`；
- 方法：`METHOD_BUFFERED`。

返回最后一次 signaling read/write 和 media write 的 I/O status、BRB status、Bluetooth status、请求长度、BRB buffer/remaining size 与 flags，供真机调试使用。

### `IOCTL_LDAC_NATIVE_GET_OPEN_DIAGNOSTICS`

- `lpInBuffer`：无；
- `lpOutBuffer`：`LDAC_NATIVE_OPEN_DIAGNOSTICS`；
- 方法：`METHOD_BUFFERED`。

只读返回最后一次 signaling 或 media L2CAP OPEN 的 I/O status、BRB status、Bluetooth status、PSM、channel flags，以及 BthPort 提供的远端 `Response`/`ResponseStatus`。只有 `REMOTE_RESPONSE_VALID` 置位时，后两个字段才解释为远端拒绝原因；当前已知值为 PSM 不支持、安全阻止或无资源。这个 IOCTL 不会打开、关闭或恢复任何蓝牙通道。

### `IOCTL_LDAC_NATIVE_CLOSE_CHANNELS`

无输入和输出。当前同步等待最多 5 秒；已断连时也返回成功。

## 当前边界

- ABI layout 由 `driver/tests/ioctl_abi_tests.c` 检查；
- `tools/transport_probe.c` 是 ABI 0.5 的参考用户态调用方；`--discover` 执行 capability-only 会话，OPEN 失败时读取上述只读诊断，`--media-session` 验证第二条 L2CAP、`OPEN` 与 `START`；
- WDK 编译、代码分析和 INF signability 已通过；
- AX211/BthPort/XM5 已完成 signaling、Media L2CAP、真实 LDAC 媒体包、系统音频、HQ/Auto 和断线重连真机验证；实际媒体会话曾协商 inbound/outbound MTU 1021/895 bytes；
- 本轮 profile interface 释放与完整 transfer rundown 已通过 WDK Release `/W4 /WX`、Driver Minimum Rules、ApiValidator 和 INF signability，但尚未安装到真机；取消压力、休眠/唤醒与 Driver Verifier 仍未验证。
