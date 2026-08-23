// SPDX-License-Identifier: Apache-2.0
#include "v1_avrcp_filter_decoder.h"

#include <cstring>

namespace native_ldac::agent {
namespace {

constexpr std::size_t kMicrosoftHeaderSize = 8u;
constexpr std::size_t kEventRawPrefixCapacity = 64u;

void CopyRawPrefix(avrcp_observer_event* out,
                   const std::uint8_t* data,
                   std::size_t size) {
    if (out == nullptr) return;
    out->raw_prefix_size = size < kEventRawPrefixCapacity
        ? static_cast<std::uint8_t>(size)
        : static_cast<std::uint8_t>(kEventRawPrefixCapacity);
    if (out->raw_prefix_size != 0u && data != nullptr) {
        std::memcpy(out->raw_prefix, data, out->raw_prefix_size);
    }
    out->raw_total_size = size > 0xFFFFu
        ? static_cast<std::uint16_t>(0xFFFFu)
        : static_cast<std::uint16_t>(size);
}

int CapabilitiesIncludeVolume(const std::uint8_t* parameters,
                              std::size_t parameters_size) {
    std::size_t index;
    std::size_t count;
    if (parameters == nullptr || parameters_size < 2u ||
        parameters[0] != AVRCP_CAPABILITY_EVENTS_SUPPORTED) {
        return 0;
    }
    count = parameters[1];
    if (parameters_size != count + 2u) return 0;
    for (index = 0u; index < count; ++index) {
        if (parameters[2u + index] == AVRCP_EVENT_VOLUME_CHANGED) {
            return 1;
        }
    }
    return 0;
}

}  // namespace

V1AvrcpFilterPayloadLayout V1AvrcpFilterDetectLayout(
    const std::uint8_t* data,
    std::size_t size) {
    if (data == nullptr) return V1AvrcpFilterPayloadLayout::None;
    if (size >= 11u && data[0] == 0x01u && data[1] == 0x00u &&
        data[2] == 0x00u && data[3] == 0x00u &&
        data[9] == 0x11u && data[10] == 0x0Eu) {
        return V1AvrcpFilterPayloadLayout::MicrosoftPrivateHeader;
    }
    if (size >= 8u && data[1] == 0x11u && data[2] == 0x0Eu) {
        return V1AvrcpFilterPayloadLayout::Direct;
    }
    return V1AvrcpFilterPayloadLayout::None;
}

avrcp_status V1AvrcpFilterDecodePacket(
    const std::uint8_t* data,
    std::size_t size,
    V1AvrcpFilterPayloadLayout layout,
    avrcp_observer_event* out) {
    const std::uint8_t* packet;
    std::size_t packet_size;
    avrcp_status status;
    avrcp_frame frame;
    if (out == nullptr || data == nullptr ||
        layout == V1AvrcpFilterPayloadLayout::None) {
        return AVRCP_INVALID_ARGUMENT;
    }
    std::memset(out, 0, sizeof(*out));
    CopyRawPrefix(out, data, size);
    packet = data;
    packet_size = size;
    if (layout == V1AvrcpFilterPayloadLayout::MicrosoftPrivateHeader) {
        if (size < 11u || data[0] != 0x01u || data[1] != 0x00u ||
            data[2] != 0x00u || data[3] != 0x00u ||
            data[9] != 0x11u || data[10] != 0x0Eu) {
            out->kind = AVRCP_OBSERVER_EVENT_PROTOCOL_ERROR;
            out->error_code = AVRCP_PROTOCOL_ERROR;
            return AVRCP_PROTOCOL_ERROR;
        }
        packet = data + kMicrosoftHeaderSize;
        packet_size = size - kMicrosoftHeaderSize;
    }

    status = avrcp_parse_frame(packet, packet_size, &frame);
    if (status != AVRCP_OK) {
        out->kind = AVRCP_OBSERVER_EVENT_PROTOCOL_ERROR;
        out->error_code = static_cast<int>(status);
        return status;
    }
    if (frame.opcode == AVRCP_OPCODE_PASS_THROUGH) {
        avrcp_pass_through pass_through;
        status = avrcp_parse_pass_through(packet, packet_size, &pass_through);
        if (status != AVRCP_OK) {
            out->kind = AVRCP_OBSERVER_EVENT_PROTOCOL_ERROR;
            out->error_code = static_cast<int>(status);
            return status;
        }
        out->kind = AVRCP_OBSERVER_EVENT_PASS_THROUGH;
        out->operation_id = pass_through.operation_id;
        out->released = pass_through.released;
        out->response_code = frame.ctype_or_response;
        return AVRCP_OK;
    }

    {
        avrcp_vendor_frame vendor;
        const std::uint8_t* parameters;
        std::size_t parameter_size;
        std::size_t index;
        status = avrcp_parse_vendor_frame(packet, packet_size, &vendor);
        if (status != AVRCP_OK) {
            out->kind = AVRCP_OBSERVER_EVENT_PROTOCOL_ERROR;
            out->error_code = static_cast<int>(status);
            return status;
        }
        parameters = packet + vendor.parameters_offset;
        parameter_size = vendor.parameters_size;
        out->pdu_id = vendor.pdu_id;
        out->parameter_size = parameter_size < 8u
            ? static_cast<std::uint8_t>(parameter_size)
            : static_cast<std::uint8_t>(8u);
        for (index = 0u; index < out->parameter_size; ++index) {
            out->parameter_bytes[index] = parameters[index];
        }
        if (vendor.frame.command_response == 0u) {
            out->kind = AVRCP_OBSERVER_EVENT_VENDOR_COMMAND;
            return AVRCP_OK;
        }
        out->response_code = vendor.frame.ctype_or_response;
        if (vendor.pdu_id == AVRCP_PDU_GET_CAPABILITIES &&
            parameter_size >= 2u &&
            parameters[0] == AVRCP_CAPABILITY_EVENTS_SUPPORTED) {
            out->kind = AVRCP_OBSERVER_EVENT_VOLUME_CAPABILITY;
            out->volume_supported = static_cast<std::uint8_t>(
                CapabilitiesIncludeVolume(parameters, parameter_size));
            return AVRCP_OK;
        }
        if (vendor.pdu_id == AVRCP_PDU_REGISTER_NOTIFICATION &&
            parameter_size >= 2u &&
            parameters[0] == AVRCP_EVENT_VOLUME_CHANGED &&
            parameters[1] <= 0x7Fu) {
            out->kind = AVRCP_OBSERVER_EVENT_VOLUME_CHANGED;
            out->volume_supported = 1u;
            out->absolute_volume = parameters[1];
            return AVRCP_OK;
        }
        out->kind = AVRCP_OBSERVER_EVENT_VENDOR_COMMAND;
        return AVRCP_OK;
    }
}

}  // namespace native_ldac::agent
