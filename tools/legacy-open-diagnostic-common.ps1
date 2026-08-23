# SPDX-License-Identifier: Apache-2.0
Set-StrictMode -Version Latest

function Get-LegacyOpenDiagnosticSummary {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Text,
        [Parameter(Mandatory = $true)][bool]$DiscoveryPassed
    )

    $openDiagnosticLine = if ($Text -match
        '(?m)^(L2CAP OPEN diagnostic #\d+:.+)\r?$') {
        $Matches[1].Trim()
    } else {
        $null
    }
    $remoteResponse = if ($Text -match
        '(?m)^Remote L2CAP response: (\d+) \(([^)]+)\), status (\d+)\.\r?$') {
        [pscustomobject][ordered]@{
            code = [int]$Matches[1]
            name = [string]$Matches[2]
            status = [int]$Matches[3]
        }
    } else {
        $null
    }
    $disposition = if ($DiscoveryPassed) {
        'avdtp_discover_passed'
    } elseif ($null -ne $remoteResponse) {
        switch ([int]$remoteResponse.code) {
            2 { 'remote_psm_not_supported' }
            3 { 'remote_security_block' }
            4 { 'remote_no_resources' }
            default { 'remote_unclassified_response' }
        }
    } elseif ($null -ne $openDiagnosticLine) {
        'no_valid_remote_response'
    } else {
        'open_diagnostic_missing'
    }

    return [pscustomobject][ordered]@{
        open_diagnostic_reported = $null -ne $openDiagnosticLine
        open_diagnostic = $openDiagnosticLine
        remote_response = $remoteResponse
        diagnostic_disposition = $disposition
    }
}
