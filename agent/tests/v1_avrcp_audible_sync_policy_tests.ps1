# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))

function Read-ProjectFile([string] $RelativePath) {
    return Get-Content -LiteralPath `
        (Join-Path $projectRoot $RelativePath) -Raw
}

$check = Read-ProjectFile 'tools\run-v1-avrcp-audible-sync-check.ps1'
$executor = Read-ProjectFile 'tools\v1_avrcp_action_executor.cpp'
$sink = Read-ProjectFile 'agent\v1_avrcp_windows_sink.cpp'

function Assert-Policy {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) { throw $Message }
}

foreach ($required in @(
        'ConfirmV1AvrcpAudibleSync',
        'verify-v1-avrcp-observer-candidate.ps1',
        'GetUnresolvedProviderPathFromPSPath',
        'NativeLdacAvrcpObserver.inf',
        'microsoft_bluetooth_avrcptransport.inf',
        '--wait-acl-connect',
        'ACL watcher armed',
        'XM5 ACL event: connected',
        'Press Enter here once audio is playing on the PC',
        'Swipe the XM5 volume up and down once',
        '[executor] ',
        '--play-system',
        "--open-attempts', '1'",
        '--stop-event',
        'XM5 accepted START; the LDAC Media transport is ready',
        '--volume-sync',
        '--route-media-keys',
        '--apply',
        'action send-xm5-volume value=\d+ \(sent\)',
        'PauseObservationSeconds',
        'ResumeObservationSeconds',
        'ResumeCooldownSeconds',
        'CLOSE before re-opening',
        'PauseKeepSession',
        'SkipSyncWindow',
        'warm-up only',
        'PAUSE your player now',
        'FirstAuthorityDirection',
        'Get-V1AudibleSyncDefaultRenderVolume',
        'pc_volume_before_media',
        'clearly HIGHER',
        'clearly LOWER',
        'BoundaryCheckSeconds',
        'MediaKeyCheckSeconds',
        'DiagnoseMediaKeys',
        'diagnose_media_keys',
        '--diagnose-media-keys',
        'BOUNDARY VOLUME CHECK',
        'MEDIA KEY CHECK',
        'BOUNDARY satisfied',
        'MEDIA KEY window complete',
        'play_pause_injected_count',
        'next_injected_count',
        'previous_injected_count',
        'all_standard_keys_seen',
        'satisfied',
        'first-authority',
        'action inject vk=0x',
        'PAUSE PHASE',
        'RESUME PHASE',
        'sync_survived',
        'sync_resumed',
        'pause-phase',
        'resume-phase',
        '=== CHECK SUMMARY ===',
        'Required check(s) failed',
        'pauseSyncRequired',
        'resumeSyncRequired',
        'Get-V1AudibleSyncMediaKeySummary',
        'IncludePartial',
        'StringSplitOptions',
        'executorAliveAtPauseStart',
        'raw_pass_through_event_count',
        'sink_failure_count',
        'playback_status_notification_count',
        'media_resumed',
        'volume_sync_resumed',
        'Leave the PC player PLAYING',
        'Double-tap the XM5 once now',
        'EndpointVolumeProbePath',
        'Get-V1AudibleSyncDefaultRenderName',
        'default_render',
        'DefaultSettleSeconds',
        'waiting for it to settle',
        'The default render endpoint changed during the run',
        'Console]::OutputEncoding',
        'UTF8Encoding',
        'Restore-V1AudibleSyncMicrosoft',
        'observerInstalled',
        'recover-observer-clean.ps1',
        '/delete-driver',
        '/restart-device',
        'XM5 AUDIBLE SYNC WINDOW READY')) {
    if (-not $check.Contains($required)) {
        throw "The AVRCP audible sync check contract is missing: $required"
    }
}

foreach ($required in @(
        'IsAbsoluteVolumeControlReady',
        'NLD_AVRCP_OBSERVER_STATUS_CHANNEL_OPEN',
        'NLD_AVRCP_OBSERVER_STATUS_VOLUME_SUPPORTED',
        'NLD_AVRCP_OBSERVER_STATUS_OBSERVING',
        'live: control channel ready; absolute-volume notifications are ',
        'active (flags=',
        'event absolute-volume kind=',
        '$controlReadyDeadline',
        'control channel became ready')) {
    if (-not $executor.Contains($required) -and
        -not $check.Contains($required)) {
        throw "The audible sync readiness contract is missing: $required"
    }
}

Assert-Policy (
    $check -notmatch 'MEDIA KEY satisfied \(injection observed\); advancing'
) 'The media-key phase must be time-bounded, not end after the first injection.'

Assert-Policy (
    $check -notmatch "action = 'pause-phase'\s+passed = `$true"
) 'The pause phase must not be hard-coded as passed.'

Assert-Policy (
    $check -notmatch "action = 'resume-phase'\s+passed = \[bool\]`$result\.resume\.start_ready"
) 'The resume phase must include the observed sync result in its pass condition.'

foreach ($required in @(
        'v1_media_session_monitor.h',
        'GetLiveMediaSessionSnapshot',
        'V1MediaSessionSnapshot',
        'media_session_monitor.Start',
        'media_session_monitor.Stop',
        'SetMediaKeyDiagnostics',
        'diagnostic: pass-through',
        'sequence=',
        'timestamp_100ns=',
        'response=',
        'session_present=',
        'owner_lease=',
        'bounded LDAC-session fallback')) {
    if (-not $executor.Contains($required)) {
        throw "The live executor media-session contract is missing: $required"
    }
}

Assert-Policy ($sink.Contains('diagnostic: SendInput')) `
    'The Windows sink does not expose SendInput diagnostics.'

foreach ($forbidden in @(
        'Disable-PnpDevice',
        'Enable-PnpDevice',
        'Set-PnpDevice',
        'Set-Service',
        'Stop-Service',
        'Start-Service',
        'Restart-Computer',
        'SetDefaultEndpoint',
        'SetMasterVolumeLevelScalar',
        'SendInput',
        '--stream-silence-continuous',
        '--stream-silence',
        '--play-endpoint',
        '--stream-tone')) {
    if ($check.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The AVRCP audible sync check contains a forbidden operation: $forbidden"
    }
}

# Bounded restart discipline: at most one handoff restart plus one restore
# restart, never a loop.
$restartMatches = [regex]::Matches(
    $check,
    "'/restart-device', \`$target\.InstanceId|'/restart-device', \`$InstanceId")
Assert-Policy ($restartMatches.Count -le 2) `
    "The audible sync check exceeds the bounded restart budget " +
    "(found $($restartMatches.Count)); expected at most handoff + restore."

# The audible media process must start only after a physical XM5 connect is
# observed by the ACL watcher.
Assert-Policy ($check.IndexOf('XM5 ACL event: connected') -lt
               $check.IndexOf('--play-system')) `
    'The audible media can start before the physical XM5 connect is observed.'

# The observer handoff must happen only after media START readiness, matching
# the verified write-path gate order; switching while XM5 is off leaves the
# observer's single outbound AVCTP OPEN without a ready peer.
Assert-Policy ($check.IndexOf('XM5 accepted START; the LDAC Media transport is ready') -lt
               $check.IndexOf('Phase 2: switching the exact PDO')) `
    'The observer handoff can start before media START readiness.'

# The media process must be asked to stop gracefully before any kill.
Assert-Policy ($check.IndexOf('NativeLdacAudibleSyncStop') -lt
               $check.IndexOf('Stop-V1AudibleSyncProcess $mediaProcess')) `
    'The audible media session is not stopped gracefully before termination.'

# Restore must converge to the Microsoft baseline.
Assert-Policy ($check.Contains('Test-V1AudibleSyncMicrosoftBaseline') -and
               $check.Contains('Microsoft AVRCP could not be restored')) `
    'The audible sync check does not verify Microsoft restoration.'

# The check must not touch the Bluetooth radio or other devices.
Assert-Policy ($check -notmatch 'Set-BluetoothRadio|Disable-Bluetooth|/remove-device') `
    'The audible sync check contains a radio or unrelated PnP mutation.'

$tokens = $null
$errors = $null
[void][Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $projectRoot 'tools\run-v1-avrcp-audible-sync-check.ps1'),
    [ref]$tokens,
    [ref]$errors)
Assert-Policy (@($errors).Count -eq 0) `
    "The audible sync check does not parse: " +
    (@($errors | ForEach-Object { $_.Message }) -join '; ')

Write-Host 'V1 AVRCP audible sync check policy tests passed.'
