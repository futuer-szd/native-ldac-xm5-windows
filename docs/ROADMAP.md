# Roadmap

This roadmap describes work after the paused public snapshot. It is not a promise that any item is already implemented.

## Phase 1: resume the frozen gate

1. Build a clean Release candidate from the frozen source.
2. Run the known `44100/16/HQ/stereo` baseline.
3. Run `44100/24/HQ/stereo` and require active PCM snapshot, transport result, non-zero media packets, matching HQ telemetry and normal cleanup.
4. Confirm both endpoint format layers are restored.

## Phase 2: format matrix

Test one dimension at a time: 44.1/48/88.2/96 kHz, 16/24 valid bits, and stereo/dual/mono. Sample-rate, bit-depth and channel changes require a media-session boundary and a fresh PCM/WaveRT stream; HQ/SQ/MQ quality changes can remain within an active media session if the encoder contract permits it.

## Phase 3: product lifecycle

- Add a transactional UI model with requested, applied and effective state shown separately.
- Make “Reconnect device” explicitly rebuild the PCM/audio session and, only when required, the Bluetooth transport generation.
- Add persistence, rollback and failure-safe restoration for saved parameters.
- Integrate bounded automatic quality reduction from measured media-write pressure.

## Phase 4: release hardening

- Exercise pause/resume, default-device changes, sleep/wake, Bluetooth reconnect and long soak.
- Validate driver install/update/rollback on a clean test machine.
- Publish only source, build metadata, licenses and sanitized evidence summaries.
