// SPDX-License-Identifier: Apache-2.0
#include "../xm5_media_control_event.h"

#include <cstdio>

namespace {

int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,    \
                         __LINE__, #condition);                              \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

using native_ldac::agent::Xm5MediaControl;

}  // namespace

int main() {
    using native_ldac::agent::Xm5MediaControlFromAppCommand;
    using native_ldac::agent::Xm5MediaControlFromConsumerUsage;
    using native_ldac::agent::Xm5MediaControlFromVirtualKey;
    using native_ldac::agent::Xm5MediaControlName;

    CHECK(Xm5MediaControlFromVirtualKey(0xAFu) ==
          Xm5MediaControl::VolumeUp);
    CHECK(Xm5MediaControlFromVirtualKey(0xB3u) ==
          Xm5MediaControl::PlayPause);
    CHECK(Xm5MediaControlFromVirtualKey(0xB0u) ==
          Xm5MediaControl::NextTrack);
    CHECK(Xm5MediaControlFromVirtualKey(0xB1u) ==
          Xm5MediaControl::PreviousTrack);
    CHECK(Xm5MediaControlFromVirtualKey(0x20u) ==
          Xm5MediaControl::Unknown);

    CHECK(Xm5MediaControlFromAppCommand(11u) ==
          Xm5MediaControl::NextTrack);
    CHECK(Xm5MediaControlFromAppCommand(47u) ==
          Xm5MediaControl::Pause);
    CHECK(Xm5MediaControlFromAppCommand(46u) ==
          Xm5MediaControl::Play);
    CHECK(Xm5MediaControlFromAppCommand(0u) ==
          Xm5MediaControl::Unknown);

    CHECK(Xm5MediaControlFromConsumerUsage(0x00E9u) ==
          Xm5MediaControl::VolumeUp);
    CHECK(Xm5MediaControlFromConsumerUsage(0x00EAu) ==
          Xm5MediaControl::VolumeDown);
    CHECK(Xm5MediaControlFromConsumerUsage(0x00CDu) ==
          Xm5MediaControl::PlayPause);
    CHECK(Xm5MediaControlFromConsumerUsage(0x00E2u) ==
          Xm5MediaControl::Mute);
    CHECK(Xm5MediaControlFromConsumerUsage(0x00B7u) ==
          Xm5MediaControl::Stop);
    CHECK(Xm5MediaControlFromConsumerUsage(0x00B5u) ==
          Xm5MediaControl::NextTrack);
    CHECK(Xm5MediaControlFromConsumerUsage(0x00B6u) ==
          Xm5MediaControl::PreviousTrack);
    CHECK(Xm5MediaControlFromConsumerUsage(0xFFFFu) ==
          Xm5MediaControl::Unknown);
    CHECK(Xm5MediaControlName(Xm5MediaControl::PreviousTrack)[0] == L'p');

    if (failures != 0) return 1;
    std::puts("XM5 media-control event mapping tests passed.");
    return 0;
}
