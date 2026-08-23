#pragma once

#define NOMINMAX
#include <windows.h>

#include <bthdef.h>

namespace native_ldac::agent {

enum class Xm5AclEvent {
    None,
    Connected,
    Disconnected,
};

Xm5AclEvent ParseXm5AclDeviceChange(BTH_ADDR target_address,
                                    WPARAM event_type,
                                    LPARAM event_data);

}  // namespace native_ldac::agent
