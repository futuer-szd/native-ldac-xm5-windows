# SPDX-License-Identifier: Apache-2.0

$script:V1AvrcpFilterTargetPrefix =
    'BTHENUM\{0000110E-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'
$script:V1AvrcpFilterOriginalInf = 'NativeLdacAvrcpIoFilter.inf'
$script:V1AvrcpFilterService = 'NativeLdacAvrcpIoFilter'
$script:V1AvrcpFilterBaselineInf = 'microsoft_bluetooth_avrcptransport.inf'
$script:V1AvrcpFilterBaselineService = 'Microsoft_Bluetooth_AvrcpTransport'

function Assert-V1AvrcpFilterAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'The V1 AVRCP filter gate requires an elevated PowerShell 7 terminal.'
    }
}

function Get-V1AvrcpFilterPropertyData {
    param(
        [Parameter(Mandatory = $true)][string]$InstanceId,
        [Parameter(Mandatory = $true)][string]$KeyName
    )
    $property = Get-PnpDeviceProperty -InstanceId $InstanceId `
        -KeyName $KeyName -ErrorAction SilentlyContinue
    if ($null -eq $property) { return $null }
    return $property.Data
}

function Get-V1AvrcpFilterTargetDevice {
    $matches = @(Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object {
        $_.InstanceId.StartsWith(
            $script:V1AvrcpFilterTargetPrefix,
            [StringComparison]::OrdinalIgnoreCase)
    })
    if ($matches.Count -ne 1) {
        throw 'Exactly one paired XM5 AVRCP 0x110E PDO is required.'
    }
    return $matches[0]
}

function Get-V1AvrcpFilterSnapshot {
    param([Parameter(Mandatory = $true)]$Device)
    return [pscustomobject][ordered]@{
        instance_id = [string]$Device.InstanceId
        present = [bool]$Device.Present
        status = [string]$Device.Status
        problem = [string]$Device.Problem
        problem_code = [int](Get-V1AvrcpFilterPropertyData `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_ProblemCode')
        inf = [string](Get-V1AvrcpFilterPropertyData `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_DriverInfPath')
        service = [string](Get-V1AvrcpFilterPropertyData `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_Service')
        parent = [string](Get-V1AvrcpFilterPropertyData `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_Parent')
        container_id = [string](Get-V1AvrcpFilterPropertyData `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_ContainerId')
    }
}

function Test-V1AvrcpFilterMicrosoftBaseline {
    param([Parameter(Mandatory = $true)]$Snapshot)
    return [bool]$Snapshot.present -and
        [int]$Snapshot.problem_code -eq 0 -and
        [string]$Snapshot.inf -ieq $script:V1AvrcpFilterBaselineInf -and
        [string]$Snapshot.service -ieq $script:V1AvrcpFilterBaselineService
}

function Get-V1AvrcpFilterPackages {
    return @(Get-WindowsDriver -Online -All | Where-Object {
        (Split-Path -Leaf ([string]$_.OriginalFileName)) -ieq
            $script:V1AvrcpFilterOriginalInf
    } | ForEach-Object {
        [pscustomobject][ordered]@{
            published_inf = [string]$_.Driver
            original_inf = Split-Path -Leaf ([string]$_.OriginalFileName)
            version = [string]$_.Version
            provider = [string]$_.ProviderName
        }
    })
}

function Assert-V1AvrcpFilterPublishedInf {
    param([Parameter(Mandatory = $true)][string]$PublishedInf)
    if ($PublishedInf -notmatch '^oem\d+\.inf$') {
        throw "The filter published INF is invalid: $PublishedInf"
    }
    return $PublishedInf.ToLowerInvariant()
}

function Get-V1AvrcpFilterPublishedInfFromOutput {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [object[]]$Lines
    )
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
        throw "pnputil did not report exactly one filter published INF (found $($matches.Count))."
    }
    return Assert-V1AvrcpFilterPublishedInf -PublishedInf ([string]@($matches)[0])
}

function Test-V1AvrcpFilterPublishedInfMatchesCandidate {
    param(
        [Parameter(Mandatory = $true)][string]$PublishedInf,
        [Parameter(Mandatory = $true)][string]$CandidateInfPath
    )
    $publishedInf = Assert-V1AvrcpFilterPublishedInf `
        -PublishedInf $PublishedInf
    if (-not (Test-Path -LiteralPath $CandidateInfPath -PathType Leaf)) {
        return $false
    }
    $publishedInfPath = Join-Path $env:WINDIR "INF\$publishedInf"
    if (-not (Test-Path -LiteralPath $publishedInfPath -PathType Leaf)) {
        return $false
    }
    return (Get-FileHash -LiteralPath $CandidateInfPath `
            -Algorithm SHA256).Hash -ceq
        (Get-FileHash -LiteralPath $publishedInfPath `
            -Algorithm SHA256).Hash
}

function Invoke-V1AvrcpFilterPnpUtil {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    $lines = @(& pnputil.exe @Arguments 2>&1)
    return [pscustomobject][ordered]@{
        exit_code = [int]$LASTEXITCODE
        lines = @($lines)
    }
}

function Test-V1AvrcpFilterDeleteExitCode {
    param([Parameter(Mandatory = $true)][long]$ExitCode)
    return $ExitCode -in @(0, 259, -536870340, 3758096956)
}

function Write-V1AvrcpFilterJsonAtomically {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]$Value
    )
    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $temporary = "$Path.$([Guid]::NewGuid().ToString('N')).tmp"
    try {
        $Value | ConvertTo-Json -Depth 10 |
            Set-Content -LiteralPath $temporary -Encoding utf8
        [void](Get-Content -LiteralPath $temporary -Raw | ConvertFrom-Json)
        Move-Item -LiteralPath $temporary -Destination $Path -Force
    } finally {
        if (Test-Path -LiteralPath $temporary -PathType Leaf) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
}

function Wait-V1AvrcpFilterMicrosoftBaseline {
    param(
        [Parameter(Mandatory = $true)][string]$InstanceId,
        [ValidateRange(1, 120)][int]$TimeoutSeconds = 30
    )
    $deadline = [DateTimeOffset]::Now.AddSeconds($TimeoutSeconds)
    do {
        $device = Get-PnpDevice -InstanceId $InstanceId `
            -ErrorAction SilentlyContinue
        if ($null -ne $device) {
            $snapshot = Get-V1AvrcpFilterSnapshot -Device $device
            if (Test-V1AvrcpFilterMicrosoftBaseline -Snapshot $snapshot) {
                return $snapshot
            }
        }
        Start-Sleep -Milliseconds 500
    } while ([DateTimeOffset]::Now -lt $deadline)
    return $null
}

function Test-V1AvrcpFilterControlAbsent {
    param([Parameter(Mandatory = $true)][string]$ProbePath)
    $probeOutput = @(& $ProbePath --duration-seconds 1 2>&1)
    $exitCode = [int]$LASTEXITCODE
    $probeLines = [System.Collections.Generic.List[string]]::new()
    foreach ($line in $probeOutput) {
        [void]$probeLines.Add([string]$line)
    }
    return [pscustomobject][ordered]@{
        absent = $exitCode -eq 3
        healthy = $exitCode -eq 0
        exit_code = $exitCode
        lines = @($probeLines)
    }
}

function Invoke-V1AvrcpFilterRollback {
    param(
        [Parameter(Mandatory = $true)][string]$PublishedInf,
        [Parameter(Mandatory = $true)][string]$InstanceId,
        [Parameter(Mandatory = $true)][string]$ProbePath,
        [Parameter(Mandatory = $true)][string]$LogDirectory,
        [bool]$AllowExactPdoRestart = $false,
        [int]$ExistingExactPdoRestartCount = 0
    )

    $steps = [System.Collections.Generic.List[object]]::new()
    $passed = $true
    $publishedInf = Assert-V1AvrcpFilterPublishedInf `
        -PublishedInf $PublishedInf
    $packages = @(Get-V1AvrcpFilterPackages)
    $unmanaged = @($packages | Where-Object {
        $_.published_inf -ine $publishedInf
    })
    if ($unmanaged.Count -ne 0) {
        throw "Unmanaged Native AVRCP filter packages block rollback: $($unmanaged.published_inf -join ', ')"
    }
    $present = @($packages | Where-Object {
        $_.published_inf -ieq $publishedInf
    })
    if ($present.Count -eq 1) {
        $delete = Invoke-V1AvrcpFilterPnpUtil -Arguments @(
            '/delete-driver', $publishedInf, '/uninstall')
        $delete.lines | Set-Content -LiteralPath `
            (Join-Path $LogDirectory "delete-$publishedInf.log") -Encoding utf8
        $steps.Add([pscustomobject][ordered]@{
            action = 'delete-filter-package'
            published_inf = $publishedInf
            exit_code = $delete.exit_code
            lines = @($delete.lines)
        })
        if (-not (Test-V1AvrcpFilterDeleteExitCode `
                -ExitCode $delete.exit_code)) {
            $passed = $false
        }
    } elseif ($present.Count -gt 1) {
        throw "Multiple Driver Store entries match the exact filter package: $publishedInf"
    } else {
        $steps.Add([pscustomobject][ordered]@{
            action = 'skip-absent-filter-package'
            published_inf = $publishedInf
        })
    }

    $remaining = @(Get-V1AvrcpFilterPackages)
    if ($remaining.Count -ne 0) { $passed = $false }
    $scan = Invoke-V1AvrcpFilterPnpUtil -Arguments @('/scan-devices')
    $scan.lines | Set-Content -LiteralPath `
        (Join-Path $LogDirectory 'scan.log') -Encoding utf8
    $steps.Add([pscustomobject][ordered]@{
        action = 'scan-devices'
        exit_code = $scan.exit_code
        lines = @($scan.lines)
    })
    if ($scan.exit_code -ne 0) { $passed = $false }
    Start-Sleep -Seconds 2

    $snapshot = Wait-V1AvrcpFilterMicrosoftBaseline `
        -InstanceId $InstanceId -TimeoutSeconds 30
    if ($null -eq $snapshot) { $passed = $false }
    $control = Test-V1AvrcpFilterControlAbsent -ProbePath $ProbePath
    $control.lines | Set-Content -LiteralPath `
        (Join-Path $LogDirectory 'filter-control-after-delete.log') -Encoding utf8
    $steps.Add([pscustomobject][ordered]@{
        action = 'verify-filter-control-absent'
        exit_code = $control.exit_code
        absent = [bool]$control.absent
    })
    if (-not [bool]$control.absent) {
        if (-not $AllowExactPdoRestart -or
            $ExistingExactPdoRestartCount -ne 0) {
            $passed = $false
        } else {
            $restart = Invoke-V1AvrcpFilterPnpUtil -Arguments @(
                '/restart-device', $InstanceId)
            $restart.lines | Set-Content -LiteralPath `
                (Join-Path $LogDirectory 'rollback-exact-pdo-restart.log') -Encoding utf8
            $steps.Add([pscustomobject][ordered]@{
                action = 'restart-exact-avrcp-pdo-for-rollback'
                exit_code = $restart.exit_code
                lines = @($restart.lines)
            })
            if ($restart.exit_code -ne 0) {
                $passed = $false
            } else {
                Start-Sleep -Seconds 2
                $snapshot = Wait-V1AvrcpFilterMicrosoftBaseline `
                    -InstanceId $InstanceId -TimeoutSeconds 30
                $control = Test-V1AvrcpFilterControlAbsent `
                    -ProbePath $ProbePath
                if ($null -eq $snapshot -or -not [bool]$control.absent) {
                    $passed = $false
                }
            }
        }
    }
    [pscustomobject][ordered]@{
        passed = $passed
        steps = @($steps)
        remaining_packages = @(Get-V1AvrcpFilterPackages)
        final_target = $snapshot
        filter_control = $control
    }
}
