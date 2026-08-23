# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmLegacyInstallRollbackGate,
    [string]$CandidatePath,
    [string]$BackupPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

throw @'
The same-boot legacy install/info/rollback gate is retired and cannot modify
the system. It proved only that ABI 0.4 could be queried after a hot driver
replacement; it did not establish a usable post-replacement AVDTP lifecycle.

Use cleanup-native-ldac-test-state.ps1 followed by the explicit two-boot
prepare-legacy-reboot-gate.ps1 / run-legacy-post-reboot-transport-gate.ps1
sequence. There is intentionally no command-line override.
'@
