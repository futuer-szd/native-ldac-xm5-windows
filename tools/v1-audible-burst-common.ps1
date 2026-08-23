# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'v1-pcm-burst-common.ps1')

function Get-V1AudibleBurstCandidate {
    param([Parameter(Mandatory = $true)][string]$CandidatePath)
    $root = [System.IO.Path]::GetFullPath($CandidatePath)
    $manifestPath = Join-Path $root 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "V1 audible-burst manifest is missing: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw |
        ConvertFrom-Json
    $required = @(
        'verified_policy_v5_transport_prerequisite',
        'installed_LdacNative_driver_tree_prerequisite',
        'exact_XM5_ACL_generation',
        'render_demand_authorized',
        'wait_for_active_WaveRT_before_consumer_lease',
        'audible_PCM_before_Bluetooth_OPEN',
        'maximum_fixed_gain_0_25',
        'bounded_120000_ms_pretransport_PCM_wait',
        'bounded_5000_ms_PCM_clock_pacing',
        'AVDTP_START_then_SUSPEND_CLOSE',
        'retry_only_OpenSignaling_Win32_71',
        'maximum_four_zero_exchange_open_attempts',
        'consumer_lease_release_required',
        'no_LinkState_write',
        'no_driver_install',
        'no_reboot')
    $capabilities = @($manifest.capabilities |
        ForEach-Object { [string]$_ })
    $expectedFiles = @(
        'v1_presence_agent.exe',
        'v1_transport_audible_worker.exe',
        'audio_endpoint_probe.exe',
        'xm5_connection_probe.exe',
        'xm5_connection_probe.manifest.json')
    $manifestFiles = @($manifest.files)
    $manifestPaths = @($manifestFiles |
        ForEach-Object { [string]$_.path })
    if ([int]$manifest.manifest_version -ne 1 -or
        [int]$manifest.transport_policy_version -ne 6 -or
        $manifest.source_dirty -ne $false -or
        [string]$manifest.source_commit -notmatch '^[0-9a-fA-F]{40}$' -or
        [string]$manifest.driver_tree -notmatch '^[0-9a-fA-F]{40}$' -or
        @($required | Where-Object { $_ -notin $capabilities }).Count -ne 0 -or
        $manifestFiles.Count -ne $expectedFiles.Count -or
        @($expectedFiles | Where-Object {
                $_ -notin $manifestPaths }).Count -ne 0 -or
        @($manifestPaths | Select-Object -Unique).Count -ne
            $expectedFiles.Count) {
        throw 'The V1 audible-burst candidate contract is invalid.'
    }
    foreach ($file in $manifestFiles) {
        $path = Join-Path $root ([string]$file.path)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            [long]$file.length -le 0 -or
            (Get-Item -LiteralPath $path).Length -ne [long]$file.length -or
            -not (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.Equals(
                [string]$file.sha256,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "V1 audible-burst file failed its hash check: $($file.path)"
        }
    }
    [pscustomobject]@{ root = $root; manifest = $manifest }
}
