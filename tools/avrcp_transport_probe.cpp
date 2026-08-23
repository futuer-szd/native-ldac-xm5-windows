// SPDX-License-Identifier: Apache-2.0
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <setupapi.h>

#include <cstdio>
#include <cwchar>
#include <fcntl.h>
#include <io.h>
#include <vector>

namespace {

// Private interface published by Microsoft.Bluetooth.AvrcpTransport.sys.
// This diagnostic never sends an IOCTL or channel payload.
constexpr GUID kAvrcpTransportInterface = {
    0xbc03ba80,
    0xefb5,
    0x4149,
    {0x9c, 0xdd, 0x0d, 0x10, 0x74, 0xa9, 0x40, 0xe1}};

void PrintHelp() {
    std::wprintf(
        L"Usage: avrcp_transport_probe [--open]\n"
        L"  (default)  Enumerate Microsoft's private AVRCP transport "
        L"interfaces.\n"
        L"  --open     Also attempt a zero-access CreateFile; no IOCTL or "
        L"data is sent.\n");
}

bool ParseArguments(int argc, wchar_t** argv, bool* open_interface) {
    if (open_interface == nullptr) return false;
    *open_interface = false;
    for (int index = 1; index < argc; ++index) {
        if (std::wcscmp(argv[index], L"--help") == 0 ||
            std::wcscmp(argv[index], L"-h") == 0) {
            PrintHelp();
            return false;
        }
        if (std::wcscmp(argv[index], L"--open") == 0) {
            *open_interface = true;
            continue;
        }
        std::fwprintf(stderr, L"Unknown argument: %ls\n", argv[index]);
        PrintHelp();
        return false;
    }
    return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    (void)_setmode(_fileno(stdout), _O_U16TEXT);
    (void)_setmode(_fileno(stderr), _O_U16TEXT);

    bool open_interface = false;
    if (!ParseArguments(argc, argv, &open_interface)) {
        return (argc > 1 &&
                (std::wcscmp(argv[1], L"--help") == 0 ||
                 std::wcscmp(argv[1], L"-h") == 0))
                   ? 0
                   : 2;
    }

    HDEVINFO device_info = SetupDiGetClassDevsW(
        &kAvrcpTransportInterface,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (device_info == INVALID_HANDLE_VALUE) {
        std::fwprintf(stderr,
                      L"SetupDiGetClassDevs failed (Win32 %lu).\n",
                      GetLastError());
        return 1;
    }

    DWORD found = 0;
    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interface_data{};
        interface_data.cbSize = sizeof(interface_data);
        if (!SetupDiEnumDeviceInterfaces(device_info,
                                         nullptr,
                                         &kAvrcpTransportInterface,
                                         index,
                                         &interface_data)) {
            const DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_ITEMS) {
                std::fwprintf(stderr,
                              L"SetupDiEnumDeviceInterfaces failed "
                              L"(Win32 %lu).\n",
                              error);
                SetupDiDestroyDeviceInfoList(device_info);
                return 1;
            }
            break;
        }

        DWORD required_size = 0;
        (void)SetupDiGetDeviceInterfaceDetailW(device_info,
                                               &interface_data,
                                               nullptr,
                                               0,
                                               &required_size,
                                               nullptr);
        if (required_size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            std::fwprintf(stderr, L"Invalid interface detail size.\n");
            SetupDiDestroyDeviceInfoList(device_info);
            return 1;
        }

        std::vector<BYTE> detail_buffer(required_size);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
            detail_buffer.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(device_info,
                                              &interface_data,
                                              detail,
                                              required_size,
                                              nullptr,
                                              nullptr)) {
            std::fwprintf(stderr,
                          L"SetupDiGetDeviceInterfaceDetail failed "
                          L"(Win32 %lu).\n",
                          GetLastError());
            SetupDiDestroyDeviceInfoList(device_info);
            return 1;
        }

        ++found;
        std::wprintf(L"Interface %lu: %ls\n", found, detail->DevicePath);
        if (!open_interface) continue;

        HANDLE handle = CreateFileW(detail->DevicePath,
                                    0,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            std::wprintf(L"  Zero-access open denied/failed: Win32 %lu.\n",
                         GetLastError());
        } else {
            std::wprintf(L"  Zero-access open succeeded; handle closed "
                         L"without sending an IOCTL.\n");
            CloseHandle(handle);
        }
    }

    SetupDiDestroyDeviceInfoList(device_info);
    if (found == 0) {
        std::wprintf(L"No present Microsoft AVRCP transport interface found.\n");
        return 3;
    }
    std::wprintf(L"Enumerated %lu interface(s); no IOCTL or data was sent.\n",
                 found);
    return 0;
}
