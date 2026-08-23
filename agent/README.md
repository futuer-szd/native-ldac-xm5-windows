# Native LDAC user-session agent

V1 is being rebuilt as an event-driven user-session watcher. The committed
`V1LifecycleState` contract separates physical ACL presence, WaveRT render
demand, engine readiness, and the media session. A real XM5 ACL connect may
publish the endpoint, but cannot start the engine or open AVDTP. WaveRT RUN may
start the contained engine, but only an explicit engine-ready lease can issue
the single OPEN allowed for that ACL generation. Remote disconnect and watcher
lease expiry fail-muted, invalidate the generation, stop the child, and unplug
the endpoint without attempting a Bluetooth exchange on a lost link.

The current `ldac_agent.exe` remains the previous no-console C++ supervisor
until this reducer and the shared ACL event parser are wired into its hidden
window. Installed Direct-PDO mode and Direct-PDO hardware trials are retired
from the V1 path. Bounded legacy hardware trials still use
`transport_probe.exe --play-endpoint`; all modes default to LDAC HQ.

`v1_presence_agent.exe` is the first bounded V1 integration. It owns a
message-only window and the reusable `Xm5AclWatcher`, consumes only exact XM5
ACL connect/disconnect notifications, and feeds them into the V1 reducer. It
does not query `fConnected`, create a child process, inspect WaveRT, or open a
Bluetooth transport. This mode exists to validate the listener/reducer boundary
before endpoint or media actions are connected.

Daily mode now also owns a read-only AVRCP observer session boundary. Only
after its contained PCM worker reports `MediaStarted` does it enumerate the
exact observer interface, verify ABI 0.11, and request the one permitted
`BEGIN_OBSERVATION` for that observation session. The driver acquires the BTH
profile only then, so a persistent PDO cannot retain it across physical ACL
generations. The host drains driver events
through the existing mapper while a media-scoped lease is active and revokes
that lease on media stop or ACL loss. Interface absence, an ABI mismatch, or an
observer poll error is recorded without stopping the LDAC media session. This
increment remains ObserveOnly; Core Audio, input, and `SEND_COMMAND` writes are
intentionally deferred until the resident timing gate passes.

The next optional `--endpoint-presence` mode opens only the Native PCM KS
property set and writes `PhysicalPresence`; it cannot write media `LinkState`,
read PCM, or open Bluetooth. Exact ACL connect publishes a generation-bound
presence lease, refreshed every five seconds while online. Exact disconnect
publishes absent immediately. The driver expires the lease after fifteen
seconds if the listener crashes. WaveRT Jack state is now derived only from
this lease; media `LinkState` remains the real Bluetooth-session contract.
PCM reader ownership is a third, independent, process/generation-bound lease,
so a contained engine can consume WaveRT without claiming that AVDTP is
connected.

The bounded `--observe-render-demand` mode adds a GET-only PCM Info observer.
It is dormant while ACL presence is absent and polls at 250 ms only while the
XM5 is present. Two equal samples are required before publishing WaveRT
RUN/STOP, so a single transient cannot become render demand. Reducer engine
actions are counted but deliberately not executed in this gate: PCM Read,
child creation, media LinkState writes, and Bluetooth OPEN remain unavailable.

The next bounded `--observe-engine-ready --engine-stub <path>` mode executes
only the engine-process lease. The stub is created suspended, assigned to a
kill-on-close Job Object, then resumed; it opens two named Events, publishes
ready, and waits for stop. It has no PCM, codec, SetupAPI, or Bluetooth code.
Engine ready is reduced normally so the pending transport OPEN can be counted,
then `TransportOpenSuppressed` clears that unexecuted media intent while keeping
the engine lease ready. WaveRT STOP, ACL disconnect, lease expiry, or agent exit
stops or contains the child. `transport_open_executed` remains zero in this
gate.

`v1_pcm_encode_engine.exe` is the following dry-engine candidate. It accepts
the same ready/stop Event contract, opens the Native PCM source only after
WaveRT RUN, acquires the independent PCM consumer lease, creates an HQ stereo
LDAC encoder for the active sample rate, and
signals ready only after one complete PCM read and successful encoded frame.
Encoded bytes stay in a local buffer and are discarded. The executable does
not link the protocol, RTP, Direct-PDO media sink, or Bluetooth libraries and
does not write media `LinkState`.

The next boundary extends the same contained child host with an optional
event-only transport-worker contract. The parent creates private named Events
for one OPEN authorization, media started/stopped/failed acknowledgements, and
distinct graceful-stop versus cancel requests before the child is resumed.
Only one authorization is accepted for an ACL generation. Normal WaveRT STOP
requests graceful transport shutdown; ACL disconnect and media failure request
local cancellation, so a lost link cannot be mistaken for permission to send
SUSPEND/CLOSE. The current `v1_transport_worker_stub.exe` implements only this
Event protocol and has no PCM, driver IO, AVDTP, or Bluetooth code. It exists to
validate containment and lifecycle wiring before a real transport worker is
introduced.

The capability-only transport extraction is separate from that lifecycle
stub. `v1_transport_session` owns one bounded DISCOVER/GET_CAPABILITIES flow
against an injected backend. It performs one signaling OPEN attempt, has no
retry or sleep path, stops before SET_CONFIGURATION, and always requests local
channel cleanup after a successful OPEN. Its mock suite covers legacy
GET_CAPABILITIES fallback, non-LDAC endpoints, wrong labels, remote rejects,
backend failures, cleanup failure, and cancellation during pending exchange.
`V1TransportDriverBackend` is the compile-verified ABI 0.5 adapter: it validates
one unique interface, driver version and addresses, submits signaling READ
before WRITE with overlapped cancellation, and exposes no media-channel or
media-write operation. It is used only by the contained capability worker;
hardware execution remains behind the transactional ownership gate.

The next pre-stream layer is a separate zero-packet configuration target.
`V1TransportConfigurationSession` drives the shared AVDTP source only through
capability discovery, SET_CONFIGURATION, AVDTP OPEN, Media L2CAP OPEN, and an
immediate AVDTP CLOSE followed by local channel cleanup. It never calls START
or links PCM, encoder, RTP, or media-write code. Its production backend adds
only `IOCTL_LDAC_NATIVE_OPEN_MEDIA` in a separate library, so the earlier
capability-only worker remains unable to open media. The parent watcher keeps
the same exact ACL generation, RenderDemand, contained-child, and bounded
OpenSignaling Win32 71 recovery policy.

That ownership decision is now explicit: the product architecture keeps
`LdacNative` as the persistent XM5 A2DP Sink function driver and retains
the previously installed profile driver only as a rollback package. The first hardware gate is intentionally
temporary. `v1_transport_discovery_worker.exe` runs the capability-only core in
the same Job-contained Event contract, publishes a distinct
`CapabilitiesDiscovered` event, writes an atomic session result, and never
publishes `MediaStarted`. The discovery-only agent mode suppresses the pending
media OPEN after that event, requests local cancellation, requires the worker's
stopped acknowledgement, and then waits for the real ACL disconnect.

Current responsibilities:

- enforce one agent instance with a Local mutex;
- request graceful probe shutdown through a per-generation named Event;
- create the probe suspended, place it in a kill-on-close Job Object, then resume it so an abnormal agent exit cannot leave an orphan media process;
- constrain each legacy probe generation to one signaling open attempt and
  keep a 30-second cooldown after media/protocol failure;
- in installed mode, require both the public Windows Bluetooth connected state
  and a Direct-PDO audio interface reporting its PnP transport ready, then allow
  three seconds for the profile stack to settle;
- register for Native LDAC transport device-interface changes through the
  hidden Windows message window; while XM5 is absent, wait on that event and a
  15-second safety watchdog instead of polling every two seconds;
- register for audio-interface changes and, in installed mode, query the
  Native endpoint's WaveRT state at 500 ms only after XM5 and its transport are
  ready;
- keep the media/LDAC child process and encoder absent until the presence gates
  pass and the Native endpoint has actually entered KSSTATE_RUN;
- apply the same presence/settle gate to the bounded real-hardware trial while
  keeping fake-probe integration tests independent of Bluetooth state;
- reset the backoff after a session remains alive for 30 seconds;
- keep agent state and raw probe telemetry in separate rotating logs (1 MiB and 8 MiB, three backups each);
- publish a small UTF-8 JSON state snapshot through atomic file replacement;
- read `%LOCALAPPDATA%\NativeLdac\config.json` in installed mode and apply
  versioned enable, quality, channel-mode, sample-rate, and bit-depth settings;
- poll config at 500 ms while streaming; changes request graceful probe
  shutdown and start a new generation with the updated media format;
- accept `mq`, `sq`, `hq`, or `auto` as the startup quality.
- listen for Windows session-end messages through a hidden window and request the same graceful shutdown during logoff or shutdown.

The Direct-PDO engine opens the PCM source and generation-bound Media sink on
the same KS interface. It creates libldac only after WaveRT RUN and AVDTP START,
paces RTP packets from the PCM clock, and exits when WaveRT leaves RUN. The
driver rejects stale generations, non-streaming writes, and packets larger than
the negotiated MTU. The first UI control channel remains an atomic, versioned
config/state pair. HFP audio switching is outside the current self-use scope.

Direct-PDO trial mode is deliberately bounded and cannot impersonate an
installed agent. It requires an explicit duration, independent instance/log/
state paths, and the adjacent staged Direct engine; it rejects `--probe`,
configuration files, `--once`, protected-install paths, and unbounded runs.
The trial uses the same XM5 presence, transport readiness, WaveRT RUN, media
fault, and generation-bound recovery gates as installed mode.

The transport driver currently grants access only to SYSTEM and Administrators. The supplied installer therefore registers a highest-run-level logon task for the current interactive user instead of using a normal unelevated Run key.
