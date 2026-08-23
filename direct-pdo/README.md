# Native LDAC direct-PDO prototype

This directory contains the non-installable migration core for merging the
PortCls render endpoint and BthPort AVDTP transport on the XM5 A2DP service
PDO.

`NativeLdacDirectPdoCore.vcxproj` produces only a kernel-compatible static
library. It intentionally contains no INF, CAT, SYS, service registration,
hardware ID, or package project. The same lifecycle contract is compiled by
CMake into a host test so asynchronous PortCls/AVDTP transitions can be
validated before any driver binding exists.

The contract maps desired PortCls states to one serialized transport action:

- acquire/pause: AVDTP session open, media suspended;
- run: AVDTP session open and streaming;
- stop: suspend if required, then close;
- PnP stop/remove: cancel pending work and close before going offline;
- transport failure: fail closed until an explicit retry creates a new
  generation.

Build the non-installable artifact with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\build-direct-pdo-core.ps1 -Configuration Release
```

The PortCls prototype now consumes this library and queries
`GUID_BTHDDI_PROFILE_DRIVER_INTERFACE` from the same A2DP PDO during adapter
initialization. It validates the returned BthPort function table, retains the
provider reference while the adapter is active, and releases that reference
exactly once during stop/remove cleanup. A separate host-testable ownership
contract covers failed queries, stop while a query is pending, retry, and
stale completion generations.

Build the link/static-inspection prototype with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\build-direct-pdo-prototype.ps1 -Configuration Release
```

It emits a distinctly named SYS for link/static inspection, but deliberately
has no INF, CAT, certificate, hardware ID, service name, or install script.
During adapter initialization the SYS retrieves the remote device address with
`IOCTL_INTERNAL_BTHENUM_GET_DEVINFO`, allocates
`BRB_HCI_GET_LOCAL_BD_ADDR` for the local radio address, and retains a
referenced target PDO. Its synchronous internal-request wrapper owns its IRP,
cancels on a bounded timeout, waits for the completion routine, and only then
frees the IRP/BRB. Address discovery has a separate host-tested generation
contract and fails closed if either address is unavailable or invalid.

The prototype also contains a dormant asynchronous AVDTP signaling-channel
skeleton for PSM `0x0019`. It preallocates the close BRB before submitting
`BRB_L2CA_OPEN_ENHANCED_CHANNEL`; the IRP completion routine only captures the
result and queues a PASSIVE_LEVEL worker. That worker is the sole owner allowed
to transfer the channel and close BRB into the adapter context. PnP stop or an
explicit close can cancel a pending open, but still waits for completion and
closes a channel if the lower driver won the cancel race. A generation contract
rejects stale completions, and teardown does not release the BthPort interface
or referenced PDO until the request object is fully drained.

The prototype now routes render-pin KSSTATE intent through a host-tested action
dispatcher. Exactly one reusable work item owns backend execution at a time;
PortCls callbacks only update intent and queue work. The PASSIVE_LEVEL worker
owns the complete AVDTP source lifecycle: `DISCOVER`, capability selection,
LDAC `SET_CONFIGURATION`, `OPEN`, a second L2CAP channel for Media, `START`,
`SUSPEND`, and `CLOSE`. Signaling and Media have independent channel contexts;
PnP stop cancels pending work, drains both contexts, and only then releases the
BthPort provider and PDO reference. The direct prototype remains
non-installable.

A separate kernel-compatible transaction helper constructs the two-byte AVDTP
`DISCOVER` command header and validates a single-packet response by generation,
transaction label, signal ID, packet type, and message type. The dispatcher
uses the bounded signaling read/write path, while the helper remains covered by
offline tests. The SYS must not be installed.

That lifetime work now has two host-tested contracts. The indication owner
keeps a generation-tagged allocation alive until both its local owner and all
BthPort references are released; teardown disarms remote notifications before
dropping the local reference, and stale or duplicate callbacks cannot mutate a
new generation. The transfer owner permits one read and one write concurrently,
returns an explicit cancellation mask on close/remote disconnect, and rejects
late completions from an older channel generation. These contracts are now used
by a deliberately serial signaling runtime. Its read/write entry points
allocate a nonpaged transfer buffer and `BRB_L2CA_ACL_TRANSFER` for each
operation, use the existing bounded completion-and-cancel wrapper, validate
`RemainingBufferSize`, then free both objects only after the request is no
longer owned by BthPort. Close and PnP stop take the same mutex, so they wait
for at most one bounded transfer before closing the channel. The entry points
are used by the render dispatcher for bounded AVDTP exchanges. The prototype
now retains each successful enhanced-channel request for the channel lifetime
and registers `CALLBACK_DISCONNECT`. BthPort reference indications protect the
per-channel callback context; a matching remote disconnect invalidates the
channel generation, rejects later transfers, and queues the same serialized
cancel-and-close path used by other transport failures. Stale and duplicate
disconnects cannot mutate a newer channel.

Streaming also has an implicit media heartbeat. Every accepted generation-bound
packet refreshes a three-second watchdog; if the user-mode engine disappears or
stops writing, the dispatcher invalidates that generation, rejects new media,
closes only the owned Signaling/Media channels, and publishes the endpoint as
unplugged. The watchdog is disarmed outside KS `RUN`, so idle and merely
connected devices have no periodic watchdog activity. Media status ABI v2 now
publishes the fault reason and session generation. Its write-only recovery
property accepts only the exact failed generation/reason after KS has reached
`STOP`, the dispatcher is idle, and signaling ownership has been released. An
accepted request only resets the local contract to closed and publishes a new
generation; it does not submit Bluetooth `OPEN`. A later WaveRT `RUN` is still
the sole trigger for a new AVDTP session.

The installed agent uses this recovery property conservatively. A media
watchdog timeout can arm a local idle recovery; remote-disconnect and backend
faults additionally require the agent to observe an XM5 disconnected-to-
connected edge. Recovery attempts are awakened by Windows device events with a
bounded local-state fallback and never invoke the diagnostic `DISCOVER` path.
This prevents a stale Windows connected flag during headphone shutdown from
turning a failed generation into a Bluetooth reconnect loop.

The signaling owner now also exposes a one-shot `NldBthSignalingDiscover`
composition. It builds exactly one two-byte command, holds the operation mutex
across one write and one short read, then validates the response with the
host-tested transaction helper. The default per-operation timeout is two
seconds. The normal render dispatcher uses the lower-level bounded read/write
operations with the full protocol state machine; this one-shot helper remains
reserved for the isolated diagnostic worker.

The explicit diagnostic lifecycle is also modeled independently from the
normal render-pin dispatcher. PnP start remains idle until a discover request
is made; a request can execute only `OPEN -> DISCOVER -> CLOSE`. A failed
discover still schedules close, stale generations cannot complete a newer
request, and PnP stop replaces queued work or cancels the active backend before
`cancel-and-close`.

That contract is now backed by a reusable kernel work item in the
non-installable PortCls prototype. The runtime owns a 4 KiB nonpaged response
buffer and publishes only status metadata plus at most the first 32 response
bytes in a read-only snapshot. The PortCls prototype exposes that fixed
128-byte snapshot through a GET/BASICSUPPORT-only KS property. Adapter teardown
stops this diagnostic worker before the normal render dispatcher, signaling
owner, profile interface, and PDO are released. There is deliberately no IOCTL,
SET/execute property, dispatcher action, UI, or user-mode caller for
`RequestDiscover`, so loading the prototype
cannot queue the worker or send AVDTP traffic. Before any trigger is added, the
diagnostic runtime and normal render dispatcher require exclusive ownership of
their shared signaling context through the arbiter described below.

The shared arbiter is now implemented as a host-tested generation contract and
a spin-lock-protected kernel runtime. It permits exactly one owner: render,
diagnostic, or none. Diagnostic acquisition is rejected while render demand is
present; new render demand while diagnostic work owns the channel reports an
explicit preemption requirement and prevents another diagnostic request.
Render ownership is retained until the signaling channel is confirmed closed,
and diagnostic ownership spans its complete open/discover/close sequence. PnP
cleanup stops both clients before forcing the arbiter offline. No external
diagnostic trigger exists yet, so this arbitration changes no Bluetooth
behavior in the prototype.

The next internal layer is a fixed v1 diagnostic control ABI. Its 32-byte
request, 32-byte response, and 128-byte snapshot use explicit size/version
fields, separate query/execute access, reserved-field validation, and an
explicit-request flag for one bounded `DISCOVER`. The kernel facade combines
diagnostic and arbiter snapshots and refuses requests while render owns or
demands signaling, while PnP is stopping, or while another diagnostic action
is active. `NldDirectPdoControlExecute` intentionally has no caller; the
diagnostic snapshot property remains read-only and exposes no diagnostic
request structure, IOCTL, INF, UI, or user-mode execute path.

The render dispatcher now also owns a host-tested preemption controller. When
its PASSIVE worker observes diagnostic ownership, it requests a recoverable
diagnostic cancel-and-close, waits for the signaling channel and diagnostic
arbiter generation to be released, reacquires render ownership, and retries
the original render open. Generation checks reject stale completions; cancel,
close, timeout, and reacquisition failures remain fail-closed. KS callbacks do
not block on this sequence. Because the execute facade still has no caller,
the prototype cannot start a diagnostic action; reading the snapshot cannot
open L2CAP or send an AVDTP command.

The render path now has a separate fixed v1 Media ABI. A read-only status
property publishes PnP/RUN/START state, the current Media-channel generation,
and negotiated outgoing MTU. Its write-only packet property accepts exactly one
RTP/LDAC packet only when all of those gates still match. The generation is
validated again while holding the Media operation mutex, closing the race with
SUSPEND, CLOSE, or a newly opened channel. Oversized, stale, reserved-field, and
non-streaming requests fail before a BthPort transfer is submitted.

`ldac_direct_engine.exe` locates the KS interface that exposes both PCM and
Direct-PDO Media properties, waits for WaveRT RUN plus AVDTP START, and only then
creates libldac. It packetizes and paces RTP in user mode, submits each packet
through the generation-bound property, and destroys the encoder when WaveRT
leaves RUN. The legacy test transport remains separate and is not used by the
installed Direct-PDO agent path.

The coordinated validation bundle builder stages the prototype SYS, agent,
Direct engine, and read-only endpoint probe with one source commit, explicit
PCM/format/link/media ABI versions, and SHA-256 hashes. It fails if the source
tree is dirty by default or if an INF, CAT, certificate, hardware ID, or other
installable material enters the bundle. `audio_endpoint_probe --direct-status`
queries Media ABI v3 without changing state. Runtime readiness and bounded
Direct-PDO trial scripts require an already installed matching ABI; they do
not install this prototype. The crash-recovery variant terminates only its own
Job-contained agent/engine, verifies the three-second media timeout, and
requires an exact generation-bound idle recovery before starting a second
bounded session.

An installable candidate is now produced under a separate build property and
identity: `NativeLdacDirectPdo.sys`, service `NativeLdacDirectPdo`, and the
exact tested XM5 A2DP Sink hardware ID. Its INF combines the PortCls WaveRT/
topology registrations with the Bluetooth PDO match. The WDK package build
runs ApiValidator and Inf2Cat signability, test-signs SYS and CAT, and stages
the coordinated agent, Direct engine, and status probe with hashes. The
candidate artifact is marked staged-only and contains no installer. A separate
transactional installer requires the exact healthy PDO, matching XM5 Container
ID, TESTSIGNING, an exported original non-LDAC rollback INF, and no active LDAC
task/process. It records state before mutation, binds the package, and accepts
the installation only when the same KS interface exposes PCM ABI v2 and
Direct-PDO Media ABI v3. Failure first restores the pre-transaction driver and
falls back to the verified original A2DP backup when necessary. Manual rollback
is idempotent and intentionally retains the shared test certificate. None of
these install paths run during build or verification.
