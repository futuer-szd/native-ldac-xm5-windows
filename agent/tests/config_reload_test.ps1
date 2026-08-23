[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Agent,
    [Parameter(Mandatory = $true)]
    [string]$Probe,
    [Parameter(Mandatory = $true)]
    [string]$LogRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-TestConfig {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [int]$Revision,
        [Parameter(Mandatory = $true)]
        [string]$Quality,
        [Parameter(Mandatory = $true)]
        [string]$ChannelMode,
        [int]$SampleRate = 48000,
        [int]$BitsPerSample = 16
    )
    $temporary = "$Path.tmp"
    [ordered]@{
        version = 3
        revision = $Revision
        enabled = $true
        quality = $Quality
        channel_mode = $ChannelMode
        sample_rate = $SampleRate
        bits_per_sample = $BitsPerSample
    } | ConvertTo-Json | Set-Content -LiteralPath $temporary -Encoding UTF8
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

function Wait-ForText {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Text,
        [int]$Attempts = 50
    )
    for ($attempt = 0; $attempt -lt $Attempts; $attempt++) {
        if (Test-Path -LiteralPath $Path -PathType Leaf) {
            $contents = Get-Content -LiteralPath $Path -Raw
            if ($contents.Contains($Text)) {
                return $true
            }
        }
        Start-Sleep -Milliseconds 100
    }
    return $false
}

$instanceSuffix = 'ctest-config-reload'
$agentLog = Join-Path $LogRoot 'ldac-agent-config-reload.log'
$probeLog = Join-Path $LogRoot 'ldac-agent-config-reload-probe.log'
$statePath = Join-Path $LogRoot 'ldac-agent-config-reload-state.json'
$configPath = Join-Path $LogRoot 'ldac-agent-config-reload.json'
$paths = @($agentLog, $probeLog, $statePath, $configPath, "$configPath.tmp")
Remove-Item -LiteralPath $paths -ErrorAction SilentlyContinue
Write-TestConfig -Path $configPath -Revision 1 -Quality 'hq' -ChannelMode 'stereo'

$quotedProbe = '"' + $Probe + '"'
$quotedAgentLog = '"' + $agentLog + '"'
$quotedProbeLog = '"' + $probeLog + '"'
$quotedStatePath = '"' + $statePath + '"'
$quotedConfigPath = '"' + $configPath + '"'
$arguments = "--probe $quotedProbe --test-config $quotedConfigPath --run-for-ms 7000 --instance-suffix $instanceSuffix --log $quotedAgentLog --probe-log $quotedProbeLog --state $quotedStatePath"
$agentProcess = Start-Process -FilePath $Agent `
    -ArgumentList $arguments `
    -WindowStyle Hidden `
    -PassThru

try {
    if (-not (Wait-ForText -Path $probeLog -Text 'Agent probe stub quality: hq.')) {
        throw 'The first HQ fake probe generation did not start.'
    }
    if (-not (Wait-ForText -Path $probeLog -Text 'Agent probe stub channel mode: stereo.')) {
        throw 'The first stereo fake probe generation did not start.'
    }

    Write-TestConfig -Path $configPath -Revision 2 -Quality 'sq' -ChannelMode 'mono' -SampleRate 96000 -BitsPerSample 24
    if (-not (Wait-ForText -Path $probeLog -Text 'Agent probe stub quality: sq.')) {
        throw 'The agent did not restart the fake probe with SQ.'
    }
    if (-not (Wait-ForText -Path $probeLog -Text 'Agent probe stub channel mode: mono.')) {
        throw 'The agent did not restart the fake probe in mono mode.'
    }
    if (-not (Wait-ForText -Path $probeLog -Text 'Agent probe stub format: 96000 Hz, 24-bit.')) {
        throw 'The agent did not restart the fake probe with the new endpoint format.'
    }

    if (-not $agentProcess.WaitForExit(10000)) {
        throw 'The config reload agent did not exit at its test deadline.'
    }
    if ($agentProcess.ExitCode -ne 0) {
        throw "The config reload agent returned $($agentProcess.ExitCode)."
    }
    $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    if ($state.state -ne 'stopped' -or $state.probe_pid -ne 0 -or
        $state.generation -lt 2 -or $state.config_revision -ne 2 -or
        $state.quality -ne 'sq') {
        throw "Unexpected final config reload state: $($state | ConvertTo-Json -Compress)"
    }
} finally {
    if (-not $agentProcess.HasExited) {
        $stopper = Start-Process -FilePath $Agent `
            -ArgumentList "--stop --instance-suffix $instanceSuffix" `
            -WindowStyle Hidden `
            -Wait `
            -PassThru
        if ($stopper.ExitCode -eq 0) {
            $null = $agentProcess.WaitForExit(5000)
        }
    }
    if (-not $agentProcess.HasExited) {
        Stop-Process -Id $agentProcess.Id -Force -ErrorAction SilentlyContinue
    }
}
