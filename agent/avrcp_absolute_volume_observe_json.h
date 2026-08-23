// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "avrcp_absolute_volume_observe_log.h"

#include <cstddef>
#include <cstdint>

namespace native_ldac::agent {

constexpr std::size_t kAvrcpVolumeObserveSnapshotMaximumJsonBytes = 4096u;

enum class AvrcpVolumeObserveJsonStatus : std::uint32_t {
    Succeeded = 0u,
    InvalidArgument,
    InvalidEnum,
    OutOfRange,
    InvalidSnapshot,
    BufferTooSmall,
    InternalOverflow,
};

// bytes_written excludes the required trailing NUL. On every failure the
// caller's output buffer and bytes_written value remain unchanged.
AvrcpVolumeObserveJsonStatus SerializeAvrcpVolumeObserveSnapshotJson(
    const AvrcpVolumeObserveSnapshot& snapshot,
    char* output,
    std::size_t output_capacity,
    std::size_t* bytes_written);

}  // namespace native_ldac::agent
