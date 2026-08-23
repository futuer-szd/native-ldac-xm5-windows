# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [switch]$IncludeFullDump
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an elevated PowerShell 7 terminal.'
}

$kdCandidates = @(
    'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\kd.exe',
    'C:\Program Files\Windows Kits\10\Debuggers\x64\kd.exe'
)
$kd = $kdCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $kd) { throw 'WinDbg kd.exe was not found (install Windows SDK/WDK Debugging Tools).' }

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outRoot = Join-Path $projectRoot 'artifacts\crash-analysis'
New-Item -ItemType Directory -Path $outRoot -Force | Out-Null

$minidumps = @(Get-ChildItem 'C:\Windows\Minidump' -Filter '*.dmp' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime)
if ($minidumps.Count -eq 0) { throw 'No minidumps found.' }

$summary = @()
foreach ($dump in $minidumps) {
    $base = $dump.BaseName
    $log = Join-Path $outRoot ($base + '-analyze.txt')
    Write-Host ("Analyzing {0} ({1} bytes)..." -f $dump.Name, $dump.Length)
    & $kd -z $dump.FullName -logo $log -c '!analyze -v; q'
    $summary += [pscustomobject]@{
        Dump = $dump.Name
        Time = $dump.LastWriteTime
        Log = $log
    }
}

if ($IncludeFullDump -and (Test-Path -LiteralPath 'C:\Windows\MEMORY.DMP')) {
    $memLog = Join-Path $outRoot 'MEMORY-analyze.txt'
    Write-Host 'Analyzing MEMORY.DMP (this can take several minutes)...'
    & $kd -z 'C:\Windows\MEMORY.DMP' -logo $memLog -c '!analyze -v; q'
    $summary += [pscustomobject]@{ Dump = 'MEMORY.DMP'; Time = (Get-Item 'C:\Windows\MEMORY.DMP').LastWriteTime; Log = $memLog }
}

$summary | Format-Table -AutoSize
Write-Host "Analysis output: $outRoot"
Write-Host 'Open each *-analyze.txt and look for: BUGCHECK_CODE, FAILURE_BUCKET_ID, MODULE_NAME, IMAGE_NAME, STACK_TEXT.'