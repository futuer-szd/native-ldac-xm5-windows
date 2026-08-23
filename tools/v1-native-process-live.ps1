# SPDX-License-Identifier: Apache-2.0

function Invoke-V1NativeProcessLive {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [string[]]$ArgumentList,
        [scriptblock]$LineSink = {
            param([string]$Line)
            Write-Host $Line
        }
    )

    $lines = [System.Collections.Generic.List[string]]::new()
    & $FilePath @ArgumentList 2>&1 | ForEach-Object {
        $line = [string]$_
        [void]$lines.Add($line)
        $null = & $LineSink $line
    }
    $exitCode = $LASTEXITCODE
    [pscustomobject][ordered]@{
        exit_code = [int]$exitCode
        lines = @($lines | ForEach-Object { [string]$_ })
    }
}
