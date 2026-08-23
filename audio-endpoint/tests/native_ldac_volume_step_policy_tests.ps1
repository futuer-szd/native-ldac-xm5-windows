# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$definitions = Get-Content -LiteralPath (
    Join-Path $root 'audio-endpoint\Source\Inc\definitions.h') -Raw
$helper = Get-Content -LiteralPath (
    Join-Path $root 'audio-endpoint\Source\Utilities\kshelper.cpp') -Raw
$topology = Get-Content -LiteralPath (
    Join-Path $root 'audio-endpoint\Source\Filters\speakertopo.cpp') -Raw
$probe = Get-Content -LiteralPath (
    Join-Path $root 'tools\endpoint_volume_probe.cpp') -Raw

if (-not $definitions.Contains(
        '#define VOLUME_STEPPING_DELTA       0x0800')) {
    throw 'Native LDAC does not expose the required 1/32 dB volume step.'
}
if (-not $helper.Contains(
        'Range[i].SteppingDelta        = VOLUME_STEPPING_DELTA')) {
    throw 'The topology basic-support range does not use the fixed step.'
}
if (-not $helper.Contains('VOLUME_NORMALIZE_IN_RANGE(*plVolume)')) {
    throw 'Volume writes do not normalize to the advertised step.'
}
if (-not $topology.Contains('pMiniport->GenerateControlChange')) {
    throw 'Volume writes do not notify Core Audio of the control change.'
}
foreach ($required in @(
        'GetVolumeRange',
        'GetVolumeStepInfo',
        'volume range:',
        'volume step:')) {
    if (-not $probe.Contains($required)) {
        throw "The read-only volume diagnostic is missing: $required"
    }
}

Write-Host 'Native LDAC fine volume-step policy tests passed.'
