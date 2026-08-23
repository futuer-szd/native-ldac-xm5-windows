# V1 Connective Sync Handoff (Sanitized)

The V1 connection/lifecycle work is paused in the source snapshot. The public implementation contains the state machines, endpoint presence, VolumeSync integration, worker containment and normal media cleanup. The remaining work is product hardening: reconnect semantics, persisted requested/applied/effective state, sleep/wake recovery and long-run evidence.

Build and test instructions are in the root README and `tools/README.md`. Machine-specific handoff logs, addresses, package names and recovery paths are intentionally excluded.
