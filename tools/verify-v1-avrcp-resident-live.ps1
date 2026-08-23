# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [string]$ProbePath,
    [string]$TransportProbePath,
    [ValidateRange(10, 60)]
    [int]$ObservationSeconds = 20,
    [switch]$ConfirmResidentLiveCheck,
    [switch]$RestartExactPdoAfterMediaReady
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:TargetPrefix =
    'BTHENUM\{0000110E-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Resident live check requires an elevated PowerShell 7 terminal.'
    }
}

function Get-PropertyData {
    param([string]$InstanceId, [string]$KeyName)
    $property = Get-PnpDeviceProperty -InstanceId $InstanceId `
        -KeyName $KeyName -ErrorAction SilentlyContinue
    if ($null -eq $property) { return $null }
    return $property.Data
}

function Get-AvrcpPdoSnapshot {
    $device = Get-PnpDevice -ErrorAction SilentlyContinue |
        Where-Object {
            $_.InstanceId.StartsWith(
                $script:TargetPrefix,
                [StringComparison]::OrdinalIgnoreCase)
        } | Select-Object -First 1
    if ($null -eq $device) { return $null }
    [pscustomobject][ordered]@{
        instance_id = [string]$device.InstanceId
        status = [string]$device.Status
        service = [string](Get-PropertyData -InstanceId $device.InstanceId `
            -KeyName 'DEVPKEY_Device_Service')
        inf = [string](Get-PropertyData -InstanceId $device.InstanceId `
            -KeyName 'DEVPKEY_Device_DriverInfPath')
        problem_code = [int](Get-PropertyData -InstanceId $device.InstanceId `
            -KeyName 'DEVPKEY_Device_ProblemCode')
    }
}

function Invoke-PnpUtil {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    $lines = @(& pnputil.exe @Arguments 2>&1)
    [pscustomobject][ordered]@{
        exit_code = $LASTEXITCODE
        lines = @($lines)
    }
}

Assert-Administrator
if (-not $ConfirmResidentLiveCheck) {
    throw 'Refusing resident live check. Re-run with -ConfirmResidentLiveCheck.'
}
if (-not $RestartExactPdoAfterMediaReady) {
    throw 'This diagnostic requires one explicit post-media restart of the exact XM5 AVRCP PDO. Re-run with -RestartExactPdoAfterMediaReady.'
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($ProbePath)) {
    $ProbePath = Join-Path $projectRoot `
        'build\protocol\Release\v1_avrcp_observer_probe.exe'
}
if ([string]::IsNullOrWhiteSpace($TransportProbePath)) {
    $TransportProbePath = Join-Path $projectRoot `
        'build\protocol\Release\transport_probe.exe'
}
$ProbePath = [IO.Path]::GetFullPath($ProbePath)
$TransportProbePath = [IO.Path]::GetFullPath($TransportProbePath)
if (-not (Test-Path -LiteralPath $ProbePath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $TransportProbePath -PathType Leaf)) {
    throw 'Observer probe or transport probe executable is missing.'
}

$snapshot = Get-AvrcpPdoSnapshot
if ($null -eq $snapshot -or
    $snapshot.service -ne 'NativeLdacAvrcpObserver' -or
    $snapshot.problem_code -ne 0) {
    throw 'The resident AVRCP observer is not bound and healthy on the exact XM5 0x110E PDO.'
}

$trialRoot = Join-Path $projectRoot (
    'artifacts\v1-volume-sync\resident\live-check-' +
    (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $trialRoot -Force | Out-Null

$targetDescription = 'the exact XM5 AVRCP 0x110E PDO with a bounded LDAC silence media session'
$actionDescription = 'Start silence media, restart that PDO once, observe AVRCP events, then stop and clean up'
if (-not $PSCmdlet.ShouldProcess($targetDescription, $actionDescription)) {
    return
}

$stopEventName = 'nld_avrcp_resident_check_stop'
$stopEvent = [Threading.EventWaitHandle]::new(
    $false,
    [Threading.EventResetMode]::ManualReset,
    $stopEventName,
    [ref]$false)
if (-not $stopEvent) {
    throw 'Could not create the bounded media stop event.'
}

$transportOut = Join-Path $trialRoot 'transport.out.log'
$transportErr = Join-Path $trialRoot 'transport.err.log'
$transportArguments = @(
    '--stream-silence-continuous',
    '--open-attempts', '1',
    '--stop-event', $stopEventName)
$transportProcess = Start-Process `
    -FilePath $TransportProbePath `
    -ArgumentList $transportArguments `
    -RedirectStandardOutput $transportOut `
    -RedirectStandardError $transportErr `
    -WindowStyle Hidden `
    -PassThru

$probeLines = @()
$transportReady = $false
$failure = $null
$restartExit = -1
$restartLines = @()
$restartedReady = $false
$afterRestart = $null
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while ([DateTime]::UtcNow -lt $deadline -and
           -not $transportProcess.HasExited) {
        if (Test-Path -LiteralPath $transportOut -PathType Leaf) {
            $text = Get-Content -LiteralPath $transportOut -Raw
            if ($text -match
                '(?m)^XM5 accepted START; the LDAC Media transport is ready\.\s*$') {
                $transportReady = $true
                break
            }
        }
        Start-Sleep -Milliseconds 200
    }
    if (-not $transportReady) {
        if (Test-Path -LiteralPath $transportErr -PathType Leaf) {
            $errText = Get-Content -LiteralPath $transportErr -Raw
            if ($errText -match 'Win32 121') {
                throw 'The A2DP signaling OPEN timed out (Win32 121) because the XM5 link has been idle. Turn XM5 off for a few seconds, turn it back on, and rerun immediately after it connects.'
            }
        }
        throw 'The silence media session did not reach START within 30 seconds.'
    }

    $restart = Invoke-PnpUtil -Arguments @(
        '/restart-device', $snapshot.instance_id)
    $restartExit = $restart.exit_code
    $restartLines = @($restart.lines)
    $restartLines | Set-Content -LiteralPath `
        (Join-Path $trialRoot 'restart.log') -Encoding utf8
    if ($restartExit -ne 0) {
        throw "The exact AVRCP PDO restart failed with exit $restartExit."
    }

    $restartDeadline = [DateTime]::UtcNow.AddSeconds(20)
    while ([DateTime]::UtcNow -lt $restartDeadline) {
        $afterRestart = Get-AvrcpPdoSnapshot
        if ($null -ne $afterRestart -and
            $afterRestart.instance_id -ieq $snapshot.instance_id -and
            $afterRestart.inf -ieq $snapshot.inf -and
            $afterRestart.service -ieq 'NativeLdacAvrcpObserver' -and
            $afterRestart.status -ieq 'OK' -and
            $afterRestart.problem_code -eq 0) {
            $restartedReady = $true
            break
        }
        Start-Sleep -Milliseconds 300
    }
    if (-not $restartedReady) {
        throw 'The exact AVRCP PDO did not return under the same healthy resident package after its one restart.'
    }

    Write-Host 'The exact XM5 AVRCP PDO restarted once and returned healthy while the silence media session remained active.'
    Write-Host 'The observer probe will now issue the resident driver''s one explicit AVCTP observation activation.'
    $probeLines = @(& $ProbePath `
        --duration-seconds $ObservationSeconds 2>&1)
    $probeLines | ForEach-Object { Write-Host $_ }
    $probeLines | Set-Content -LiteralPath `
        (Join-Path $trialRoot 'observer.log') -Encoding utf8
    if ($LASTEXITCODE -ne 0) {
        throw "The observer probe failed with exit $LASTEXITCODE."
    }
} catch {
    $failure = $_.Exception.Message
} finally {
    [void]$stopEvent.Set()
    if (-not $transportProcess.HasExited) {
        [void]$transportProcess.WaitForExit(30000)
    }
    $transportExit = $transportProcess.ExitCode
    $transportProcess.Dispose()
    $stopEvent.Dispose()
}

$transportText = if (Test-Path -LiteralPath $transportOut -PathType Leaf) {
    Get-Content -LiteralPath $transportOut -Raw
} else { '' }
$transportErrText = if (Test-Path -LiteralPath $transportErr -PathType Leaf) {
    Get-Content -LiteralPath $transportErr -Raw
} else { '' }

$joined = $probeLines -join "`n"
$passed = [string]::IsNullOrWhiteSpace($failure) -and
    $transportExit -eq 0 -and
    $restartExit -eq 0 -and $restartedReady -and
    $transportText -match '(?m)^Signaling channel closed\.\s*$' -and
    $joined -match 'type=volume-capability' -and
    $joined -match 'type=absolute-volume'
$result = [ordered]@{
    result_version = 1
    created_at = (Get-Date).ToString('o')
    passed = $passed
    failure = $failure
    transport_exit = $transportExit
    transport_ready = $transportReady
    pdo = $snapshot
    observer_activation = [ordered]@{
        requested_after_media_ready = $transportReady
        pdo_restarted = $restartedReady
    }
    avrcp_pdo_restart = [ordered]@{
        exit_code = $restartExit
        lines = @($restartLines)
        pdo_after_restart = $afterRestart
    }
    observer_lines = @($probeLines)
    transport_closed = $transportText -match
        '(?m)^Signaling channel closed\.\s*$'
    transport_stdout = $transportText
    transport_stderr = $transportErrText
}
$resultPath = Join-Path $trialRoot 'result.json'
$result | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $resultPath -Encoding utf8

if (-not $passed) {
    Write-Host "Resident live check failed. Result: $resultPath"
    exit 1
}
Write-Host 'Resident live check passed.'
Write-Host 'The resident observer completed the AVCTP channel during a silence media session and observed volume capability/absolute-volume events.'
Write-Host "Result: $resultPath"
