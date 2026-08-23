# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$AllowDirtySource
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The V1 AVRCP filter candidate build requires PowerShell 7.'
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$candidateRoot = Join-Path $projectRoot `
    'artifacts\v1-volume-sync\avrcp-filter-candidate'
$driverProject = Join-Path $projectRoot `
    'avrcp-filter\NativeLdacAvrcpIoFilter.vcxproj'
$driverRoot = Join-Path $projectRoot "avrcp-filter\x64\$Configuration"
$driverOutput = Join-Path $driverRoot 'NativeLdacAvrcpIoFilter'
$certificateSource = Join-Path $driverRoot 'NativeLdacAvrcpIoFilter.cer'
$buildRoot = Join-Path $projectRoot 'build\protocol'
$probeSource = Join-Path $buildRoot `
    "$Configuration\v1_avrcp_filter_probe.exe"
$aclProbeSource = Join-Path $buildRoot `
    "$Configuration\xm5_connection_probe.exe"
$transportProbeSource = Join-Path $buildRoot `
    "$Configuration\transport_probe.exe"

$dirty = @(& git.exe -C $projectRoot status --porcelain)
if (-not $AllowDirtySource -and ($LASTEXITCODE -ne 0 -or $dirty.Count -ne 0)) {
    throw 'The AVRCP filter candidate must be built from clean Git HEAD.'
}
$sourceCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $sourceCommit.Length -ne 40) {
    throw 'The current Git commit could not be resolved.'
}

if (-not $AllowDirtySource) {
    & (Join-Path $PSScriptRoot 'verify-v1-golden-checkpoint.ps1')
}

$msbuildCandidates = @(
    'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe'
)
$msbuild = $msbuildCandidates |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $msbuild) { throw '64-bit Visual Studio 2022 MSBuild was not found.' }

$cmakeCandidates = @(
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
)
$cmake = $cmakeCandidates |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $cmake) { throw 'Visual Studio CMake was not found.' }

$dumpbin = $null
$vcRoots = @(
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC'
)
foreach ($vcRoot in $vcRoots) {
    if (-not (Test-Path -LiteralPath $vcRoot -PathType Container)) {
        continue
    }
    $versions = @(Get-ChildItem -LiteralPath $vcRoot -Directory |
        Sort-Object Name -Descending)
    foreach ($version in $versions) {
        $candidate = Join-Path $version.FullName `
            'bin\Hostx64\x64\dumpbin.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $dumpbin = $candidate
            break
        }
    }
    if ($dumpbin) { break }
}
if (-not $dumpbin) { throw 'Visual Studio dumpbin.exe was not found.' }

& $msbuild $driverProject /m /t:Rebuild `
    "/p:Configuration=$Configuration" /p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw 'The AVRCP filter driver build failed.' }
& $cmake -S $projectRoot -B $buildRoot -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& $cmake --build $buildRoot --config $Configuration --target `
    v1_avrcp_filter_probe xm5_connection_probe transport_probe `
    nativeldac_avrcp_filter_trace_contract_tests
if ($LASTEXITCODE -ne 0) { throw 'The AVRCP filter support build failed.' }
& (Join-Path $buildRoot `
    "$Configuration\nativeldac_avrcp_filter_trace_contract_tests.exe")
if ($LASTEXITCODE -ne 0) { throw 'AVRCP filter trace tests failed.' }
& (Join-Path $projectRoot `
    'agent\tests\v1_avrcp_filter_policy_tests.ps1')
& (Join-Path $projectRoot `
    'agent\tests\v1_avrcp_filter_gate_policy_tests.ps1')

$sysSource = Join-Path $driverOutput 'NativeLdacAvrcpIoFilter.sys'
if (-not (Test-Path -LiteralPath $driverOutput -PathType Container) -or
    -not (Test-Path -LiteralPath $sysSource -PathType Leaf) -or
    -not (Test-Path -LiteralPath $certificateSource -PathType Leaf) -or
    -not (Test-Path -LiteralPath $probeSource -PathType Leaf) -or
    -not (Test-Path -LiteralPath $aclProbeSource -PathType Leaf) -or
    -not (Test-Path -LiteralPath $transportProbeSource -PathType Leaf)) {
    throw 'The filter package, certificate, or probe output is missing.'
}

$imports = @(& $dumpbin /imports $sysSource)
if ($LASTEXITCODE -ne 0) { throw 'Driver import inspection failed.' }
$importText = $imports -join [Environment]::NewLine
if ($importText -match '(?i)btampm|BtaMpm|BthmpSetServiceStateEx|bthport') {
    throw 'The filter binary imports a private Bluetooth/MPM dependency.'
}

$expectedPrefix = [IO.Path]::GetFullPath((Join-Path $projectRoot `
    'artifacts\v1-volume-sync')) + [IO.Path]::DirectorySeparatorChar
$resolvedCandidate = [IO.Path]::GetFullPath($candidateRoot)
if (-not $resolvedCandidate.StartsWith(
        $expectedPrefix,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The candidate output escaped the V1 volume-sync artifact root.'
}
if (Test-Path -LiteralPath $candidateRoot) {
    Remove-Item -LiteralPath $candidateRoot -Recurse -Force
}
$packageRoot = Join-Path $candidateRoot 'package'
$toolsRoot = Join-Path $candidateRoot 'tools'
New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
New-Item -ItemType Directory -Path $toolsRoot -Force | Out-Null
Get-ChildItem -LiteralPath $driverOutput -File |
    Copy-Item -Destination $packageRoot -Force
Copy-Item -LiteralPath $certificateSource `
    -Destination (Join-Path $packageRoot 'NativeLdacAvrcpIoFilter.cer') `
    -Force
Copy-Item -LiteralPath $probeSource `
    -Destination (Join-Path $toolsRoot 'v1_avrcp_filter_probe.exe') -Force
Copy-Item -LiteralPath $aclProbeSource `
    -Destination (Join-Path $toolsRoot 'xm5_connection_probe.exe') -Force
Copy-Item -LiteralPath $transportProbeSource `
    -Destination (Join-Path $toolsRoot 'transport_probe.exe') -Force
$imports | Set-Content -LiteralPath `
    (Join-Path $candidateRoot 'driver-imports.txt') -Encoding utf8

$files = Get-ChildItem -LiteralPath $candidateRoot -Recurse -File |
    Sort-Object FullName |
    ForEach-Object {
        [ordered]@{
            path = [IO.Path]::GetRelativePath($candidateRoot, $_.FullName)
            length = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName `
                -Algorithm SHA256).Hash
        }
    }
$manifest = [ordered]@{
    manifest_version = 1
    policy_version = 7
    created_at = (Get-Date).ToString('o')
    source_commit = $sourceCommit
    source_dirty = [bool]$AllowDirtySource
    configuration = $Configuration
    target_hardware_id = `
        'BTHENUM\{0000110E-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
    extension_inf = $true
    filter_position = 'upper'
    filter_service = 'NativeLdacAvrcpIoFilter'
    expected_function_inf = 'microsoft_bluetooth_avrcptransport.inf'
    expected_function_service = 'Microsoft_Bluetooth_AvrcpTransport'
    public_ioctl_access = 'read-write-volume-only'
    abi_major = 0
    abi_minor = 2
    observe_only = $false
    allowed_write = 'SetAbsoluteVolume only, AVRCP value 0..127'
    pass_through_unknown_requests = $true
    observed_request_types = @('device-control', 'internal-device-control')
    raw_prefix_bytes = 32
    raw_prefix_methods = @(
        'METHOD_BUFFERED',
        'METHOD_IN_DIRECT',
        'METHOD_OUT_DIRECT'
    )
    method_neither_raw_capture = $false
    first_request_arming = $true
    first_request_timeout_publishes_window_status = $true
    no_post_connect_request_failure_code = `
        'no-post-connect-filter-request'
    filter_probe_stderr_in_result = $true
    acl_probe_included = $true
    transport_probe_included = $true
    bounded_ldac_silence_media_prerequisite = $true
    audible_playback = $false
    probe_overrides_allowed = $false
    transport_info_preflight = $true
    filter_probe_starts_after_acl_connect = $true
    filter_probe_starts_after_silence_media_ready = $true
    filter_probe_pre_arm_queue_drain = $true
    gesture_prompt_after_filter_probe_armed = $true
    complete_line_live_forwarding = $true
    high_volume_trace_saved_without_terminal_flood = $true
    decoded_volume_evidence_required = $true
    decoded_pass_through_evidence_required = $true
    decoded_summary_in_filter_probe = $true
    microsoft_private_write_layout = '8-byte header plus AVRCP SetAbsoluteVolume'
    write_timeout_ms = 2000
    install_requires_exact_pdo_restart = $true
    maximum_exact_pdo_restarts = 1
    private_bluetooth_imports = $false
    function_driver_replacement = $false
    class_filter = $false
    installation_performed = $false
    files = @($files)
}
$manifestPath = Join-Path $candidateRoot 'manifest.json'
$manifest | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $manifestPath -Encoding utf8
$manifestHash = (Get-FileHash -LiteralPath $manifestPath `
    -Algorithm SHA256).Hash
Set-Content -LiteralPath (Join-Path $candidateRoot 'manifest.sha256') `
    -Value "$manifestHash  manifest.json" -Encoding ascii

& (Join-Path $PSScriptRoot `
    'verify-v1-avrcp-filter-candidate.ps1') `
    -CandidatePath $candidateRoot

Write-Host "V1 AVRCP upper-filter candidate built: $candidateRoot"
Write-Host "Source: $sourceCommit"
Write-Host 'No driver was staged, installed, bound, restarted, or loaded.'
