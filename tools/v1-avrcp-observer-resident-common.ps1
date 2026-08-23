# SPDX-License-Identifier: Apache-2.0

function Assert-ResidentPublishedInfName {
    param([Parameter(Mandatory = $true)][string]$PublishedInf)

    if ($PublishedInf -notmatch '^oem\d+\.inf$') {
        throw "Resident published INF is invalid: $PublishedInf"
    }
    return $PublishedInf.ToLowerInvariant()
}

function Get-ResidentPublishedInfFromPnpUtilOutput {
    param([Parameter(Mandatory = $true)][object[]]$Lines)

    $matches = [System.Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($line in $Lines) {
        $match = [regex]::Match(
            [string]$line,
            '^\s*(?:Published\s+Name|发布名称)\s*:\s*(oem\d+\.inf)\s*$',
            [Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if ($match.Success) {
            [void]$matches.Add($match.Groups[1].Value)
        }
    }
    if ($matches.Count -ne 1) {
        throw "pnputil did not report exactly one resident published INF (found $($matches.Count))."
    }
    return Assert-ResidentPublishedInfName -PublishedInf ([string]@($matches)[0])
}

function Get-ResidentCandidateInfHash {
    param([Parameter(Mandatory = $true)][string]$CandidateInfPath)

    if (-not (Test-Path -LiteralPath $CandidateInfPath -PathType Leaf)) {
        throw "Resident candidate INF is missing: $CandidateInfPath"
    }
    return (Get-FileHash -LiteralPath $CandidateInfPath -Algorithm SHA256).Hash
}

function Test-ResidentPublishedInfMatchesCandidate {
    param(
        [Parameter(Mandatory = $true)][string]$PublishedInf,
        [Parameter(Mandatory = $true)][string]$CandidateInfPath
    )

    $publishedInf = Assert-ResidentPublishedInfName -PublishedInf $PublishedInf
    $publishedInfPath = Join-Path $env:WINDIR "INF\$publishedInf"
    if (-not (Test-Path -LiteralPath $publishedInfPath -PathType Leaf)) {
        return $false
    }
    return (Get-ResidentCandidateInfHash -CandidateInfPath $CandidateInfPath) -ceq
        (Get-FileHash -LiteralPath $publishedInfPath -Algorithm SHA256).Hash
}

function Get-ResidentStateProperty {
    param(
        [Parameter(Mandatory = $true)]$State,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $property = $State.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function Get-ResidentCurrentPublishedInf {
    param([Parameter(Mandatory = $true)]$State)

    $currentPackage = Get-ResidentStateProperty -State $State -Name 'current_package'
    $publishedInf = $null
    if ($null -ne $currentPackage) {
        if ($currentPackage -is [string]) {
            $publishedInf = $currentPackage
        } else {
            $publishedInf = Get-ResidentStateProperty `
                -State $currentPackage -Name 'published_inf'
        }
    }
    if ([string]::IsNullOrWhiteSpace([string]$publishedInf)) {
        $publishedInf = Get-ResidentStateProperty -State $State -Name 'published_inf'
    }
    return Assert-ResidentPublishedInfName -PublishedInf ([string]$publishedInf)
}

function Get-ResidentHistoricalPublishedInfs {
    param([Parameter(Mandatory = $true)]$State)

    $current = Get-ResidentCurrentPublishedInf -State $State
    $history = Get-ResidentStateProperty `
        -State $State -Name 'previous_observer_packages'
    if ($null -eq $history) { return @() }

    $seen = [System.Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    $result = [System.Collections.Generic.List[string]]::new()
    foreach ($entry in @($history)) {
        $publishedInf = if ($entry -is [string]) {
            $entry
        } else {
            Get-ResidentStateProperty -State $entry -Name 'published_inf'
        }
        if ([string]::IsNullOrWhiteSpace([string]$publishedInf)) {
            throw 'Resident install state has a historical package without published_inf.'
        }
        $canonical = Assert-ResidentPublishedInfName -PublishedInf ([string]$publishedInf)
        if ($canonical -ine $current -and $seen.Add($canonical)) {
            [void]$result.Add($canonical)
        }
    }
    return @($result)
}

function Test-ResidentPnpUtilDeleteExitCode {
    param([Parameter(Mandatory = $true)][long]$ExitCode)

    return $ExitCode -in @(
        0,
        259,
        -536870340,
        3758096956
    )
}

function Get-ResidentRollbackPackagePlan {
    param(
        [Parameter(Mandatory = $true)]$State,
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [object[]]$CandidatePackages,
        [AllowNull()][string]$ActiveObserverInf
    )

    $current = Get-ResidentCurrentPublishedInf -State $State
    $historical = @(Get-ResidentHistoricalPublishedInfs -State $State)
    $known = @($current) + $historical
    $active = $null
    if (-not [string]::IsNullOrWhiteSpace($ActiveObserverInf)) {
        $active = Assert-ResidentPublishedInfName `
            -PublishedInf $ActiveObserverInf
        if ($active -notin $known) {
            throw "The active Native observer package is not recorded in resident install state: $active"
        }
    }

    $candidateInfs = [System.Collections.Generic.List[string]]::new()
    foreach ($candidate in $CandidatePackages) {
        $candidateInf = if ($candidate -is [string]) {
            $candidate
        } else {
            Get-ResidentStateProperty `
                -State $candidate -Name 'published_inf'
        }
        if ([string]::IsNullOrWhiteSpace([string]$candidateInf)) {
            throw 'A Native observer Driver Store entry has no published_inf.'
        }
        [void]$candidateInfs.Add((Assert-ResidentPublishedInfName `
            -PublishedInf ([string]$candidateInf)))
    }

    $unmanaged = @($candidateInfs | Where-Object { $_ -notin $known })
    if ($unmanaged.Count -ne 0) {
        throw "Unmanaged Native observer packages block rollback: $($unmanaged -join ', ')"
    }

    [pscustomobject][ordered]@{
        known_packages = @($known)
        inactive_packages = @($known | Where-Object { $_ -ine $active })
        active_package = $active
        present_packages = @($candidateInfs)
    }
}

function Write-ResidentJsonAtomically {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]$Value
    )

    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $temporaryPath = "$Path.$([Guid]::NewGuid().ToString('N')).tmp"
    try {
        $Value | ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath $temporaryPath -Encoding utf8
        [void](Get-Content -LiteralPath $temporaryPath -Raw | ConvertFrom-Json)
        Move-Item -LiteralPath $temporaryPath -Destination $Path -Force
    } finally {
        if (Test-Path -LiteralPath $temporaryPath -PathType Leaf) {
            Remove-Item -LiteralPath $temporaryPath -Force
        }
    }
}
