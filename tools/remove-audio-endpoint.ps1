# SPDX-License-Identifier: Apache-2.0
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param([switch]$ConfirmEndpointRemoval)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

throw @'
This legacy endpoint remover is retired and cannot modify the system. It did
not verify the original A2DP binding, physical XM5 disconnection, transport
package absence, or the final clean baseline.

Use cleanup-native-ldac-test-state.ps1 -ConfirmNativeLdacCleanup. The new
maintenance transaction removes only ROOT\NativeLdacAudio and
NativeLdacAudio.inf packages while preserving Bluetooth pairing,
the original AltA2DP package, the rollback backup, and the shared certificate.
'@
