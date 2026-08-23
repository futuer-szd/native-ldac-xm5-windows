# HQ 24-bit Gate Handoff (Sanitized)

The fixed gate is the existing daily entry point. Run one quality tier at a time from an elevated PowerShell 7 session:

```powershell
& .\artifacts\v1-daily-host\candidate\run-v1-daily-quality-gate.ps1 `
    -Quality hq `
    -VolumeSync
```

The 24-bit specialization changes only the PCM format transaction and then reuses the same daily lifecycle. It must not inject media keys, start playback automatically or create a second media-control flow.

Acceptance requires all of the following in the structured result: requested active PCM format, real XM5 START, non-zero media packets, matching quality/nominal bitrate, non-silent PCM, zero protocol/backend errors, normal SUSPEND/CLOSE, and restoration of the pre-test format.

The current implementation uses `32-bit container + 24 valid bits` for Windows 24-bit PCM. Offline tests and policy checks pass. A new endpoint candidate and a fresh hardware run are still required before marking 24-bit as PASS.
