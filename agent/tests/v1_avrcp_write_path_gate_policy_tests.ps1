# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))

function Read-ProjectFile([string] $RelativePath) {
    return Get-Content -LiteralPath `
        (Join-Path $projectRoot $RelativePath) -Raw
}

$gate = Read-ProjectFile 'tools\run-v1-avrcp-write-path-gate.ps1'
$filterCommon = Read-ProjectFile 'tools\v1-avrcp-filter-gate-common.ps1'

function Assert-Policy {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) { throw $Message }
}

foreach ($required in @(
        'ConfirmV1AvrcpWritePath',
        'verify-v1-avrcp-filter-candidate.ps1',
        'policy_version -lt 6',
        'NativeLdacAvrcpObserver.inf',
        'microsoft_bluetooth_avrcptransport.inf',
        '--stream-silence-continuous',
        "--open-attempts', '1'",
        'XM5 ACL event: connected',
        '/restart-device',
        '--apply',
        '--volume-sync',
        '--route-media-keys',
        '0x8001A00C',
        'ioctl=$script:SendCommandIoctl',
        'raw=.*\b50 00 00 00 00 00 00 00 01',
        '^oem\d+\.inf$',
        'action send-xm5-volume value=\d+ \(sent\)',
        '/delete-driver',
        'XM5 ACTION WINDOW READY')) {
    if (-not $gate.Contains($required)) {
        throw "The AVRCP write-path gate contract is missing: $required"
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
        'transport_probe --discover',
        '--play-system',
        '--play-endpoint',
        '--stream-tone')) {
    if ($gate.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The AVRCP write-path gate contains a forbidden operation: $forbidden"
    }
}

# Bounded restart discipline: one restart per phase, never a loop.
$restartMatches = [regex]::Matches(
    $gate,
    "'/restart-device', \`$target\.InstanceId")
Assert-Policy ($restartMatches.Count -eq 3) `
    "The write-path gate does not keep one bounded restart per phase " +
    "(found $($restartMatches.Count)); expected filter, handoff, restore."

# The observer handoff must happen only after media START readiness.
Assert-Policy ($gate.IndexOf('XM5 accepted START; the LDAC Media transport is ready') -lt
               $gate.IndexOf('Phase 3: switching the exact PDO')) `
    'The observer handoff can start before media readiness.'

# The media process must be asked to stop gracefully before any kill.
Assert-Policy ($gate.IndexOf('NativeLdacWritePathStop') -lt
               $gate.IndexOf('Stop-V1WritePathProcess $mediaProcess')) `
    'The media session is not stopped gracefully before termination.'

# Restore must converge to the Microsoft baseline before the filter rollback.
Assert-Policy ($gate.IndexOf('Test-V1WritePathMicrosoftBaseline') -lt
               $gate.IndexOf('Invoke-V1AvrcpFilterRollback')) `
    'The restore does not verify Microsoft ownership before the filter rollback.'

# Evidence gates: no SEND_COMMAND IOCTL or no sent write fails the trial.
Assert-Policy ($gate.Contains('No SEND_COMMAND IOCTL with pdu 0x50 was observed') -and
               $gate.Contains('The executor did not report any sent SetAbsoluteVolume write')) `
    'The write-path gate accepts missing IOCTL or write evidence.'

# The media process must fail fast with a diagnostic when it exits before
# START instead of idling until the connect timeout.
Assert-Policy ($gate.Contains('The encoded-silence media process exited before START') -and
               $gate.Contains('$mediaProcess.HasExited') -and
               $gate.Contains('Code 38 means LdacNative failed to load')) `
    'The write-path gate does not fail fast with an A2DP diagnostic when the media process exits before START.'

# The gate must not install or bind anything while XM5 is on before the
# ACL watcher arms, and must not touch the Bluetooth radio or other devices.
Assert-Policy ($gate -notmatch 'Set-BluetoothRadio|Disable-Bluetooth|/remove-device') `
    'The write-path gate contains a radio or unrelated PnP mutation.'

$tokens = $null
$errors = $null
[void][Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $projectRoot 'tools\run-v1-avrcp-write-path-gate.ps1'),
    [ref]$tokens,
    [ref]$errors)
Assert-Policy (@($errors).Count -eq 0) `
    "The write-path gate does not parse: " +
    (@($errors | ForEach-Object { $_.Message }) -join '; ')

Write-Host 'V1 AVRCP write-path gate policy tests passed.'
