# Tools

The `tools/` directory contains build, probe, candidate-validation, hardware-gate and recovery scripts. Public source includes the scripts; generated packages and local evidence stay under ignored `artifacts/` directories.

## Safe read-only entry points

```powershell
pwsh -NoProfile -File .\tools\probe-native-stack.ps1
.\build\protocol\Release\transport_probe.exe --info
.\build\protocol\Release\audio_endpoint_probe.exe --scan-interfaces
.\build\protocol\Release\audio_endpoint_probe.exe --format
```

These checks enumerate the target profile/endpoint and query published ABIs. They do not install a driver, modify PnP state or start a media session.

## Builds

- `build-audio-endpoint.ps1`: build and stage the WaveRT endpoint package.
- `build-v1-daily-host-candidate.ps1`: build the daily host/worker/UI candidate and hash manifest.
- `build-legacy-candidate.ps1`: stage the split transport candidate for controlled development recovery.
- `build-direct-pdo-candidate.ps1`: stage the experimental Direct-PDO candidate; this path remains frozen for ordinary use.

Build scripts write only ignored output. Driver installation and endpoint updates are separate administrator operations with explicit confirmation switches.

## Daily quality gate

Run exactly one tier per invocation from an elevated PowerShell 7 session:

```powershell
& .\artifacts\v1-daily-host\candidate\run-v1-daily-quality-gate.ps1 `
    -Quality hq `
    -VolumeSync
```

The same entry supports `sq` and `mq`. Optional format parameters are forwarded through the daily host and worker. A run passes only when the structured transport result contains the requested active PCM format, real START, non-zero media packets, matching quality/bitrate and normal cleanup. Preferred/shared-format output alone is not sufficient.

## Format transaction

`endpoint_format_control.exe` and `run-v1-daily-quality-gate.ps1` manage both Native preferred format and Windows shared-mode device format. The gate records original, requested, applied and restored values. Windows 24-bit PCM uses a 32-bit container with 24 valid bits.

## Recovery and destructive operations

Scripts that install, update, remove, restart or roll back drivers are fail-closed and require administrator privileges plus a dedicated confirmation switch. Before running one:

1. Stop all LDAC agents, workers and probes.
2. Verify the exact target device and current binding with read-only commands.
3. Preserve the existing rollback package outside the public repository.
4. Review `SupportsShouldProcess` output and generated transaction state.

Never delete an unknown `oem*.inf` by guesswork. Published INF names are machine-generated and must be resolved from the current transaction or device state.

## Evidence handling

Raw event traces, device instance IDs, Bluetooth addresses, Driver Store names, signing fingerprints and user paths are local evidence. Do not add them to Git. Publish only a sanitized summary of the pass criteria and outcome.
