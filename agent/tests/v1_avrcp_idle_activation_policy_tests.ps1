# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))

function Read-ProjectFile([string] $RelativePath) {
    return Get-Content -LiteralPath `
        (Join-Path $projectRoot $RelativePath) -Raw
}

$probe = Read-ProjectFile 'tools\v1_avrcp_observer_probe.cpp'
$script = Read-ProjectFile 'tools\run-v1-avrcp-idle-activation-experiment.ps1'

function Assert-Policy {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) { throw $Message }
}

foreach ($required in @(
        '--wait-acl-connect',
        '--delay-seconds',
        '0..30',
        'ACTIVATION_REQUIRED',
        'BEGIN_OBSERVATION',
        '--verify-same-channel-write',
        'SubmitSameChannelVolumeWrite',
        'same-channel write response',
        'same-channel write summary',
        '0x09u',
        'channel hold summary',
        'post-activation status',
        'Waiting for a new XM5 ACL connect',
        'initial_generation',
        'LastOpenStatus')) {
    if (-not $probe.Contains($required)) {
        throw "The idle-activation probe is missing: $required"
    }
}

foreach ($forbidden in @(
        'SendInput',
        'IAudioEndpointVolume',
        'pnputil',
        '/restart-device',
        'SetMasterVolumeLevelScalar')) {
    if ($probe.Contains($forbidden)) {
        throw "The idle-activation probe is not read-only: $forbidden"
    }
}

foreach ($required in @(
        'ConfirmV1AvrcpIdleActivationExperiment',
        'verify-v1-avrcp-observer-candidate.ps1',
        'NativeLdacAvrcpObserver.inf',
        'microsoft_bluetooth_avrcptransport.inf',
        '@(0, 1, 5, 15)',
        '[int]$DelaySeconds = 0',
        '[switch]$AllDelays',
        'Press Enter after XM5 is off',
        'xm5_connection_probe.exe',
        '--wait-acl-connect', '300',
        'XM5 ACL event: connected',
        'protocol-error',
        'channel up',
        '--verify-same-channel-write',
        'same-channel write submitted',
        'same-channel write response',
        'experiment_completed',
        'feasibility_passed',
        'schema_version = 2',
        '[ValidateRange(20, 3600)]',
        'probe_exit_code',
        'avrcp-idle-activation-restore-failed',
        '--delay-seconds',
        'Action: power on XM5 now (no media, no playback).',
        'Action: power off XM5 now.',
        '/delete-driver',
        '/scan-devices',
        '/restart-device',
        'Test-V1IdleMicrosoftBaseline',
        'channel_hold_summary')) {
    if (-not $script.Contains($required)) {
        throw "The idle-activation experiment script is missing: $required"
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
        'v1_avrcp_action_executor',
        '--apply',
        'Set-BluetoothRadio',
        '--stream-silence-continuous',
        '--play-system')) {
    if ($script.Contains($forbidden)) {
        throw "The idle-activation experiment contains a forbidden operation: $forbidden"
    }
}

$tokens = $null
$errors = $null
[void][Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $projectRoot 'tools\run-v1-avrcp-idle-activation-experiment.ps1'),
    [ref]$tokens,
    [ref]$errors)
Assert-Policy (@($errors).Count -eq 0) `
    "The idle-activation experiment does not parse: " +
    (@($errors | ForEach-Object { $_.Message }) -join '; ')

Write-Host 'V1 AVRCP idle-activation policy tests passed.'
