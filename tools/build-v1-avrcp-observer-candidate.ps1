# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The V1 AVRCP observer candidate build requires PowerShell 7.'
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$candidateRoot = Join-Path $projectRoot `
    'artifacts\v1-volume-sync\avrcp-observer-candidate'
$driverProject = Join-Path $projectRoot `
    'avrcp-sideband\NativeLdacAvrcpObserver.vcxproj'
$driverOutput = Join-Path $projectRoot `
    "avrcp-sideband\x64\$Configuration\NativeLdacAvrcpObserver"
$driverRoot = Join-Path $projectRoot `
    "avrcp-sideband\x64\$Configuration"
$certificateSource = Join-Path $driverRoot 'NativeLdacAvrcpObserver.cer'
$buildRoot = Join-Path $projectRoot 'build\protocol'
$probeSource = Join-Path $buildRoot `
    "$Configuration\v1_avrcp_observer_probe.exe"
$aclProbeSource = Join-Path $buildRoot `
    "$Configuration\xm5_connection_probe.exe"
$transportProbeSource = Join-Path $buildRoot `
    "$Configuration\transport_probe.exe"

$dirty = @(& git.exe -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or $dirty.Count -ne 0) {
    throw 'The AVRCP observer candidate must be built from clean Git HEAD.'
}
$sourceCommit = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $sourceCommit.Length -ne 40) {
    throw 'The current Git commit could not be resolved.'
}

& (Join-Path $PSScriptRoot 'verify-v1-golden-checkpoint.ps1')

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

& $msbuild $driverProject /m /t:Rebuild `
    "/p:Configuration=$Configuration" /p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw 'The AVRCP observer driver build failed.' }
& $cmake -S $projectRoot -B $buildRoot -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& $cmake --build $buildRoot --config $Configuration --target `
    v1_avrcp_observer_probe xm5_connection_probe transport_probe avrcp_tests `
    nativeldac_avrcp_event_queue_contract_tests
if ($LASTEXITCODE -ne 0) { throw 'The AVRCP observer support build failed.' }
& (Join-Path $buildRoot "$Configuration\avrcp_tests.exe")
if ($LASTEXITCODE -ne 0) { throw 'AVRCP protocol tests failed.' }
& (Join-Path $buildRoot `
    "$Configuration\nativeldac_avrcp_event_queue_contract_tests.exe")
if ($LASTEXITCODE -ne 0) { throw 'AVRCP event queue tests failed.' }
& (Join-Path $projectRoot `
    'agent\tests\v1_avrcp_observer_policy_tests.ps1')

if (-not (Test-Path -LiteralPath $driverOutput -PathType Container) -or
    -not (Test-Path -LiteralPath $certificateSource -PathType Leaf) -or
    -not (Test-Path -LiteralPath $probeSource -PathType Leaf) -or
    -not (Test-Path -LiteralPath $aclProbeSource -PathType Leaf) -or
    -not (Test-Path -LiteralPath $transportProbeSource -PathType Leaf)) {
    throw 'The driver package or observer probe output is missing.'
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
    -Destination (Join-Path $packageRoot 'NativeLdacAvrcpObserver.cer') -Force
Copy-Item -LiteralPath $probeSource `
    -Destination (Join-Path $toolsRoot 'v1_avrcp_observer_probe.exe') -Force
Copy-Item -LiteralPath $aclProbeSource `
    -Destination (Join-Path $toolsRoot 'xm5_connection_probe.exe') -Force
Copy-Item -LiteralPath $transportProbeSource `
    -Destination (Join-Path $toolsRoot 'transport_probe.exe') -Force

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
    policy_version = 3
    created_at = (Get-Date).ToString('o')
    source_commit = $sourceCommit
    configuration = $Configuration
    target_instance_prefix = `
        'BTHENUM\{0000110E-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
    target_service = 'NativeLdacAvrcpObserver'
    expected_previous_inf = 'microsoft_bluetooth_avrcptransport.inf'
    expected_previous_service = 'Microsoft_Bluetooth_AvrcpTransport'
    avctp_control_psm = 0x0017
    avctp_control_direction = 'outbound'
    fixed_psm_listener = $false
    outbound_open = $true
    outbound_open_attempts = 1
    abi_minor = 11
    avdtp_capability_hold_prerequisite = $true
    avdtp_media_session_prerequisite = $true
    observation_activation =
        'explicit read-only BEGIN_OBSERVATION after the media session is ready'
    profile_acquisition =
        'deferred to the active observation session and current physical ACL'
    binding_order = @(
        'stage candidate while Microsoft AVRCP remains bound',
        'physical ACL connect',
        'capability-only AVDTP signaling hold ready',
        'bind exact AVRCP 0x110E PDO',
        'explicit read-only BEGIN_OBSERVATION after media session ready',
        'acquire a fresh BTH profile for the current physical ACL',
        'one outbound AVCTP PSM 0x0017 OPEN',
        'restore Microsoft AVRCP while AVDTP hold remains active',
        'release AVDTP signaling'
    )
    observe_only = $false
    public_ioctl_access = 'read-write'
    allowed_protocol_writes = @(
        'GET_CAPABILITIES(events-supported)',
        'REGISTER_NOTIFICATION(volume-changed)',
        'REGISTER_NOTIFICATION(changed write-back)',
        'SET_ABSOLUTE_VOLUME',
        'PASS_THROUGH response only'
    )
    forbidden_actions = @(
        'Core Audio volume write',
        'media-key injection',
        'player control',
        'AVDTP SET_CONFIGURATION, media OPEN, START, or media packet',
        'A2DP or ROOT endpoint binding'
        'automatic AVCTP OPEN during PnP start',
        'BTH profile retention across observation sessions',
        'AVCTP OPEN retry'
    )
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
    'verify-v1-avrcp-observer-candidate.ps1') `
    -CandidatePath $candidateRoot

Write-Host "V1 AVRCP observer candidate built: $candidateRoot"
Write-Host "Source: $sourceCommit"
Write-Host 'No driver was staged, installed, bound, restarted, or loaded.'
