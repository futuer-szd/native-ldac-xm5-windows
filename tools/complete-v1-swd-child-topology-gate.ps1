# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'Medium')]
param(
    [switch]$ConfirmV1SwdChildTopologyCompletion,
    [Parameter(Mandatory = $true)][string]$TrialPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-swd-child-topology-common.ps1')

if ($PSVersionTable.PSEdition -ne 'Core' -or
    $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'The SWD child topology completion requires PowerShell 7.'
}
if (-not $ConfirmV1SwdChildTopologyCompletion) {
    throw 'Refusing to finalize the corrected SWD evidence without -ConfirmV1SwdChildTopologyCompletion.'
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$trialRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot `
    'artifacts\v1-volume-sync\trial'))
$directory = [IO.Path]::GetFullPath($TrialPath)
$relative = [IO.Path]::GetRelativePath($trialRoot, $directory)
if ($relative -eq '..' -or $relative.StartsWith(
        '..' + [IO.Path]::DirectorySeparatorChar)) {
    throw 'The completion path must be inside the V1 volume-sync trial root.'
}

$paths = [ordered]@{
    result = Join-Path $directory 'result.json'
    before = Join-Path $directory 'before.json'
    during = Join-Path $directory 'during.json'
    after = Join-Path $directory 'after.json'
    probe_out = Join-Path $directory 'probe.out.log'
    probe_err = Join-Path $directory 'probe.err.log'
}
foreach ($path in $paths.Values) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required SWD lifecycle evidence is missing: $path"
    }
}

$original = Get-Content -LiteralPath $paths.result -Raw | ConvertFrom-Json
$before = Get-Content -LiteralPath $paths.before -Raw | ConvertFrom-Json
$during = Get-Content -LiteralPath $paths.during -Raw | ConvertFrom-Json
$after = Get-Content -LiteralPath $paths.after -Raw | ConvertFrom-Json
$probeOut = Get-Content -LiteralPath $paths.probe_out -Raw
$probeErr = Get-Content -LiteralPath $paths.probe_err -Raw
if ([int]$original.schema_version -ne 1 -or
    [int]$original.policy_version -ne 1 -or
    $original.passed -ne $false -or
    [string]$original.source_commit -notmatch '^[0-9a-fA-F]{40}$' -or
    -not $original.topology.valid -or
    $original.topology.write_authorization -ne $false) {
    throw 'The selected result is not an eligible policy 1 evidence-contract false failure.'
}
foreach ($property in @(
        'driver_installed', 'audio_endpoint_created', 'pnp_restarted',
        'bluetooth_toggled', 'endpoint_written', 'avrcp_written')) {
    if ($original.safety.$property -ne $false) {
        throw "The original safety evidence is not fail-closed: $property"
    }
}

$head = (& git.exe -C $projectRoot rev-parse HEAD).Trim()
$status = @(& git.exe -C $projectRoot status --porcelain `
    --untracked-files=all)
if ($LASTEXITCODE -ne 0 -or $status.Count -ne 0 -or
    $head -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'The SWD topology completion requires clean Git source.'
}
& git.exe -C $projectRoot merge-base --is-ancestor `
    ([string]$original.source_commit) $head
if ($LASTEXITCODE -ne 0) {
    throw 'The recorded policy 1 source is not an ancestor of the current fix.'
}

$duration = [int]$original.duration_seconds
$expectedProbe =
    '(?ms)^Driverless SWD child created: ' +
    [regex]::Escape($script:V1SwdChildInstanceId) +
    "\r?\nHolding for $duration second\(s\); no driver or interface is registered\.\r?\n" +
    'Driverless SWD child handle closed; the probe is removed\.\r?\n?$'
if ($probeOut -notmatch $expectedProbe -or
    -not [string]::IsNullOrWhiteSpace($probeErr)) {
    throw 'The stored SWD probe lifecycle log is incomplete or contains an error.'
}
if (-not (Test-V1SwdChildLifecycleEvidence -Before $before `
        -During $during -After $after -Probe $original.probe `
        -ExpectedParent ([string]$during.child_devices[0].parent) `
        -ExpectedContainer ([string]$during.child_devices[0].container_id))) {
    throw 'The stored trial still does not satisfy the corrected policy 2 evidence contract.'
}
if ([string]$during.child_devices[0].parent -ine
        [string]$before.transport.instance_id -or
    [string]$during.child_devices[0].container_id -ine
        [string]$before.transport.container_id) {
    throw 'The stored child does not use the recorded XM5 transport parent and container.'
}

$activeChildren = @(Get-PnpDevice `
    -InstanceId $script:V1SwdChildInstanceId `
    -ErrorAction SilentlyContinue | Where-Object { [bool]$_.Present })
if ($activeChildren.Count -ne 0) {
    throw 'The SWD topology child is unexpectedly still active.'
}

$completedPath = Join-Path $directory 'completed-result.json'
if (Test-Path -LiteralPath $completedPath) {
    throw 'This SWD topology evidence has already been completed.'
}
Write-Host 'The policy 1 raw lifecycle evidence satisfies the corrected policy 2 contract.'
Write-Host 'The only prior mismatches were the Windows inbox null-service INF and a serialized null Hardware ID.'
Write-Host 'This completion writes one JSON file and does not create a device or change the driver, PnP, Bluetooth, endpoint, or audio path.'
if (-not $PSCmdlet.ShouldProcess(
        $directory,
        'Finalize the already completed SWD child lifecycle evidence')) {
    return
}

$completed = [ordered]@{
    schema_version = 1
    policy_version = $script:V1SwdChildTopologyPolicyVersion
    passed = $true
    finalized_at = (Get-Date).ToString('o')
    finalized_from_policy_version = 1
    finalized_after_evidence_contract_correction = $true
    system_experiment_rerun_required = $false
    source_commit = [string]$original.source_commit
    completion_source_commit = $head
    original_result = $paths.result
    before = $paths.before
    during = $paths.during
    after = $paths.after
    probe_out = $paths.probe_out
    probe_err = $paths.probe_err
    child_instance_id = [string]$during.child_devices[0].instance_id
    child_parent = [string]$during.child_devices[0].parent
    child_container_id = [string]$during.child_devices[0].container_id
    inbox_null_driver_inf = [string]$during.child_devices[0].published_inf
    function_service = [string]$during.child_devices[0].service
    nonempty_hardware_id_count = @($during.child_devices[0].hardware_ids |
        Where-Object {
            -not [string]::IsNullOrWhiteSpace([string]$_)
        }).Count
    active_child_absent_after_close = $true
    driver_package_inventory_unchanged = $true
    endpoint_volume_snapshot_unchanged = $true
    probe = $original.probe
    safety = $original.safety
}
$temporaryPath = "$completedPath.tmp"
$completed | ConvertTo-Json -Depth 8 | Set-Content `
    -LiteralPath $temporaryPath -Encoding utf8NoBOM
Move-Item -LiteralPath $temporaryPath -Destination $completedPath -Force

Write-Host 'V1 driverless SWD child topology evidence finalized successfully.'
Write-Host 'No third system-level creation is required.'
Write-Host "Result: $completedPath"
