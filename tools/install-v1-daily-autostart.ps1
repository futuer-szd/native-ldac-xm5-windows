# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmV1DailyInstall,
    [string]$CandidateRoot = (Join-Path $PSScriptRoot `
        '..\artifacts\v1-daily-host\candidate'),
    [string]$InstallRoot = (Join-Path $env:ProgramFiles 'NativeLdac\V1'),
    [string]$RuntimeRoot = (Join-Path $env:LOCALAPPDATA 'NativeLdac\V1'),
    [string]$InstanceSuffix = 'default',
    [ValidateRange(65536, 33554432)]
    [long]$MaximumLogBytes = 4194304,
    [ValidateRange(1, 8)]
    [int]$RetainedLogs = 4,
    [switch]$StartNow,
    [switch]$EnableVolumeSync
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-daily-host-common.ps1')

Assert-V1DailyPowerShell7
Assert-V1DailyHandoffRetired
Assert-V1DailyAdministrator
if (-not $ConfirmV1DailyInstall) {
    throw 'Refusing to install the V1 daily login host. Re-run with -ConfirmV1DailyInstall.'
}
if (-not (Test-V1DailyInstanceSuffix -Value $InstanceSuffix)) {
    throw 'The V1 daily instance suffix is invalid.'
}

$candidate = [IO.Path]::GetFullPath($CandidateRoot)
$manifest = Test-V1DailyBundleManifest -Root $candidate
$paths = Get-V1DailyPaths -InstallRoot $InstallRoot `
    -RuntimeRoot $RuntimeRoot
$legacyTask = Get-ScheduledTask -TaskName $script:V1LegacyTaskName `
    -ErrorAction SilentlyContinue
if ($legacyTask) {
    throw 'The legacy Native LDAC login task is still installed. Remove it before installing the V1 daily host.'
}
if (Get-ScheduledTask -TaskName $script:V1DailyTaskName `
        -ErrorAction SilentlyContinue) {
    throw 'The V1 daily login task is already installed. Remove it before installing another build.'
}
if (Test-Path -LiteralPath $paths.InstallRoot) {
    throw "The V1 daily install directory already exists: $($paths.InstallRoot)"
}
$legacyProcesses = @(Get-CimInstance Win32_Process `
    -Filter "Name = 'ldac_agent.exe'" -ErrorAction SilentlyContinue)
if ($legacyProcesses.Count -ne 0) {
    throw 'The legacy LDAC agent is running. Stop and remove it before installing the V1 daily host.'
}

$target = "$script:V1DailyTaskName for $([Security.Principal.WindowsIdentity]::GetCurrent().Name)"
if (-not $PSCmdlet.ShouldProcess(
        $target,
        'Install the protected V1 daily bundle and register one elevated at-logon task')) {
    return
}

New-Item -ItemType Directory -Path $paths.InstallRoot -Force | Out-Null
try {
    foreach ($entry in @($manifest.files)) {
        $source = Join-Path $candidate ([string]$entry.path)
        $destination = Join-Path $paths.InstallRoot ([string]$entry.path)
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }
    Copy-Item -LiteralPath (Join-Path $candidate 'manifest.json') `
        -Destination $paths.Manifest -Force
    $null = Test-V1DailyBundleManifest -Root $paths.InstallRoot

    foreach ($directory in @(
            $paths.RuntimeRoot,
            $paths.StateDirectory,
            $paths.ResultDirectory,
            $paths.LogDirectory)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
    $configTemporary = "$($paths.Config).tmp.$PID"
    [ordered]@{
        schema_version = 1
        instance_suffix = $InstanceSuffix
        maximum_log_bytes = $MaximumLogBytes
        retained_logs = $RetainedLogs
        source_commit = [string]$manifest.source_commit
        host_policy_version = $script:V1DailyHostPolicyVersion
        volume_sync = [ordered]@{
            enabled = [bool]$EnableVolumeSync
        }
    } | ConvertTo-Json | Set-Content -LiteralPath $configTemporary `
        -Encoding utf8NoBOM
    Move-Item -LiteralPath $configTemporary -Destination $paths.Config `
        -Force

    $pwshPath = (Get-Command pwsh.exe -ErrorAction Stop).Source
    $startScript = Join-Path $paths.InstallRoot `
        'start-v1-daily-host.ps1'
    $taskArguments =
        "-NoProfile -ExecutionPolicy Bypass -File `"$startScript`" " +
        "-InstallRoot `"$($paths.InstallRoot)`" " +
        "-RuntimeRoot `"$($paths.RuntimeRoot)`""
    $taskAction = New-ScheduledTaskAction -Execute $pwshPath `
        -Argument $taskArguments -WorkingDirectory $paths.InstallRoot
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $taskTrigger = New-ScheduledTaskTrigger -AtLogOn -User $identity.Name
    $taskPrincipal = New-ScheduledTaskPrincipal -UserId $identity.Name `
        -LogonType Interactive -RunLevel Highest
    $taskSettings = New-ScheduledTaskSettingsSet `
        -AllowStartIfOnBatteries `
        -DontStopIfGoingOnBatteries `
        -StartWhenAvailable `
        -MultipleInstances IgnoreNew `
        -RestartCount 3 `
        -RestartInterval (New-TimeSpan -Minutes 1) `
        -ExecutionTimeLimit ([timespan]::Zero)
    $null = Register-ScheduledTask -TaskName $script:V1DailyTaskName `
        -Action $taskAction -Trigger $taskTrigger `
        -Principal $taskPrincipal -Settings $taskSettings `
        -Description 'Native LDAC V1 continuous user-session host.'

    [ordered]@{
        installed_at = (Get-Date).ToString('o')
        task_name = $script:V1DailyTaskName
        user = $identity.Name
        install_root = $paths.InstallRoot
        runtime_root = $paths.RuntimeRoot
        source_commit = [string]$manifest.source_commit
        host_policy_version = $script:V1DailyHostPolicyVersion
        instance_suffix = $InstanceSuffix
        powershell = $pwshPath
        volume_sync_enabled = [bool]$EnableVolumeSync
        handoff_retired = $true
    } | ConvertTo-Json | Set-Content -LiteralPath $paths.InstallState `
        -Encoding utf8NoBOM
} catch {
    $registeredTask = Get-ScheduledTask -TaskName $script:V1DailyTaskName `
        -ErrorAction SilentlyContinue
    if ($registeredTask) {
        Unregister-ScheduledTask -TaskName $script:V1DailyTaskName `
            -Confirm:$false
    }
    if (Test-Path -LiteralPath $paths.InstallRoot -PathType Container) {
        Remove-Item -LiteralPath $paths.InstallRoot -Recurse -Force
    }
    throw
}

if ($StartNow) {
    Start-ScheduledTask -TaskName $script:V1DailyTaskName
}
Write-Host "Installed V1 daily task: $script:V1DailyTaskName"
Write-Host "Install root: $($paths.InstallRoot)"
Write-Host "Runtime root: $($paths.RuntimeRoot)"
Write-Host "Source commit: $($manifest.source_commit)"
Write-Host ($StartNow `
    ? 'The daily host was requested to start now.' `
    : 'The daily host will start at the next user logon.')
