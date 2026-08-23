# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Read-ProjectFile([string]$RelativePath) {
    return Get-Content -LiteralPath (Join-Path $projectRoot $RelativePath) `
        -Raw
}

$common = Read-ProjectFile 'tools\v1-daily-host-common.ps1'
$start = Read-ProjectFile 'tools\start-v1-daily-host.ps1'
$stop = Read-ProjectFile 'tools\stop-v1-daily-host.ps1'
$status = Read-ProjectFile 'tools\get-v1-daily-host-status.ps1'
$install = Read-ProjectFile 'tools\install-v1-daily-autostart.ps1'
$remove = Read-ProjectFile 'tools\remove-v1-daily-autostart.ps1'
$uiRunner = Read-ProjectFile 'tools\run-v1-daily-ui.ps1'
$qualityGate = Read-ProjectFile 'tools\run-v1-daily-quality-gate.ps1'
$dailyTrial = Read-ProjectFile 'tools\run-v1-daily-full-cycle.ps1'
$presenceAgent = Read-ProjectFile 'agent\v1_presence_agent.cpp'
$engineHost = Read-ProjectFile 'agent\v1_engine_ready_host.cpp'
$workerOptions = Read-ProjectFile 'agent\v1_transport_configuration_worker.cpp'
$pcmAdapter = Read-ProjectFile 'agent\v1_transport_pcm_source_adapter.cpp'
$formatControl = Read-ProjectFile 'tools\endpoint_format_control.cpp'
$build = Read-ProjectFile 'tools\build-v1-daily-host-candidate.ps1'
$cmake = Read-ProjectFile 'CMakeLists.txt'

foreach ($required in @(
        'requires PowerShell 7',
        'maximum_log_bytes',
        'retained_logs',
        'Invoke-V1DailyLogRotation',
        'Test-V1DailyBundleManifest',
        'sample_peak_ceiling -ne 1.0',
        'fault_requires_fresh_acl',
        'Get-V1DailyProcesses')) {
    if (-not $common.Contains($required)) {
        throw "V1 daily common policy is missing: $required"
    }
}
foreach ($required in @(
        "'--sample-rate'",
        "'--bits'",
        '$SampleRateHz -ne 0',
        '$BitsPerSample -ne 0')) {
    if (-not $dailyTrial.Contains($required)) {
        throw "V1 daily trial format forwarding is missing: $required"
    }
}
foreach ($required in @(
        'daily_sample_rate_hz',
        'daily_bits_per_sample',
        'argument == L"--sample-rate"',
        'argument == L"--bits"',
        ' --sample-rate ',
        ' --bits ',
        'native_pcm_source_set_preferred_format',
        'native_pcm_source_get_preferred_format',
        'before.sample_rate_hz == requested_sample_rate_hz_',
        'requested_sample_rate_hz_',
        'requested_bits_per_sample_')) {
    $implementation = $presenceAgent + $engineHost + $workerOptions + $pcmAdapter
    if (-not $implementation.Contains($required)) {
        throw "V1 daily PCM format forwarding is missing: $required"
    }
}
foreach ($required in @(
        "'--daily'",
        "'--engine-executable'",
        '$paths.Worker',
        "PSObject.Properties['volume_sync']",
        "PSObject.Properties['enabled']",
        "'--volume-sync'",
        '1>> $paths.StandardLog',
        '2>> $paths.ErrorLog')) {
    if (-not $start.Contains($required)) {
        throw "V1 daily start policy is missing: $required"
    }
}
if (-not $common.Contains(
        "Worker = Join-Path `$resolvedInstallRoot 'v1_transport_daily_worker.exe'")) {
    throw 'The V1 daily worker path is not fixed by the shared path policy.'
}
foreach ($required in @(
        '--stop-daily',
        'It was not forcibly terminated')) {
    if (-not $stop.Contains($required)) {
        throw "V1 daily stop policy is missing: $required"
    }
}
if ($stop -match '(?i)Stop-Process|taskkill|TerminateProcess') {
    throw 'The V1 daily stop wrapper contains a forced termination path.'
}
foreach ($required in @(
        'SupportsShouldProcess',
        'ConfirmV1DailyInstall',
        '$script:V1LegacyTaskName',
        'New-ScheduledTaskTrigger -AtLogOn',
        'MultipleInstances IgnoreNew',
        'RestartCount 3',
        'ExecutionTimeLimit ([timespan]::Zero)',
        'Get-Command pwsh.exe')) {
    if (-not $install.Contains($required)) {
        throw "V1 daily installer policy is missing: $required"
    }
}
foreach ($required in @(
        'ConfirmV1DailyRemoval',
        'Unregister-ScheduledTask',
        'Preserved config, state, results, and logs')) {
    if (-not $remove.Contains($required)) {
        throw "V1 daily remover policy is missing: $required"
    }
}
foreach ($required in @(
        'sourceStatus.Count -ne 0',
        '--target v1_presence_agent v1_transport_daily_worker audio_endpoint_probe endpoint_format_control',
        'sample_peak_ceiling = 1.0',
        'fade_in_ms = 500.0',
        "'run-v1-daily-ui.ps1'",
        "'run-v1-daily-full-cycle.ps1'",
        "'run-v1-daily-quality-gate.ps1'",
        "'audio_endpoint_probe.exe'",
        "'endpoint_format_control.exe'",
        "'ldac_control.py'",
        "'daily_state.py'",
        'ceiling_ramp_ms = 0.0',
        'No driver, task, process, Bluetooth request')) {
    if (-not $build.Contains($required)) {
        throw "V1 daily candidate builder policy is missing: $required"
    }
}
foreach ($required in @(
        "[ValidateSet('hq', 'sq', 'mq')]",
        '[ValidateSet(44100, 48000, 88200, 96000)]',
        '[ValidateSet(16, 24)]',
        "[ValidateSet('stereo', 'dual', 'mono')]",
        'audio_endpoint_probe.exe',
        "'--set-format'",
        'endpoint_format_control.exe',
        'channel[(]s[)]',
        '([0-9]+)-bit container',
        'Test-V1DailyBundleManifest',
        'manifest.source_commit',
        "[IO.Directory]::GetParent(`$projectRoot)",
        "Join-Path `$projectRoot '.git'",
        'run-v1-daily-full-cycle.ps1')) {
    if (-not $qualityGate.Contains($required)) {
        throw "The packaged quality gate is missing: $required"
    }
}
foreach ($required in @(
        'NATIVE_LDAC_V1_STATE_PATH',
        'NATIVE_LDAC_V1_RESULT_PATH',
        'pythonw.exe',
        'Start-Process')) {
    if (-not $uiRunner.Contains($required)) {
        throw "V1 daily UI runner is missing: $required"
    }
}
foreach ($forbidden in @(
        'Assert-V1DailyAdministrator',
        'RunLevel Highest',
        'SetEvent',
        '--stop-daily')) {
    if ($uiRunner.Contains($forbidden)) {
        throw "V1 daily read-only UI runner gained control authority: $forbidden"
    }
}
$formatImplementation = $formatControl
foreach ($forbidden in @(
        'SetDefaultEndpoint',
        'SetMasterVolume',
        'SetMute',
        'SetDefaultAudioEndpoint',
        'pnputil',
        'Restart-Computer')) {
    if ($formatImplementation.Contains($forbidden)) {
        throw "Endpoint format control contains a forbidden system operation: $forbidden"
    }
}
foreach ($forbidden in @(
        'pnputil',
        'Disable-PnpDevice',
        'Enable-PnpDevice',
        'Set-NetAdapter',
        'BluetoothSetServiceState',
        'SetDefaultEndpoint')) {
    $implementation = $common + $start + $stop + $status +
        $install + $remove + $uiRunner + $build
    if ($implementation.IndexOf(
            $forbidden,
            [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "V1 daily packaging touches a forbidden system path: $forbidden"
    }
}
foreach ($required in @(
        'v1_daily_host_common',
        'v1_daily_packaging_policy',
        'COMMAND pwsh.exe')) {
    if (-not $cmake.Contains($required)) {
        throw "V1 daily packaging CTest policy is missing: $required"
    }
}
if (-not $status.Contains('completed_media_sessions_for_generation')) {
    throw 'The V1 daily status wrapper omits session telemetry.'
}

Write-Host 'V1 daily packaging policy tests passed.'
