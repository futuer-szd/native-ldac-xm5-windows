# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-FileEntry {
    param([Parameter(Mandatory = $true)][string]$Path)

    $file = Get-Item -LiteralPath $Path
    $hash = Get-FileHash -LiteralPath $Path -Algorithm SHA256
    return [ordered]@{
        name = $file.Name
        length = $file.Length
        sha256 = $hash.Hash
    }
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourceCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $sourceCommit -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'Could not determine the source Git commit.'
}
$sourceStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0) {
    throw 'Could not determine the source Git status.'
}
$sourceDirty = $sourceStatus.Count -ne 0
if ($sourceDirty) {
    throw 'Refusing to build a legacy candidate from a dirty Git worktree.'
}

$lastVerifiedDriverCommit =
    '5ed098f8ebfd6e65fa119add3e86f15de8ad1a47'
$lastVerifiedDriverTree =
    '1e2706b4abaabd2abcaa5796b4be2bc11dfd36da'
$approvedDiagnosticDriverCommit =
    'f3621916841ead3aff0342604712c21477b33a35'
$approvedDiagnosticDriverTree =
    '85a0b46231ae2f3212e6616346e2d6905314f0ff'
$currentDriverTree = (& git.exe -C $projectRoot rev-parse `
    'HEAD:driver').Trim()
if ($LASTEXITCODE -ne 0 -or
    $currentDriverTree -ne $approvedDiagnosticDriverTree) {
    throw 'The LdacNative driver source differs from the approved ABI 0.5 one-shot inbound-listener tree. Review and update the frozen reference before building another candidate.'
}

$msbuildCandidates = @(
    'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe'
)
$msbuildPath = $msbuildCandidates |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $msbuildPath) {
    throw '64-bit Visual Studio 2022 MSBuild was not found.'
}

$driverProject = Join-Path $projectRoot 'driver\LdacNative.vcxproj'
& $msbuildPath $driverProject /m /t:Rebuild `
    "/p:Configuration=$Configuration" /p:Platform=x64
if ($LASTEXITCODE -ne 0) {
    throw "Legacy transport driver build failed with exit code $LASTEXITCODE."
}

$agentScript = Join-Path $PSScriptRoot 'build-agent.ps1'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $agentScript `
    -Configuration $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "Legacy agent build failed with exit code $LASTEXITCODE."
}

$connectionProbeScript = Join-Path $PSScriptRoot `
    'build-xm5-connection-probe.ps1'
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File $connectionProbeScript -Configuration $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "XM5 connection probe build failed with exit code $LASTEXITCODE."
}

$postBuildCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$postBuildStatus = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or $postBuildCommit -ne $sourceCommit) {
    throw 'The source Git commit changed during the legacy candidate build.'
}
if ($postBuildStatus.Count -ne 0) {
    throw 'The legacy candidate build changed the clean source worktree.'
}

$candidateRoot = Join-Path $projectRoot 'artifacts\legacy-candidate'
$packageRoot = Join-Path $candidateRoot 'package'
$expectedPrefix = $projectRoot.TrimEnd('\') + '\'
if (-not $candidateRoot.StartsWith(
        $expectedPrefix,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Legacy candidate path escaped the workspace: $candidateRoot"
}
if (Test-Path -LiteralPath $candidateRoot -PathType Container) {
    Remove-Item -LiteralPath $candidateRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null

$driverRoot = Join-Path $projectRoot "driver\x64\$Configuration"
$driverPackageRoot = Join-Path $driverRoot 'LdacNative'
$packageSources = [ordered]@{
    'LdacNative.inf' = Join-Path $driverPackageRoot 'LdacNative.inf'
    'LdacNative.sys' = Join-Path $driverPackageRoot 'LdacNative.sys'
    'ldacnative.cat' = Join-Path $driverPackageRoot 'ldacnative.cat'
    'LdacNative.cer' = Join-Path $driverRoot 'LdacNative.cer'
}
$companionSources = [ordered]@{
    'ldac_agent.exe' = Join-Path $projectRoot 'artifacts\agent\ldac_agent.exe'
    'transport_probe.exe' = Join-Path $projectRoot 'artifacts\agent\transport_probe.exe'
    'xm5_connection_probe.exe' = Join-Path $projectRoot `
        'artifacts\diagnostics\xm5_connection_probe.exe'
    'xm5_connection_probe.manifest.json' = Join-Path $projectRoot `
        'artifacts\diagnostics\xm5_connection_probe.manifest.json'
}
$allSources = @($packageSources.Values) + @($companionSources.Values)
$missingSources = @($allSources | Where-Object {
    -not (Test-Path -LiteralPath $_ -PathType Leaf)
})
if ($missingSources.Count -ne 0) {
    throw "Legacy candidate build outputs are missing: $($missingSources -join ', ')"
}
foreach ($entry in $packageSources.GetEnumerator()) {
    Copy-Item -LiteralPath $entry.Value `
        -Destination (Join-Path $packageRoot $entry.Key) -Force
}
foreach ($entry in $companionSources.GetEnumerator()) {
    Copy-Item -LiteralPath $entry.Value `
        -Destination (Join-Path $candidateRoot $entry.Key) -Force
}

$certificatePath = Join-Path $packageRoot 'LdacNative.cer'
$catalogPath = Join-Path $packageRoot 'ldacnative.cat'
$driverPath = Join-Path $packageRoot 'LdacNative.sys'
$certificate = Get-PfxCertificate -FilePath $certificatePath
foreach ($signaturePath in @($catalogPath, $driverPath)) {
    $signature = Get-AuthenticodeSignature -LiteralPath $signaturePath
    if ($null -eq $signature.SignerCertificate -or
        $signature.SignerCertificate.Thumbprint -ne $certificate.Thumbprint) {
        throw "Legacy candidate signature mismatch: $signaturePath"
    }
}

$packageEntries = @($packageSources.Keys | ForEach-Object {
    Get-FileEntry -Path (Join-Path $packageRoot $_)
})
$companionEntries = @($companionSources.Keys | ForEach-Object {
    Get-FileEntry -Path (Join-Path $candidateRoot $_)
})
$manifest = [ordered]@{
    manifest_version = 3
    created_at = (Get-Date).ToString('o')
    source_commit = $sourceCommit
    source_dirty = $sourceDirty
    configuration = $Configuration
    installable = $true
    staged_only = $true
    install_script_included = $false
    architecture = 'legacy_split_user_mode_avdtp'
    hardware_id = 'BTHENUM\{0000110B-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
    service_name = 'LdacNative'
    driver_abi = '0.5'
    last_verified_driver_commit = $lastVerifiedDriverCommit
    last_verified_driver_tree = $lastVerifiedDriverTree
    approved_diagnostic_driver_commit = $approvedDiagnosticDriverCommit
    approved_diagnostic_driver_tree = $approvedDiagnosticDriverTree
    current_driver_tree = $currentDriverTree
    certificate_thumbprint = $certificate.Thumbprint
    package_files = $packageEntries
    companion_files = $companionEntries
    policy = [ordered]@{
        direct_pdo_included = $false
        background_open_attempts = 1
        unexpected_exit_requires_fresh_transport_generation = $true
        first_hardware_gate = 'clean_baseline_install_reboot_acl_confirmed_single_open_diagnostic'
        requires_clean_original_a2dp = $true
        requires_reboot_before_avdtp = $true
        hot_swap_playback_forbidden = $true
        open_failure_telemetry_required = $true
        requires_acl_connect_event = $true
        requires_operator_power_confirmation = $true
    }
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content `
    -LiteralPath (Join-Path $candidateRoot 'manifest.json') `
    -Encoding UTF8

$verifyScript = Join-Path $PSScriptRoot 'verify-legacy-candidate.ps1'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $verifyScript `
    -CandidatePath $candidateRoot
if ($LASTEXITCODE -ne 0) {
    throw "Legacy candidate verification failed with exit code $LASTEXITCODE."
}

Write-Host "Built legacy candidate: $candidateRoot"
Write-Host "Source commit: $sourceCommit"
Write-Host 'No driver, certificate, task, process, or system setting was changed.'
