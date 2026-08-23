# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [switch]$Remove,
    [string]$TaskName = 'NativeLdacAvrcpHandoffHost'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'v1-avrcp-filter-gate-common.ps1')

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The V1 AVRCP handoff retirement tool requires PowerShell 7.'
}
Assert-V1AvrcpFilterAdministrator

if (-not $Remove) {
    throw 'The V1 AVRCP handoff host is retired and cannot be installed. Only -Remove is supported.'
}
if ($Remove) {
    if (-not $PSCmdlet.ShouldProcess(
            "retired task '$TaskName' and exact handoff host processes",
            'Stop and remove the V1 AVRCP handoff host')) {
        return
    }
    $task = Get-ScheduledTask -TaskName $TaskName `
        -ErrorAction SilentlyContinue
    if ($null -ne $task) {
        Stop-ScheduledTask -TaskName $TaskName `
            -ErrorAction SilentlyContinue
    }
    $processes = @(Get-CimInstance Win32_Process `
        -Filter "Name = 'v1_avrcp_handoff_host.exe'" `
        -ErrorAction SilentlyContinue)
    $allowedRoots = @(
        [IO.Path]::GetFullPath((Join-Path $PSScriptRoot `
            '..\build\protocol\Release')),
        [IO.Path]::GetFullPath((Join-Path $env:ProgramFiles 'NativeLdac')))
    foreach ($process in $processes) {
        $processPath = [string]$process.ExecutablePath
        if ([string]::IsNullOrWhiteSpace($processPath)) {
            throw "Refusing to stop handoff PID $($process.ProcessId): executable path is unavailable."
        }
        $resolvedPath = [IO.Path]::GetFullPath($processPath)
        $allowed = $false
        foreach ($root in $allowedRoots) {
            if ($resolvedPath.StartsWith(
                    $root + [IO.Path]::DirectorySeparatorChar,
                    [StringComparison]::OrdinalIgnoreCase)) {
                $allowed = $true
                break
            }
        }
        if (-not $allowed) {
            throw "Refusing to stop unexpected handoff executable: $resolvedPath"
        }
        Stop-Process -Id ([int]$process.ProcessId) -Force `
            -ErrorAction Stop
        Wait-Process -Id ([int]$process.ProcessId) -Timeout 10 `
            -ErrorAction SilentlyContinue
        if (Get-Process -Id ([int]$process.ProcessId) `
                -ErrorAction SilentlyContinue) {
            throw "The retired handoff PID $($process.ProcessId) did not exit."
        }
    }
    if ($null -ne $task) {
        Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false `
            -ErrorAction Stop
    }
    foreach ($file in @(
            (Join-Path $env:ProgramData 'NativeLdac\handoff-host.log'),
            (Join-Path $env:ProgramData `
                'NativeLdac\avrcp-handoff-state.json'))) {
        if (Test-Path -LiteralPath $file -PathType Leaf) {
            Remove-Item -LiteralPath $file -Force
        }
    }
    Write-Host 'The retired AVRCP handoff host is not installed or running.'
    return
}
