#include "xm5_acl_event.h"

#include <bthdef.h>
#include <dbt.h>

#include <cstddef>
#include <cstring>

namespace native_ldac::agent {
Xm5AclEvent ParseXm5AclDeviceChange(BTH_ADDR target_address,
                                    WPARAM event_type,
                                    LPARAM event_data) {
    if (target_address == 0 || event_type != DBT_CUSTOMEVENT ||
        event_data == 0) {
        return Xm5AclEvent::None;
    }
    const auto* broadcast =
        reinterpret_cast<const DEV_BROADCAST_HANDLE*>(event_data);
    const std::size_t required =
        offsetof(DEV_BROADCAST_HANDLE, dbch_data) +
        sizeof(BTH_HCI_EVENT_INFO);
    if (broadcast->dbch_devicetype != DBT_DEVTYP_HANDLE ||
        broadcast->dbch_size < required ||
        !IsEqualGUID(broadcast->dbch_eventguid,
                     GUID_BLUETOOTH_HCI_EVENT)) {
        return Xm5AclEvent::None;
    }

    BTH_HCI_EVENT_INFO event_info{};
    std::memcpy(&event_info,
                broadcast->dbch_data,
                sizeof(event_info));
    if (event_info.bthAddress != target_address ||
        event_info.connectionType != HCI_CONNECTION_TYPE_ACL) {
        return Xm5AclEvent::None;
    }
    return event_info.connected != 0
        ? Xm5AclEvent::Connected
        : Xm5AclEvent::Disconnected;
}

}  // namespace native_ldac::agent
