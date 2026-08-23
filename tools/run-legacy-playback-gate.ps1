# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [switch]$ConfirmLegacyPlaybackGate,
    [ValidateRange(30, 90)]
    [int]$DurationSeconds = 60,
    [ValidateSet('mq', 'sq', 'hq')]
    [string]$Quality = 'hq',
    [string]$CandidatePath,
    [string]$BackupPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

throw @'
The same-boot legacy playback gate is retired and cannot modify the system.
Real hardware evidence showed that ABI readiness after a hot driver replacement
does not prove that BthPort has rebuilt the AVDTP profile lifecycle. The last
verified playback baseline crossed a full Windows reboot after installation.

Use the reviewed sequence instead:
  1. cleanup-native-ldac-test-state.ps1
  2. reboot and verify the clean original-A2DP baseline
  3. prepare-legacy-reboot-gate.ps1
  4. reboot with the XM5 powered off
  5. run-legacy-post-reboot-transport-gate.ps1

There is intentionally no command-line override for same-boot playback.
'@
