# Architecture

## Data path

```text
Windows application
  -> Windows Audio Engine
  -> Native LDAC WaveRT render endpoint
  -> bounded PCM ring / ABI snapshot
  -> user-mode LDAC worker
  -> AVDTP + RTP/LDAC
  -> KMDF Bluetooth profile transport
  -> Windows BthPort / system radio driver
  -> Sony WH-1000XM5
```

## Components

- `protocol/`: portable AVDTP source state, LDAC capability selection and RTP packetization.
- `driver/`: KMDF profile transport for signaling/media channel IOCTLs. It does not replace the radio driver or manage pairing keys.
- `audio-endpoint/`: PortCls/WaveRT render endpoint with dynamic PCM format and a bounded ring buffer.
- `engine/`: PCM conversion, endpoint volume/mute, libldac encoding and pacing.
- `agent/`: presence, render demand, worker containment, daily configuration and VolumeSync lifecycle.
- `ui/`: local control/status surface using the agent's published state and configuration IPC.

## Lifecycles

Quality selection (MQ/SQ/HQ) belongs to the encoder and can be applied without changing the Windows endpoint format. Sample rate, bit depth and channel mode affect PCM and AVDTP configuration, so they require a bounded format transaction and a fresh media session. The safe sequence is `SUSPEND -> CLOSE`, rebuild PCM/WaveRT state, then `OPEN -> START`.

The UI must distinguish:

- requested: saved user preference;
- applied: values written to configuration endpoints;
- effective: values observed in the active PCM snapshot and transport result.

## Failure boundaries

- Setting preferred format does not prove that Windows opened an active stream with that format.
- A driver ABI/readiness query does not prove that AVDTP media started.
- PASS requires structured evidence from the active PCM snapshot and transport result.
- Cleanup favors normal SUSPEND/CLOSE. Forced process termination is only a bounded containment fallback.

## Driver and device changes

Most user configuration changes need only a user-mode media-session rebuild. Endpoint format-table or PCM ABI changes require an endpoint driver update. Bluetooth profile transport changes require its driver update and may require a PnP/device lifecycle rebuild. A full system reboot is reserved for an installed driver object that cannot be replaced safely in the current boot or when Windows explicitly reports reboot required.

Third-party sample provenance and licenses are documented in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).
