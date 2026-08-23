# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateSet('Release')][string]$Configuration = 'Release',
    [string]$TopologyPrerequisitePath,
    [string]$GoldenCheckpointPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-swd-endpoint-candidate-common.ps1')

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The transport-owned endpoint candidate requires PowerShell 7.'
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourceCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$sourceStatus = @(& git.exe -C $projectRoot status --porcelain `
    --untracked-files=all)
if ($LASTEXITCODE -ne 0 -or
    $sourceCommit -notmatch '^[0-9a-fA-F]{40}$' -or
    $sourceStatus.Count -ne 0) {
    throw 'The transport-owned endpoint candidate requires clean Git source.'
}

if ([string]::IsNullOrWhiteSpace($TopologyPrerequisitePath)) {
    $TopologyPrerequisitePath = Join-Path $projectRoot `
        'artifacts\v1-volume-sync\trial\driverless-child-20260804-204810-231\completed-result.json'
}
$TopologyPrerequisitePath = [IO.Path]::GetFullPath(
    $TopologyPrerequisitePath)
$topology = Get-Content -LiteralPath $TopologyPrerequisitePath -Raw |
    ConvertFrom-Json
if ([int]$topology.policy_version -ne 2 -or
    $topology.passed -ne $true -or
    $topology.system_experiment_rerun_required -ne $false -or
    $topology.active_child_absent_after_close -ne $true -or
    [string]$topology.child_parent -notmatch '^BTHENUM\\' -or
    [string]$topology.child_container_id -notmatch
        '^\{[0-9A-Fa-f-]{36}\}$') {
    throw 'The completed driverless-child topology prerequisite is invalid.'
}

if ([string]::IsNullOrWhiteSpace($GoldenCheckpointPath)) {
    $GoldenCheckpointPath = (Get-Content -LiteralPath (Join-Path `
        $projectRoot 'artifacts\v1-golden\latest.txt') -Raw).Trim()
}
& (Join-Path $PSScriptRoot 'verify-v1-golden-checkpoint.ps1') `
    -CheckpointPath $GoldenCheckpointPath
# PowerShell verification failures propagate as terminating errors. Do not
# inspect $LASTEXITCODE here because nested native fail-closed probes may leave
# an expected nonzero value after the script itself succeeds.

$containerHeaderPath = Join-Path $projectRoot `
    'audio-endpoint\Source\Inc\nativeldac_remote_container.h'
$containerHeader = Get-Content -LiteralPath $containerHeaderPath -Raw
$containerMatch = [regex]::Match(
    $containerHeader,
    'NATIVE_LDAC_REMOTE_CONTAINER_ID_STRING\s+\\\s*\r?\n\s*"([0-9A-Fa-f-]{36})"')
if (-not $containerMatch.Success -or
    ('{' + $containerMatch.Groups[1].Value + '}') -ine
        [string]$topology.child_container_id) {
    throw 'The compiled Native endpoint Container ID does not match the verified XM5 topology.'
}

$msbuildCandidates = @(
    'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe'
)
$msbuild = $msbuildCandidates | Where-Object {
    Test-Path -LiteralPath $_ -PathType Leaf
} | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($msbuild)) {
    throw '64-bit Visual Studio 2022 MSBuild was not found.'
}
$solution = Join-Path $projectRoot 'audio-endpoint\SimpleAudioSample.sln'
$driverDate = [DateTime]::UtcNow.ToString(
    'MM/dd/yyyy', [Globalization.CultureInfo]::InvariantCulture)
& $msbuild $solution /m /t:Rebuild /p:Configuration=Release `
    /p:Platform=x64 /p:NativeLdacSwdEndpointCandidate=true `
    "/p:NativeLdacDriverDate=$driverDate"
if ($LASTEXITCODE -ne 0) {
    throw 'The transport-owned endpoint driver build failed.'
}

$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$buildRoot = Join-Path $projectRoot 'build\protocol'
& $cmake -S $projectRoot -B $buildRoot -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) {
    throw 'CMake configure failed.'
}
& $cmake --build $buildRoot --config $Configuration --target `
    v1_swd_endpoint_host audio_endpoint_probe endpoint_volume_probe `
    xm5_connection_probe transport_probe
if ($LASTEXITCODE -ne 0) {
    throw 'The endpoint candidate companion build failed.'
}

$postCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$postStatus = @(& git.exe -C $projectRoot status --porcelain `
    --untracked-files=all)
if ($LASTEXITCODE -ne 0 -or $postCommit -cne $sourceCommit -or
    $postStatus.Count -ne 0) {
    throw 'The candidate build changed the clean source worktree.'
}

$sourcePackage = Join-Path $projectRoot `
    "audio-endpoint\x64\$Configuration\package"
$sourceCertificate = Join-Path $projectRoot `
    "audio-endpoint\x64\$Configuration\package.cer"
$packageSources = [ordered]@{
    'NativeLdacSwdAudio.inf' = Join-Path $sourcePackage `
        'NativeLdacSwdAudio.inf'
    'NativeLdacSwdAudio.sys' = Join-Path $sourcePackage `
        'NativeLdacSwdAudio.sys'
    'NativeLdacSwdAudio.cat' = Join-Path $sourcePackage `
        'NativeLdacSwdAudio.cat'
    'NativeLdacSwdAudio.cer' = $sourceCertificate
}
$rootSources = [ordered]@{
    'v1_swd_endpoint_host.exe' = Join-Path $buildRoot `
        "$Configuration\v1_swd_endpoint_host.exe"
    'audio_endpoint_probe.exe' = Join-Path $buildRoot `
        "$Configuration\audio_endpoint_probe.exe"
    'endpoint_volume_probe.exe' = Join-Path $buildRoot `
        "$Configuration\endpoint_volume_probe.exe"
    'xm5_connection_probe.exe' = Join-Path $buildRoot `
        "$Configuration\xm5_connection_probe.exe"
    'transport_probe.exe' = Join-Path $buildRoot `
        "$Configuration\transport_probe.exe"
    'verify-v1-golden-checkpoint.ps1' = Join-Path $PSScriptRoot `
        'verify-v1-golden-checkpoint.ps1'
    'v1-swd-endpoint-candidate-common.ps1' = Join-Path $PSScriptRoot `
        'v1-swd-endpoint-candidate-common.ps1'
    'verify-v1-swd-endpoint-candidate.ps1' = Join-Path $PSScriptRoot `
        'verify-v1-swd-endpoint-candidate.ps1'
    'topology-prerequisite.json' = $TopologyPrerequisitePath
}
$missing = @(@($packageSources.Values) + @($rootSources.Values) |
    Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
if ($missing.Count -ne 0) {
    throw "The endpoint candidate inputs are missing: $($missing -join ', ')"
}

$output = Join-Path $projectRoot `
    'artifacts\v1-volume-sync\endpoint-candidate'
if (Test-Path -LiteralPath $output -PathType Container) {
    Remove-Item -LiteralPath $output -Recurse -Force
}
$packageOutput = Join-Path $output 'package'
New-Item -ItemType Directory -Path $packageOutput -Force | Out-Null
$files = @()
foreach ($entry in $packageSources.GetEnumerator()) {
    $destination = Join-Path $packageOutput $entry.Key
    Copy-Item -LiteralPath $entry.Value -Destination $destination -Force
    $item = Get-Item -LiteralPath $destination
    $files += [ordered]@{
        path = 'package\' + $entry.Key
        length = [long]$item.Length
        sha256 = (Get-FileHash -LiteralPath $destination `
            -Algorithm SHA256).Hash
    }
}
foreach ($entry in $rootSources.GetEnumerator()) {
    $destination = Join-Path $output $entry.Key
    Copy-Item -LiteralPath $entry.Value -Destination $destination -Force
    $item = Get-Item -LiteralPath $destination
    $files += [ordered]@{
        path = $entry.Key
        length = [long]$item.Length
        sha256 = (Get-FileHash -LiteralPath $destination `
            -Algorithm SHA256).Hash
    }
}

$certificate = Get-PfxCertificate -FilePath (Join-Path $packageOutput `
    'NativeLdacSwdAudio.cer')
$catalogSignature = Get-AuthenticodeSignature -LiteralPath (Join-Path `
    $packageOutput 'NativeLdacSwdAudio.cat')
$driverSignature = Get-AuthenticodeSignature -LiteralPath (Join-Path `
    $packageOutput 'NativeLdacSwdAudio.sys')
if ($null -eq $catalogSignature.SignerCertificate -or
    $null -eq $driverSignature.SignerCertificate -or
    $catalogSignature.SignerCertificate.Thumbprint -ne
        $certificate.Thumbprint -or
    $driverSignature.SignerCertificate.Thumbprint -ne
        $certificate.Thumbprint) {
    throw 'The endpoint candidate SYS/CAT signatures are invalid.'
}

$manifest = [ordered]@{
    manifest_version = 1
    policy_version = $script:V1SwdEndpointCandidatePolicyVersion
    created_at = (Get-Date).ToString('o')
    source_commit = $sourceCommit
    source_dirty = $false
    configuration = $Configuration
    installable = $true
    staged_only = $true
    install_script_included = $false
    device_creation_tool_included = $true
    device_creation_default = $false
    current_root_endpoint_preserved = $true
    current_audio_path_changed = $false
    volume_observation_presence_supported = $true
    stop_event_supported = $true
    xm5_connection_probe_included = $true
    capability_only_signaling_hold_supported = $true
    never_default_render_role_mask = 0x00000107
    hardware_id = $script:V1SwdEndpointHardwareId
    service_name = $script:V1SwdEndpointService
    expected_instance_id = $script:V1SwdEndpointInstanceId
    expected_parent = [string]$topology.child_parent
    remote_container_id = [string]$topology.child_container_id
    endpoint_name = 'Native LDAC SWD - WH-1000XM5 (candidate)'
    topology_prerequisite = 'topology-prerequisite.json'
    golden_checkpoint = [IO.Path]::GetFullPath($GoldenCheckpointPath)
    certificate_thumbprint = $certificate.Thumbprint
    driver_signature_status = [string]$driverSignature.Status
    catalog_signature_status = [string]$catalogSignature.Status
    files = @($files)
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content `
    -LiteralPath (Join-Path $output 'manifest.json') -Encoding utf8NoBOM

& (Join-Path $output 'verify-v1-swd-endpoint-candidate.ps1') `
    -CandidatePath $output

Write-Host "Built V1 transport-owned endpoint candidate: $output"
Write-Host "Source commit: $sourceCommit"
Write-Host 'The package is staged-only. No driver, certificate, device, endpoint, Bluetooth state, or audio path was changed.'
