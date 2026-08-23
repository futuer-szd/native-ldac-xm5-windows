# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
$source = Get-Content -LiteralPath `
    (Join-Path $projectRoot 'engine\windows\native_pcm_source.cpp') -Raw
$probe = Get-Content -LiteralPath `
    (Join-Path $projectRoot 'tools\transport_probe.c') -Raw
$engine = Get-Content -LiteralPath `
    (Join-Path $projectRoot 'engine\windows\v1_pcm_encode_engine.cpp') -Raw

$drainStart = $source.IndexOf(
    'static native_pcm_source_status drain_driver',
    [StringComparison]::Ordinal)
$readStart = $source.IndexOf(
    'native_pcm_source_read_f32_stereo',
    $drainStart,
    [StringComparison]::Ordinal)
if ($drainStart -lt 0 -or $readStart -le $drainStart) {
    throw 'Could not isolate the PCM driver-drain implementation.'
}
$drain = $source.Substring($drainStart, $readStart - $drainStart)
foreach ($forbidden in @(
        'preferred_format.SampleRate',
        'preferred_format.BitsPerSample',
        'ERROR_BAD_FORMAT')) {
    if ($drain.Contains($forbidden)) {
        throw "PCM drain still rejects a supported active format because of stale preference: $forbidden"
    }
}
foreach ($required in @(
        'snapshot.sample_rate_hz',
        'ldac_encoder_create')) {
    if (-not $engine.Contains($required)) {
        throw "Dry engine does not derive encoding from active PCM: $required"
    }
}
foreach ($required in @(
        'Windows did not reopen the active Native endpoint',
        'snapshot.sample_rate_hz != preferredSampleRateHz',
        'snapshot.bits_per_sample != preferredBitsPerSample')) {
    if (-not $probe.Contains($required)) {
        throw "Legacy transport lost its explicit pre-OPEN format gate: $required"
    }
}

Write-Host 'Native PCM active-format policy tests passed.'
