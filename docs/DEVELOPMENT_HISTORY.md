# Development History (Public Snapshot)

This is the sanitized engineering log for the public source snapshot. Local machine paths, Bluetooth addresses, host/user names, generated INF names, certificate fingerprints and raw hardware artifacts are intentionally omitted.

## 1. Architecture

The project combines a KMDF Bluetooth profile transport, a PortCls/WaveRT render endpoint, a user-mode PCM/LDAC engine, protocol libraries, diagnostics and a small Tk UI. The transport uses the Windows Bluetooth stack and exposes bounded, cancellable IOCTL operations for AVDTP signaling and media writes. The endpoint exposes a bounded PCM ring with stream epochs and active-format snapshots.

## 2. Transport and codec milestones

- Implemented AVDTP source state handling, LDAC capability parsing and RTP/LDAC packetization with MTU checks.
- Implemented asynchronous signaling/media L2CAP transport with cancellation, timeout and normal `SUSPEND -> CLOSE` cleanup.
- Integrated a fixed libldac revision under the documented third-party licenses.
- Verified MQ/SQ/HQ streams, controlled tone playback, system audio capture, endpoint bridge playback and conservative write-latency ABR behavior on the target hardware path.

## 3. Endpoint and audio milestones

- Added a WaveRT render endpoint with a bounded 250 ms PCM ring, stream epoch, active/idle state and written/read/dropped counters.
- Added float32, 16-bit and 24-valid-bit conversion into the LDAC encoder input path.
- Added Windows shared-mode format control and readback. Format changes are treated as transactions: set preferred format, set shared-mode format, verify the active stream, and restore both layers on exit.
- Identified the 24-bit failure boundary: a packed three-byte Windows container did not cause the active shared-mode stream to reopen. The frozen fix uses a 32-bit container with 24 valid bits while preserving the public 24-bit semantic.

## 4. Daily gate and VolumeSync milestones

- Added bounded HQ/SQ/MQ daily configuration and a fixed-quality gate that runs exactly one quality tier per invocation.
- Forwarded requested sample rate and bit depth through the daily host, agent, worker and PCM source. The worker waits for an active snapshot that matches before opening the transport.
- Added a refresh transition for the case where the preferred format already equals the requested value but the active stream is stale.
- Preserved the existing 16-bit baseline, VolumeSync, AVRCP observation, cleanup and restore behavior.

## 5. Hardware evidence

The strongest frozen evidence is the 44.1 kHz/16-bit/HQ/stereo daily run with VolumeSync. Earlier work also recorded a 96 kHz/24-bit short trial after the endpoint and transport lifecycle were rebuilt, but that result is retained as historical evidence rather than a current release gate. The latest 24-bit container fix has offline and policy coverage; a fresh hardware pass is intentionally left for the next development session.

## 6. Recent source changes

- `2344307`: parameterized daily quality gate.
- `7e6f21b`: aligned the HQ 24-bit gate with the daily lifecycle.
- `900c501`: added shared-mode endpoint format control.
- `27b5098`: enforced active PCM format matching.
- `276ac60`: retriggered stale active-stream format transitions.
- `5866358`: switched 24-bit to the Windows-compatible container representation.

## 7. Pause point

The repository is frozen at the current source baseline. The next experiment is deliberately small: rebuild a clean candidate, repeat the known 16-bit baseline, then run 24-bit and require transport-result/PCM-snapshot evidence. No driver or protocol redesign is part of this snapshot.

## 8. Public snapshot preparation

The publication pass replaced hard-coded Bluetooth addresses and real Container IDs with runtime discovery or synthetic fixtures. The ACL watcher now resolves the unique paired `WH-1000XM5` address before filtering HCI events; hardware recovery scripts use the paired-device registry rather than a checked-in address. Public documentation was condensed around the actual architecture, verified baseline, 24-bit boundary and remaining evidence.

Release verification completed with a successful full build, Python tests 58/58, PowerShell parsing for 252 scripts, and 205 passing CTest entries when the existing `v1_engine_ready_host_tests` stress timeout was excluded. That test remains documented as a harness/concurrency risk instead of being hidden by a timeout increase.
