# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\..\tools\direct-pdo-install-common.ps1')

function Assert-Status {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][bool]$Expected,
        [int]$ExitCode = 0
    )

    $actual = Test-DirectPdoRuntimeStatusText `
        -StatusText $Text `
        -ExitCode $ExitCode
    if ($actual -ne $Expected) {
        throw "$Name expected $Expected but received $actual."
    }
}

$healthyLines = @(
    'PCM ABI 2: 48000 Hz, 2 channel(s), 16-bit, block 4 bytes.'
    'Direct-PDO Media ABI 3: idle (1), flags 0x08750101.'
    'Failure: none (0), last media status 0xC00000BB; packets 0, bytes 0.'
    'Backend activity: idle.'
)
Assert-Status -Name 'LF healthy status' `
    -Text ($healthyLines -join "`n") -Expected $true
Assert-Status -Name 'CRLF healthy status' `
    -Text ($healthyLines -join "`r`n") -Expected $true
Assert-Status -Name 'nonzero probe exit' `
    -Text ($healthyLines -join "`r`n") -Expected $false -ExitCode 1

$active = $healthyLines -replace 'Backend activity: idle\.', `
    'Backend activity: active.'
Assert-Status -Name 'active backend' `
    -Text ($active -join "`r`n") -Expected $false

$faulted = $healthyLines -replace `
    'Direct-PDO Media ABI 3: idle \(1\)', `
    'Direct-PDO Media ABI 3: faulted (5)'
Assert-Status -Name 'faulted runtime' `
    -Text ($faulted -join "`r`n") -Expected $false

$mediaTimeout = $faulted -replace 'Failure: none \(0\)', `
    'Failure: media-timeout (2)'
if (-not (Test-DirectPdoRecoverableMediaTimeoutStatusText `
        -StatusText ($mediaTimeout -join "`r`n") -ExitCode 0)) {
    throw 'Quiescent media-timeout status was not accepted for trial recovery.'
}
$activeMediaTimeout = $mediaTimeout -replace `
    'Backend activity: idle\.', 'Backend activity: active.'
if (Test-DirectPdoRecoverableMediaTimeoutStatusText `
        -StatusText ($activeMediaTimeout -join "`r`n") -ExitCode 0) {
    throw 'Active media-timeout backend was accepted for trial recovery.'
}

$failed = $healthyLines -replace 'Failure: none \(0\)', `
    'Failure: backend (3)'
Assert-Status -Name 'recorded backend failure' `
    -Text ($failed -join "`r`n") -Expected $false
if (Test-DirectPdoRecoverableMediaTimeoutStatusText `
        -StatusText ($failed -join "`r`n") -ExitCode 0) {
    throw 'Backend failure was accepted as a recoverable media timeout.'
}

$validationManifest = [pscustomobject]@{
    installable = $false
}
if ((Get-DirectPdoArtifactKind -Manifest $validationManifest) -ne
    'validation-bundle') {
    throw 'Noninstallable validation bundle was not classified correctly.'
}
$candidateManifest = [pscustomobject]@{
    installable = $true
    staged_only = $true
    service_name = 'NativeLdacDirectPdo'
}
if ((Get-DirectPdoArtifactKind -Manifest $candidateManifest) -ne
    'candidate') {
    throw 'Staged Direct-PDO candidate was not classified correctly.'
}
$unsafeManifest = [pscustomobject]@{
    installable = $true
    staged_only = $false
    service_name = 'NativeLdacDirectPdo'
}
if ((Get-DirectPdoArtifactKind -Manifest $unsafeManifest) -ne 'invalid') {
    throw 'Unstaged installable artifact was accepted.'
}
