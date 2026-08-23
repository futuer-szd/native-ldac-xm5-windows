# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..'))
$installerPath = Join-Path $projectRoot 'tools\install-agent-autostart.ps1'
$builderPath = Join-Path $projectRoot 'tools\build-agent.ps1'
$agentPath = Join-Path $projectRoot 'agent\ldac_agent.cpp'

$installer = Get-Content -LiteralPath $installerPath -Raw
$builder = Get-Content -LiteralPath $builderPath -Raw
$agent = Get-Content -LiteralPath $agentPath -Raw

if (-not $installer.Contains('--installed-legacy')) {
    throw 'The login task does not explicitly select legacy mode.'
}
if ($installer.Contains('$stagedDirectEngine') -or
    $installer.Contains('$installedDirectEngine') -or
    $installer.Contains('direct_engine_sha256')) {
    throw 'The legacy login installer still deploys a Direct-PDO engine.'
}
if (-not $installer.Contains('transport_probe.sha256')) {
    throw 'The legacy login installer does not protect the staged probe hash.'
}
if (-not $builder.Contains('[switch]$IncludeDirectPdoEngine') -or
    -not $builder.Contains('legacy-user-mode-avdtp')) {
    throw 'The staged agent bundle does not default to the legacy architecture.'
}
if (-not $agent.Contains('} else if (argument == L"--installed-legacy")') -or
    -not $agent.Contains('return options.direct_pdo_trial;')) {
    throw 'The agent runtime does not separate legacy install from Direct-PDO.'
}

Write-Host 'Legacy login installation policy tests passed.'
