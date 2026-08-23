# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmAgentInstall,
    [ValidateSet('mq', 'sq', 'hq', 'auto')]
    [string]$Quality = 'hq',
    [ValidateSet('stereo', 'dual', 'mono')]
    [string]$ChannelMode = 'stereo',
    [ValidateSet(44100, 48000, 88200, 96000)]
    [int]$SampleRate = 48000,
    [ValidateSet(16, 24)]
    [int]$BitsPerSample = 16,
    [switch]$StartNow
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this script from an elevated Windows PowerShell.'
    }
}

function Wait-AgentExit {
    param(
        [int]$TimeoutSeconds = 30
    )

    for ($attempt = 0; $attempt -lt $TimeoutSeconds; $attempt++) {
        $agents = @(Get-Process -Name ldac_agent -ErrorAction SilentlyContinue)
        if ($agents.Count -eq 0) {
            return $true
        }
        Start-Sleep -Seconds 1
    }
    return $false
}

function Set-ProtectedInstallAcl {
    param([Parameter(Mandatory)][string]$Path)

    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetAccessRuleProtection($true, $false)
    $inheritance = [Security.AccessControl.InheritanceFlags]'ContainerInherit,ObjectInherit'
    $propagation = [Security.AccessControl.PropagationFlags]::None
    $allow = [Security.AccessControl.AccessControlType]::Allow
    $rules = @(
        [Security.AccessControl.FileSystemAccessRule]::new(
            [Security.Principal.SecurityIdentifier]'S-1-5-18',
            [Security.AccessControl.FileSystemRights]::FullControl,
            $inheritance,
            $propagation,
            $allow),
        [Security.AccessControl.FileSystemAccessRule]::new(
            [Security.Principal.SecurityIdentifier]'S-1-5-32-544',
            [Security.AccessControl.FileSystemRights]::FullControl,
            $inheritance,
            $propagation,
            $allow),
        [Security.AccessControl.FileSystemAccessRule]::new(
            [Security.Principal.SecurityIdentifier]'S-1-5-32-545',
            [Security.AccessControl.FileSystemRights]'ReadAndExecute,Synchronize',
            $inheritance,
            $propagation,
            $allow)
    )
    foreach ($rule in $rules) {
        $acl.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $Path -AclObject $acl
}

function Set-ProtectedFileAcl {
    param([Parameter(Mandatory)][string]$Path)

    $acl = [Security.AccessControl.FileSecurity]::new()
    $acl.SetAccessRuleProtection($true, $false)
    $inheritance = [Security.AccessControl.InheritanceFlags]::None
    $propagation = [Security.AccessControl.PropagationFlags]::None
    $allow = [Security.AccessControl.AccessControlType]::Allow
    $rules = @(
        [Security.AccessControl.FileSystemAccessRule]::new(
            [Security.Principal.SecurityIdentifier]'S-1-5-18',
            [Security.AccessControl.FileSystemRights]::FullControl,
            $inheritance,
            $propagation,
            $allow),
        [Security.AccessControl.FileSystemAccessRule]::new(
            [Security.Principal.SecurityIdentifier]'S-1-5-32-544',
            [Security.AccessControl.FileSystemRights]::FullControl,
            $inheritance,
            $propagation,
            $allow),
        [Security.AccessControl.FileSystemAccessRule]::new(
            [Security.Principal.SecurityIdentifier]'S-1-5-32-545',
            [Security.AccessControl.FileSystemRights]'ReadAndExecute,Synchronize',
            $inheritance,
            $propagation,
            $allow)
    )
    foreach ($rule in $rules) {
        $acl.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $Path -AclObject $acl
}

Assert-Administrator
if (-not $ConfirmAgentInstall) {
    throw 'Refusing to install the login agent. Re-run with -ConfirmAgentInstall.'
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$stagedRoot = Join-Path $projectRoot 'artifacts\agent'
$stagedAgent = Join-Path $stagedRoot 'ldac_agent.exe'
$stagedProbe = Join-Path $stagedRoot 'transport_probe.exe'
$requiredFiles = @($stagedAgent, $stagedProbe)
$missingFiles = @($requiredFiles | Where-Object {
    -not (Test-Path -LiteralPath $_ -PathType Leaf)
})
if ($missingFiles.Count -ne 0) {
    throw "Staged agent files are missing. Run tools\build-agent.ps1 first: $($missingFiles -join ', ')"
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$userName = $identity.Name
$nativeRoot = Join-Path $env:LOCALAPPDATA 'NativeLdac'
$configPath = Join-Path $nativeRoot 'config.json'
$programFilesRoot = [System.IO.Path]::GetFullPath($env:ProgramFiles)
$installRoot = Join-Path $programFilesRoot 'NativeLdac\bin'
$legacyInstallRoot = Join-Path $nativeRoot 'bin'
$installedAgent = Join-Path $installRoot 'ldac_agent.exe'
$installedProbe = Join-Path $installRoot 'transport_probe.exe'
$taskName = 'Native LDAC Agent'
$target = "$taskName for $userName, installed in $installRoot"
$actionDescription = "Install the elevated user-session login agent with default quality $Quality, channel mode $ChannelMode, and format $SampleRate Hz/$BitsPerSample-bit"
if (-not $PSCmdlet.ShouldProcess($target, $actionDescription)) {
    return
}

$stopProcess = Start-Process -FilePath $stagedAgent `
    -ArgumentList @('--stop') `
    -WindowStyle Hidden `
    -Wait `
    -PassThru
if ($stopProcess.ExitCode -eq 0 -and -not (Wait-AgentExit)) {
    throw 'The existing LDAC agent did not stop within 30 seconds. Installation was not changed.'
}

$activeProbe = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @(
        'transport_probe.exe',
        'ldac_direct_engine.exe',
        'audio_endpoint_probe.exe')
})
if ($activeProbe.Count -ne 0) {
    $processSummary = @($activeProbe | ForEach-Object {
        "$($_.Name) (PID $($_.ProcessId))"
    })
    throw "Stop the LDAC UI/probe session first: $($processSummary -join ', ')"
}

New-Item -ItemType Directory -Path $installRoot -Force | Out-Null
Set-ProtectedInstallAcl -Path $installRoot
Copy-Item -LiteralPath $stagedAgent -Destination $installedAgent -Force
Copy-Item -LiteralPath $stagedProbe -Destination $installedProbe -Force
$installedAgentHash =
    (Get-FileHash -LiteralPath $installedAgent -Algorithm SHA256).Hash
$installedProbeHash =
    (Get-FileHash -LiteralPath $installedProbe -Algorithm SHA256).Hash
$installedProbeHash | Set-Content `
    -LiteralPath (Join-Path $installRoot 'transport_probe.sha256') `
    -Encoding ASCII
$protectedFiles = @(
    $installedAgent,
    $installedProbe,
    (Join-Path $installRoot 'transport_probe.sha256')
)
foreach ($protectedFile in $protectedFiles) {
    Set-ProtectedFileAcl -Path $protectedFile
}
$retiredDirectFiles = @(
    (Join-Path $installRoot 'ldac_direct_engine.exe'),
    (Join-Path $installRoot 'ldac_direct_engine.sha256')
)
foreach ($retiredDirectFile in $retiredDirectFiles) {
    if (Test-Path -LiteralPath $retiredDirectFile -PathType Leaf) {
        Remove-Item -LiteralPath $retiredDirectFile -Force
    }
}

New-Item -ItemType Directory -Path $nativeRoot -Force | Out-Null
if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
    $configTemporaryPath = "$configPath.tmp.$PID"
    [ordered]@{
        version = 3
        revision = 1
        enabled = $true
        quality = $Quality
        channel_mode = $ChannelMode
        sample_rate = $SampleRate
        bits_per_sample = $BitsPerSample
    } | ConvertTo-Json | Set-Content `
        -LiteralPath $configTemporaryPath `
        -Encoding UTF8
    Move-Item -LiteralPath $configTemporaryPath `
        -Destination $configPath `
        -Force
}

$taskAction = New-ScheduledTaskAction `
    -Execute $installedAgent `
    -Argument "--installed-legacy --quality $Quality --channel-mode $ChannelMode --sample-rate $SampleRate --bits $BitsPerSample" `
    -WorkingDirectory $installRoot
$taskTrigger = New-ScheduledTaskTrigger -AtLogOn -User $userName
$taskPrincipal = New-ScheduledTaskPrincipal `
    -UserId $userName `
    -LogonType Interactive `
    -RunLevel Highest
$taskSettings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -StartWhenAvailable `
    -MultipleInstances IgnoreNew `
    -ExecutionTimeLimit ([timespan]::Zero)

$null = Register-ScheduledTask `
    -TaskName $taskName `
    -Action $taskAction `
    -Trigger $taskTrigger `
    -Principal $taskPrincipal `
    -Settings $taskSettings `
    -Description 'Native AX211 to WH-1000XM5 LDAC user-session agent.' `
    -Force

if (Test-Path -LiteralPath $legacyInstallRoot -PathType Container) {
    Remove-Item -LiteralPath $legacyInstallRoot -Recurse -Force
}

$state = [ordered]@{
    installed_at = (Get-Date).ToString('o')
    task_name = $taskName
    user = $userName
    run_level = 'Highest'
    quality = $Quality
    channel_mode = $ChannelMode
    sample_rate = $SampleRate
    bits_per_sample = $BitsPerSample
    architecture = 'legacy_split_user_mode_avdtp'
    presence_gate = 'connected_and_transport_ready_with_fresh_generation_after_unexpected_exit'
    config_path = $configPath
    install_root = $installRoot
    agent_sha256 = $installedAgentHash
    probe_sha256 = $installedProbeHash
}
$statePath = Join-Path $nativeRoot 'install-state.json'
$state | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $statePath -Encoding UTF8

if ($StartNow) {
    Start-ScheduledTask -TaskName $taskName
}

$task = Get-ScheduledTask -TaskName $taskName
Write-Host "Installed task: $($task.TaskName), state $($task.State)"
Write-Host "Installed agent: $installedAgent"
Write-Host "Default quality: $Quality"
Write-Host "Default channel mode: $ChannelMode"
Write-Host "Default endpoint format: $SampleRate Hz, $BitsPerSample-bit"
Write-Host "Runtime config: $configPath"
Write-Host 'Connection gate: an unexpected session exit must be followed by old transport removal and a fresh XM5 transport generation.'
Write-Host "Agent log: $(Join-Path $nativeRoot 'logs\agent.log')"
Write-Host "Probe log: $(Join-Path $nativeRoot 'logs\probe.log')"
if ($StartNow) {
    Write-Host 'The task was requested to start now.'
} else {
    Write-Host 'The agent will start automatically at the next user logon.'
}
