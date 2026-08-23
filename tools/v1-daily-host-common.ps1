# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:V1DailyTaskName = 'Native LDAC V1 Daily Host'
$script:V1LegacyTaskName = 'Native LDAC Agent'
$script:V1DailyHostPolicyVersion = 31

function Assert-V1DailyPowerShell7 {
    if ($PSVersionTable.PSEdition -ne 'Core' -or
        $PSVersionTable.PSVersion.Major -lt 7) {
        throw 'The V1 daily host requires PowerShell 7. Run it with pwsh.exe, not powershell.exe.'
    }
}

function Assert-V1DailyHandoffRetired {
    $task = Get-ScheduledTask -TaskName 'NativeLdacAvrcpHandoffHost' `
        -ErrorAction SilentlyContinue
    if ($null -ne $task) {
        throw 'The retired NativeLdacAvrcpHandoffHost task is installed.'
    }
    $processes = @(Get-CimInstance Win32_Process `
        -Filter "Name = 'v1_avrcp_handoff_host.exe'" `
        -ErrorAction SilentlyContinue)
    if ($processes.Count -ne 0) {
        throw 'The retired v1_avrcp_handoff_host.exe process is running.'
    }
}

function Assert-V1DailyAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this V1 daily host operation from an elevated PowerShell 7 terminal.'
    }
}

function Test-V1DailyInstanceSuffix {
    param([Parameter(Mandatory)][string]$Value)

    return $Value.Length -ge 1 -and $Value.Length -le 64 -and
        $Value -cmatch '^[A-Za-z0-9._-]+$'
}

function Get-V1DailyPaths {
    param(
        [Parameter(Mandatory)][string]$InstallRoot,
        [Parameter(Mandatory)][string]$RuntimeRoot
    )

    $resolvedInstallRoot = [IO.Path]::GetFullPath($InstallRoot)
    $resolvedRuntimeRoot = [IO.Path]::GetFullPath($RuntimeRoot)
    [pscustomobject]@{
        InstallRoot = $resolvedInstallRoot
        RuntimeRoot = $resolvedRuntimeRoot
        Agent = Join-Path $resolvedInstallRoot 'v1_presence_agent.exe'
        Worker = Join-Path $resolvedInstallRoot 'v1_transport_daily_worker.exe'
        Manifest = Join-Path $resolvedInstallRoot 'manifest.json'
        Config = Join-Path $resolvedRuntimeRoot 'config.json'
        InstallState = Join-Path $resolvedRuntimeRoot 'install-state.json'
        StateDirectory = Join-Path $resolvedRuntimeRoot 'state'
        State = Join-Path $resolvedRuntimeRoot 'state\daily-state.json'
        ResultDirectory = Join-Path $resolvedRuntimeRoot 'results'
        Result = Join-Path $resolvedRuntimeRoot 'results\latest-session.json'
        LogDirectory = Join-Path $resolvedRuntimeRoot 'logs'
        StandardLog = Join-Path $resolvedRuntimeRoot 'logs\daily-host.log'
        ErrorLog = Join-Path $resolvedRuntimeRoot 'logs\daily-host.error.log'
    }
}

function Read-V1DailyConfig {
    param([Parameter(Mandatory)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "V1 daily config is missing: $Path"
    }
    $config = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    if ([int]$config.schema_version -ne 1 -or
        -not (Test-V1DailyInstanceSuffix -Value ([string]$config.instance_suffix)) -or
        [long]$config.maximum_log_bytes -lt 65536 -or
        [long]$config.maximum_log_bytes -gt 33554432 -or
        [int]$config.retained_logs -lt 1 -or
        [int]$config.retained_logs -gt 8) {
        throw 'The V1 daily config does not satisfy policy 31.'
    }
    return $config
}

function Invoke-V1DailyLogRotation {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][long]$MaximumBytes,
        [Parameter(Mandatory)][int]$RetainedLogs
    )

    if ($MaximumBytes -lt 65536 -or $MaximumBytes -gt 33554432 -or
        $RetainedLogs -lt 1 -or $RetainedLogs -gt 8) {
        throw 'Invalid V1 daily log rotation bounds.'
    }
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return
    }
    $item = Get-Item -LiteralPath $Path
    if ([long]$item.Length -lt $MaximumBytes) {
        return
    }

    $oldest = "$Path.$RetainedLogs"
    if (Test-Path -LiteralPath $oldest -PathType Leaf) {
        Remove-Item -LiteralPath $oldest -Force
    }
    for ($index = $RetainedLogs - 1; $index -ge 1; --$index) {
        $source = "$Path.$index"
        $destination = "$Path.$($index + 1)"
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            Move-Item -LiteralPath $source -Destination $destination -Force
        }
    }
    Move-Item -LiteralPath $Path -Destination "$Path.1" -Force
}

function Get-V1DailyFileSha256 {
    param([Parameter(Mandatory)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Get-V1DailyObjectValue {
    param(
        [object]$Object,
        [Parameter(Mandatory)][string]$Name,
        [object]$Default = $null
    )

    if ($null -eq $Object) {
        return $Default
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $Default
    }
    return $property.Value
}

function Get-V1DailyTransportResultSet {
    param([Parameter(Mandatory)][string]$ResultPath)

    $resolvedPath = [IO.Path]::GetFullPath($ResultPath)
    $directory = [IO.Path]::GetDirectoryName($resolvedPath)
    $baseName = [IO.Path]::GetFileName($resolvedPath)
    $archivePattern = '^{0}\.generation-(\d+)\.worker-(\d+)\.attempt-(\d+)\.json$' -f
        [regex]::Escape($baseName)
    $archives = @()
    if (Test-Path -LiteralPath $directory -PathType Container) {
        $items = @(Get-ChildItem -LiteralPath $directory -File)
        foreach ($item in $items) {
            if ($item.Name -notmatch $archivePattern) {
                continue
            }
            $archives += [pscustomobject]@{
                Generation = [uint64]$Matches[1]
                Worker = [uint64]$Matches[2]
                Attempt = [uint32]$Matches[3]
                Path = $item.FullName
            }
        }
    }
    $archives = @($archives | Sort-Object Generation, Worker, Attempt)
    if ($archives.Count -eq 0) {
        if (-not (Test-Path -LiteralPath $resolvedPath -PathType Leaf)) {
            return @()
        }
        $value = Get-Content -LiteralPath $resolvedPath -Raw |
            ConvertFrom-Json
        return @([pscustomobject]@{
            Generation = [uint64](Get-V1DailyObjectValue `
                -Object $value -Name 'session_generation' -Default 0)
            Worker = [uint64]0
            Attempt = [uint32]0
            Path = $resolvedPath
            Value = $value
        })
    }

    $results = @()
    foreach ($archive in $archives) {
        $value = Get-Content -LiteralPath $archive.Path -Raw |
            ConvertFrom-Json
        $results += [pscustomobject]@{
            Generation = $archive.Generation
            Worker = $archive.Worker
            Attempt = $archive.Attempt
            Path = $archive.Path
            Value = $value
        }
    }
    return @($results)
}

function Merge-V1DailyTransportResults {
    param([Parameter(Mandatory)][object[]]$Results)

    $merged = [ordered]@{
        result_count = 0
        failed_result_count = 0
        last_disposition = ''
        last_stage = 0
        backend_error = 0
        media_packets_written = [int64]0
        actual_duration_ms = [int64]0
        pcm_stream_stop_detected = $false
        pcm_rebind_attempts = [int64]0
        pcm_rebind_failures = [int64]0
        volume_change_count = [int64]0
        media_write_not_ready_retries = [int64]0
        media_write_not_ready_exhaustions = [int64]0
        pcm_transient_timeout_count = [int64]0
        pcm_transient_timeout_recovery_count = [int64]0
        pcm_transient_timeout_exhausted_count = [int64]0
        pcm_transient_timeout_max_streak_ms = [int64]0
        pause_suspend_count = [int64]0
        pause_resume_start_count = [int64]0
        pause_wait_prepare_attempts = [int64]0
        remote_stream_cleanup_required = $false
        encoder_qualities = @()
        sample_rates_hz = @()
        bits_per_samples = @()
        channel_modes = @()
        nominal_ldac_bitrates_kbps = @()
    }
    foreach ($result in @($Results)) {
        $value = $result.Value
        ++$merged.result_count
        $disposition = [string](Get-V1DailyObjectValue `
            -Object $value -Name 'disposition' -Default '')
        $merged.last_disposition = $disposition
        $quality = [string](Get-V1DailyObjectValue `
            -Object $value -Name 'encoder_quality' -Default '')
        if ($quality -in @('HQ', 'SQ', 'MQ') -and
            $quality -notin $merged.encoder_qualities) {
            $merged.encoder_qualities += $quality
        }
        $sampleRate = [int](Get-V1DailyObjectValue `
            -Object $value -Name 'sample_rate_hz' -Default 0)
        if ($sampleRate -in @(44100, 48000, 88200, 96000) -and
            $sampleRate -notin $merged.sample_rates_hz) {
            $merged.sample_rates_hz += $sampleRate
        }
        $bits = [int](Get-V1DailyObjectValue `
            -Object $value -Name 'bits_per_sample' -Default 0)
        if ($bits -in @(16, 24) -and $bits -notin $merged.bits_per_samples) {
            $merged.bits_per_samples += $bits
        }
        $channelMode = [int](Get-V1DailyObjectValue `
            -Object $value -Name 'channel_mode' -Default 0)
        if ($channelMode -in @(1, 2, 4) -and
            $channelMode -notin $merged.channel_modes) {
            $merged.channel_modes += $channelMode
        }
        $nominalBitrate = [int](Get-V1DailyObjectValue `
            -Object $value -Name 'nominal_ldac_bitrate_kbps' -Default 0)
        if ($nominalBitrate -gt 0 -and
            $nominalBitrate -notin $merged.nominal_ldac_bitrates_kbps) {
            $merged.nominal_ldac_bitrates_kbps += $nominalBitrate
        }
        $merged.last_stage = [int](Get-V1DailyObjectValue `
            -Object $value -Name 'stage' -Default 0)
        if ($disposition -notin @('succeeded', 'cancelled')) {
            ++$merged.failed_result_count
        }
        $resultBackendError = [int](Get-V1DailyObjectValue `
            -Object $value -Name 'backend_error' -Default 0)
        if ($resultBackendError -ne 0) {
            $merged.backend_error = $resultBackendError
        }
        foreach ($name in @(
                'media_packets_written',
                'actual_duration_ms',
                'pcm_rebind_attempts',
                'pcm_rebind_failures',
                'volume_change_count',
                'media_write_not_ready_retries',
                'media_write_not_ready_exhaustions',
                'pcm_transient_timeout_count',
                'pcm_transient_timeout_recovery_count',
                'pcm_transient_timeout_exhausted_count',
                'pause_suspend_count',
                'pause_resume_start_count',
                'pause_wait_prepare_attempts')) {
            $merged[$name] += [int64](Get-V1DailyObjectValue `
                -Object $value -Name $name -Default 0)
        }
        $streak = [int64](Get-V1DailyObjectValue `
            -Object $value -Name 'pcm_transient_timeout_max_streak_ms' `
            -Default 0)
        if ($streak -gt $merged.pcm_transient_timeout_max_streak_ms) {
            $merged.pcm_transient_timeout_max_streak_ms = $streak
        }
        if ([bool](Get-V1DailyObjectValue `
                -Object $value -Name 'pcm_stream_stop_detected' `
                -Default $false)) {
            $merged.pcm_stream_stop_detected = $true
        }
        if ([bool](Get-V1DailyObjectValue `
                -Object $value -Name 'remote_stream_cleanup_required' `
                -Default $false)) {
            $merged.remote_stream_cleanup_required = $true
        }
    }
    return [pscustomobject]$merged
}

function Get-V1DailyFormatGateDecision {
    param(
        [Parameter(Mandatory)]
        [object]$TransportAggregate,
        [int]$SampleRateHz = 0,
        [int]$BitsPerSample = 0,
        [ValidateSet('stereo', 'dual', 'mono')]
        [string]$ChannelMode = 'stereo'
    )

    $observedRates = @(
        Get-V1DailyObjectValue $TransportAggregate 'sample_rates_hz' @())
    $observedBits = @(
        Get-V1DailyObjectValue $TransportAggregate 'bits_per_samples' @())
    $observedChannels = @(
        Get-V1DailyObjectValue $TransportAggregate 'channel_modes' @())
    $expectedChannel = switch ($ChannelMode) {
        'stereo' { 1 }
        'dual' { 2 }
        'mono' { 4 }
    }
    $rateMatched = $SampleRateHz -eq 0 -or
        ($observedRates.Count -eq 1 -and
         [int]$observedRates[0] -eq $SampleRateHz)
    $bitsMatched = $BitsPerSample -eq 0 -or
        ($observedBits.Count -eq 1 -and
         [int]$observedBits[0] -eq $BitsPerSample)
    $channelMatched = $observedChannels.Count -eq 1 -and
        [int]$observedChannels[0] -eq $expectedChannel
    return [pscustomobject]@{
        requested_sample_rate_hz = $SampleRateHz
        requested_bits_per_sample = $BitsPerSample
        requested_channel_mode = $ChannelMode
        expected_channel_mode_code = $expectedChannel
        observed_sample_rates_hz = $observedRates
        observed_bits_per_samples = $observedBits
        observed_channel_modes = $observedChannels
        sample_rate_matched = $rateMatched
        bits_per_sample_matched = $bitsMatched
        channel_mode_matched = $channelMatched
        passed = $rateMatched -and $bitsMatched -and $channelMatched
    }
}

function Get-V1DailyQualityGateDecision {
    param(
        [Parameter(Mandatory)]
        [ValidateSet('hq', 'sq', 'mq')]
        [string]$Quality,
        [Parameter(Mandatory)]
        [object]$TransportAggregate
    )

    $expectedQuality = $Quality.ToUpperInvariant()
    $observedSampleRates = @(
        Get-V1DailyObjectValue $TransportAggregate 'sample_rates_hz' @())
    $expectedNominalBitrates = @($observedSampleRates | ForEach-Object {
        $x441Family = [int]$_ -in @(44100, 88200)
        switch ($Quality) {
            'hq' { if ($x441Family) { 909 } else { 990 } }
            'sq' { if ($x441Family) { 606 } else { 660 } }
            'mq' { if ($x441Family) { 303 } else { 330 } }
        }
    } | Select-Object -Unique)
    if ($expectedNominalBitrates.Count -eq 0) {
        $expectedNominalBitrates = @(switch ($Quality) {
            'hq' { 909 }
            'sq' { 606 }
            'mq' { 303 }
        })
    }
    $observedQualities = @(
        Get-V1DailyObjectValue $TransportAggregate 'encoder_qualities' @())
    $observedNominalBitrates = @(
        Get-V1DailyObjectValue $TransportAggregate `
            'nominal_ldac_bitrates_kbps' @())
    $qualityMatched = $observedQualities.Count -eq 1 -and
        [string]$observedQualities[0] -ceq $expectedQuality
    $nominalBitrateMatched =
        $observedNominalBitrates.Count -eq $expectedNominalBitrates.Count -and
        @($observedNominalBitrates | Where-Object {
            [int]$_ -notin $expectedNominalBitrates
        }).Count -eq 0
    $expectedNominalBitrate = if ($expectedNominalBitrates.Count -eq 1) {
        [int]$expectedNominalBitrates[0]
    } else { 0 }
    return [pscustomobject]@{
        expected_quality = $expectedQuality
        observed_qualities = $observedQualities
        quality_matched = $qualityMatched
        expected_nominal_ldac_bitrate_kbps = $expectedNominalBitrate
        expected_nominal_ldac_bitrates_kbps = $expectedNominalBitrates
        observed_nominal_ldac_bitrates_kbps = $observedNominalBitrates
        nominal_bitrate_matched = $nominalBitrateMatched
        passed = $qualityMatched -and $nominalBitrateMatched
    }
}

function Test-V1DailyBundleManifest {
    param([Parameter(Mandatory)][string]$Root)

    $manifestPath = Join-Path $Root 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "V1 daily manifest is missing: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw |
        ConvertFrom-Json
    if ([int]$manifest.manifest_version -ne 1 -or
        [int]$manifest.host_policy_version -ne
            $script:V1DailyHostPolicyVersion -or
        $manifest.source_dirty -ne $false -or
        [int]$manifest.requires_powershell_major -ne 7 -or
        [string]$manifest.source_commit -notmatch '^[0-9a-fA-F]{40}$' -or
        [string]$manifest.configuration -cne 'Release' -or
        [string]$manifest.audio_policy.gain -cne 'unity' -or
        [double]$manifest.audio_policy.sample_peak_ceiling -ne 1.0 -or
        $manifest.audio_policy.dynamic_windows_volume -ne $true -or
        [double]$manifest.audio_policy.startup_silence_ms -ne 20.0 -or
        [double]$manifest.audio_policy.fade_in_ms -ne 500.0 -or
        $manifest.audio_policy.transient_resume_startup_silence -ne $true -or
        $manifest.audio_policy.transient_resume_fade_in -ne $true -or
        [double]$manifest.audio_policy.ceiling_ramp_ms -ne 0.0 -or
        $manifest.audio_policy.continuous_until_explicit_stop -ne $true -or
        (@($manifest.audio_policy.quality_allowlist) -join ',') -cne 'HQ,SQ,MQ' -or
        [string]$manifest.audio_policy.quality_selection_boundary -cne
            'next_safe_media_session' -or
        [int]$manifest.lifecycle_policy.render_stability_ms -ne 1000 -or
        $manifest.lifecycle_policy.same_acl_multiple_media_sessions -ne
            $true -or
        $manifest.lifecycle_policy.fault_requires_fresh_acl -ne $true -or
        $manifest.lifecycle_policy.graceful_daily_stop -ne $true) {
        throw 'The V1 daily bundle manifest header is invalid.'
    }
    $files = @($manifest.files)
    if ($files.Count -lt 6) {
        throw 'The V1 daily bundle manifest is incomplete.'
    }
    foreach ($entry in $files) {
        $relativePath = [string]$entry.path
        if ([string]::IsNullOrWhiteSpace($relativePath) -or
            [IO.Path]::IsPathRooted($relativePath) -or
            [IO.Path]::GetFileName($relativePath) -cne $relativePath) {
            throw "Unsafe V1 daily manifest path: $relativePath"
        }
        $filePath = Join-Path $Root $relativePath
        if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) {
            throw "V1 daily bundle file is missing: $relativePath"
        }
        $item = Get-Item -LiteralPath $filePath
        if ([long]$entry.length -ne [long]$item.Length -or
            [string]$entry.sha256 -cne
                (Get-V1DailyFileSha256 -Path $filePath)) {
            throw "V1 daily bundle file hash mismatch: $relativePath"
        }
    }
    return $manifest
}

function Get-V1DailyProcesses {
    param([Parameter(Mandatory)][string]$AgentPath)

    $expectedPath = [IO.Path]::GetFullPath($AgentPath)
    $processes = @(Get-CimInstance Win32_Process `
        -Filter "Name = 'v1_presence_agent.exe'" `
        -ErrorAction SilentlyContinue)
    $matches = @()
    foreach ($process in $processes) {
        if ([string]::IsNullOrWhiteSpace([string]$process.ExecutablePath)) {
            continue
        }
        $processPath = [IO.Path]::GetFullPath(
            [string]$process.ExecutablePath)
        if ($processPath.Equals(
                $expectedPath,
                [StringComparison]::OrdinalIgnoreCase) -and
            [string]$process.CommandLine -match '(?i)(?:^|\s)--daily(?:\s|$)') {
            $matches += $process
        }
    }
    return @($matches)
}
