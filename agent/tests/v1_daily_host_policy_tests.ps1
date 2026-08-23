# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))

function Read-ProjectFile([string] $RelativePath) {
    return Get-Content -LiteralPath `
        (Join-Path $projectRoot $RelativePath) -Raw
}

$agent = Read-ProjectFile 'agent\v1_presence_agent.cpp'
$lifecycle = Read-ProjectFile 'agent\v1_lifecycle.cpp'
$lifecycleHeader = Read-ProjectFile 'agent\v1_lifecycle.h'
$instance = Read-ProjectFile 'agent\v1_daily_instance.cpp'
$worker = Read-ProjectFile 'agent\v1_transport_daily_worker.cpp'
$observerHost = Read-ProjectFile 'agent\v1_avrcp_observer_host.cpp'
$filterHost = Read-ProjectFile 'agent\v1_avrcp_filter_host.cpp'
$windowsSink = Read-ProjectFile 'agent\v1_avrcp_windows_sink.cpp'
$volumeEndpointSelector = Read-ProjectFile `
    'agent\v1_native_ldac_volume_endpoint_selector.cpp'
$mediaMonitor = Read-ProjectFile 'agent\v1_media_session_monitor.cpp'
$hfpMonitor = Read-ProjectFile 'agent\v1_hfp_capture_monitor.cpp'
$hfpRenderMonitor = Read-ProjectFile 'agent\v1_hfp_render_endpoint_monitor.cpp'
$hfpRenderSelector = Read-ProjectFile 'agent\v1_hfp_render_endpoint_selector.cpp'
$hfpSwitch = Read-ProjectFile 'agent\v1_hfp_switch_state.cpp'
$hfpShadow = Read-ProjectFile 'agent\v1_hfp_shadow_state.cpp'
$cmake = Read-ProjectFile 'CMakeLists.txt'
$dailyInstall = Read-ProjectFile 'tools\install-v1-daily-autostart.ps1'
$dailyStart = Read-ProjectFile 'tools\start-v1-daily-host.ps1'
$dailyTrial = Read-ProjectFile 'tools\run-v1-daily-full-cycle.ps1'
$qualityGate = Read-ProjectFile 'tools\run-v1-daily-quality-gate.ps1'
$engineHost = Read-ProjectFile 'agent\v1_engine_ready_host.cpp'
$engineHostHeader = Read-ProjectFile 'agent\v1_engine_ready_host.h'
$workerOptions = Read-ProjectFile 'agent\v1_transport_configuration_worker.cpp'
$pcmAdapter = Read-ProjectFile 'agent\v1_transport_pcm_source_adapter.cpp'
$dailyCommon = Read-ProjectFile 'tools\v1-daily-host-common.ps1'
$dailyIpc = Read-ProjectFile 'agent\v1_daily_config_ipc.h'

foreach ($required in @(
        'EnableVolumeSync',
        'volume_sync = [ordered]@{',
        'enabled = [bool]$EnableVolumeSync')) {
    if (-not $dailyInstall.Contains($required)) {
        throw "V1 daily installer is missing explicit volume/handoff configuration: $required"
    }
}
foreach ($required in @(
        'L"--quality"',
        'daily_quality',
        'ParseV1DailyQuality',
        'V1DailyQualityName',
        'NativeLdac.V1.Config." + options.instance_suffix')) {
    if (-not ($agent.Contains($required) -or $dailyIpc.Contains($required))) {
        throw "V1 daily quality configuration contract is missing: $required"
    }
}
foreach ($required in @(
        "[ValidateSet('hq', 'sq', 'mq')]",
        'expected_quality',
        'quality_matched',
        'expected_nominal_ldac_bitrate_kbps',
        'nominal_bitrate_matched')) {
    if (-not $dailyTrial.Contains($required)) {
        throw "V1 daily quality gate summary is missing: $required"
    }
}
foreach ($required in @(
        "'--sample-rate'",
        "'--bits'",
        '$SampleRateHz -ne 0',
        '$BitsPerSample -ne 0')) {
    if (-not $dailyTrial.Contains($required)) {
        throw "V1 daily runner is missing PCM format forwarding: $required"
    }
}
foreach ($required in @(
        'daily_sample_rate_hz',
        'daily_bits_per_sample',
        'argument == L"--sample-rate"',
        'argument == L"--bits"')) {
    if (-not $agent.Contains($required)) {
        throw "V1 daily agent is missing PCM format parsing: $required"
    }
}
foreach ($required in @(
        'sample_rate_hz',
        'bits_per_sample',
        ' --sample-rate ',
        ' --bits ')) {
    if (-not ($engineHost.Contains($required) -or
              $engineHostHeader.Contains($required) -or
              $workerOptions.Contains($required))) {
        throw "V1 daily worker format forwarding is missing: $required"
    }
}
foreach ($required in @(
        'native_pcm_source_set_preferred_format',
        'native_pcm_source_get_preferred_format',
        'before.sample_rate_hz == requested_sample_rate_hz_',
        'snapshot.sample_rate_hz, snapshot.bits_per_sample',
        'requested_sample_rate_hz_',
        'requested_bits_per_sample_',
        'snapshot.sample_rate_hz == requested_sample_rate_hz_')) {
    if (-not $pcmAdapter.Contains($required)) {
        throw "V1 daily PCM active-format enforcement is missing: $required"
    }
}
foreach ($required in @(
        '[ValidateSet(44100, 48000, 88200, 96000)]',
        '[ValidateSet(16, 24)]',
        "[ValidateSet('stereo', 'dual', 'mono')]",
        '$SampleRateHz = 44100',
        '$BitsPerSample = 16',
        "`$ChannelMode = 'stereo'",
        'Assert-V1DailyAdministrator',
        'audio_endpoint_probe.exe',
        'endpoint_format_control.exe',
        'Windows shared-mode endpoint format',
        'channel[(]s[)]',
        '([0-9]+)-bit container',
        "'--set-format'",
        'Get-V1DailyEndpointFormat',
        'Get-V1DailySharedEndpointFormat',
        'shared_mode_restored = $sharedRestored',
        'restored = $restored')) {
    if (-not $qualityGate.Contains($required)) {
        throw "V1 daily endpoint format gate is missing: $required"
    }
}
foreach ($required in @(
        "PSObject.Properties['volume_sync']",
        'V1 daily host remains running until an explicit stop request.',
        'State: $($paths.State)',
        'Transport result: $($paths.Result)')) {
    if (-not $dailyStart.Contains($required)) {
        throw "V1 daily starter is missing safe runtime diagnostics: $required"
    }
}
foreach ($required in @(
        "'daily-full-cycle-'",
        "'daily-state.json'",
        "'transport-result.json'",
        "'daily-host.log'",
        "'daily-events.jsonl'",
        "'daily-summary.json'",
        'V1 daily host started (',
        'volume-sync=on',
        'handoff=not-used',
        'Stop: & "{0}"',
        '等待 XM5 连接：请现在开启或重新连接 XM5；连接成功后再开始播放。',
        'Add-V1DailyEvent',
        'Add-V1DailySimpleLog',
        'record_kind = ''v1-daily-summary''',
        'daily.completed',
        'Write-V1DailyRuntimeLine',
        'action set-windows-volume',
        'action send-xm5-volume',
        'observer.control_ready',
        'control_ready_count',
        'control_timeout_count',
        'render.pause_suspended',
        'LDAC 媒体包已停止',
        'media_key.microsoft_owned',
        'media_key.bootstrap_scheduled',
        'bootstrap_play_fallback_injected_count',
        'ConvertFrom-Json',
        '--stop-daily --instance-suffix')) {
    if (-not $dailyTrial.Contains($required)) {
        throw "V1 daily trial runner is missing controlled runtime handling: $required"
    }
}
foreach ($required in @(
        'Get-V1DailyTransportResultSet',
        'Merge-V1DailyTransportResults',
        'failed_transport_result_count',
        'transport_result_count',
        'transport_results = $transportResultPaths',
        '$avrcpControlRequired = [bool]$VolumeSync')) {
    if (-not ($dailyTrial.Contains($required) -or
              $dailyCommon.Contains($required))) {
        throw "V1 daily multi-generation summary is missing: $required"
    }
}
if ($dailyTrial.Contains(
        '$avrcpControlRequired = [bool]$VolumeSync -and [bool]$Handoff')) {
    throw 'The Microsoft-preserving volume bridge is still optional in the daily summary.'
}

foreach ($required in @(
        '--daily',
        '--stop-daily',
        '--instance-suffix',
        '\"host_process_id\": %lu',
        'GetCurrentProcessId()',
        'WaitForMultipleObjects(',
        'wait_handle_count, wait_handles, FALSE, timeout)',
        'const ULONGLONG remaining = wake_tick > now',
        '? wake_tick - now',
        ': 0u;',
        'transport_open_render_stability_ms = 1000u',
        'allow_multiple_media_sessions = true',
        'V1ActionGracefulStopTransport',
        'transport_worker_sequence',
        'L".worker-"')) {
    if (-not $agent.Contains($required)) {
        throw "V1 daily host is missing: $required"
    }
}
if ($agent.Contains('const ULONGLONG remaining = wake_tick - now;')) {
    throw 'The daily scheduler can underflow when a synchronous action overruns a deadline.'
}

foreach ($required in @(
        'V1AvrcpObserverHost',
        'ReconcileAvrcpObserver',
        'V1AvrcpWindowsSink',
        '--volume-sync',
        'avrcp_volume_sync_enabled',
        'apply_endpoint_volume',
        'V1 daily AVRCP activation requested after MediaStarted',
        'V1 daily AVRCP control channel ready',
        'single_gain_ready',
        'SetSingleGainReady',
        'volume_change_event',
        'control readiness timed out',
        'kAvrcpActivePollMs = 25u',
        'next_avrcp_poll',
        'volume sync is fail-safe disabled for the',
        'Microsoft AVRCP owner handoff is',
        'ERROR_NOT_SUPPORTED',
        'RequestAvrcpHandoffWithEndpointQuiesce',
        'RebindEndpointPresenceLease',
        'endpoint_presence_rebinds',
        'avrcp_observer_activation_attempts')) {
    if (-not $agent.Contains($required)) {
        throw "V1 daily AVRCP host integration is missing: $required"
    }
}

$mediaStartedIndex = $agent.LastIndexOf('V1TransportWorkerEvent::MediaStarted')
$reconcileIndex = $agent.IndexOf('ReconcileAvrcpControl(', $mediaStartedIndex)
if ($mediaStartedIndex -lt 0 -or $reconcileIndex -le $mediaStartedIndex) {
    throw 'The daily AVRCP control bridge is not activated after MediaStarted.'
}

$handoffRequestIndex = $agent.IndexOf(
    'RequestAvrcpHandoffWithEndpointQuiesce(')
if ($handoffRequestIndex -lt 0 -or
    $agent.Contains('V1 ACL pre-audio AVRCP handoff/rebind failed') -or
    $agent.Contains('handoff completed before PCM authorization')) {
    throw 'AVRCP handoff must follow the proven MediaStarted bootstrap order.'
}

foreach ($required in @(
        'IOCTL_NLD_AVRCP_FILTER_SET_ABSOLUTE_VOLUME',
        'V1AvrcpFilterHost',
        'Microsoft-preserving AVRCP volume bridge',
        'BeginSession',
        'SetAbsoluteVolume',
        'V1AvrcpEndAclGeneration',
        'mapper_.acl_generation != generation',
        'mapper_.headset_preferred = options_.headset_preferred',
        'options_.media_routing = false',
        'V1AvrcpBootstrapPlayDecision::InjectPlay')) {
    if (-not ($agent.Contains($required) -or $filterHost.Contains($required))) {
        throw "The Microsoft-preserving filter bridge is missing: $required"
    }
}

foreach ($required in @(
        'physically_connected',
        'resident_filter_poll',
        'V1 ACL connection control hook failed')) {
    if (-not $agent.Contains($required)) {
        throw "The resident ACL connection hook is missing: $required"
    }
}

if (-not $agent.Contains('setvbuf(stdout, nullptr, _IONBF, 0u)') -or
    -not $agent.Contains('setvbuf(stderr, nullptr, _IONBF, 0u)')) {
    throw 'The redirected daily native output can buffer real-time action events.'
}

foreach ($required in @(
        'GENERIC_READ',
        'OpenReadWrite',
        'IOCTL_NLD_AVRCP_OBSERVER_SEND_COMMAND',
        'write_enabled',
        'IOCTL_NLD_AVRCP_OBSERVER_BEGIN_OBSERVATION',
        'NLD_AVRCP_OBSERVER_ABI_MINOR',
        'V1AvrcpObserverActivationResult')) {
    if (-not $observerHost.Contains($required)) {
        throw "V1 daily AVRCP observer host is missing: $required"
    }
}
foreach ($forbidden in @(
        'SendInput',
        'IAudioEndpointVolume')) {
    if ($observerHost.Contains($forbidden)) {
        throw "V1 daily AVRCP observer host does Windows audio writes directly: $forbidden"
    }
}
foreach ($required in @(
        'RegisterControlChangeNotify',
        'UnregisterControlChangeNotify',
        'IAudioEndpointVolumeCallback',
        'ConsumeWindowsVolumeChange',
        'EnumAudioEndpoints',
        'PKEY_Device_FriendlyName',
        'PKEY_Device_ContainerId',
        'NativeLdacRemoteContainerId',
        'ReleaseVolumeEndpoint',
        'pending_windows_volume_valid_')) {
    if (-not $windowsSink.Contains($required)) {
        throw "V1 Windows volume notification path is missing: $required"
    }
}
foreach ($required in @(
        'V1NativeLdacVolumeEndpointIdentity::Matched',
        'V1NativeLdacVolumeEndpointIdentity::Ambiguous')) {
    if (-not $volumeEndpointSelector.Contains($required)) {
        throw "V1 exact Native LDAC volume endpoint selector is missing: $required"
    }
}
foreach ($required in @(
        'options.daily_mode && options.volume_sync',
        'WindowsVolumeEndpointReady()',
        'filter_host->initial_volume_seen() && endpoint_ready')) {
    if (-not $agent.Contains($required)) {
        throw "V1 exact volume endpoint readiness gate is missing: $required"
    }
}
foreach ($required in @(
        'audio.volume_endpoint_bound',
        'Native LDAC 音量端点暂时不可用')) {
    if (-not $dailyTrial.Contains($required)) {
        throw "V1 daily trial lacks exact volume endpoint diagnostics: $required"
    }
}
if (-not $observerHost.Contains(
        'sink_->ConsumeWindowsVolumeChange(&notified)') -or
    -not $observerHost.Contains(
        '!sink_->WindowsVolumeNotificationsActive()')) {
    throw 'The observer host no longer prefers Core Audio notifications over polling.'
}
foreach ($required in @(
        'GlobalSystemMediaTransportControlsSessionManager',
        'CurrentSessionChanged',
        'PlaybackInfoChanged',
        'GetPlaybackInfo')) {
    if (-not $mediaMonitor.Contains($required)) {
        throw "V1 media-session monitor is missing: $required"
    }
}
foreach ($required in @(
        'IAudioSessionManager2',
        'IAudioSessionNotification',
        'IAudioSessionEvents',
        'RegisterSessionNotification',
        'RegisterAudioSessionNotification',
        'PKEY_Device_FriendlyName',
        'PKEY_Device_ContainerId',
        'NativeLdacRemoteContainerId')) {
    if (-not $hfpMonitor.Contains($required)) {
        throw "V1 HFP capture monitor is missing: $required"
    }
}
foreach ($forbidden in @(
        'SetDefaultEndpoint',
        'SetMasterVolume',
        'SetMute',
        'IAudioClient')) {
    if ($hfpMonitor.Contains($forbidden)) {
        throw "V1 HFP capture monitor crossed its read-only boundary: $forbidden"
    }
}
foreach ($required in @(
        'eRender',
        'PKEY_Device_FriendlyName',
        'PKEY_Device_ContainerId',
        'NativeLdacRemoteContainerId',
        'RegisterEndpointNotificationCallback',
        'UnregisterEndpointNotificationCallback',
        'EvaluateV1HfpRenderEndpointIdentity')) {
    if (-not $hfpRenderMonitor.Contains($required) -and
        -not $hfpRenderSelector.Contains($required)) {
        throw "V1 HFP render endpoint monitor is missing: $required"
    }
}
foreach ($forbidden in @(
        'IAudioClient',
        'SetDefaultEndpoint',
        'SetMasterVolume',
        'SetMute',
        'ActivateAudioInterfaceAsync')) {
    if ($hfpRenderMonitor.Contains($forbidden)) {
        throw "V1 HFP render endpoint monitor crossed its read-only boundary: $forbidden"
    }
}
foreach ($required in @(
        'V1HfpRenderEndpointMonitor',
        'hfp_render_monitor_ready',
        'hfp_render_endpoint_present',
        'hfp_render_endpoint_matched',
        'hfp_render_bridge_ready',
        'hfp_render_sequence',
        'hfp_render_last_error',
        'ReconcileHfpRenderEndpoint')) {
    if (-not $agent.Contains($required)) {
        throw "V1 daily HFP render telemetry integration is missing: $required"
    }
}
foreach ($required in @(
        'V1HfpCaptureMonitor hfp_capture_monitor',
        'V1HfpShadowState hfp_shadow_state',
        'ReconcileHfpShadow',
        'hfp_shadow_stop_requests',
        'hfp_shadow_enter_requests',
        'hfp_shadow_exit_requests',
        'hfp_shadow_resume_requests',
        'hfp_lifecycle_suspend_plans',
        'hfp_lifecycle_resume_plans',
        'PlanV1HfpLifecycle')) {
    if (-not $agent.Contains($required)) {
        throw "V1 daily HFP shadow integration is missing: $required"
    }
}
foreach ($required in @(
        '--hfp-transport-switch',
        'bool hfp_transport_switch = false;',
        'options.hfp_transport_switch',
        'V1HfpLifecycleEventForCommand',
        'HfpSuspendLdac',
        'HfpResumeLdac',
        'hfp_lifecycle_execution_failures')) {
    if (-not $agent.Contains($required) -and
        -not $lifecycle.Contains($required) -and
        -not $lifecycleHeader.Contains($required)) {
        throw "V1 HFP transport executor is missing: $required"
    }
}
if (-not $agent.Contains(
        'PlanV1HfpLifecycle(') -or
    -not $agent.Contains(
        'options.hfp_transport_switch,')) {
    throw 'V1 HFP transport executor is not behind its explicit opt-in gate.'
}
if ($agent.Contains('ExecuteHfpSwitchActions') -or
    $agent.Contains('SetDefaultEndpoint')) {
    throw 'V1 daily HFP observation unexpectedly became an executing switch path.'
}
foreach ($required in @(
        'V1HfpActionStopLdac',
        'V1HfpActionEnterHfpOutput',
        'V1HfpActionExitHfpOutput',
        'V1HfpActionResumeLdac')) {
    if (-not $hfpSwitch.Contains($required)) {
        throw "V1 HFP switch reducer is missing: $required"
    }
}
foreach ($required in @(
        'input.monitor_ready',
        'input.capture.endpoint_matched',
        'result.capture_active',
        'switch_state_.Step')) {
    if (-not $hfpShadow.Contains($required)) {
        throw "V1 HFP shadow coordinator is missing: $required"
    }
}
if (-not $agent.Contains(
        'observer->media_session_active() &&') -or
    -not $agent.Contains('media_eligibility.session_present') -or
    -not $agent.Contains('observer->SetMediaSessionSnapshot(media_snapshot)')) {
    throw 'Paused media-session eligibility is not extending the active observer lease safely.'
}
if (-not $agent.Contains(
        '!state->lifecycle.hfp_suspended &&') -or
    -not $agent.Contains(
        'state->lifecycle.hfp_suspended ||')) {
    throw 'HFP suspension no longer forces the custom AVRCP observer lease closed.'
}
foreach ($forbidden in @(
        'IOCTL_NLD_AVRCP_OBSERVER_SEND_COMMAND',
        'SendInput',
        'IAudioEndpointVolume')) {
    if ($agent.Contains($forbidden)) {
        throw "V1 daily AVRCP integration expanded its Phase B read-only boundary: $forbidden"
    }
}
if (-not $agent.Contains(
        'options.volume_sync = state->avrcp_volume_sync_enabled') -or
    -not $agent.Contains(
        'options.media_routing = state->avrcp_volume_sync_enabled') -or
    -not $agent.Contains('bool volume_sync = false;') -or
    -not $agent.Contains(
        'state.avrcp_volume_sync_enabled = options.volume_sync;')) {
    throw 'The daily AVRCP write mode is not config-driven and off by default.'
}

$handoffArgument = 'argument == L"--handoff"'
$handoffArgumentIndex = $agent.IndexOf($handoffArgument)
if ($handoffArgumentIndex -lt 0 -or
    $agent.IndexOf('return false;', $handoffArgumentIndex) -lt 0 -or
    $agent.Contains('[--handoff]') -or
    $dailyInstall.Contains('EnableHandoff') -or
    $dailyStart.Contains("PSObject.Properties['handoff']") -or
    $cmake.Contains('add_executable(v1_avrcp_handoff_host')) {
    throw 'The retired AVRCP handoff product entry is still active.'
}

foreach ($required in @(
        'allow_multiple_media_sessions = false',
        'completed_media_sessions_for_generation = 0u')) {
    if (-not $lifecycleHeader.Contains($required)) {
        throw "V1 daily lifecycle defaults changed: $required"
    }
}

foreach ($required in @(
        'fresh_render_edge',
        'state->open_attempts_for_generation = 0u',
        '!state->allow_multiple_media_sessions')) {
    if (-not $lifecycle.Contains($required)) {
        throw "V1 daily lifecycle reducer is missing: $required"
    }
}

foreach ($required in @(
        'CreateMutexW',
        'CreateEventW',
        'OpenEventW',
        'ERROR_ALREADY_EXISTS',
        'IsValidV1DailyInstanceSuffix')) {
    if (-not $instance.Contains($required)) {
        throw "V1 daily singleton control is missing: $required"
    }
}

foreach ($required in @(
        '#define V1_TRANSPORT_PCM_DURATION_MS 0u',
        '#define V1_TRANSPORT_PCM_MAXIMUM_PACKETS 0u',
        '#define V1_TRANSPORT_PCM_CONTINUOUS_UNTIL_STOP 1',
        '#define V1_TRANSPORT_PCM_MAXIMUM_GAIN_SCALAR 1.0f',
        '#define V1_TRANSPORT_PCM_MAXIMUM_OUTPUT_PEAK 1.0f',
        '#define V1_TRANSPORT_PCM_STARTUP_SILENCE_MS 20.0f',
        '#define V1_TRANSPORT_PCM_FADE_IN_MS 500.0f',
        '#define V1_TRANSPORT_PCM_CEILING_RAMP_MS 0.0f')) {
    if (-not $worker.Contains($required)) {
        throw "V1 daily worker frozen audio policy changed: $required"
    }
}

foreach ($required in @(
        'agent/v1_daily_instance.cpp',
        'agent/v1_avrcp_observer_host.cpp',
        'v1_daily_instance_tests',
        'v1_avrcp_observer_host_tests',
        'v1_daily_host_policy',
        'COMMAND pwsh.exe')) {
    if (-not $cmake.Contains($required)) {
        throw "V1 daily host build policy is missing: $required"
    }
}

Write-Host 'V1 daily host policy tests passed.'
