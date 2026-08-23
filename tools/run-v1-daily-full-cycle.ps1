# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string]$AgentPath = (Join-Path $PSScriptRoot '..\build\protocol\Release\v1_presence_agent.exe'),
    [string]$WorkerPath = (Join-Path $PSScriptRoot '..\build\protocol\Release\v1_transport_daily_worker.exe'),
    [string]$TrialRoot = (Join-Path $PSScriptRoot '..\artifacts\v1-volume-sync\trial'),
    [string]$InstanceSuffix = 'fullcycle',
    [ValidateSet('hq', 'sq', 'mq')]
    [string]$Quality = 'hq',
    [ValidateSet(44100, 48000, 88200, 96000)]
    [int]$SampleRateHz = 0,
    [ValidateSet(16, 24)]
    [int]$BitsPerSample = 0,
    [ValidateSet('stereo', 'dual', 'mono')]
    [string]$ChannelMode = 'stereo',
    [switch]$VolumeSync,
    [switch]$Handoff
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-daily-host-common.ps1')

Assert-V1DailyPowerShell7
Assert-V1DailyHandoffRetired
if (-not (Test-V1DailyInstanceSuffix -Value $InstanceSuffix)) {
    throw "The V1 daily instance suffix is invalid: $InstanceSuffix"
}
if ($Handoff) {
    throw 'V1 AVRCP handoff is retired and cannot be requested.'
}

$agent = [IO.Path]::GetFullPath($AgentPath)
$worker = [IO.Path]::GetFullPath($WorkerPath)
$trialRoot = [IO.Path]::GetFullPath($TrialRoot)
foreach ($path in @($agent, $worker)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "V1 daily executable is missing: $path"
    }
}

$trialDirectory = Join-Path $trialRoot (
    'daily-full-cycle-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $trialDirectory -Force | Out-Null
$statePath = Join-Path $trialDirectory 'daily-state.json'
$resultPath = Join-Path $trialDirectory 'transport-result.json'
$logPath = Join-Path $trialDirectory 'daily-host.log'
$eventsPath = Join-Path $trialDirectory 'daily-events.jsonl'
$summaryPath = Join-Path $trialDirectory 'daily-summary.json'

$arguments = @(
    '--daily',
    '--state', $statePath,
    '--engine-executable', $worker,
    '--transport-result', $resultPath,
    '--instance-suffix', $InstanceSuffix,
    '--quality', $Quality
)
if ($ChannelMode -ne 'stereo') {
    $arguments += @('--channel-mode', $ChannelMode)
}
if ($SampleRateHz -ne 0) {
    $arguments += @('--sample-rate', [string]$SampleRateHz)
}
if ($BitsPerSample -ne 0) {
    $arguments += @('--bits', [string]$BitsPerSample)
}
if ($VolumeSync) {
    $arguments += '--volume-sync'
}

function Get-V1DailyJsonValue {
    param(
        [object]$Object,
        [Parameter(Mandatory = $true)]
        [string]$Name,
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

function Add-V1DailySimpleLog {
    param(
        [Parameter(Mandatory = $true)]
        [string]$LogPath,
        [Parameter(Mandatory = $true)]
        [ValidateSet('info', 'warning', 'error')]
        [string]$Severity,
        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    $timestamp = [DateTimeOffset]::Now.ToString('o')
    $line = '[{0}] {1}: {2}' -f `
        $timestamp, $Severity.ToUpperInvariant(), $Message
    Add-Content -LiteralPath $LogPath -Value $line -Encoding utf8
}

function Add-V1DailyEvent {
    param(
        [Parameter(Mandatory = $true)]
        [string]$EventsPath,
        [Parameter(Mandatory = $true)]
        [Diagnostics.Stopwatch]$Stopwatch,
        [Parameter(Mandatory = $true)]
        [string]$Source,
        [Parameter(Mandatory = $true)]
        [string]$Category,
        [Parameter(Mandatory = $true)]
        [string]$EventName,
        [Parameter(Mandatory = $true)]
        [ValidateSet('info', 'warning', 'error')]
        [string]$Severity,
        [Parameter(Mandatory = $true)]
        [string]$Message,
        [string]$Raw = ''
    )

    $record = [ordered]@{
        schema_version = 1
        timestamp = [DateTimeOffset]::Now.ToString('o')
        elapsed_ms = [int64]$Stopwatch.ElapsedMilliseconds
        source = $Source
        category = $Category
        event = $EventName
        severity = $Severity
        message = $Message
        raw = $Raw
    }
    $json = $record | ConvertTo-Json -Compress -Depth 8
    Add-Content -LiteralPath $EventsPath -Value $json -Encoding utf8
}

# Volume sliders can emit dozens of updates per second. Keep every raw event
# in the event stream, but coalesce terminal messages so the console remains
# readable without changing the control path.
$script:V1DailyVolumeTerminalState = [ordered]@{
    LastEmitAt = [DateTimeOffset]::MinValue
    PendingMessage = $null
    PendingSeverity = 'info'
}
$script:V1DailyVolumeTerminalQuietMs = 250

function Emit-V1DailyVolumeTerminal {
    param(
        [Parameter(Mandatory = $true)]
        [string]$LogPath,
        [Parameter(Mandatory = $true)]
        [string]$Message,
        [Parameter(Mandatory = $true)]
        [ValidateSet('info', 'warning', 'error')]
        [string]$Severity
    )

    Add-V1DailySimpleLog -LogPath $LogPath -Severity $Severity `
        -Message $Message
    Write-Host $Message
    $script:V1DailyVolumeTerminalState.LastEmitAt = [DateTimeOffset]::Now
}

function Flush-V1DailyVolumeTerminal {
    param(
        [Parameter(Mandatory = $true)]
        [string]$LogPath,
        [switch]$Force
    )

    $state = $script:V1DailyVolumeTerminalState
    if ($null -eq $state.PendingMessage) {
        return
    }
    $elapsedMs = ([DateTimeOffset]::Now - $state.LastEmitAt).TotalMilliseconds
    if (-not $Force -and $elapsedMs -lt $script:V1DailyVolumeTerminalQuietMs) {
        return
    }
    Emit-V1DailyVolumeTerminal -LogPath $LogPath `
        -Message $state.PendingMessage -Severity $state.PendingSeverity
    $state.PendingMessage = $null
    $state.PendingSeverity = 'info'
}

function Queue-V1DailyVolumeTerminal {
    param(
        [Parameter(Mandatory = $true)]
        [string]$LogPath,
        [Parameter(Mandatory = $true)]
        [string]$Message,
        [Parameter(Mandatory = $true)]
        [ValidateSet('info', 'warning', 'error')]
        [string]$Severity
    )

    $state = $script:V1DailyVolumeTerminalState
    Flush-V1DailyVolumeTerminal -LogPath $LogPath
    $elapsedMs = ([DateTimeOffset]::Now - $state.LastEmitAt).TotalMilliseconds
    if ($null -eq $state.PendingMessage -and
        ($state.LastEmitAt -eq [DateTimeOffset]::MinValue -or
         $elapsedMs -ge $script:V1DailyVolumeTerminalQuietMs)) {
        Emit-V1DailyVolumeTerminal -LogPath $LogPath `
            -Message $Message -Severity $Severity
        return
    }
    $state.PendingMessage = $Message
    $state.PendingSeverity = $Severity
}

function Write-V1DailyRuntimeLine {
    param(
        [Parameter(Mandatory = $true)]
        [string]$LogPath,
        [Parameter(Mandatory = $true)]
        [string]$EventsPath,
        [Parameter(Mandatory = $true)]
        [Diagnostics.Stopwatch]$Stopwatch,
        [Parameter(Mandatory = $true)]
        [object]$Entry
    )

    $line = [string]$Entry
    if ([string]::IsNullOrWhiteSpace($line)) {
        return
    }

    Flush-V1DailyVolumeTerminal -LogPath $LogPath

    $category = 'runtime'
    $eventName = 'runtime.line'
    $severity = 'info'
    $message = $line
    $showTerminal = $false

    if ($line -match '^V1 ACL connected:.*generation=(\d+)') {
        $category = 'acl'
        $eventName = 'acl.connected'
        $message = "XM5 连接成功（ACL generation $($Matches[1])）。"
        $showTerminal = $true
    } elseif ($line -match '^V1 ACL disconnected:') {
        $category = 'acl'
        $eventName = 'acl.disconnected'
        $message = 'XM5 已断开。'
        $showTerminal = $true
    } elseif ($line -match '^V1 daily GSMTC snapshot playback=([^ ]+)') {
        $category = 'media'
        $eventName = 'media.snapshot'
        if ($Matches[1] -eq 'playing') {
            $message = '检测到 PC 正在播放，正在建立 LDAC 音频链路。'
        } elseif ($Matches[1] -eq 'paused') {
            $message = 'PC 当前已暂停；等待开始播放。'
        } else {
            $message = "PC 媒体状态：$($Matches[1])。"
        }
        $showTerminal = $true
    } elseif ($line -match '^V1 render paused with GSMTC Paused; suspending LDAC media packets') {
        $category = 'render'
        $eventName = 'render.pause_suspended'
        $message = '播放已暂停；LDAC 媒体包已停止，等待 PC 继续播放。'
        $showTerminal = $true
    } elseif ($line -match '^V1 render started:') {
        $category = 'render'
        $eventName = 'render.started'
        $message = 'Windows 音频渲染已启动，正在等待稳定输入。'
        $showTerminal = $true
    } elseif ($line -match '^V1 render stopped:') {
        $category = 'render'
        $eventName = 'render.stopped'
        $severity = 'warning'
        $message = '警告：Windows 音频渲染已停止。'
        $showTerminal = $true
    } elseif ($line -match '^V1 contained continuous PCM worker started:') {
        $category = 'pcm'
        $eventName = 'pcm.worker_started'
        $message = 'PCM 音频工作进程已启动。'
        $showTerminal = $true
    } elseif ($line -match '^V1 engine ready; transport OPEN is waiting') {
        $category = 'engine'
        $eventName = 'engine.ready_waiting_for_render'
        $message = '引擎已就绪，等待音频渲染稳定。'
        $showTerminal = $true
    } elseif ($line -match '^V1 engine ready; one daily-session PCM authorization') {
        $category = 'transport'
        $eventName = 'transport.pcm_authorized'
        $message = '音频渲染已稳定，PCM 传输授权已交付。'
        $showTerminal = $true
    } elseif ($line -match '^V1 Render RUN remained stable') {
        $category = 'render'
        $eventName = 'render.stable'
        $message = '音频渲染已稳定，正在打开 LDAC 传输。'
        $showTerminal = $true
    } elseif ($line -match '^V1 daily PCM media started') {
        $category = 'transport'
        $eventName = 'media.started'
        $message = 'LDAC 音频传输已开始；请确认现在有声音。'
        $showTerminal = $true
    } elseif ($line -match '^V1 daily AVRCP handoff completed after MediaStarted') {
        $category = 'handoff'
        $eventName = 'handoff.completed_after_media'
        $message = '媒体 bootstrap 已完成，AVRCP handoff 成功；正在建立 XM5 控制通道。'
        $showTerminal = $true
    } elseif ($line -match '^V1 daily AVRCP handoff completed') {
        $category = 'handoff'
        $eventName = 'handoff.completed'
        $message = 'AVRCP handoff 成功，正在启用 XM5 控制链路。'
        $showTerminal = $true
    } elseif ($line -match '^V1 Native LDAC audio endpoint rebound') {
        $category = 'audio'
        $eventName = 'audio.endpoint_rebound'
        $message = 'Native LDAC 音频端点已重新绑定，presence lease 已刷新。'
        $showTerminal = $true
    } elseif ($line -match '^V1 daily AVRCP activation requested') {
        $category = 'avrcp'
        $eventName = 'observer.activation_requested'
        $message = 'XM5 控制通道正在建立；PC 端点增益暂时保持有效。'
        $showTerminal = $true
    } elseif ($line -match '^V1 daily AVRCP control channel ready') {
        $category = 'avrcp'
        $eventName = 'observer.control_ready'
        if ($Handoff) {
            $message = 'XM5 控制链路已就绪，初始音量已统一；现在可以测试单增益音量。'
        } else {
            $message = 'AVRCP observer 已启用；handoff 未启用，本轮仅验证 PCM 稳定性。'
        }
        $showTerminal = $true
    } elseif ($line -match '^V1 daily Microsoft-preserving AVRCP volume bridge started') {
        $category = 'avrcp'
        $eventName = 'observer.filter_bridge_started'
        $message = 'Microsoft AVRCP 保持连接，统一音量桥正在启动。'
        $showTerminal = $true
    } elseif ($line -match '^V1 daily Microsoft-preserving AVRCP volume bridge ready') {
        $category = 'avrcp'
        $eventName = 'observer.control_ready'
        $message = '统一音量桥已就绪；现在可以测试 PC 和 XM5 音量。'
        $showTerminal = $true
    } elseif ($line -match '^V1 daily Microsoft-preserving AVRCP volume bridge lost readiness') {
        $category = 'avrcp'
        $eventName = 'observer.control_lost'
        $severity = 'warning'
        $message = 'Native LDAC 音量端点暂时不可用；已恢复 PC 端增益并等待重新绑定。'
        $showTerminal = $true
    } elseif ($line -match '^V1 Windows volume endpoint bound exact=yes rebind=(\d+) id=(.+)\.$') {
        $category = 'audio'
        $eventName = 'audio.volume_endpoint_bound'
        $message = "Native LDAC 音量端点已精确绑定（重绑次数=$($Matches[1])）。"
        $showTerminal = $true
    } elseif ($line -match '^V1 daily AVRCP control readiness lost') {
        $category = 'avrcp'
        $eventName = 'observer.control_lost'
        $severity = 'warning'
        $message = '警告：XM5 控制链路失去就绪状态，PC 端点增益已恢复。'
        $showTerminal = $true
    } elseif ($line -match '^V1 daily AVRCP control readiness timed out') {
        $category = 'avrcp'
        $eventName = 'observer.control_timeout'
        $severity = 'error'
        $message = "XM5 控制通道建立超时；PC 端点增益保持有效并已触发 Microsoft 恢复：$line"
        $showTerminal = $true
    } elseif ($line -match '^V1 daily volume sync is fail-safe disabled') {
        $category = 'avrcp'
        $eventName = 'observer.unsupported_native_handoff'
        $severity = 'error'
        $message = '当前 Native 音频路径不支持安全的 AVRCP 驱动切换；已保留正常 PC 音量，未执行切换。'
        $showTerminal = $true
    } elseif ($line -match '^action set-windows-volume percent=(\d+) muted=(\w+)') {
        $category = 'avrcp.volume'
        $eventName = 'volume.xm5_to_pc'
        $muteText = if ($Matches[2] -eq 'yes') { '静音' } else { '非静音' }
        $message = "XM5 音量事件：PC 音量更新为 $($Matches[1])%（$muteText）。"
        $showTerminal = $false
    } elseif ($line -match '^action send-xm5-volume value=(\d+) \(sent\)') {
        $category = 'avrcp.volume'
        $eventName = 'volume.pc_to_xm5'
        $message = "PC 音量事件：已发送到 XM5（AVRCP value=$($Matches[1])）。"
        $showTerminal = $false
    } elseif ($line -match '^action send-xm5-volume value=(\d+) \(pending;') {
        $category = 'avrcp.volume'
        $eventName = 'volume.pc_to_xm5_pending'
        $message = "PC 音量变化已排队，等待当前 AVRCP 事务完成（value=$($Matches[1])）。"
        $showTerminal = $true
    } elseif ($line -match '^V1 daily Microsoft-owned AVRCP media key observed operation=0x([0-9A-Fa-f]+)') {
        $category = 'avrcp.media_key'
        $eventName = 'media_key.microsoft_owned'
        $operation = [Convert]::ToUInt32($Matches[1], 16)
        $label = switch ($operation) {
            0x44 { '播放' }
            0x46 { '暂停' }
            0x4B { '下一曲' }
            0x4C { '上一曲' }
            default { '其他' }
        }
        $message = "XM5 媒体按键事件：$label（由 Microsoft 处理）。"
        $showTerminal = $true
    } elseif ($line -match '^V1 daily bootstrap PLAY arbitration scheduled') {
        $category = 'avrcp.media_key'
        $eventName = 'media_key.bootstrap_scheduled'
        $message = 'XM5 请求播放；正在等待 Microsoft 处理。'
        $showTerminal = $true
    } elseif ($line -match '^V1 daily bootstrap PLAY was handled by Microsoft') {
        $category = 'avrcp.media_key'
        $eventName = 'media_key.bootstrap_microsoft_handled'
        $message = 'Microsoft 已处理播放请求；未执行重复注入。'
        $showTerminal = $true
    } elseif ($line -match '^V1 daily bootstrap PLAY fallback injected') {
        $category = 'avrcp.media_key'
        $eventName = 'media_key.bootstrap_fallback_injected'
        $message = 'Microsoft 未处理播放请求；已补发一次播放。'
        $showTerminal = $true
    } elseif ($line -match '^action inject vk=0x[0-9A-Fa-f]+ action=(\d+)') {
        $category = 'avrcp.media_key'
        $eventName = 'media_key.inject'
        $actionValue = [uint32]$Matches[1]
        $labels = @()
        if (($actionValue -band 2u) -ne 0u) { $labels += '音量+' }
        if (($actionValue -band 4u) -ne 0u) { $labels += '音量-' }
        if (($actionValue -band 8u) -ne 0u) { $labels += '静音切换' }
        if (($actionValue -band 16u) -ne 0u) { $labels += '播放' }
        if (($actionValue -band 32u) -ne 0u) { $labels += '暂停' }
        if (($actionValue -band 64u) -ne 0u) { $labels += '播放/暂停' }
        if (($actionValue -band 128u) -ne 0u) { $labels += '停止' }
        if (($actionValue -band 256u) -ne 0u) { $labels += '下一曲' }
        if (($actionValue -band 512u) -ne 0u) { $labels += '上一曲' }
        $labelText = if ($labels.Count -gt 0) { $labels -join '+' } else { '未知媒体按键' }
        $message = "XM5 媒体按键事件：$labelText。"
        $showTerminal = $true
    } elseif ($line -match '^action notify-playback-status=.*\(queued;') {
        $category = 'avrcp.playback'
        $eventName = 'playback.status_queued'
        $message = '播放状态已同步到 XM5。'
        $showTerminal = $true
    } elseif ($line -match '^action notify-playback-status=.*\(pending;') {
        $category = 'avrcp.playback'
        $eventName = 'playback.status_pending'
        if ($Handoff) {
            $message = "播放状态已排队，等待 XM5 控制通道或当前事务：$line"
            $showTerminal = $true
        } else {
            $message = '当前 filter 路径未发送播放状态；音量同步不受影响。'
            $showTerminal = $false
        }
    } elseif ($line -match '^V1 daily stop request received') {
        $eventName = 'daily.stop_requested'
        $message = '正在停止测试实例并清理音频链路。'
        $showTerminal = $true
    } elseif ($line -match '^V1 presence agent stopped:') {
        $eventName = 'daily.stopped'
        $message = '测试实例已停止。'
        $showTerminal = $true
    } elseif ($line -match '(?i)(failed|failure|error|timeout|timed out|fail-closed|unavailable|incompatible|backend-failure)') {
        $severity = if ($line -match '(?i)(failed|failure|error|timeout|timed out|fail-closed|backend-failure)') {
            'error'
        } else {
            'warning'
        }
        $eventName = 'runtime.diagnostic'
        $message = "错误/警告：$line"
        $showTerminal = $true
    }

    Add-V1DailyEvent -EventsPath $EventsPath -Stopwatch $Stopwatch `
        -Source 'presence-agent' -Category $category -EventName $eventName `
        -Severity $severity -Message $message -Raw $line
    if ($eventName -eq 'volume.xm5_to_pc') {
        # Remote volume changes are user-visible feedback. Print them as soon
        # as the event arrives; only the high-rate PC write direction is
        # coalesced to keep slider drags readable.
        Flush-V1DailyVolumeTerminal -LogPath $LogPath -Force
        Emit-V1DailyVolumeTerminal -LogPath $LogPath `
            -Message $message -Severity $severity
    } elseif ($eventName -eq 'volume.pc_to_xm5') {
        Queue-V1DailyVolumeTerminal -LogPath $LogPath `
            -Message $message -Severity $severity
    } elseif ($showTerminal) {
        Flush-V1DailyVolumeTerminal -LogPath $LogPath -Force
        Add-V1DailySimpleLog -LogPath $LogPath -Severity $severity `
            -Message $message
        Write-Host $message
    }
}

$runStartedAt = [DateTimeOffset]::Now
$runStopwatch = [Diagnostics.Stopwatch]::StartNew()
Add-V1DailySimpleLog -LogPath $logPath -Severity info `
    -Message "V1 daily host started (quality=$Quality, volume-sync=$([bool]$VolumeSync), handoff=$([bool]$Handoff))."
Add-V1DailyEvent -EventsPath $eventsPath -Stopwatch $runStopwatch `
    -Source 'runner' -Category 'daily' -EventName 'daily.started' `
    -Severity info -Message 'V1 daily host started.' `
    -Raw "quality=$Quality; volume_sync=$([bool]$VolumeSync); handoff=$([bool]$Handoff)"

$volumeMode = if ($VolumeSync) { 'volume-sync=on' } else { 'volume-sync=off' }
$handoffMode = 'handoff=not-used'
Write-Host "V1 daily host started (quality=$Quality, $volumeMode, $handoffMode)."
Write-Host "State: $statePath"
Write-Host "Transport: $resultPath"
Write-Host "Log: $logPath"
Write-Host "Events: $eventsPath"
Write-Host "Summary: $summaryPath"
Write-Host ('Stop: & "{0}" --stop-daily --instance-suffix {1}' -f $agent, $InstanceSuffix)
Write-Host '等待 XM5 连接：请现在开启或重新连接 XM5；连接成功后再开始播放。'

$exitCode = 1
$resultExitCode = 1
try {
    # Preserve every native stdout/stderr line in the developer event stream
    # while forwarding only operational milestones and actionable errors.
    & $agent @arguments 2>&1 | ForEach-Object {
        Write-V1DailyRuntimeLine -LogPath $logPath -EventsPath $eventsPath `
            -Stopwatch $runStopwatch -Entry $_
    }
    $exitCode = $LASTEXITCODE
} finally {
    Flush-V1DailyVolumeTerminal -LogPath $logPath -Force
    $runStopwatch.Stop()
    $stateJson = $null
    $transportJson = $null
    $transportResults = @()
    $transportAggregate = $null
    $transportResultPaths = @()
    if (Test-Path -LiteralPath $statePath -PathType Leaf) {
        try {
            $stateJson = Get-Content -LiteralPath $statePath -Raw |
                ConvertFrom-Json
        } catch {
            Add-V1DailySimpleLog -LogPath $logPath -Severity warning `
                -Message ("无法读取 daily-state.json：{0}" -f $_.Exception.Message)
        }
    }
    try {
        $transportResults = @(Get-V1DailyTransportResultSet `
            -ResultPath $resultPath)
        if ($transportResults.Count -gt 0) {
            $transportJson = $transportResults[-1].Value
            $transportAggregate = Merge-V1DailyTransportResults `
                -Results $transportResults
            $transportResultPaths = @(
                $transportResults | ForEach-Object { $_.Path })
            if ($transportJson.disposition -notin @('succeeded', 'cancelled')) {
                $diagnostic = (
                    "错误：LDAC 传输未完成（disposition={0}, stage={1}, Win32={2}, PCM准备={3}, " +
                    "PCM重启={4}, 音量变化={5}）。详见 Transport 文件。"
                ) -f
                    $transportJson.disposition,
                    $transportJson.stage,
                    $transportJson.backend_error,
                    $transportJson.pcm_prepare_attempts,
                    $transportJson.pcm_epoch_restarts,
                    $transportJson.volume_change_count
                Write-Host $diagnostic
            } elseif ($transportJson.disposition -eq 'cancelled') {
                Write-Host '传输已按停止请求结束。'
            }
        }
    } catch {
        Write-Host ("警告：无法读取 Transport 结果：{0}" -f $_.Exception.Message)
    }
    $disposition = [string](Get-V1DailyJsonValue `
        -Object $transportAggregate -Name 'last_disposition' -Default '')
    $backendError = [int](Get-V1DailyJsonValue `
        -Object $transportAggregate -Name 'backend_error' -Default 0)
    $remoteCleanup = [bool](Get-V1DailyJsonValue `
        -Object $transportAggregate -Name 'remote_stream_cleanup_required' `
        -Default $false)
    $failedTransportResultCount = [int](Get-V1DailyJsonValue `
        -Object $transportAggregate -Name 'failed_result_count' -Default 0)
    $qualityGate = if ($null -ne $transportAggregate) {
        Get-V1DailyQualityGateDecision -Quality $Quality `
            -TransportAggregate $transportAggregate
    } else {
        [pscustomobject]@{
            expected_quality = $Quality.ToUpperInvariant()
            observed_qualities = @()
            quality_matched = $false
            expected_nominal_ldac_bitrate_kbps = 0
            observed_nominal_ldac_bitrates_kbps = @()
            nominal_bitrate_matched = $false
            passed = $false
        }
    }
    $formatGate = if ($null -ne $transportAggregate) {
        Get-V1DailyFormatGateDecision -TransportAggregate $transportAggregate `
            -SampleRateHz $SampleRateHz -BitsPerSample $BitsPerSample `
            -ChannelMode $ChannelMode
    } else {
        [pscustomobject]@{
            requested_sample_rate_hz = $SampleRateHz
            requested_bits_per_sample = $BitsPerSample
            requested_channel_mode = $ChannelMode
            observed_sample_rates_hz = @()
            observed_bits_per_samples = @()
            observed_channel_modes = @()
            sample_rate_matched = $false
            bits_per_sample_matched = $false
            channel_mode_matched = $false
            passed = $false
        }
    }
    $avrcpControlReadyCount = 0
    $avrcpControlLostCount = 0
    $avrcpControlTimeoutCount = 0
    $avrcpUnsupportedCount = 0
    $pcToXm5VolumeCount = 0
    $xm5ToPcVolumeCount = 0
    $microsoftMediaKeyCount = 0
    $bootstrapPlayScheduledCount = 0
    $bootstrapPlayMicrosoftHandledCount = 0
    $bootstrapPlayFallbackInjectedCount = 0
    if (Test-Path -LiteralPath $eventsPath -PathType Leaf) {
        foreach ($eventLine in @(Get-Content -LiteralPath $eventsPath)) {
            try {
                $eventRecord = $eventLine | ConvertFrom-Json
                if ([string]$eventRecord.event -eq 'observer.control_ready') {
                    ++$avrcpControlReadyCount
                } elseif ([string]$eventRecord.event -eq 'observer.control_lost') {
                    ++$avrcpControlLostCount
                } elseif ([string]$eventRecord.event -eq 'observer.control_timeout') {
                    ++$avrcpControlTimeoutCount
                } elseif ([string]$eventRecord.event -eq 'observer.unsupported_native_handoff') {
                    ++$avrcpUnsupportedCount
                } elseif ([string]$eventRecord.event -eq 'volume.pc_to_xm5') {
                    ++$pcToXm5VolumeCount
                } elseif ([string]$eventRecord.event -eq 'volume.xm5_to_pc') {
                    ++$xm5ToPcVolumeCount
                } elseif ([string]$eventRecord.event -eq 'media_key.microsoft_owned') {
                    ++$microsoftMediaKeyCount
                } elseif ([string]$eventRecord.event -eq 'media_key.bootstrap_scheduled') {
                    ++$bootstrapPlayScheduledCount
                } elseif ([string]$eventRecord.event -eq 'media_key.bootstrap_microsoft_handled') {
                    ++$bootstrapPlayMicrosoftHandledCount
                } elseif ([string]$eventRecord.event -eq 'media_key.bootstrap_fallback_injected') {
                    ++$bootstrapPlayFallbackInjectedCount
                }
            } catch {
                # A malformed diagnostic line must not hide transport cleanup;
                # the summary remains conservative via the zero ready count.
            }
        }
    }
    $avrcpControlRequired = [bool]$VolumeSync
    $summaryStatus = if ($null -eq $transportAggregate) {
        'incomplete'
    } elseif ($failedTransportResultCount -ne 0 -or
              $exitCode -ne 0 -or $remoteCleanup -or
              -not $qualityGate.passed -or -not $formatGate.passed -or
              ($avrcpControlRequired -and
               ($avrcpControlReadyCount -eq 0 -or
                $avrcpControlLostCount -ne 0 -or
                $avrcpControlTimeoutCount -ne 0 -or
                $avrcpUnsupportedCount -ne 0))) {
        'error'
    } else {
        'stopped'
    }
    $summaryError = if ($summaryStatus -eq 'error') {
        "disposition=$disposition; stage=$([int](Get-V1DailyJsonValue $transportAggregate 'last_stage' 0)); Win32=$backendError; failed_transport_results=$failedTransportResultCount; expected_quality=$($qualityGate.expected_quality); observed_quality=$($qualityGate.observed_qualities -join ','); expected_nominal_kbps=$($qualityGate.expected_nominal_ldac_bitrate_kbps); observed_nominal_kbps=$($qualityGate.observed_nominal_ldac_bitrates_kbps -join ','); requested_rate=$SampleRateHz; requested_bits=$BitsPerSample; requested_channel=$ChannelMode; observed_rates=$($formatGate.observed_sample_rates_hz -join ','); observed_bits=$($formatGate.observed_bits_per_samples -join ','); observed_channels=$($formatGate.observed_channel_modes -join ','); avrcp_control_ready=$avrcpControlReadyCount; avrcp_control_lost=$avrcpControlLostCount; avrcp_control_timeout=$avrcpControlTimeoutCount; avrcp_unsupported=$avrcpUnsupportedCount"
    } else {
        ''
    }
    $resultExitCode = if ($summaryStatus -eq 'error' -or
                          $summaryStatus -eq 'incomplete') {
        if ($exitCode -eq 0) { 1 } else { $exitCode }
    } else {
        $exitCode
    }
    $summary = [ordered]@{
        schema_version = 1
        record_kind = 'v1-daily-summary'
        started_at = $runStartedAt.ToString('o')
        completed_at = [DateTimeOffset]::Now.ToString('o')
        duration_ms = [int64]$runStopwatch.ElapsedMilliseconds
        instance_suffix = $InstanceSuffix
        configuration = [ordered]@{
            volume_sync = [bool]$VolumeSync
            requested_quality = $Quality.ToUpperInvariant()
            requested_sample_rate_hz = $SampleRateHz
            requested_bits_per_sample = $BitsPerSample
            requested_channel_mode = $ChannelMode
            handoff_requested = [bool]$Handoff
            handoff = $false
            agent_path = $agent
            worker_path = $worker
        }
        connection = [ordered]@{
            physical_presence = Get-V1DailyJsonValue $stateJson 'physical_presence' ''
            acl_generation = [int64](Get-V1DailyJsonValue $stateJson 'acl_generation' 0)
            connected_events = [int](Get-V1DailyJsonValue $stateJson 'connected_events' 0)
            disconnected_events = [int](Get-V1DailyJsonValue $stateJson 'disconnected_events' 0)
        }
        media = [ordered]@{
            playback = Get-V1DailyJsonValue $stateJson 'avrcp_pc_playback' 'unknown'
            started_events = [int](Get-V1DailyJsonValue $stateJson 'media_started_events' 0)
            stopped_events = [int](Get-V1DailyJsonValue $stateJson 'media_stopped_events' 0)
            failed_events = [int](Get-V1DailyJsonValue $stateJson 'media_failed_events' 0)
        }
        avrcp = [ordered]@{
            control_required = $avrcpControlRequired
            control_ready_count = $avrcpControlReadyCount
            control_lost_count = $avrcpControlLostCount
            control_timeout_count = $avrcpControlTimeoutCount
            unsupported_native_handoff_count = $avrcpUnsupportedCount
            pc_to_xm5_volume_count = $pcToXm5VolumeCount
            xm5_to_pc_volume_count = $xm5ToPcVolumeCount
            microsoft_media_key_count = $microsoftMediaKeyCount
            bootstrap_play_scheduled_count = $bootstrapPlayScheduledCount
            bootstrap_play_microsoft_handled_count = $bootstrapPlayMicrosoftHandledCount
            bootstrap_play_fallback_injected_count = $bootstrapPlayFallbackInjectedCount
        }
        audio = [ordered]@{
            expected_quality = $qualityGate.expected_quality
            observed_qualities = $qualityGate.observed_qualities
            quality_matched = $qualityGate.quality_matched
            expected_nominal_ldac_bitrate_kbps = $qualityGate.expected_nominal_ldac_bitrate_kbps
            observed_nominal_ldac_bitrates_kbps = $qualityGate.observed_nominal_ldac_bitrates_kbps
            nominal_bitrate_matched = $qualityGate.nominal_bitrate_matched
            requested_sample_rate_hz = $formatGate.requested_sample_rate_hz
            requested_bits_per_sample = $formatGate.requested_bits_per_sample
            requested_channel_mode = $formatGate.requested_channel_mode
            observed_sample_rates_hz = $formatGate.observed_sample_rates_hz
            observed_bits_per_samples = $formatGate.observed_bits_per_samples
            observed_channel_modes = $formatGate.observed_channel_modes
            sample_rate_matched = $formatGate.sample_rate_matched
            bits_per_sample_matched = $formatGate.bits_per_sample_matched
            channel_mode_matched = $formatGate.channel_mode_matched
            transport_result_count = [int](Get-V1DailyJsonValue $transportAggregate 'result_count' 0)
            packets_written = [int64](Get-V1DailyJsonValue $transportAggregate 'media_packets_written' 0)
            duration_ms = [int64](Get-V1DailyJsonValue $transportAggregate 'actual_duration_ms' 0)
            pcm_stream_stop_detected = [bool](Get-V1DailyJsonValue $transportAggregate 'pcm_stream_stop_detected' $false)
            pcm_rebind_attempts = [int64](Get-V1DailyJsonValue $transportAggregate 'pcm_rebind_attempts' 0)
            pcm_rebind_failures = [int64](Get-V1DailyJsonValue $transportAggregate 'pcm_rebind_failures' 0)
            volume_change_count = [int64](Get-V1DailyJsonValue $transportAggregate 'volume_change_count' 0)
            media_write_not_ready_retries = [int64](Get-V1DailyJsonValue $transportAggregate 'media_write_not_ready_retries' 0)
            media_write_not_ready_exhaustions = [int64](Get-V1DailyJsonValue $transportAggregate 'media_write_not_ready_exhaustions' 0)
            pcm_transient_timeout_count = [int64](Get-V1DailyJsonValue $transportAggregate 'pcm_transient_timeout_count' 0)
            pcm_transient_timeout_recovery_count = [int64](Get-V1DailyJsonValue $transportAggregate 'pcm_transient_timeout_recovery_count' 0)
            pcm_transient_timeout_exhausted_count = [int64](Get-V1DailyJsonValue $transportAggregate 'pcm_transient_timeout_exhausted_count' 0)
            pcm_transient_timeout_max_streak_ms = [int64](Get-V1DailyJsonValue $transportAggregate 'pcm_transient_timeout_max_streak_ms' 0)
            pause_suspend_count = [int64](Get-V1DailyJsonValue $transportAggregate 'pause_suspend_count' 0)
            pause_resume_start_count = [int64](Get-V1DailyJsonValue $transportAggregate 'pause_resume_start_count' 0)
            pause_wait_prepare_attempts = [int64](Get-V1DailyJsonValue $transportAggregate 'pause_wait_prepare_attempts' 0)
            remote_stream_cleanup_required = $remoteCleanup
        }
        outcome = [ordered]@{
            status = $summaryStatus
            exit_code = $resultExitCode
            agent_exit_code = $exitCode
            disposition = $disposition
            backend_error = $backendError
            failed_transport_result_count = $failedTransportResultCount
            error = $summaryError
        }
        artifacts = [ordered]@{
            state = $statePath
            summary = $summaryPath
            simple_log = $logPath
            events = $eventsPath
            transport_result = $resultPath
            transport_results = $transportResultPaths
        }
    }
    $summaryJson = $summary | ConvertTo-Json -Depth 10
    $summaryTemporary = $summaryPath + '.tmp'
    Set-Content -LiteralPath $summaryTemporary -Value $summaryJson -Encoding utf8
    Move-Item -LiteralPath $summaryTemporary -Destination $summaryPath -Force
    Add-V1DailySimpleLog -LogPath $logPath -Severity `
        $(if ($summaryStatus -eq 'error') { 'error' } else { 'info' }) `
        -Message "Daily summary: $summaryStatus"
    Add-V1DailyEvent -EventsPath $eventsPath -Stopwatch $runStopwatch `
        -Source 'runner' -Category 'daily' -EventName 'daily.completed' `
        -Severity $(if ($summaryStatus -eq 'error') { 'error' } else { 'info' }) `
        -Message "Daily summary: $summaryStatus" -Raw $summaryError
    Write-Host "Summary written: $summaryPath"
    Write-Host "V1 daily host result code $resultExitCode (agent exit code $exitCode)."
}
exit $resultExitCode
