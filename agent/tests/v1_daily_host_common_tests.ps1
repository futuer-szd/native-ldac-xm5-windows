# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $projectRoot 'tools\v1-daily-host-common.ps1')
Assert-V1DailyPowerShell7

$temporaryRoot = Join-Path $projectRoot `
    "tmp\v1-daily-common-$PID-$([DateTime]::UtcNow.Ticks)"
try {
    New-Item -ItemType Directory -Path $temporaryRoot -Force | Out-Null
    $configPath = Join-Path $temporaryRoot 'config.json'
    [ordered]@{
        schema_version = 1
        instance_suffix = 'ctest-1'
        maximum_log_bytes = 65536
        retained_logs = 2
    } | ConvertTo-Json | Set-Content -LiteralPath $configPath `
        -Encoding utf8NoBOM
    $config = Read-V1DailyConfig -Path $configPath
    if ([string]$config.instance_suffix -cne 'ctest-1') {
        throw 'The valid daily config did not round-trip.'
    }

    $logPath = Join-Path $temporaryRoot 'daily.log'
    [IO.File]::WriteAllBytes($logPath, [byte[]]::new(65536))
    Invoke-V1DailyLogRotation -Path $logPath -MaximumBytes 65536 `
        -RetainedLogs 2
    if ((Test-Path -LiteralPath $logPath) -or
        -not (Test-Path -LiteralPath "$logPath.1" -PathType Leaf)) {
        throw 'First bounded daily log rotation failed.'
    }
    [IO.File]::WriteAllBytes($logPath, [byte[]]::new(65536))
    Invoke-V1DailyLogRotation -Path $logPath -MaximumBytes 65536 `
        -RetainedLogs 2
    if (-not (Test-Path -LiteralPath "$logPath.1" -PathType Leaf) -or
        -not (Test-Path -LiteralPath "$logPath.2" -PathType Leaf) -or
        (Test-Path -LiteralPath "$logPath.3")) {
        throw 'Retained daily log generations escaped their bound.'
    }

    $bundle = Join-Path $temporaryRoot 'bundle'
    New-Item -ItemType Directory -Path $bundle | Out-Null
    $entries = @()
    foreach ($name in @('a.exe', 'b.exe', 'c.ps1', 'd.ps1', 'e.ps1', 'f.ps1')) {
        $file = Join-Path $bundle $name
        Set-Content -LiteralPath $file -Value $name -Encoding utf8NoBOM
        $item = Get-Item -LiteralPath $file
        $entries += [ordered]@{
            path = $name
            length = [long]$item.Length
            sha256 = Get-V1DailyFileSha256 -Path $file
        }
    }
    [ordered]@{
        manifest_version = 1
        host_policy_version = 31
        source_commit = ('a' * 40)
        source_dirty = $false
        configuration = 'Release'
        requires_powershell_major = 7
        audio_policy = [ordered]@{
            quality_allowlist = @('HQ', 'SQ', 'MQ')
            quality_selection_boundary = 'next_safe_media_session'
            gain = 'unity'
            sample_peak_ceiling = 1.0
            dynamic_windows_volume = $true
            startup_silence_ms = 20.0
            fade_in_ms = 500.0
            transient_resume_startup_silence = $true
            transient_resume_fade_in = $true
            ceiling_ramp_ms = 0.0
            continuous_until_explicit_stop = $true
        }
        lifecycle_policy = [ordered]@{
            render_stability_ms = 1000
            same_acl_multiple_media_sessions = $true
            fault_requires_fresh_acl = $true
            graceful_daily_stop = $true
        }
        files = @($entries)
    } | ConvertTo-Json -Depth 4 | Set-Content `
        -LiteralPath (Join-Path $bundle 'manifest.json') `
        -Encoding utf8NoBOM
    $null = Test-V1DailyBundleManifest -Root $bundle

    Add-Content -LiteralPath (Join-Path $bundle 'a.exe') `
        -Value 'tamper' -Encoding utf8NoBOM
    $tamperRejected = $false
    try {
        $null = Test-V1DailyBundleManifest -Root $bundle
    } catch {
        $tamperRejected = $true
    }
    if (-not $tamperRejected) {
        throw 'A tampered daily bundle was accepted.'
    }

    $resultPath = Join-Path $temporaryRoot 'transport-result.json'
    $generation1 = [ordered]@{
        session_generation = 1
        disposition = 'cancelled'
        stage = 11
        backend_error = 0
        media_packets_written = 100
        actual_duration_ms = 1000
        pcm_stream_stop_detected = $false
        pcm_rebind_attempts = 0
        pcm_rebind_failures = 0
        volume_change_count = 0
        media_write_not_ready_retries = 0
        media_write_not_ready_exhaustions = 0
        pcm_transient_timeout_count = 1
        pcm_transient_timeout_recovery_count = 1
        pcm_transient_timeout_exhausted_count = 0
        pcm_transient_timeout_max_streak_ms = 250
        pause_suspend_count = 2
        pause_resume_start_count = 2
        pause_wait_prepare_attempts = 5
        remote_stream_cleanup_required = $false
        encoder_quality = 'HQ'
        sample_rate_hz = 44100
        bits_per_sample = 24
        channel_mode = 1
        nominal_ldac_bitrate_kbps = 909
    }
    $generation2 = [ordered]@{
        session_generation = 2
        disposition = 'cancelled'
        stage = 11
        backend_error = 0
        media_packets_written = 200
        actual_duration_ms = 2000
        pcm_stream_stop_detected = $false
        pcm_rebind_attempts = 1
        pcm_rebind_failures = 0
        volume_change_count = 0
        media_write_not_ready_retries = 2
        media_write_not_ready_exhaustions = 0
        pcm_transient_timeout_count = 0
        pcm_transient_timeout_recovery_count = 0
        pcm_transient_timeout_exhausted_count = 0
        pcm_transient_timeout_max_streak_ms = 0
        pause_suspend_count = 1
        pause_resume_start_count = 1
        pause_wait_prepare_attempts = 3
        remote_stream_cleanup_required = $false
        encoder_quality = 'HQ'
        sample_rate_hz = 44100
        bits_per_sample = 24
        channel_mode = 1
        nominal_ldac_bitrate_kbps = 909
    }
    $generation2 | ConvertTo-Json | Set-Content `
        -LiteralPath $resultPath -Encoding utf8NoBOM
    $generation1 | ConvertTo-Json | Set-Content `
        -LiteralPath "$resultPath.generation-1.worker-1.attempt-1.json" `
        -Encoding utf8NoBOM
    $generation2 | ConvertTo-Json | Set-Content `
        -LiteralPath "$resultPath.generation-2.worker-2.attempt-1.json" `
        -Encoding utf8NoBOM
    $transportResults = @(Get-V1DailyTransportResultSet `
        -ResultPath $resultPath)
    if ($transportResults.Count -ne 2 -or
        [uint64]$transportResults[0].Generation -ne 1 -or
        [uint64]$transportResults[1].Generation -ne 2) {
        throw 'Archived transport results were not selected and ordered.'
    }
    $transportSummary = Merge-V1DailyTransportResults `
        -Results $transportResults
    if ([int]$transportSummary.result_count -ne 2 -or
        [int64]$transportSummary.media_packets_written -ne 300 -or
        [int64]$transportSummary.actual_duration_ms -ne 3000 -or
        [int64]$transportSummary.pause_suspend_count -ne 3 -or
        [int64]$transportSummary.pause_resume_start_count -ne 3 -or
        [int64]$transportSummary.pause_wait_prepare_attempts -ne 8 -or
        [int64]$transportSummary.pcm_rebind_attempts -ne 1 -or
        [int64]$transportSummary.media_write_not_ready_retries -ne 2 -or
        [int64]$transportSummary.pcm_transient_timeout_max_streak_ms -ne 250 -or
        [int]$transportSummary.failed_result_count -ne 0) {
        throw 'Multi-generation transport summary aggregation is incorrect.'
    }
    if (@($transportSummary.encoder_qualities).Count -ne 1 -or
        [string]$transportSummary.encoder_qualities[0] -cne 'HQ' -or
        @($transportSummary.sample_rates_hz).Count -ne 1 -or
        [int]$transportSummary.sample_rates_hz[0] -ne 44100 -or
        @($transportSummary.bits_per_samples).Count -ne 1 -or
        [int]$transportSummary.bits_per_samples[0] -ne 24 -or
        @($transportSummary.channel_modes).Count -ne 1 -or
        [int]$transportSummary.channel_modes[0] -ne 1 -or
        @($transportSummary.nominal_ldac_bitrates_kbps).Count -ne 1 -or
        [int]$transportSummary.nominal_ldac_bitrates_kbps[0] -ne 909) {
        throw 'Quality and nominal bitrate aggregation is incorrect.'
    }
    $hqDecision = Get-V1DailyQualityGateDecision -Quality hq `
        -TransportAggregate $transportSummary
    if (-not $hqDecision.passed) {
        throw 'Matching HQ quality evidence was rejected.'
    }
    $formatDecision = Get-V1DailyFormatGateDecision `
        -TransportAggregate $transportSummary -SampleRateHz 44100 `
        -BitsPerSample 24 -ChannelMode stereo
    if (-not $formatDecision.passed) {
        throw 'Matching 44.1-kHz/24-bit/stereo format evidence was rejected.'
    }
    foreach ($mismatch in @(
            @{ Rate = 48000; Bits = 24; Mode = 'stereo' },
            @{ Rate = 44100; Bits = 16; Mode = 'stereo' },
            @{ Rate = 44100; Bits = 24; Mode = 'dual' },
            @{ Rate = 44100; Bits = 24; Mode = 'mono' })) {
        if ((Get-V1DailyFormatGateDecision `
                -TransportAggregate $transportSummary `
                -SampleRateHz $mismatch.Rate -BitsPerSample $mismatch.Bits `
                -ChannelMode $mismatch.Mode).passed) {
            throw 'Mismatched daily format evidence was accepted.'
        }
    }
    foreach ($mode in @(
            @{ Name = 'dual'; Code = 2 },
            @{ Name = 'mono'; Code = 4 })) {
        $modeAggregate = [pscustomobject]@{
            sample_rates_hz = @(44100)
            bits_per_samples = @(24)
            channel_modes = @($mode.Code)
        }
        if (-not (Get-V1DailyFormatGateDecision `
                -TransportAggregate $modeAggregate -SampleRateHz 44100 `
                -BitsPerSample 24 -ChannelMode $mode.Name).passed) {
            throw "Matching $($mode.Name) format evidence was rejected."
        }
    }
    $mixedFormatAggregate = [pscustomobject]@{
        sample_rates_hz = @(44100, 48000)
        bits_per_samples = @(16, 24)
        channel_modes = @(1, 2)
    }
    if ((Get-V1DailyFormatGateDecision `
            -TransportAggregate $mixedFormatAggregate -SampleRateHz 44100 `
            -BitsPerSample 24 -ChannelMode stereo).passed) {
        throw 'Mixed daily format evidence was accepted.'
    }
    $hq96Aggregate = [pscustomobject]@{
        encoder_qualities = @('HQ')
        sample_rates_hz = @(96000)
        nominal_ldac_bitrates_kbps = @(990)
    }
    $hq96Decision = Get-V1DailyQualityGateDecision -Quality hq `
        -TransportAggregate $hq96Aggregate
    if (-not $hq96Decision.passed -or
        [int]$hq96Decision.expected_nominal_ldac_bitrate_kbps -ne 990) {
        throw 'Matching 96-kHz HQ quality evidence was rejected.'
    }
    $wrongDecision = Get-V1DailyQualityGateDecision -Quality sq `
        -TransportAggregate $transportSummary
    if ($wrongDecision.passed -or $wrongDecision.quality_matched -or
        $wrongDecision.nominal_bitrate_matched) {
        throw 'Mismatched SQ quality evidence was accepted.'
    }
    $mixedAggregate = [pscustomobject]@{
        encoder_qualities = @('HQ', 'SQ')
        sample_rates_hz = @(44100)
        nominal_ldac_bitrates_kbps = @(909, 606)
    }
    if ((Get-V1DailyQualityGateDecision -Quality hq `
            -TransportAggregate $mixedAggregate).passed) {
        throw 'Mixed quality evidence was accepted.'
    }
} finally {
    if (Test-Path -LiteralPath $temporaryRoot -PathType Container) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}

Write-Host 'V1 daily host common behavior tests passed.'
