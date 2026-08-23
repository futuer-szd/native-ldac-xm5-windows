// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace native_ldac::agent {

enum class Xm5MediaControl : std::uint8_t {
    Unknown = 0,
    VolumeUp,
    VolumeDown,
    Mute,
    PlayPause,
    Play,
    Pause,
    Stop,
    NextTrack,
    PreviousTrack,
};

Xm5MediaControl Xm5MediaControlFromVirtualKey(std::uint16_t virtual_key);
Xm5MediaControl Xm5MediaControlFromAppCommand(std::uint16_t app_command);
Xm5MediaControl Xm5MediaControlFromConsumerUsage(std::uint16_t usage);
const wchar_t* Xm5MediaControlName(Xm5MediaControl control);
// Inverse mapping used by the action executor. Unknown controls map to 0.
std::uint16_t Xm5MediaControlVirtualKey(Xm5MediaControl control);

}  // namespace native_ldac::agent
