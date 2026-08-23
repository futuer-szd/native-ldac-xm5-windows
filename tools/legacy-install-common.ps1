# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest

$script:LegacyXm5A2dpPrefix =
    'BTHENUM\{0000110B-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0DF0'

function Assert-LegacyAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this script from an elevated Windows PowerShell.'
    }
}

function Write-LegacyJsonAtomic {
    param(
        [Parameter(Mandatory = $true)]$Value,
        [Parameter(Mandatory = $true)][string]$Path
    )

    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) `
        -Force | Out-Null
    $temporaryPath = "$Path.tmp.$PID"
    try {
        $Value | ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath $temporaryPath -Encoding UTF8
        Move-Item -LiteralPath $temporaryPath -Destination $Path -Force
    } finally {
        if (Test-Path -LiteralPath $temporaryPath -PathType Leaf) {
            Remove-Item -LiteralPath $temporaryPath -Force
        }
    }
}

function Get-LegacyXm5A2dpDevices {
    return @(Get-PnpDevice -PresentOnly | Where-Object {
        ([string]$_.InstanceId).StartsWith(
            $script:LegacyXm5A2dpPrefix,
            [StringComparison]::OrdinalIgnoreCase)
    })
}

function Get-LegacyXm5A2dpSnapshot {
    param([Parameter(Mandatory = $true)]$Device)

    $problem = Get-PnpDeviceProperty -InstanceId $Device.InstanceId `
        -KeyName 'DEVPKEY_Device_ProblemCode' -ErrorAction SilentlyContinue
    return [pscustomobject][ordered]@{
        instance_id = [string]$Device.InstanceId
        friendly_name = [string]$Device.FriendlyName
        service = [string](Get-PnpDeviceProperty `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_Service').Data
        published_inf = [string](Get-PnpDeviceProperty `
            -InstanceId $Device.InstanceId `
            -KeyName 'DEVPKEY_Device_DriverInfPath').Data
        problem_code = if ($null -eq $problem) { 0 } else { [int]$problem.Data }
    }
}

function Wait-LegacyXm5A2dpService {
    param(
        [Parameter(Mandatory = $true)][string]$ExpectedService,
        [int]$TimeoutSeconds = 30
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $devices = @(Get-LegacyXm5A2dpDevices)
        if ($devices.Count -eq 1) {
            $snapshot = Get-LegacyXm5A2dpSnapshot -Device $devices[0]
            if ($snapshot.service -eq $ExpectedService) {
                return $snapshot
            }
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    return $null
}

function Invoke-LegacyPnpUtil {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$LogPath,
        [int[]]$AcceptedExitCodes = @(0, 3010)
    )

    $output = @()
    $exitCode = -1
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = @(& pnputil.exe @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $LogPath) `
        -Force | Out-Null
    $output | Set-Content -LiteralPath $LogPath -Encoding UTF8
    $output | ForEach-Object { Write-Host $_ }
    if ($exitCode -notin $AcceptedExitCodes) {
        throw "pnputil failed with exit code ${exitCode}: $($Arguments -join ' ')"
    }
    return [pscustomobject]@{
        exit_code = $exitCode
        reboot_required = $exitCode -eq 3010
    }
}

function Get-LegacyDriverPackages {
    param([Parameter(Mandatory = $true)][string[]]$OriginalInfNames)

    $drivers = @()
    try {
        $drivers = @(Get-WindowsDriver -Online -All -ErrorAction Stop)
    } catch [System.Runtime.InteropServices.COMException] {
        $output = @(& pnputil.exe /enum-drivers 2>&1)
        if ($LASTEXITCODE -ne 0) {
            throw
        }
        $blocks = @()
        $current = @()
        foreach ($line in $output) {
            if ([string]::IsNullOrWhiteSpace([string]$line)) {
                if ($current.Count -ne 0) {
                    $blocks += ,@($current)
                    $current = @()
                }
            } else {
                $current += [string]$line
            }
        }
        if ($current.Count -ne 0) {
            $blocks += ,@($current)
        }
        $fallbackDrivers = foreach ($block in $blocks) {
            $text = $block -join "`n"
            $published = [regex]::Match(
                $text,
                '(?im)^(?:Published Name|发布名称)\s*:\s*(\S+)\s*$')
            $original = [regex]::Match(
                $text,
                '(?im)^(?:Original Name|原始名称)\s*:\s*(\S+)\s*$')
            if (-not $published.Success -or -not $original.Success) {
                continue
            }
            $provider = [regex]::Match(
                $text,
                '(?im)^(?:Provider Name|提供程序名称)\s*:\s*(.+?)\s*$')
            $version = [regex]::Match(
                $text,
                '(?im)^(?:Driver Version|驱动程序版本)\s*:\s*(.+?)\s*$')
            [pscustomobject]@{
                Driver = $published.Groups[1].Value
                OriginalFileName = $original.Groups[1].Value
                ProviderName = if ($provider.Success) {
                    $provider.Groups[1].Value
                } else {
                    ''
                }
                Version = if ($version.Success) {
                    $version.Groups[1].Value
                } else {
                    ''
                }
            }
        }
        $drivers = @($fallbackDrivers)
    }
    return @($drivers | Where-Object {
        $leaf = Split-Path -Leaf ([string]$_.OriginalFileName)
        $leaf -in $OriginalInfNames
    })
}

function Remove-LegacyTestDriverPackages {
    param([Parameter(Mandatory = $true)][string]$LogDirectory)

    $removed = @()
    $packages = @(Get-LegacyDriverPackages `
        -OriginalInfNames @('LdacNative.inf'))
    foreach ($package in $packages) {
        $publishedInf = [string]$package.Driver
        if ([string]::IsNullOrWhiteSpace($publishedInf)) {
            continue
        }
        $null = Invoke-LegacyPnpUtil `
            -Arguments @('/delete-driver', $publishedInf, '/uninstall', '/force') `
            -LogPath (Join-Path $LogDirectory "remove-$publishedInf.log")
        $removed += $publishedInf
    }
    return @($removed)
}

function Restore-LegacyOriginalA2dp {
    param(
        [Parameter(Mandatory = $true)][string]$BackupPath,
        [Parameter(Mandatory = $true)][string]$LogDirectory
    )

    $backupRoot = [System.IO.Path]::GetFullPath($BackupPath)
    $statePath = Join-Path $backupRoot 'state.json'
    if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) {
        throw "Original A2DP backup state is missing: $statePath"
    }
    $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    if ([string]$state.service -in @('LdacNative', 'NativeLdacDirectPdo')) {
        throw "Rollback backup is not an original A2DP package: $backupRoot"
    }
    $infs = @(Get-ChildItem -LiteralPath $backupRoot -Recurse `
        -Filter '*.inf' -File)
    if ($infs.Count -eq 0) {
        throw "Rollback backup contains no INF: $backupRoot"
    }
    foreach ($inf in $infs) {
        $result = Invoke-LegacyPnpUtil `
            -Arguments @('/add-driver', $inf.FullName, '/install') `
            -LogPath (Join-Path $LogDirectory "restore-$($inf.Name).log") `
            -AcceptedExitCodes @(0, 259, 3010)
        if ($result.exit_code -eq 259) {
            $packages = @(Get-LegacyDriverPackages `
                -OriginalInfNames @($inf.Name))
            if ($packages.Count -eq 0) {
                throw "PnPUtil returned 259, but $($inf.Name) is absent from the Driver Store."
            }
        }
    }
    $null = Invoke-LegacyPnpUtil -Arguments @('/scan-devices') `
        -LogPath (Join-Path $LogDirectory 'scan-devices.log')
    return $state
}

function Invoke-LegacyNativeCapture {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.Arguments = $Arguments -join ' '
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Could not start: $FilePath"
    }
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    return [pscustomobject]@{
        exit_code = $process.ExitCode
        stdout = $stdout
        stderr = $stderr
    }
}
