# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$commonPath = Join-Path $root 'tools\v1-normal-stop-common.ps1'
$scriptPath = Join-Path $root 'tools\get-v1-normal-stop-resume-status.ps1'
. $commonPath

$cases = @(
    @{a=@($false,$true,$true,$true,$true); s='source-dirty'; x='clean-worktree'; r=$false},
    @{a=@($true,$false,$true,$true,$true); s='prerequisite-invalid'; x='repair-prerequisite'; r=$false},
    @{a=@($true,$true,$false,$false,$false); s='candidate-missing'; x='rebuild-candidate'; r=$false},
    @{a=@($true,$true,$true,$false,$false); s='candidate-invalid'; x='rebuild-candidate'; r=$false},
    @{a=@($true,$true,$true,$true,$false); s='candidate-stale'; x='rebuild-candidate'; r=$false},
    @{a=@($true,$true,$true,$true,$true); s='ready-to-resume'; x='run-read-only-preflight'; r=$true}
)
foreach ($case in $cases) {
    $decision = Get-V1NormalStopResumeDecision `
        -GitClean $case.a[0] -PrerequisiteValid $case.a[1] `
        -CandidatePresent $case.a[2] -CandidateValid $case.a[3] `
        -CandidateCurrent $case.a[4]
    if ([string]$decision.state -ne $case.s -or
        [string]$decision.action -ne $case.x -or
        [bool]$decision.ready -ne $case.r) {
        throw "Unexpected resume decision for $($case.s)."
    }
}

function Write-TestCandidateManifest {
    param(
        [Parameter(Mandatory = $true)][string]$CandidateRoot,
        [Parameter(Mandatory = $true)][string]$Configuration,
        [Parameter(Mandatory = $true)][string]$FidelityPrerequisitePath,
        [Parameter(Mandatory = $true)][string]$FidelityPrerequisiteSource,
        [Parameter(Mandatory = $true)][string]$PnpPrerequisitePath,
        [Parameter(Mandatory = $true)][string]$PnpPrerequisiteSource
    )
    $fileNames = @(
        'v1_presence_agent.exe',
        'v1_transport_normal_stop_worker.exe',
        'audio_endpoint_probe.exe',
        'endpoint_volume_probe.exe',
        'transport_probe.exe',
        'xm5_connection_probe.exe',
        'xm5_connection_probe.manifest.json')
    $files = @()
    foreach ($name in $fileNames) {
        $path = Join-Path $CandidateRoot $name
        Set-Content -LiteralPath $path -Value "fixture-$name" -Encoding UTF8
        $item = Get-Item -LiteralPath $path
        $files += [ordered]@{
            path = $name
            length = [long]$item.Length
            sha256 = Get-V1FileSha256 -Path $path
        }
    }
    $manifest = [ordered]@{
        manifest_version = 1
        transport_policy_version = 20
        source_commit = ('1' * 40)
        driver_tree = '85a0b46231ae2f3212e6616346e2d6905314f0ff'
        source_dirty = $false
        configuration = $Configuration
        fidelity_prerequisite = $FidelityPrerequisitePath
        fidelity_prerequisite_source_commit =
            $FidelityPrerequisiteSource
        fidelity_driver_tree =
            'b31841ff597f1c871addb750539b4f5d39d2cf7e'
        pnp_prerequisite = $PnpPrerequisitePath
        pnp_prerequisite_source_commit = $PnpPrerequisiteSource
        output_policy = 'transparent-unity-full-scale-boundary'
        peak_measurement = 'digital-sample-peak'
        peak_unit = 'dBFS'
        sample_peak_ceiling = 1.0
        maximum_gain_scalar = 1.0
        maximum_duration_ms = 60000
        minimum_stop_duration_ms = 5000
        startup_silence_ms = 20.0
        fade_in_ms = 100.0
        ceiling_ramp_ms = 0.0
        transport_open_render_stability_ms = 1000
        required_transport_open_attempts = 1
        capabilities = @(
            'verified_policy_v10_transparent_transport_prerequisite',
            'verified_policy_v14_inbound_pnp_rundown_prerequisite',
            'one_shot_inbound_listener',
            'inbound_signaling_ready_flags_0xF',
            'successful_open_diagnostics_required',
            'inbound_signaling_channel_required',
            'single_transport_attempt_required',
            'zero_transport_retry_success_contract',
            'exact_XM5_ACL_generation',
            'render_stop_required_after_media_started',
            'graceful_STOP_uses_SUSPEND_CLOSE',
            'ACL_disconnect_uses_local_cancel_only',
            'streaming_peer_close_observer',
            'remote_close_accept_local_cleanup',
            'pre_media_render_gap_accounting',
            'pre_start_pcm_discard_accounting',
            'continuous_render_epoch_before_transport_open',
            'dynamic_post_volume_tracking',
            'bounded_post_start_render_rebind',
            'public_endpoint_state_capture',
            'unity_post_volume_gain',
            'unity_full_scale_sample_boundary_only',
            'no_post_start_ceiling_ramp',
            'sent_frame_fade_in_100_ms',
            'encoded_startup_silence_20_ms',
            'transient_resume_encoded_silence_20_ms',
            'transient_resume_fade_in_100_ms',
            'minimum_5000_ms_before_render_stop',
            'maximum_60000_ms_hard_bound',
            'consumer_lease_release_required',
            'physical_ACL_disconnect_required',
            'no_LinkState_write',
            'no_driver_install',
            'no_reboot')
        files = @($files)
    }
    $manifest | ConvertTo-Json -Depth 6 | Set-Content `
        -LiteralPath (Join-Path $CandidateRoot 'manifest.json') `
        -Encoding UTF8
}

$fixtureRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
    "v1-normal-stop-resume-$([Guid]::NewGuid().ToString('N'))"
$candidateRoot = Join-Path $fixtureRoot 'candidate'
$expectedFidelityPrerequisite = Join-Path $fixtureRoot 'policy-v10.json'
$expectedPnpPrerequisite = Join-Path $fixtureRoot 'policy-v14.json'
New-Item -ItemType Directory -Path $candidateRoot -Force | Out-Null
try {
    Set-Content -LiteralPath $expectedFidelityPrerequisite `
        -Value '{}' -Encoding UTF8
    Set-Content -LiteralPath $expectedPnpPrerequisite `
        -Value '{}' -Encoding UTF8
    Write-TestCandidateManifest -CandidateRoot $candidateRoot `
        -Configuration 'Release' `
        -FidelityPrerequisitePath $expectedFidelityPrerequisite `
        -FidelityPrerequisiteSource ('3' * 40) `
        -PnpPrerequisitePath $expectedPnpPrerequisite `
        -PnpPrerequisiteSource ('4' * 40)
    [void](Get-V1NormalStopCandidate -CandidatePath $candidateRoot `
        -ExpectedFidelityPrerequisitePath $expectedFidelityPrerequisite `
        -ExpectedPnpPrerequisitePath $expectedPnpPrerequisite)

    Write-TestCandidateManifest -CandidateRoot $candidateRoot `
        -Configuration 'Debug' `
        -FidelityPrerequisitePath $expectedFidelityPrerequisite `
        -FidelityPrerequisiteSource ('3' * 40) `
        -PnpPrerequisitePath $expectedPnpPrerequisite `
        -PnpPrerequisiteSource ('4' * 40)
    try {
        [void](Get-V1NormalStopCandidate -CandidatePath $candidateRoot `
            -ExpectedFidelityPrerequisitePath $expectedFidelityPrerequisite `
            -ExpectedPnpPrerequisitePath $expectedPnpPrerequisite)
        throw 'A Debug hardware candidate was accepted.'
    } catch {
        if ($_.Exception.Message -eq 'A Debug hardware candidate was accepted.') {
            throw
        }
    }

    $otherPrerequisite = Join-Path $fixtureRoot 'other-policy-v10.json'
    Write-TestCandidateManifest -CandidateRoot $candidateRoot `
        -Configuration 'Release' `
        -FidelityPrerequisitePath $otherPrerequisite `
        -FidelityPrerequisiteSource ('3' * 40) `
        -PnpPrerequisitePath $expectedPnpPrerequisite `
        -PnpPrerequisiteSource ('4' * 40)
    try {
        [void](Get-V1NormalStopCandidate -CandidatePath $candidateRoot `
            -ExpectedFidelityPrerequisitePath $expectedFidelityPrerequisite `
            -ExpectedPnpPrerequisitePath $expectedPnpPrerequisite)
        throw 'A candidate with a substituted prerequisite was accepted.'
    } catch {
        if ($_.Exception.Message -eq
            'A candidate with a substituted prerequisite was accepted.') {
            throw
        }
    }
} finally {
    Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
}

$source = Get-Content -LiteralPath $scriptPath -Raw
foreach ($required in @(
        "gate = 'v1-normal-stop-policy-19'",
        'pending_resume = $true', 'candidate_current',
        'fidelity_prerequisite_valid', 'pnp_prerequisite_valid',
        'system_probed = $false', 'system_modified = $false',
        'This command was read-only and did not probe the system.')) {
    if (-not $source.Contains($required)) {
        throw "The read-only resume status contract is missing: $required"
    }
}
foreach ($forbidden in @(
        'pnputil', 'devcon', 'Restart-Computer', 'Disable-PnpDevice',
        'Enable-PnpDevice', 'Stop-Service', 'Start-Service',
        'SetDefaultEndpoint', 'xm5_connection_probe',
        'audio_endpoint_probe')) {
    if ($source.IndexOf(
            $forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "The resume status tool probes or mutates the system: $forbidden"
    }
}
$tokens = $null
$errors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile(
    $scriptPath, [ref]$tokens, [ref]$errors)
if (@($errors).Count -ne 0) {
    throw 'The resume status script does not parse.'
}

Write-Host 'V1 normal-stop resume policy tests passed.'
