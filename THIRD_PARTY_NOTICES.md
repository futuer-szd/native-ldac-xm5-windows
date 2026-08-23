# Third-party notices

## Microsoft Windows Driver Samples — BthEcho

`driver/sys/device.c`, `driver/sys/device.h`, `driver/sys/connection.c`, and `driver/sys/connection.h` are derived in part from the Bluetooth BthEcho sample in Microsoft's Windows Driver Samples repository.

- Copyright (c) Microsoft Corporation.
- License: Microsoft Public License (MS-PL)
- Source: https://github.com/microsoft/Windows-driver-samples/tree/main/bluetooth/bthecho
- Full license text: [driver/MS-PL.txt](driver/MS-PL.txt)

The remainder of this repository is licensed as described in the root [LICENSE](LICENSE) and per-file SPDX identifiers.

## Microsoft Windows Driver Samples — Simple Audio Sample

`audio-endpoint/` is derived from Microsoft's Simple Audio Sample and has been
reduced to a single WaveRT render endpoint for the Native LDAC PCM source.

- Copyright (c) Microsoft Corporation.
- License: Microsoft Public License (MS-PL)
- Source: https://github.com/microsoft/Windows-driver-samples/tree/main/audio/simpleaudiosample
- Upstream revision: `2ee527bfeb0aeb6be11f0a8b6dce4011b358ce89`
- Full license text: [audio-endpoint/MS-PL.txt](audio-endpoint/MS-PL.txt)

## Sony/AOSP libldac and EHfive ldacBT packaging

`vendor/ldacBT` contains the EHfive CMake packaging and its `libldac` submodule. The encoder implementation is Sony/AOSP libldac and is compiled into the user-mode engine.

- EHfive ldacBT revision: `6579bd585a618f2e1612b3c1650d2b7fcfb1d43f`
- AOSP libldac revision: `82b6a1abee84787b8fa167efe20290073f60db2d`
- License: Apache License 2.0
- Sources: https://github.com/EHfive/ldacBT and https://android.googlesource.com/platform/external/libldac
- Full license text: [vendor/ldacBT/libldac/LICENSE](vendor/ldacBT/libldac/LICENSE)
