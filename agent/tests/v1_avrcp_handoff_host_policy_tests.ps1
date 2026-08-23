# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))

function Read-ProjectFile([string] $RelativePath) {
    return Get-Content -LiteralPath `
        (Join-Path $projectRoot $RelativePath) -Raw
}

$hostSource = Read-ProjectFile 'agent\v1_avrcp_handoff_host.cpp'
$ipcSource = Read-ProjectFile 'agent\v1_avrcp_handoff_ipc.cpp'
$stateHeader = Read-ProjectFile 'agent\v1_avrcp_handoff_state.h'
$cmake = Read-ProjectFile 'CMakeLists.txt'

function Assert-Policy {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) { throw $Message }
}

foreach ($required in @(
        '--candidate-path',
        '--instance-id',
        '--once',
        'WaitForAnyRequest',
        'V1AvrcpHandoffState',
        'request_stage_observer',
        'request_restore_restart',
        'notify_daily_active',
        'SignalHandoffCompleted',
        'SignalRestoreCompleted',
        'kMicrosoftService',
        'kObserverService',
        'NativeLdacAvrcpObserver.inf',
        '/add-driver',
        '/restart-device',
        '/delete-driver',
        '/scan-devices',
        'kBindPollTimeoutMs',
        'kRestoreBindPollTimeoutMs',
        'restore-restart-failed',
        'DEVPKEY_Device_DriverInfPath',
        'unexpected-current-owner',
        'independent restore completed',
        'resident_published_inf',
        'has_recorded_observer',
        'restore using the recorded observer INF despite a transient unhealthy owner snapshot',
        'handoff completion failure rolled back to Microsoft',
        'absent PDO restore prepared Microsoft for next enumeration',
        'handoff-host.log',
        'LogHost',
        'NUL')) {
    if (-not $hostSource.Contains($required)) {
        throw "The AVRCP handoff host is missing: $required"
    }
}

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
        'IAudioEndpointVolume',
        'Set-BluetoothRadio')) {
    if ($hostSource.Contains($forbidden)) {
        throw "The AVRCP handoff host contains a forbidden operation: $forbidden"
    }
}

# Bounded restart discipline: one handoff fallback restart plus one restore
# restart, never a loop.
$restartMatches = [regex]::Matches(
    $hostSource,
    'L"/restart-device"')
Assert-Policy ($restartMatches.Count -le 2) `
    "The handoff host exceeds the bounded restart budget " +
    "(found $($restartMatches.Count)); expected at most handoff + restore."

# Restore order inside RestoreMicrosoft: delete the observer package, scan,
# then restart the exact PDO, then verify the Microsoft binding.
$restoreFnStart = $hostSource.IndexOf(
    'bool RestoreMicrosoft(DWORD* error)')
$restoreFnEnd = $hostSource.IndexOf(
    '    V1AvrcpHandoffIpc* ipc_ = nullptr;')
Assert-Policy ($restoreFnStart -ge 0 -and $restoreFnEnd -gt $restoreFnStart) `
    'The RestoreMicrosoft function was not found.'
$restoreBody = $hostSource.Substring(
    $restoreFnStart, $restoreFnEnd - $restoreFnStart)
$restoreDelete = $restoreBody.IndexOf('L"/delete-driver"')
$restoreScan = $restoreBody.IndexOf('L"/scan-devices"')
$restoreRestartCall = $restoreBody.IndexOf('RestartDevice(error)')
$restoreVerify = $restoreBody.IndexOf('WaitForBinding(')
Assert-Policy (
    $restoreDelete -ge 0 -and
    $restoreScan -gt $restoreDelete -and
    $restoreRestartCall -gt $restoreScan -and
    $restoreVerify -gt $restoreRestartCall) `
    'The handoff host restore order is not delete -> scan -> restart -> verify.'
Assert-Policy ($hostSource -match 'bool RunRestore\(DWORD\* error\)' -and
               $hostSource -match 'IsMicrosoftBaseline\(current\)' -and
               $hostSource -match 'IsObserverBound\(current\)' -and
               $hostSource -match 'RestoreMicrosoft\(error\)') `
    'An independent restore request does not execute the real owner transaction.'
Assert-Policy ($hostSource -match 'if \(!ipc_->SignalHandoffCompleted' -and
               $hostSource -match 'if \(!RestoreMicrosoft\(error\)\)' -and
               $hostSource -match 'handoff completion failure rollback failed') `
    'A handoff completion notification failure does not roll back the observer owner.'

# The handoff host must not touch the Bluetooth radio or unrelated devices.
Assert-Policy ($hostSource -notmatch '/remove-device') `
    'The handoff host contains a radio or unrelated PnP mutation.'

# The state machine contract is preserved.
foreach ($required in @(
        'enum class V1AvrcpHandoffPhase',
        'MicrosoftHeld',
        'HandoffPending',
        'ObserverActive',
        'RestorePending',
        'handoff_restart_used',
        'restore_restart_used')) {
    if (-not $stateHeader.Contains($required)) {
        throw "The handoff state machine contract is missing: $required"
    }
}

# IPC host-side completion APIs exist and record errors in the state file.
foreach ($required in @(
        'WaitForHandoffRequest',
        'WaitForRestoreRequest',
        'ReadRequestInfo',
        'SignalHandoffCompleted',
        'SignalRestoreCompleted')) {
    if (-not $ipcSource.Contains($required)) {
        throw "The handoff IPC host side is missing: $required"
    }
}

if (-not $cmake.Contains('v1_avrcp_handoff_host') -or
    -not $cmake.Contains(
        'agent/v1_avrcp_handoff_host.cpp')) {
    throw 'The AVRCP handoff host is not registered in the build.'
}

Write-Host 'V1 AVRCP handoff host policy tests passed.'
