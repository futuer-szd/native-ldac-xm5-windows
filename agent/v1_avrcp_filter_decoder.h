// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

#include "ldac_native/avrcp.h"

namespace native_ldac::agent {

// The XM5 AVRCP upper filter records the raw prefix of every forwarded
// device-control IRP. Real captures show two payload layouts:
//
//  - Direct: transport read completions (ioctl 0x00414010) begin with the
//    AVCTP packet at byte 0.
//  - MicrosoftPrivateHeader: transport write requests (ioctl 0x00414014)
//    begin with an 8-byte private header
//    (01 00 00 00 <payload length LE> 00 00 00) followed by the AVCTP
//    packet at byte 8.
enum class V1AvrcpFilterPayloadLayout : std::uint8_t {
    None = 0u,
    Direct = 1u,
    MicrosoftPrivateHeader = 2u,
};

V1AvrcpFilterPayloadLayout V1AvrcpFilterDetectLayout(
    const std::uint8_t* data,
    std::size_t size);

// Decodes one raw filter prefix into the shared AVRCP observer event
// vocabulary (volume capability, absolute volume, PASS THROUGH, vendor
// command, protocol error). Non-AVRCP or malformed payloads return an error
// status and a PROTOCOL_ERROR event carrying the raw prefix.
avrcp_status V1AvrcpFilterDecodePacket(
    const std::uint8_t* data,
    std::size_t size,
    V1AvrcpFilterPayloadLayout layout,
    avrcp_observer_event* out);

}  // namespace native_ldac::agent
