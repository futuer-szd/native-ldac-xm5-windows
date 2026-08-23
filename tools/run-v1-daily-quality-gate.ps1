# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateSet('hq', 'sq', 'mq')]
    [string]$Quality,
    [ValidateSet(44100, 48000, 88200, 96000)]
    [int]$SampleRateHz = 44100,
    [ValidateSet(16, 24)]
    [int]$BitsPerSample = 16,
    [ValidateSet('stereo', 'dual', 'mono')]
    [string]$ChannelMode = 'stereo',
    [switch]$VolumeSync,
    [string]$CandidateRoot = $PSScriptRoot,
    [string]$TrialRoot = (Join-Path $PSScriptRoot '..\artifacts\v1-volume-sync\quality-gates')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-daily-host-common.ps1')

Assert-V1DailyPowerShell7
Assert-V1DailyHandoffRetired
Assert-V1DailyAdministrator
if ([string]::IsNullOrWhiteSpace($Quality)) {
    throw 'Select exactly one quality: -Quality hq, -Quality sq, or -Quality mq.'
}

function Invoke-V1DailyCapturedNative {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [string[]]$Arguments = @()
    )

    $lines = @(& $FilePath @Arguments 2>&1)
    return [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Text = ($lines -join "`r`n")
    }
}

function Get-V1DailyEndpointFormat {
    param([Parameter(Mandatory = $true)][string]$ProbePath)

    $capture = Invoke-V1DailyCapturedNative -FilePath $ProbePath `
        -Arguments @('--format')
    if ($capture.ExitCode -ne 0 -or
        $capture.Text -notmatch 'Preferred format: (\d+) Hz, (\d+)-bit') {
        throw "Native LDAC endpoint format query failed: $($capture.Text)"
    }
    return [pscustomobject]@{
        SampleRateHz = [int]$Matches[1]
        BitsPerSample = [int]$Matches[2]
        Text = $capture.Text
    }
}

function Get-V1DailySharedEndpointFormat {
    param([Parameter(Mandatory = $true)][string]$ControlPath)

    $capture = Invoke-V1DailyCapturedNative -FilePath $ControlPath `
        -Arguments @('--format')
    if ($capture.ExitCode -ne 0 -or
        $capture.Text -notmatch (
            'Windows shared-mode device format: ([0-9]+) Hz, ([0-9]+) channel[(]s[)], ' +
            '([0-9]+)-bit container, ([0-9]+) valid bit[(]s[)], block ([0-9]+) bytes')) {
        throw "Windows shared-mode format query failed: $($capture.Text)"
    }
    return [pscustomobject]@{
        SampleRateHz = [int]$Matches[1]
        Channels = [int]$Matches[2]
        ContainerBits = [int]$Matches[3]
        ValidBits = [int]$Matches[4]
        BlockAlign = [int]$Matches[5]
        Text = $capture.Text
    }
}

$candidate = [IO.Path]::GetFullPath($CandidateRoot)
$manifest = Get-Content -LiteralPath (Join-Path $candidate 'manifest.json') -Raw |
    ConvertFrom-Json
$null = Test-V1DailyBundleManifest -Root $candidate
$projectRoot = [IO.Path]::GetFullPath($PSScriptRoot)
while ($true) {
    if (Test-Path -LiteralPath (Join-Path $projectRoot '.git')) {
        break
    }
    $parent = [IO.Directory]::GetParent($projectRoot)
    if ($null -eq $parent -or $parent.FullName -eq $projectRoot) {
        $projectRoot = $null
        break
    }
    $projectRoot = $parent.FullName
}
if ($null -ne $projectRoot) {
    $head = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
    $status = @(& git.exe -C $projectRoot status --porcelain)
    if ($LASTEXITCODE -ne 0 -or $head -cne [string]$manifest.source_commit -or
        $status.Count -ne 0) {
        throw 'The quality gate requires a clean candidate matching current Git HEAD.'
    }
}

$runner = Join-Path $candidate 'run-v1-daily-full-cycle.ps1'
$agent = Join-Path $candidate 'v1_presence_agent.exe'
$worker = Join-Path $candidate 'v1_transport_daily_worker.exe'
$endpointProbe = Join-Path $candidate 'audio_endpoint_probe.exe'
$endpointFormatControl = Join-Path $candidate 'endpoint_format_control.exe'
foreach ($path in @($runner, $agent, $worker)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "The quality gate candidate is missing: $path"
    }
}
if (-not (Test-Path -LiteralPath $endpointProbe -PathType Leaf)) {
    throw "The quality gate candidate is missing the endpoint format probe: $endpointProbe"
}
if (-not (Test-Path -LiteralPath $endpointFormatControl -PathType Leaf)) {
    throw "The quality gate candidate is missing shared-mode format control: $endpointFormatControl"
}

$qualityTrialRoot = Join-Path ([IO.Path]::GetFullPath($TrialRoot)) $Quality.ToUpperInvariant()
$suffix = 'quality-{0}-{1}' -f $Quality, $PID
$runnerArguments = @(
    '-NoProfile',
    '-ExecutionPolicy', 'Bypass',
    '-File', $runner,
    '-AgentPath', $agent,
    '-WorkerPath', $worker,
    '-TrialRoot', $qualityTrialRoot,
    '-InstanceSuffix', $suffix,
    '-Quality', $Quality,
    '-SampleRateHz', [string]$SampleRateHz,
    '-BitsPerSample', [string]$BitsPerSample,
    '-ChannelMode', $ChannelMode
)
if ($VolumeSync) { $runnerArguments += '-VolumeSync' }

Write-Host "Starting policy $script:V1DailyHostPolicyVersion $($Quality.ToUpperInvariant()) quality gate."
Write-Host "Candidate commit: $($manifest.source_commit)"
Write-Host "Trial root: $qualityTrialRoot"
$originalFormat = $null
$appliedFormat = $null
$originalSharedFormat = $null
$appliedSharedFormat = $null
$runnerExitCode = 1
$restoreExitCode = $null
$sharedRestoreExitCode = $null
$restored = $false
$sharedRestored = $false
$gateError = ''
$restoreError = ''
try {
    $originalFormat = Get-V1DailyEndpointFormat -ProbePath $endpointProbe
    $originalSharedFormat = Get-V1DailySharedEndpointFormat `
        -ControlPath $endpointFormatControl
    Write-Host "Original Native LDAC endpoint format: $($originalFormat.SampleRateHz) Hz/$($originalFormat.BitsPerSample)-bit."
    Write-Host "Original Windows shared-mode endpoint format: $($originalSharedFormat.SampleRateHz) Hz/$($originalSharedFormat.ValidBits)-bit."

    $set = Invoke-V1DailyCapturedNative -FilePath $endpointProbe `
        -Arguments @('--set-format', [string]$SampleRateHz, [string]$BitsPerSample)
    $appliedFormat = Get-V1DailyEndpointFormat -ProbePath $endpointProbe
    if ($set.ExitCode -ne 0 -or
        $appliedFormat.SampleRateHz -ne $SampleRateHz -or
        $appliedFormat.BitsPerSample -ne $BitsPerSample) {
        throw "Native LDAC endpoint did not retain requested format $SampleRateHz Hz/$BitsPerSample-bit: $($set.Text)"
    }
    Write-Host "Applied Native LDAC endpoint format: $($appliedFormat.SampleRateHz) Hz/$($appliedFormat.BitsPerSample)-bit."

    $sharedSet = Invoke-V1DailyCapturedNative -FilePath $endpointFormatControl `
        -Arguments @('--set-format', [string]$SampleRateHz, [string]$BitsPerSample)
    $appliedSharedFormat = Get-V1DailySharedEndpointFormat `
        -ControlPath $endpointFormatControl
    $expectedContainerBits = if ($BitsPerSample -eq 24) { 32 } else { $BitsPerSample }
    if ($sharedSet.ExitCode -ne 0 -or
        $appliedSharedFormat.SampleRateHz -ne $SampleRateHz -or
        $appliedSharedFormat.ValidBits -ne $BitsPerSample -or
        $appliedSharedFormat.ContainerBits -ne $expectedContainerBits) {
        throw "Windows shared-mode endpoint did not retain requested format $SampleRateHz Hz/$BitsPerSample-bit: $($sharedSet.Text)"
    }
    Write-Host "Applied Windows shared-mode endpoint format: $($appliedSharedFormat.SampleRateHz) Hz/$($appliedSharedFormat.ValidBits)-bit."

    $pwsh = Join-Path $PSHOME 'pwsh.exe'
    & $pwsh @runnerArguments
    $runnerExitCode = $LASTEXITCODE
} catch {
    $gateError = $_.Exception.Message
    Write-Warning $gateError
} finally {
    if ($null -ne $originalFormat) {
        try {
            $restore = Invoke-V1DailyCapturedNative -FilePath $endpointProbe `
                -Arguments @('--set-format', [string]$originalFormat.SampleRateHz,
                    [string]$originalFormat.BitsPerSample)
            $restoreExitCode = $restore.ExitCode
            $restoredFormat = Get-V1DailyEndpointFormat -ProbePath $endpointProbe
            $restored = $restore.ExitCode -eq 0 -and
                $restoredFormat.SampleRateHz -eq $originalFormat.SampleRateHz -and
                $restoredFormat.BitsPerSample -eq $originalFormat.BitsPerSample
            if ($restored) {
                Write-Host "Restored Native LDAC endpoint format: $($restoredFormat.SampleRateHz) Hz/$($restoredFormat.BitsPerSample)-bit."
            } else {
                $restoreError = "Native LDAC endpoint format restore failed: $($restore.Text)"
                Write-Warning $restoreError
            }
        } catch {
            $restoreExitCode = -1
            $restored = $false
            $restoreError = "Native LDAC endpoint format restore failed: $($_.Exception.Message)"
            Write-Warning $restoreError
        }
    }
    if ($null -ne $originalSharedFormat) {
        try {
            $sharedRestore = Invoke-V1DailyCapturedNative -FilePath $endpointFormatControl `
                -Arguments @('--set-format', [string]$originalSharedFormat.SampleRateHz,
                    [string]$originalSharedFormat.ValidBits)
            $sharedRestoreExitCode = $sharedRestore.ExitCode
            $restoredSharedFormat = Get-V1DailySharedEndpointFormat `
                -ControlPath $endpointFormatControl
            $sharedRestored = $sharedRestore.ExitCode -eq 0 -and
                $restoredSharedFormat.SampleRateHz -eq $originalSharedFormat.SampleRateHz -and
                $restoredSharedFormat.ValidBits -eq $originalSharedFormat.ValidBits -and
                $restoredSharedFormat.ContainerBits -eq $originalSharedFormat.ContainerBits
            if ($sharedRestored) {
                Write-Host "Restored Windows shared-mode endpoint format: $($restoredSharedFormat.SampleRateHz) Hz/$($restoredSharedFormat.ValidBits)-bit."
            } else {
                $restoreError = "Windows shared-mode endpoint format restore failed: $($sharedRestore.Text)"
                Write-Warning $restoreError
            }
        } catch {
            $sharedRestoreExitCode = -1
            $sharedRestored = $false
            $restoreError = "Windows shared-mode endpoint format restore failed: $($_.Exception.Message)"
            Write-Warning $restoreError
        }
    }
}

$formatRecord = [ordered]@{
    schema_version = 1
    record_kind = 'v1-daily-quality-gate-format'
    completed_at = [DateTimeOffset]::Now.ToString('o')
    requested = [ordered]@{
        sample_rate_hz = $SampleRateHz
        bits_per_sample = $BitsPerSample
        channel_mode = $ChannelMode
    }
    original = if ($null -eq $originalFormat) { $null } else {
        [ordered]@{
            sample_rate_hz = $originalFormat.SampleRateHz
        bits_per_sample = $originalFormat.BitsPerSample
        }
    }
    applied = if ($null -eq $appliedFormat) { $null } else {
        [ordered]@{
            sample_rate_hz = $appliedFormat.SampleRateHz
            bits_per_sample = $appliedFormat.BitsPerSample
        }
    }
    restored = $restored
    restore_exit_code = $restoreExitCode
    shared_mode_original = if ($null -eq $originalSharedFormat) { $null } else {
        [ordered]@{
            sample_rate_hz = $originalSharedFormat.SampleRateHz
            channels = $originalSharedFormat.Channels
            container_bits = $originalSharedFormat.ContainerBits
            valid_bits = $originalSharedFormat.ValidBits
            block_align = $originalSharedFormat.BlockAlign
        }
    }
    shared_mode_applied = if ($null -eq $appliedSharedFormat) { $null } else {
        [ordered]@{
            sample_rate_hz = $appliedSharedFormat.SampleRateHz
            channels = $appliedSharedFormat.Channels
            container_bits = $appliedSharedFormat.ContainerBits
            valid_bits = $appliedSharedFormat.ValidBits
            block_align = $appliedSharedFormat.BlockAlign
        }
    }
    shared_mode_restored = $sharedRestored
    shared_mode_restore_exit_code = $sharedRestoreExitCode
    runner_exit_code = $runnerExitCode
    error = $gateError
    restore_error = $restoreError
}
New-Item -ItemType Directory -Path $qualityTrialRoot -Force | Out-Null
$formatRecordPath = Join-Path $qualityTrialRoot `
    ('quality-gate-format-{0}-{1}.json' -f
        (Get-Date -Format 'yyyyMMdd-HHmmssfff'), $PID)
$formatRecord | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $formatRecordPath `
    -Encoding utf8NoBOM
Write-Host "Format gate record: $formatRecordPath"

if (-not $restored -or -not $sharedRestored) {
    exit 1
}
exit $runnerExitCode
