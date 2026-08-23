// SPDX-License-Identifier: Apache-2.0
#include "xm5_media_control_event.h"

namespace native_ldac::agent {
namespace {

constexpr std::uint16_t kVirtualKeyVolumeMute = 0xADu;
constexpr std::uint16_t kVirtualKeyVolumeDown = 0xAEu;
constexpr std::uint16_t kVirtualKeyVolumeUp = 0xAFu;
constexpr std::uint16_t kVirtualKeyMediaNextTrack = 0xB0u;
constexpr std::uint16_t kVirtualKeyMediaPreviousTrack = 0xB1u;
constexpr std::uint16_t kVirtualKeyMediaStop = 0xB2u;
constexpr std::uint16_t kVirtualKeyMediaPlayPause = 0xB3u;

constexpr std::uint16_t kAppCommandVolumeMute = 8u;
constexpr std::uint16_t kAppCommandVolumeDown = 9u;
constexpr std::uint16_t kAppCommandVolumeUp = 10u;
constexpr std::uint16_t kAppCommandMediaNextTrack = 11u;
constexpr std::uint16_t kAppCommandMediaPreviousTrack = 12u;
constexpr std::uint16_t kAppCommandMediaStop = 13u;
constexpr std::uint16_t kAppCommandMediaPlayPause = 14u;
constexpr std::uint16_t kAppCommandMediaPlay = 46u;
constexpr std::uint16_t kAppCommandMediaPause = 47u;

// USB HID Usage Tables, Consumer Page (0x0C).
constexpr std::uint16_t kConsumerUsageScanNextTrack = 0x00B5u;
constexpr std::uint16_t kConsumerUsageScanPreviousTrack = 0x00B6u;
constexpr std::uint16_t kConsumerUsageStop = 0x00B7u;
constexpr std::uint16_t kConsumerUsagePlayPause = 0x00CDu;
constexpr std::uint16_t kConsumerUsageMute = 0x00E2u;
constexpr std::uint16_t kConsumerUsageVolumeIncrement = 0x00E9u;
constexpr std::uint16_t kConsumerUsageVolumeDecrement = 0x00EAu;

}  // namespace

Xm5MediaControl Xm5MediaControlFromVirtualKey(std::uint16_t virtual_key) {
    switch (virtual_key) {
        case kVirtualKeyVolumeUp:
            return Xm5MediaControl::VolumeUp;
        case kVirtualKeyVolumeDown:
            return Xm5MediaControl::VolumeDown;
        case kVirtualKeyVolumeMute:
            return Xm5MediaControl::Mute;
        case kVirtualKeyMediaPlayPause:
            return Xm5MediaControl::PlayPause;
        case kVirtualKeyMediaStop:
            return Xm5MediaControl::Stop;
        case kVirtualKeyMediaNextTrack:
            return Xm5MediaControl::NextTrack;
        case kVirtualKeyMediaPreviousTrack:
            return Xm5MediaControl::PreviousTrack;
        default:
            return Xm5MediaControl::Unknown;
    }
}

Xm5MediaControl Xm5MediaControlFromAppCommand(std::uint16_t app_command) {
    switch (app_command) {
        case kAppCommandVolumeUp:
            return Xm5MediaControl::VolumeUp;
        case kAppCommandVolumeDown:
            return Xm5MediaControl::VolumeDown;
        case kAppCommandVolumeMute:
            return Xm5MediaControl::Mute;
        case kAppCommandMediaPlayPause:
            return Xm5MediaControl::PlayPause;
        case kAppCommandMediaPlay:
            return Xm5MediaControl::Play;
        case kAppCommandMediaPause:
            return Xm5MediaControl::Pause;
        case kAppCommandMediaStop:
            return Xm5MediaControl::Stop;
        case kAppCommandMediaNextTrack:
            return Xm5MediaControl::NextTrack;
        case kAppCommandMediaPreviousTrack:
            return Xm5MediaControl::PreviousTrack;
        default:
            return Xm5MediaControl::Unknown;
    }
}

Xm5MediaControl Xm5MediaControlFromConsumerUsage(std::uint16_t usage) {
    switch (usage) {
        case kConsumerUsageVolumeIncrement:
            return Xm5MediaControl::VolumeUp;
        case kConsumerUsageVolumeDecrement:
            return Xm5MediaControl::VolumeDown;
        case kConsumerUsageMute:
            return Xm5MediaControl::Mute;
        case kConsumerUsagePlayPause:
            return Xm5MediaControl::PlayPause;
        case kConsumerUsageStop:
            return Xm5MediaControl::Stop;
        case kConsumerUsageScanNextTrack:
            return Xm5MediaControl::NextTrack;
        case kConsumerUsageScanPreviousTrack:
            return Xm5MediaControl::PreviousTrack;
        default:
            return Xm5MediaControl::Unknown;
    }
}

std::uint16_t Xm5MediaControlVirtualKey(Xm5MediaControl control) {
    switch (control) {
        case Xm5MediaControl::VolumeUp:
            return kVirtualKeyVolumeUp;
        case Xm5MediaControl::VolumeDown:
            return kVirtualKeyVolumeDown;
        case Xm5MediaControl::Mute:
            return kVirtualKeyVolumeMute;
        case Xm5MediaControl::PlayPause:
            return kVirtualKeyMediaPlayPause;
        case Xm5MediaControl::Stop:
            return kVirtualKeyMediaStop;
        case Xm5MediaControl::NextTrack:
            return kVirtualKeyMediaNextTrack;
        case Xm5MediaControl::PreviousTrack:
            return kVirtualKeyMediaPreviousTrack;
        default:
            return 0u;
    }
}

const wchar_t* Xm5MediaControlName(Xm5MediaControl control) {
    switch (control) {
        case Xm5MediaControl::VolumeUp:
            return L"volume-up";
        case Xm5MediaControl::VolumeDown:
            return L"volume-down";
        case Xm5MediaControl::Mute:
            return L"mute";
        case Xm5MediaControl::PlayPause:
            return L"play-pause";
        case Xm5MediaControl::Play:
            return L"play";
        case Xm5MediaControl::Pause:
            return L"pause";
        case Xm5MediaControl::Stop:
            return L"stop";
        case Xm5MediaControl::NextTrack:
            return L"next-track";
        case Xm5MediaControl::PreviousTrack:
            return L"previous-track";
        default:
            return L"unknown";
    }
}

}  // namespace native_ldac::agent
