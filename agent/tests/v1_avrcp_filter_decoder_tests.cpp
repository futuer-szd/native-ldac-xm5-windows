// SPDX-License-Identifier: Apache-2.0
#include "../v1_avrcp_filter_decoder.h"

#include <cstdint>
#include <cstdio>

namespace {

using native_ldac::agent::V1AvrcpFilterDecodePacket;
using native_ldac::agent::V1AvrcpFilterDetectLayout;
using native_ldac::agent::V1AvrcpFilterPayloadLayout;

int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,    \
                         __LINE__, #condition);                              \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

const std::uint8_t kCapabilitiesStable[] = {
    0x42, 0x11, 0x0E, 0x0C, 0x48, 0x00, 0x00, 0x19,
    0x58, 0x10, 0x00, 0x00, 0x03, 0x03, 0x01, 0x0D};
const std::uint8_t kVolumeInterim42[] = {
    0x52, 0x11, 0x0E, 0x0F, 0x48, 0x00, 0x00, 0x19,
    0x58, 0x31, 0x00, 0x00, 0x02, 0x0D, 0x2A};
const std::uint8_t kVolumeChanged51[] = {
    0x62, 0x11, 0x0E, 0x0D, 0x48, 0x00, 0x00, 0x19,
    0x58, 0x31, 0x00, 0x00, 0x02, 0x0D, 0x33};
const std::uint8_t kPassThroughPlayPress[] = {
    0x80, 0x11, 0x0E, 0x00, 0x48, 0x7C, 0x44, 0x00};
const std::uint8_t kPassThroughPlayRelease[] = {
    0x90, 0x11, 0x0E, 0x00, 0x48, 0x7C, 0xC4, 0x00};
const std::uint8_t kPrivateRegisterNotification[] = {
    0x01, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00,
    0x50, 0x11, 0x0E, 0x03, 0x48, 0x00, 0x00, 0x19,
    0x58, 0x31, 0x00, 0x00, 0x05, 0x0D, 0x00, 0x00, 0x00, 0x00};
const std::uint8_t kPrivatePassThroughAccepted[] = {
    0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x82, 0x11, 0x0E, 0x09, 0x48, 0x7C, 0x44, 0x00};
const std::uint8_t kPrivateSetAbsoluteVolumeWrite[] = {
    0x01, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00,
    0x50, 0x11, 0x0E, 0x00, 0x48, 0x00, 0x00, 0x19,
    0x58, 0x50, 0x00, 0x00, 0x01, 0x2F};
const std::uint8_t kSetAbsoluteVolumeAccepted[] = {
    0x52, 0x11, 0x0E, 0x09, 0x48, 0x00, 0x00, 0x19,
    0x58, 0x50, 0x00, 0x00, 0x01, 0x2F};

void TestLayoutDetection() {
    CHECK(V1AvrcpFilterDetectLayout(
              kCapabilitiesStable, sizeof(kCapabilitiesStable)) ==
          V1AvrcpFilterPayloadLayout::Direct);
    CHECK(V1AvrcpFilterDetectLayout(
              kPrivateRegisterNotification,
              sizeof(kPrivateRegisterNotification)) ==
          V1AvrcpFilterPayloadLayout::MicrosoftPrivateHeader);
    const std::uint8_t other[] = {0x00, 0x01, 0x02, 0x03, 0x04};
    CHECK(V1AvrcpFilterDetectLayout(other, sizeof(other)) ==
          V1AvrcpFilterPayloadLayout::None);
    CHECK(V1AvrcpFilterDetectLayout(nullptr, 0u) ==
          V1AvrcpFilterPayloadLayout::None);
}

void TestVolumeCapability() {
    avrcp_observer_event event{};
    CHECK(V1AvrcpFilterDecodePacket(
              kCapabilitiesStable,
              sizeof(kCapabilitiesStable),
              V1AvrcpFilterPayloadLayout::Direct,
              &event) == AVRCP_OK);
    CHECK(event.kind == AVRCP_OBSERVER_EVENT_VOLUME_CAPABILITY);
    CHECK(event.volume_supported == 1u);
    CHECK(event.response_code == AVRCP_RESPONSE_STABLE);
    CHECK(event.raw_total_size == sizeof(kCapabilitiesStable));
}

void TestVolumeChangedEvents() {
    avrcp_observer_event event{};
    CHECK(V1AvrcpFilterDecodePacket(
              kVolumeInterim42,
              sizeof(kVolumeInterim42),
              V1AvrcpFilterPayloadLayout::Direct,
              &event) == AVRCP_OK);
    CHECK(event.kind == AVRCP_OBSERVER_EVENT_VOLUME_CHANGED);
    CHECK(event.absolute_volume == 42u);
    CHECK(event.response_code == AVRCP_RESPONSE_INTERIM);

    event = avrcp_observer_event{};
    CHECK(V1AvrcpFilterDecodePacket(
              kVolumeChanged51,
              sizeof(kVolumeChanged51),
              V1AvrcpFilterPayloadLayout::Direct,
              &event) == AVRCP_OK);
    CHECK(event.kind == AVRCP_OBSERVER_EVENT_VOLUME_CHANGED);
    CHECK(event.absolute_volume == 51u);
    CHECK(event.response_code == AVRCP_RESPONSE_CHANGED);
}

void TestPassThrough() {
    avrcp_observer_event event{};
    CHECK(V1AvrcpFilterDecodePacket(
              kPassThroughPlayPress,
              sizeof(kPassThroughPlayPress),
              V1AvrcpFilterPayloadLayout::Direct,
              &event) == AVRCP_OK);
    CHECK(event.kind == AVRCP_OBSERVER_EVENT_PASS_THROUGH);
    CHECK(event.operation_id == AVRCP_OPERATION_PLAY);
    CHECK(event.released == 0u);
    CHECK(event.response_code == AVRCP_CTYPE_CONTROL);

    event = avrcp_observer_event{};
    CHECK(V1AvrcpFilterDecodePacket(
              kPassThroughPlayRelease,
              sizeof(kPassThroughPlayRelease),
              V1AvrcpFilterPayloadLayout::Direct,
              &event) == AVRCP_OK);
    CHECK(event.operation_id == AVRCP_OPERATION_PLAY);
    CHECK(event.released == 1u);
}

void TestMicrosoftPrivateHeaderLayout() {
    avrcp_observer_event event{};
    CHECK(V1AvrcpFilterDecodePacket(
              kPrivateRegisterNotification,
              sizeof(kPrivateRegisterNotification),
              V1AvrcpFilterPayloadLayout::MicrosoftPrivateHeader,
              &event) == AVRCP_OK);
    CHECK(event.kind == AVRCP_OBSERVER_EVENT_VENDOR_COMMAND);
    CHECK(event.pdu_id == AVRCP_PDU_REGISTER_NOTIFICATION);
    CHECK(event.parameter_size == 5u);
    CHECK(event.parameter_bytes[0] == AVRCP_EVENT_VOLUME_CHANGED);

    event = avrcp_observer_event{};
    CHECK(V1AvrcpFilterDecodePacket(
              kPrivatePassThroughAccepted,
              sizeof(kPrivatePassThroughAccepted),
              V1AvrcpFilterPayloadLayout::MicrosoftPrivateHeader,
              &event) == AVRCP_OK);
    CHECK(event.kind == AVRCP_OBSERVER_EVENT_PASS_THROUGH);
    CHECK(event.operation_id == AVRCP_OPERATION_PLAY);
    CHECK(event.response_code == AVRCP_RESPONSE_ACCEPTED);
}

void TestSetAbsoluteVolumeVendorCommand() {
    // Microsoft's SetAbsoluteVolume write request (private header layout,
    // CONTROL command, volume 47) must decode as a vendor command carrying
    // the volume parameter; the PC-to-XM5 write path stays observable.
    avrcp_observer_event event{};
    CHECK(V1AvrcpFilterDecodePacket(
              kPrivateSetAbsoluteVolumeWrite,
              sizeof(kPrivateSetAbsoluteVolumeWrite),
              V1AvrcpFilterPayloadLayout::MicrosoftPrivateHeader,
              &event) == AVRCP_OK);
    CHECK(event.kind == AVRCP_OBSERVER_EVENT_VENDOR_COMMAND);
    CHECK(event.pdu_id == AVRCP_PDU_SET_ABSOLUTE_VOLUME);
    CHECK(event.response_code == AVRCP_CTYPE_CONTROL);
    CHECK(event.parameter_size == 1u);
    CHECK(event.parameter_bytes[0] == 47u);

    // The accepted response (Direct layout, ACCEPTED, volume echo) must
    // decode with the response code and the volume parameter.
    event = avrcp_observer_event{};
    CHECK(V1AvrcpFilterDecodePacket(
              kSetAbsoluteVolumeAccepted,
              sizeof(kSetAbsoluteVolumeAccepted),
              V1AvrcpFilterPayloadLayout::Direct,
              &event) == AVRCP_OK);
    CHECK(event.kind == AVRCP_OBSERVER_EVENT_VENDOR_COMMAND);
    CHECK(event.pdu_id == AVRCP_PDU_SET_ABSOLUTE_VOLUME);
    CHECK(event.response_code == AVRCP_RESPONSE_ACCEPTED);
    CHECK(event.parameter_size == 1u);
    CHECK(event.parameter_bytes[0] == 47u);
}

void TestMalformedInputs() {
    avrcp_observer_event event{};
    const std::uint8_t truncated[] = {0x42, 0x11, 0x0E, 0x0C};
    CHECK(V1AvrcpFilterDecodePacket(
              truncated,
              sizeof(truncated),
              V1AvrcpFilterPayloadLayout::Direct,
              &event) != AVRCP_OK);
    CHECK(event.kind == AVRCP_OBSERVER_EVENT_PROTOCOL_ERROR);

    const std::uint8_t nonAvrcp[] = {0x00, 0x01, 0x02, 0x03,
                                     0x04, 0x05, 0x06, 0x07};
    CHECK(V1AvrcpFilterDecodePacket(
              nonAvrcp,
              sizeof(nonAvrcp),
              V1AvrcpFilterPayloadLayout::Direct,
              &event) != AVRCP_OK);
    CHECK(V1AvrcpFilterDecodePacket(
              nonAvrcp,
              sizeof(nonAvrcp),
              V1AvrcpFilterPayloadLayout::MicrosoftPrivateHeader,
              &event) != AVRCP_OK);
    CHECK(V1AvrcpFilterDecodePacket(
              nullptr,
              0u,
              V1AvrcpFilterPayloadLayout::Direct,
              &event) == AVRCP_INVALID_ARGUMENT);
}

}  // namespace

int main() {
    TestLayoutDetection();
    TestVolumeCapability();
    TestVolumeChangedEvents();
    TestPassThrough();
    TestMicrosoftPrivateHeaderLayout();
    TestSetAbsoluteVolumeVendorCommand();
    TestMalformedInputs();
    if (failures != 0) {
        std::fprintf(stderr,
                     "v1_avrcp_filter_decoder_tests: %d failures\n",
                     failures);
        return 1;
    }
    std::printf("v1_avrcp_filter_decoder_tests passed.\n");
    return 0;
}
