# V1 daily logging contract

Each `tools/run-v1-daily-full-cycle.ps1` trial writes two logical logging
layers. The product layer is stable and concise; the development layer is
diagnostic and may be removed or disabled after a root cause is closed.

## Product layer

- `daily-summary.json`: one final, UI-friendly record containing configuration,
  connection/media outcome, audio duration/packet totals, final status, and
  paths to the diagnostic artifacts.
- `daily-host.log`: timestamped human-readable milestones and actionable
  warnings/errors only. Native child output is not copied here.
- `daily-state.json`: live compatibility state written by the presence agent;
  existing consumers may continue to read it while the trial is running.

The product layer must not contain raw AVRCP payloads, per-read PCM snapshots,
or high-rate polling output.

## Development layer

- `daily-events.jsonl`: one JSON object per native host/runner line. Each event
  contains `timestamp`, `elapsed_ms`, `source`, `category`, `event`,
  `severity`, `message`, and `raw`.
- `transport-result.json`: detailed transport evidence, including AVDTP
  exchange state and temporary PCM stream-stop/rebind diagnostics.
- `C:\ProgramData\NativeLdac\handoff-host.log`: elevated handoff host
  timestamps for stage, bind, restore, and PnP failures.

Development events are bounded by the trial lifetime and are intended for
diagnosis/export, not normal UI display. Once a failure is understood, the
verbose PCM fields and event categories may be removed or placed behind a
developer-diagnostics switch; the product summary and simple log contract
remain.

## Status interpretation

- `daily-summary.outcome.status=stopped` means the instance ended through the
  normal stop path and the transport did not report a backend failure.
- `error` means a transport/backend failure, incomplete result, nonzero agent
  exit, or required remote cleanup remained.
- A manually stopped continuous trial may have transport disposition
  `cancelled`; that is expected when the normal cleanup flags are healthy.
