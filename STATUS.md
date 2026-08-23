# Project Status

Snapshot date: 2026-08-23
Frozen source baseline: `5866358` (`fix: use Windows-compatible 24-bit PCM container`)
Working tree policy: source and documentation only; build output, hardware artifacts, and local recovery material are excluded.

## Verified

- AVDTP capability discovery, LDAC configuration, signaling/media channel setup, START, SUSPEND and CLOSE on the target headset path.
- MQ, SQ and HQ encoder configuration with real transport telemetry; conservative media-write ABR logic.
- Native WaveRT render endpoint, bounded PCM ring, stream epoch and active-format snapshots.
- `44100/16/HQ/stereo + VolumeSync` fixed daily gate with non-zero media evidence and normal cleanup.
- Daily quality configuration, endpoint format transaction, shared-mode format readback, and restoration of the original format.
- Release builds, CTest, PowerShell policy/parser tests and Python UI/parser tests at the latest development checkpoints.

## Implemented, hardware retest pending

- 24-bit PCM is represented as Windows `32-bit container + 24 valid bits`; the endpoint table, PCM reader, format control and gates agree on that contract.
- The next required hardware check is a clean candidate rebuild followed by the existing 16-bit baseline and then a 24-bit run. A successful `Applied` line alone is insufficient; the transport result must report the requested active PCM format.
- 44.1/48/88.2/96 kHz and mono/dual are present in protocol and offline format paths. Per-mode hardware evidence is not complete.

## Deferred

- Production service installation and automatic default-device management.
- Sleep/wake, long soak, recovery across all endpoint generations, and final product UI parity.

## Public snapshot verification

- Full Release build completed successfully.
- 205 CTest entries passed when the pre-existing `v1_engine_ready_host_tests` stress case was excluded; the real-endpoint-dependent `native_pcm_source_tests` entry was skipped by design.
- `v1_engine_ready_host_tests` still times out at its 20-second multi-generation stress limit in this environment and is recorded as an unresolved test-harness risk.
- Python UI/parser tests passed 58/58; PowerShell parser validation passed for 252 files.
- Tracked-file scans found none of the audited local user/host paths, device addresses, container identifiers or signing fingerprints.

The repository is paused at this snapshot. No new feature work is implied by this document.

The public archive was re-sanitized before local teardown. Published INF names
in tests use synthetic `oem9xxx.inf` fixtures; the public Git history contains
no earlier machine-specific source history.
