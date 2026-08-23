# Public Release Checklist

## Included

- C/C++ protocol, driver, endpoint, engine and agent source.
- PowerShell build, test, candidate-validation and recovery policy scripts.
- Tk UI and parser tests.
- Architecture, protocol ABI, license and sanitized development documents.
- Third-party notices and source revisions.

## Excluded

- `build/`, `artifacts/`, `tmp/`, local reference material and generated packages.
- Raw transport logs, daily JSONL/JSON evidence, Driver Store backups and signing material.
- Personal paths, host/user names, Bluetooth addresses, real generated INF names and certificate fingerprints.

All `oem9xxx.inf` values in tests and examples are synthetic fixtures. Runtime
tools discover the installed published INF instead of relying on those values.

## Frozen acceptance statement

The 44.1 kHz/16-bit/HQ/stereo + VolumeSync path is the preserved hardware baseline. The 24-bit Windows container fix is included as source and offline-tested code, but requires a fresh hardware gate after endpoint update. Other format/channel combinations remain staged for later evidence.

## Review coverage

The local Codex workspace was searched for all 37 sessions associated with this project. The review covered the early transport/driver work, endpoint and PCM ABI work, VolumeSync integration, daily quality gates and the final 24-bit format handoff. Session transcripts and raw hardware outputs are not part of the public snapshot.

## Verification result

- Release build: passed.
- CTest: 205 entries passed with the known `v1_engine_ready_host_tests` stress timeout excluded; one real-endpoint test skipped by design.
- Python UI/parser tests: 58/58 passed.
- PowerShell parse validation: 252 files passed.
- Sensitive-string audit: no audited personal paths, host/user names, target device addresses, real Container IDs, real generated INF names or signing fingerprints remain in tracked files.
