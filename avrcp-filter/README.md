# XM5 AVRCP I/O upper filter

This directory contains an offline-only KMDF upper-filter candidate for the
single Sony WH-1000XM5 AVRCP `0x110E` PDO.

The extension INF keeps `Microsoft_Bluetooth_AvrcpTransport` as the function
driver and adds `NativeLdacAvrcpIoFilter` at the device upper-filter position.
The filter forwards normal and internal device-control requests to the lower
Microsoft stack unchanged. It records bounded request/completion metadata and
up to 32 bytes of input or output prefix in a ring buffer.

The separate `\\.\NativeLdacAvrcpIoFilter` control device exposes version,
status, dequeue, and one narrowly scoped write IOCTL. The write accepts only
an absolute volume value in the AVRCP `0..127` range and submits one observed
Microsoft private-layout `SetAbsoluteVolume` request to the existing lower
transport. It does not open a second AVCTP channel, switch the function driver,
control media keys, touch audio data, or call private MPM exports directly.

Build the offline package from clean Git HEAD with:

```powershell
pwsh.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\build-v1-avrcp-filter-candidate.ps1
```

The builder does not stage, install, bind, restart, or load the driver.

After the offline candidate has been rebuilt from clean Git HEAD, the bounded
live gate is:

```powershell
pwsh.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\run-v1-avrcp-filter-install-gate.ps1 `
  -ConfirmV1AvrcpFilterInstall
```

The gate requires XM5 to be off, keeps the Microsoft function driver, waits
until both the ACL watcher and trace probe are visibly armed, observes only
new post-connect requests, then asks for physical power-off before removing
the filter package. It can issue at most one restart of the exact AVRCP PDO.

If a trial is interrupted or ends with `rollback-required`, turn off XM5 and
run:

```powershell
pwsh.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\rollback-v1-avrcp-filter-install-gate.ps1 `
  -ConfirmV1AvrcpFilterRollback
```
