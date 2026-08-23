# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param(
    [string]$CandidatePath,
    [switch]$AsJson
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'v1-normal-stop-common.ps1')

$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
    $CandidatePath = Join-Path $root 'artifacts\v1-normal-stop\candidate'
}
$CandidatePath = [System.IO.Path]::GetFullPath($CandidatePath)
$head = (& git.exe -C $root rev-parse HEAD).Trim()
$driverTree = (& git.exe -C $root rev-parse HEAD:driver).Trim()
$gitClean = $LASTEXITCODE -eq 0 -and
    @(& git.exe -C $root status --porcelain).Count -eq 0

$fidelityPrerequisiteValid = $false
$fidelityPrerequisiteSource = $null
$fidelityPrerequisitePath = Join-Path $root `
    $script:V1NormalStopFidelityPrerequisiteRelativePath
if (Test-Path -LiteralPath $fidelityPrerequisitePath -PathType Leaf) {
    try {
        $transaction = Get-Content `
            -LiteralPath $fidelityPrerequisitePath -Raw |
            ConvertFrom-Json
        $resultPath = [string]$transaction.result
        if (Test-Path -LiteralPath $resultPath -PathType Leaf) {
            $result = Get-Content -LiteralPath $resultPath -Raw |
                ConvertFrom-Json
            $fidelityPrerequisiteValid =
                Test-V1NormalStopFidelityPrerequisite `
                    -Transaction $transaction -Result $result
            if ($fidelityPrerequisiteValid) {
                $fidelityPrerequisiteSource =
                    [string]$transaction.source_commit
            }
        }
    } catch {
        $fidelityPrerequisiteValid = $false
    }
}
$pnpPrerequisiteValid = $false
$pnpPrerequisiteSource = $null
$pnpPrerequisitePath = Join-Path $root `
    $script:V1NormalStopPnpPrerequisiteRelativePath
if (Test-Path -LiteralPath $pnpPrerequisitePath -PathType Leaf) {
    try {
        $transaction = Get-Content -LiteralPath $pnpPrerequisitePath -Raw |
            ConvertFrom-Json
        $resultPath = [string]$transaction.result
        if (Test-Path -LiteralPath $resultPath -PathType Leaf) {
            $result = Get-Content -LiteralPath $resultPath -Raw |
                ConvertFrom-Json
            $pnpPrerequisiteValid = Test-V1NormalStopPnpPrerequisite `
                -Transaction $transaction `
                -TransactionPath $pnpPrerequisitePath `
                -Result $result -ResultPath $resultPath `
                -ExpectedDriverTree $driverTree
            if ($pnpPrerequisiteValid) {
                $pnpPrerequisiteSource = [string]$transaction.source_commit
            }
        }
    } catch {
        $pnpPrerequisiteValid = $false
    }
}
$prerequisiteValid = $fidelityPrerequisiteValid -and
    $pnpPrerequisiteValid

$candidatePresent = Test-Path -LiteralPath $CandidatePath -PathType Container
$candidateValid = $false
$candidateSource = $null
$candidateDriverTree = $null
if ($candidatePresent) {
    try {
        $candidate = Get-V1NormalStopCandidate `
            -CandidatePath $CandidatePath `
            -ExpectedFidelityPrerequisitePath $fidelityPrerequisitePath `
            -ExpectedPnpPrerequisitePath $pnpPrerequisitePath
        $candidateValid = $true
        $candidateSource = [string]$candidate.manifest.source_commit
        $candidateDriverTree = [string]$candidate.manifest.driver_tree
    } catch {
        $candidateValid = $false
    }
}
$candidateCurrent = $candidateValid -and
    $candidateSource -eq $head -and
    $candidateDriverTree -eq $driverTree -and
    [string]$candidate.manifest.fidelity_prerequisite_source_commit -eq
        $fidelityPrerequisiteSource -and
    [string]$candidate.manifest.pnp_prerequisite_source_commit -eq
        $pnpPrerequisiteSource
$decision = Get-V1NormalStopResumeDecision `
    -GitClean $gitClean -PrerequisiteValid $prerequisiteValid `
    -CandidatePresent $candidatePresent -CandidateValid $candidateValid `
    -CandidateCurrent $candidateCurrent
$status = [ordered]@{
    schema_version = 1
    gate = 'v1-normal-stop-policy-19'
    pending_resume = $true
    state = [string]$decision.state
    action = [string]$decision.action
    ready = [bool]$decision.ready
    git_clean = $gitClean
    head = $head
    driver_tree = $driverTree
    prerequisite_valid = $prerequisiteValid
    fidelity_prerequisite_valid = $fidelityPrerequisiteValid
    pnp_prerequisite_valid = $pnpPrerequisiteValid
    candidate_path = $CandidatePath
    candidate_present = $candidatePresent
    candidate_valid = $candidateValid
    candidate_source_commit = $candidateSource
    candidate_driver_tree = $candidateDriverTree
    candidate_current = $candidateCurrent
    system_probed = $false
    system_modified = $false
}
if ($AsJson) {
    $status | ConvertTo-Json -Depth 4
    return
}
Write-Host "V1 normal-stop resume state: $($status.state)"
Write-Host "Required action: $($status.action)"
Write-Host "Git HEAD: $head; clean: $gitClean"
Write-Host "Prerequisite valid: $prerequisiteValid"
Write-Host "Candidate present/valid/current: $candidatePresent/$candidateValid/$candidateCurrent"
Write-Host 'This command was read-only and did not probe the system.'
