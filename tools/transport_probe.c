// SPDX-License-Identifier: Apache-2.0
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <bcrypt.h>

#include <initguid.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "ldac_native_ioctl.h"
#include "ldac_native/abr_controller.h"
#include "ldac_native/avdtp.h"
#include "ldac_native/ldac_codec.h"
#include "ldac_native/ldac_encoder.h"
#include "ldac_native/native_pcm_source.h"
#include "ldac_native/rtp_ldac.h"
#include "ldac_native/wasapi_loopback.h"

#define LDAC_TEST_MEDIA_MTU 1021u
#define LDAC_TEST_SAMPLE_RATE_HZ 96000u
#define LDAC_TEST_SILENCE_DURATION_MS 1000u
#define LDAC_TEST_TONE_DURATION_MS 3000u
#define LDAC_TEST_TONE_START_MS 500u
#define LDAC_TEST_TONE_END_MS 2500u
#define LDAC_TEST_SYSTEM_DURATION_MS 10000u
#define LDAC_TEST_TONE_HZ 440.0
#define LDAC_TEST_TONE_AMPLITUDE 0.0316227766
#define LDAC_TEST_TWO_PI 6.28318530717958647692
#define LDAC_OPEN_SIGNALING_MAX_ATTEMPTS 20u
#define LDAC_OPEN_SIGNALING_RETRY_DELAY_MS 1000u
#define LDAC_PROBE_LOCAL_SEID 1u
#define LDAC_PROBE_MAX_PEER_COMMANDS 4u
#define LDAC_PROBE_PEER_ERROR_SEP_IN_USE 0x13u

typedef enum probe_media_mode {
    PROBE_MEDIA_NONE = 0,
    PROBE_MEDIA_EMPTY_SESSION,
    PROBE_MEDIA_SILENCE_MQ,
    PROBE_MEDIA_SILENCE_CONTINUOUS_MQ,
    PROBE_MEDIA_TONE_MQ,
    PROBE_MEDIA_SYSTEM_MQ,
    PROBE_MEDIA_SYSTEM_CONTINUOUS_MQ,
    PROBE_MEDIA_ENDPOINT_CONTINUOUS
} probe_media_mode;

static HANDLE g_device = INVALID_HANDLE_VALUE;
static HANDLE g_stop_event = NULL;
static volatile LONG g_cancelled = 0;
static volatile LONG g_stop_event_requested = 0;
static DWORD g_open_signaling_max_attempts =
    LDAC_OPEN_SIGNALING_MAX_ATTEMPTS;

static BOOL run_ioctl(HANDLE device,
                      DWORD ioctlCode,
                      void *inputBuffer,
                      DWORD inputLength,
                      void *outputBuffer,
                      DWORD outputLength,
                      DWORD waitMs,
                      DWORD *bytesReturned);
static void print_transfer_diagnostics(HANDLE device);
static int print_open_diagnostics(HANDLE device);

static const wchar_t *open_operation_name(ULONG operation) {
    switch (operation) {
        case LDAC_NATIVE_OPEN_OPERATION_SIGNALING: return L"signaling";
        case LDAC_NATIVE_OPEN_OPERATION_MEDIA: return L"media";
        default: return L"none";
    }
}

static const wchar_t *connect_response_name(USHORT response) {
    switch (response) {
        case 0u: return L"success";
        case 1u: return L"pending";
        case 2u: return L"PSM not supported";
        case 3u: return L"security block";
        case 4u: return L"no resources";
        default: return L"unknown";
    }
}

static int print_open_diagnostics(HANDLE device) {
    LDAC_NATIVE_OPEN_DIAGNOSTICS diagnostics;
    DWORD bytes = 0u;

    ZeroMemory(&diagnostics, sizeof(diagnostics));
    if (!run_ioctl(device,
                   IOCTL_LDAC_NATIVE_GET_OPEN_DIAGNOSTICS,
                   NULL,
                   0u,
                   &diagnostics,
                   sizeof(diagnostics),
                   2000u,
                   &bytes) ||
        bytes < sizeof(diagnostics) ||
        diagnostics.Size != sizeof(diagnostics) ||
        (diagnostics.Flags & LDAC_NATIVE_OPEN_DIAGNOSTIC_ATTEMPTED) == 0u) {
        fwprintf(stderr, L"L2CAP OPEN diagnostics are unavailable.\n");
        return 0;
    }

    fwprintf(stderr,
             L"L2CAP OPEN diagnostic #%lu: %ls, IO 0x%08lX, "
             L"BRB 0x%08lX, Bluetooth 0x%08lX, PSM 0x%04X, "
             L"channel flags 0x%08lX.\n",
             diagnostics.Sequence,
             open_operation_name(diagnostics.Operation),
             (unsigned long)diagnostics.IoStatus,
             (unsigned long)diagnostics.BrbStatus,
             diagnostics.BtStatus,
             diagnostics.Psm,
             diagnostics.ChannelFlags);
    fwprintf(stderr,
             L"Signaling channel direction: %ls.\n",
             (diagnostics.Flags &
              LDAC_NATIVE_OPEN_DIAGNOSTIC_INBOUND_CHANNEL) != 0u
                 ? L"inbound"
                 : L"outbound");
    fwprintf(stderr,
             L"L2CAP OPEN state: %ls, %ls.\n",
             (diagnostics.Flags &
              LDAC_NATIVE_OPEN_DIAGNOSTIC_COMPLETED) != 0u
                 ? L"completed"
                 : L"pending",
             (diagnostics.Flags &
              LDAC_NATIVE_OPEN_DIAGNOSTIC_SUCCEEDED) != 0u
                 ? L"succeeded"
                 : L"not-succeeded");
    if ((diagnostics.Flags &
         LDAC_NATIVE_OPEN_DIAGNOSTIC_REMOTE_RESPONSE_VALID) != 0u) {
        fwprintf(stderr,
                 L"Remote L2CAP response: %u (%ls), status %u.\n",
                 diagnostics.Response,
                 connect_response_name(diagnostics.Response),
                 diagnostics.ResponseStatus);
    } else {
        fwprintf(stderr,
                 L"No valid negative remote L2CAP response was reported.\n");
    }
    return 1;
}

static void print_usage(const wchar_t *program) {
    wprintf(L"Native LDAC transport probe\n\n");
    wprintf(L"Usage: %ls [--info] [--open-diagnostics] [--discover] [--configure] "
            L"[--media-session] [--stream-silence] "
            L"[--stream-silence-continuous] [--stream-tone] "
            L"[--stream-system] [--play-system] [--play-endpoint] "
            L"[--quality mq|sq|hq|auto] "
            L"[--channel-mode stereo|dual|mono] "
            L"[--sample-rate 44100|48000|88200|96000] "
            L"[--bits 16|24] [--open-attempts 1-20] "
            L"[--hold-signaling-seconds 15-300] "
            L"[--stop-event name]\n\n",
            program);
    wprintf(L"  --info      Read driver ABI and Bluetooth addresses (default).\n");
    wprintf(L"  --open-diagnostics Read the last driver-recorded L2CAP OPEN result; never submit a new OPEN.\n");
    wprintf(L"  --discover  Open AVDTP signaling and query XM5 LDAC capabilities.\n");
    wprintf(L"  --configure Discover and test a 96 kHz LDAC configuration; "
            L"ABORT afterward.\n");
    wprintf(L"  --media-session Test OPEN, Media L2CAP, and START without "
            L"sending audio; SUSPEND and CLOSE afterward.\n");
    wprintf(L"  --stream-silence Encode and send one second of 96 kHz "
            L"LDAC silence; SUSPEND and CLOSE afterward.\n");
    wprintf(L"  --stream-silence-continuous Encode and send LDAC silence "
            L"until the stop event or Ctrl+C; SUSPEND and CLOSE afterward.\n");
    wprintf(L"  --stream-tone Send 0.5 s silence, a 2 s 440 Hz tone at "
            L"-30 dBFS, then 0.5 s silence using 96 kHz LDAC.\n");
    wprintf(L"  --stream-system Capture and send 10 seconds from the current "
            L"default render endpoint using WASAPI loopback and LDAC.\n");
    wprintf(L"  --play-system Continuously send the current default render "
            L"endpoint using LDAC; press Ctrl+C to stop safely.\n");
    wprintf(L"  --play-endpoint Continuously read Native LDAC - WH-1000XM5 "
            L"PCM and send it using LDAC; press Ctrl+C to stop safely.\n");
    wprintf(L"  --quality  Select LDAC MQ (default), SQ, HQ, or automatic "
            L"quality for continuous playback modes.\n");
    wprintf(L"  --channel-mode Select LDAC stereo (default), dual-channel, "
            L"or mono; mono downmixes the stereo PCM source.\n");
    wprintf(L"  --sample-rate Select the Native LDAC endpoint and LDAC "
            L"sample rate (default 48000).\n");
    wprintf(L"  --bits      Select the Native LDAC endpoint PCM bit depth "
            L"(default 16).\n");
    wprintf(L"  --hold-signaling-seconds After capability-only DISCOVER, "
            L"keep only the AVDTP signaling channel open for a bounded "
            L"15 to 300 seconds; no configuration, media channel, START, "
            L"or media packet is allowed.\n");
    wprintf(L"  --stop-event Open a named Windows event used by a UI to "
            L"request graceful shutdown.\n");
    wprintf(L"  --help      Show this help.\n");
}

static int media_mode_uses_wasapi(probe_media_mode mediaMode) {
    return mediaMode == PROBE_MEDIA_SYSTEM_MQ ||
           mediaMode == PROBE_MEDIA_SYSTEM_CONTINUOUS_MQ;
}

static int media_mode_uses_native_endpoint(probe_media_mode mediaMode) {
    return mediaMode == PROBE_MEDIA_ENDPOINT_CONTINUOUS;
}

static int media_mode_is_continuous(probe_media_mode mediaMode) {
    return mediaMode == PROBE_MEDIA_SYSTEM_CONTINUOUS_MQ ||
           mediaMode == PROBE_MEDIA_SILENCE_CONTINUOUS_MQ ||
           media_mode_uses_native_endpoint(mediaMode);
}

static int media_mode_uses_live_source(probe_media_mode mediaMode) {
    return media_mode_uses_wasapi(mediaMode) ||
           media_mode_uses_native_endpoint(mediaMode);
}

static int media_mode_streams_audio(probe_media_mode mediaMode) {
    return mediaMode == PROBE_MEDIA_SILENCE_MQ ||
           mediaMode == PROBE_MEDIA_SILENCE_CONTINUOUS_MQ ||
           mediaMode == PROBE_MEDIA_TONE_MQ ||
           media_mode_uses_live_source(mediaMode);
}

static const wchar_t *quality_name(ldac_encoder_quality quality) {
    switch (quality) {
        case LDAC_ENCODER_QUALITY_HQ: return L"HQ";
        case LDAC_ENCODER_QUALITY_SQ: return L"SQ";
        case LDAC_ENCODER_QUALITY_MQ: return L"MQ";
        default: return L"unknown";
    }
}

static int parse_quality(const wchar_t *value,
                         ldac_encoder_quality *quality,
                         int *automatic) {
    if (value == NULL || quality == NULL || automatic == NULL) return 0;
    if (_wcsicmp(value, L"auto") == 0) {
        *quality = LDAC_ENCODER_QUALITY_HQ;
        *automatic = 1;
        return 1;
    }
    if (_wcsicmp(value, L"mq") == 0) {
        *quality = LDAC_ENCODER_QUALITY_MQ;
        *automatic = 0;
        return 1;
    }
    if (_wcsicmp(value, L"sq") == 0) {
        *quality = LDAC_ENCODER_QUALITY_SQ;
        *automatic = 0;
        return 1;
    }
    if (_wcsicmp(value, L"hq") == 0) {
        *quality = LDAC_ENCODER_QUALITY_HQ;
        *automatic = 0;
        return 1;
    }
    return 0;
}

static const wchar_t *channel_mode_name(
    ldac_encoder_channel_mode channelMode) {
    switch (channelMode) {
        case LDAC_ENCODER_CHANNEL_STEREO: return L"stereo";
        case LDAC_ENCODER_CHANNEL_DUAL: return L"dual-channel";
        case LDAC_ENCODER_CHANNEL_MONO: return L"mono";
        default: return L"unknown";
    }
}

static uint8_t channel_mode_capability(
    ldac_encoder_channel_mode channelMode) {
    switch (channelMode) {
        case LDAC_ENCODER_CHANNEL_STEREO: return LDAC_CM_STEREO;
        case LDAC_ENCODER_CHANNEL_DUAL: return LDAC_CM_DUAL;
        case LDAC_ENCODER_CHANNEL_MONO: return LDAC_CM_MONO;
        default: return 0u;
    }
}

static int parse_channel_mode(const wchar_t *value,
                              ldac_encoder_channel_mode *channelMode) {
    if (value == NULL || channelMode == NULL) return 0;
    if (_wcsicmp(value, L"stereo") == 0) {
        *channelMode = LDAC_ENCODER_CHANNEL_STEREO;
        return 1;
    }
    if (_wcsicmp(value, L"dual") == 0 ||
        _wcsicmp(value, L"dual-channel") == 0) {
        *channelMode = LDAC_ENCODER_CHANNEL_DUAL;
        return 1;
    }
    if (_wcsicmp(value, L"mono") == 0) {
        *channelMode = LDAC_ENCODER_CHANNEL_MONO;
        return 1;
    }
    return 0;
}

static void print_win32_error(const wchar_t *operation, DWORD error) {
    wchar_t *message = NULL;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                  FORMAT_MESSAGE_FROM_SYSTEM |
                  FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD length = FormatMessageW(flags,
                                  NULL,
                                  error,
                                  0u,
                                  (wchar_t *)&message,
                                  0u,
                                  NULL);
    if (length != 0u && message != NULL) {
        fwprintf(stderr,
                 L"%ls failed (Win32 %lu): %ls",
                 operation,
                 error,
                 message);
        LocalFree(message);
    } else {
        fwprintf(stderr, L"%ls failed (Win32 %lu).\n", operation, error);
    }
}

static BOOL WINAPI console_control_handler(DWORD controlType) {
    if (controlType == CTRL_C_EVENT ||
        controlType == CTRL_BREAK_EVENT ||
        controlType == CTRL_CLOSE_EVENT) {
        InterlockedExchange(&g_cancelled, 1);
        if (g_device != INVALID_HANDLE_VALUE) {
            (void)CancelIoEx(g_device, NULL);
        }
        return TRUE;
    }
    return FALSE;
}

static int stop_requested(void) {
    if (InterlockedCompareExchange(&g_cancelled, 0, 0) != 0) return 1;
    if (g_stop_event != NULL &&
        WaitForSingleObject(g_stop_event, 0u) == WAIT_OBJECT_0) {
        InterlockedExchange(&g_stop_event_requested, 1);
        InterlockedExchange(&g_cancelled, 1);
        return 1;
    }
    return 0;
}

static BOOL run_ioctl(HANDLE device,
                      DWORD ioctlCode,
                      void *inputBuffer,
                      DWORD inputLength,
                      void *outputBuffer,
                      DWORD outputLength,
                      DWORD waitMs,
                      DWORD *bytesReturned) {
    OVERLAPPED overlapped;
    DWORD immediateBytes = 0u;
    DWORD transferred = 0u;
    DWORD error = ERROR_SUCCESS;
    DWORD waitResult;
    BOOL started;
    BOOL completed;

    ZeroMemory(&overlapped, sizeof(overlapped));
    overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (overlapped.hEvent == NULL) return FALSE;

    started = DeviceIoControl(device,
                              ioctlCode,
                              inputBuffer,
                              inputLength,
                              outputBuffer,
                              outputLength,
                              &immediateBytes,
                              &overlapped);
    if (!started) {
        error = GetLastError();
        if (error != ERROR_IO_PENDING) {
            CloseHandle(overlapped.hEvent);
            SetLastError(error);
            return FALSE;
        }
        waitResult = WaitForSingleObject(overlapped.hEvent, waitMs);
        if (waitResult == WAIT_TIMEOUT) {
            (void)CancelIoEx(device, &overlapped);
            (void)WaitForSingleObject(overlapped.hEvent, INFINITE);
            (void)GetOverlappedResult(device, &overlapped, &transferred, FALSE);
            CloseHandle(overlapped.hEvent);
            SetLastError(ERROR_TIMEOUT);
            return FALSE;
        }
        if (waitResult != WAIT_OBJECT_0) {
            error = GetLastError();
            (void)CancelIoEx(device, &overlapped);
            (void)WaitForSingleObject(overlapped.hEvent, INFINITE);
            CloseHandle(overlapped.hEvent);
            SetLastError(error);
            return FALSE;
        }
    }

    completed = GetOverlappedResult(device,
                                    &overlapped,
                                    &transferred,
                                    FALSE);
    if (!completed) error = GetLastError();
    CloseHandle(overlapped.hEvent);
    if (!completed) {
        SetLastError(error);
        return FALSE;
    }
    if (bytesReturned != NULL) *bytesReturned = transferred;
    return TRUE;
}

static BOOL open_signaling_with_retry(
    HANDLE device,
    const LDAC_NATIVE_OPEN_SIGNALING_REQUEST *openRequest,
    LDAC_NATIVE_CHANNEL_INFO *channel,
    DWORD *bytesReturned) {
    DWORD attempt;
    DWORD error = ERROR_SUCCESS;

    if (openRequest == NULL || channel == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    for (attempt = 1u; attempt <= g_open_signaling_max_attempts; ++attempt) {
        ZeroMemory(channel, sizeof(*channel));
        if (bytesReturned != NULL) *bytesReturned = 0u;
        if (run_ioctl(device,
                      IOCTL_LDAC_NATIVE_OPEN_SIGNALING,
                      (void *)openRequest,
                      sizeof(*openRequest),
                      channel,
                      sizeof(*channel),
                      openRequest->TimeoutMs + 2000u,
                      bytesReturned)) {
            return TRUE;
        }

        error = GetLastError();
        if ((error != ERROR_REQ_NOT_ACCEP &&
             error != ERROR_BUSY &&
             error != ERROR_NOT_READY &&
             error != ERROR_DEVICE_NOT_CONNECTED &&
             error != ERROR_TIMEOUT) ||
            attempt == g_open_signaling_max_attempts ||
            stop_requested()) {
            break;
        }

        fwprintf(stderr,
                 L"OPEN_SIGNALING was temporarily rejected (Win32 %lu); "
                 L"retrying in %lu ms (%lu/%lu).\n",
                 error,
                 (DWORD)LDAC_OPEN_SIGNALING_RETRY_DELAY_MS,
                 attempt,
                 g_open_signaling_max_attempts - 1u);
        fflush(stderr);
        Sleep(LDAC_OPEN_SIGNALING_RETRY_DELAY_MS);
    }

    SetLastError(error);
    return FALSE;
}

static HANDLE open_transport_interface(void) {
    HDEVINFO deviceInfo;
    SP_DEVICE_INTERFACE_DATA interfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail = NULL;
    DWORD requiredLength = 0u;
    DWORD error = ERROR_SUCCESS;
    HANDLE device = INVALID_HANDLE_VALUE;

    deviceInfo = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_LDAC_NATIVE_TRANSPORT,
        NULL,
        NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    ZeroMemory(&interfaceData, sizeof(interfaceData));
    interfaceData.cbSize = sizeof(interfaceData);
    if (!SetupDiEnumDeviceInterfaces(deviceInfo,
                                     NULL,
                                     &GUID_DEVINTERFACE_LDAC_NATIVE_TRANSPORT,
                                     0u,
                                     &interfaceData)) {
        error = GetLastError();
        goto cleanup;
    }

    (void)SetupDiGetDeviceInterfaceDetailW(deviceInfo,
                                           &interfaceData,
                                           NULL,
                                           0u,
                                           &requiredLength,
                                           NULL);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || requiredLength == 0u) {
        error = GetLastError();
        goto cleanup;
    }

    detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, requiredLength);
    if (detail == NULL) {
        error = ERROR_NOT_ENOUGH_MEMORY;
        goto cleanup;
    }
    detail->cbSize = sizeof(*detail);
    if (!SetupDiGetDeviceInterfaceDetailW(deviceInfo,
                                          &interfaceData,
                                          detail,
                                          requiredLength,
                                          NULL,
                                          NULL)) {
        error = GetLastError();
        goto cleanup;
    }

    wprintf(L"Interface: %ls\n", detail->DevicePath);
    device = CreateFileW(detail->DevicePath,
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                         NULL);
    if (device == INVALID_HANDLE_VALUE) error = GetLastError();

cleanup:
    if (detail != NULL) HeapFree(GetProcessHeap(), 0u, detail);
    SetupDiDestroyDeviceInfoList(deviceInfo);
    if (device == INVALID_HANDLE_VALUE) SetLastError(error);
    return device;
}

static int print_driver_info(HANDLE device, LDAC_NATIVE_DEVICE_INFO *deviceInfo) {
    LDAC_NATIVE_ABI_VERSION version;
    DWORD bytes = 0u;

    ZeroMemory(&version, sizeof(version));
    if (!run_ioctl(device,
                   IOCTL_LDAC_NATIVE_GET_VERSION,
                   NULL,
                   0u,
                   &version,
                   sizeof(version),
                   3000u,
                   &bytes)) {
        print_win32_error(L"GET_VERSION", GetLastError());
        return 0;
    }
    if (bytes < sizeof(version) || version.Size != sizeof(version)) {
        fwprintf(stderr, L"GET_VERSION returned an invalid structure.\n");
        return 0;
    }
    wprintf(L"Driver ABI: %lu.%lu, flags 0x%08lX\n",
            version.Major,
            version.Minor,
            version.Flags);
    if (version.Major != LDAC_NATIVE_ABI_MAJOR ||
        version.Minor < LDAC_NATIVE_ABI_MINOR) {
        fwprintf(stderr,
                 L"Driver ABI is incompatible; probe needs %u.%u or newer.\n",
                 LDAC_NATIVE_ABI_MAJOR,
                 LDAC_NATIVE_ABI_MINOR);
        return 0;
    }

    ZeroMemory(deviceInfo, sizeof(*deviceInfo));
    if (!run_ioctl(device,
                   IOCTL_LDAC_NATIVE_GET_DEVICE_INFO,
                   NULL,
                   0u,
                   deviceInfo,
                   sizeof(*deviceInfo),
                   3000u,
                   &bytes)) {
        print_win32_error(L"GET_DEVICE_INFO", GetLastError());
        return 0;
    }
    if (bytes < sizeof(*deviceInfo) ||
        deviceInfo->Size != sizeof(*deviceInfo)) {
        fwprintf(stderr, L"GET_DEVICE_INFO returned an invalid structure.\n");
        return 0;
    }

    wprintf(L"Ready flags: 0x%08lX\n", deviceInfo->Flags);
    wprintf(L"Remote Bluetooth address: %012llX\n",
            (unsigned long long)deviceInfo->RemoteBluetoothAddress);
    wprintf(L"Local Bluetooth address:  %012llX\n",
            (unsigned long long)deviceInfo->LocalBluetoothAddress);
    wprintf(L"AVDTP signaling PSM: 0x%04X\n", deviceInfo->SignalingPsm);
    return 1;
}

static int write_signaling(HANDLE device,
                           uint8_t *packet,
                           DWORD packetSize,
                           DWORD timeoutMs) {
    LDAC_NATIVE_SIGNALING_TRANSFER_REQUEST transfer;
    DWORD bytes = 0u;

    transfer.Size = sizeof(transfer);
    transfer.TimeoutMs = timeoutMs;
    transfer.Flags = 0u;
    if (!run_ioctl(device,
                   IOCTL_LDAC_NATIVE_WRITE_SIGNALING,
                   &transfer,
                   sizeof(transfer),
                   packet,
                   packetSize,
                   timeoutMs + 2000u,
                   &bytes)) {
        print_win32_error(L"WRITE_SIGNALING", GetLastError());
        return 0;
    }
    if (bytes != packetSize) {
        fwprintf(stderr,
                 L"WRITE_SIGNALING transferred %lu of %lu bytes.\n",
                 bytes,
                 packetSize);
        return 0;
    }
    return 1;
}

static int write_media(HANDLE device,
                       uint8_t *packet,
                       DWORD packetSize,
                       DWORD timeoutMs) {
    LDAC_NATIVE_SIGNALING_TRANSFER_REQUEST transfer;
    DWORD bytes = 0u;

    transfer.Size = sizeof(transfer);
    transfer.TimeoutMs = timeoutMs;
    transfer.Flags = 0u;
    if (!run_ioctl(device,
                   IOCTL_LDAC_NATIVE_WRITE_MEDIA,
                   &transfer,
                   sizeof(transfer),
                   packet,
                   packetSize,
                   timeoutMs + 2000u,
                   &bytes)) {
        print_win32_error(L"WRITE_MEDIA", GetLastError());
        print_transfer_diagnostics(device);
        return 0;
    }
    if (bytes != packetSize) {
        fwprintf(stderr,
                 L"WRITE_MEDIA transferred %lu of %lu bytes.\n",
                 bytes,
                 packetSize);
        print_transfer_diagnostics(device);
        return 0;
    }
    return 1;
}

static void print_packet(const wchar_t *direction,
                         const uint8_t *packet,
                         DWORD packetSize) {
    DWORD index;

    wprintf(L"%ls AVDTP (%lu bytes):", direction, packetSize);
    for (index = 0u; index < packetSize; ++index) {
        wprintf(L" %02X", packet[index]);
    }
    wprintf(L"\n");
}

static void print_transfer_result(
    const wchar_t *name,
    const LDAC_NATIVE_TRANSFER_RESULT *result) {
    wprintf(L"%ls diagnostics: sequence %lu, operation %lu, "
            L"I/O status 0x%08lX, BRB status 0x%08lX, "
            L"Bluetooth status 0x%02lX, requested %lu, "
            L"BRB buffer %lu, remaining %lu, flags 0x%08lX\n",
            name,
            result->Sequence,
            result->Operation,
            (unsigned long)result->IoStatus,
            (unsigned long)result->BrbStatus,
            result->BtStatus,
            result->RequestedBytes,
            result->BrbBufferSize,
            result->RemainingBytes,
            result->TransferFlags);
}

static void print_transfer_diagnostics(HANDLE device) {
    LDAC_NATIVE_TRANSFER_DIAGNOSTICS diagnostics;
    DWORD bytes = 0u;

    ZeroMemory(&diagnostics, sizeof(diagnostics));
    if (!run_ioctl(device,
                   IOCTL_LDAC_NATIVE_GET_TRANSFER_DIAGNOSTICS,
                   NULL,
                   0u,
                   &diagnostics,
                   sizeof(diagnostics),
                   3000u,
                   &bytes)) {
        print_win32_error(L"GET_TRANSFER_DIAGNOSTICS", GetLastError());
        return;
    }
    if (bytes < sizeof(diagnostics) ||
        diagnostics.Size != sizeof(diagnostics)) {
        fwprintf(stderr,
                 L"GET_TRANSFER_DIAGNOSTICS returned an invalid structure.\n");
        return;
    }
    print_transfer_result(L"Read", &diagnostics.Read);
    print_transfer_result(L"Write", &diagnostics.Write);
    print_transfer_result(L"Media write", &diagnostics.MediaWrite);
}

static int build_peer_command_response(const uint8_t *packet,
                                       DWORD packetSize,
                                       uint8_t *response,
                                       DWORD responseCapacity,
                                       DWORD *responseSize) {
    avdtp_header header;
    avdtp_message_type messageType = AVDTP_MESSAGE_ACCEPT;
    uint8_t payload[14];
    size_t payloadSize = 0u;
    size_t written;

    if (packet == NULL || response == NULL || responseSize == NULL ||
        avdtp_parse_header(packet, packetSize, &header) != AVDTP_OK ||
        header.packet_type != AVDTP_PACKET_SINGLE ||
        header.message_type != AVDTP_MESSAGE_COMMAND ||
        header.payload_offset > packetSize) {
        return 0;
    }

    switch (header.signal_id) {
        case AVDTP_SIGNAL_DISCOVER:
            if (header.payload_offset != packetSize) return 0;
            payload[0] = (uint8_t)((LDAC_PROBE_LOCAL_SEID << 2u) | 0x02u);
            payload[1] = 0x00u;
            payloadSize = 2u;
            break;
        case AVDTP_SIGNAL_GET_CAPABILITIES:
        case AVDTP_SIGNAL_GET_ALL_CAPABILITIES:
            if (header.payload_offset + 1u != packetSize ||
                (packet[header.payload_offset] >> 2u) != LDAC_PROBE_LOCAL_SEID) {
                return 0;
            }
            payload[0] = AVDTP_SERVICE_MEDIA_TRANSPORT;
            payload[1] = 0x00u;
            payload[2] = AVDTP_SERVICE_MEDIA_CODEC;
            payload[3] = 0x0Au;
            payload[4] = (uint8_t)(AVDTP_MEDIA_TYPE_AUDIO << 4u);
            payload[5] = AVDTP_CODEC_VENDOR;
            ldac_build_codec_info(payload + 6u, LDAC_SF_ALL, LDAC_CM_STEREO);
            payloadSize = sizeof(payload);
            break;
        case AVDTP_SIGNAL_SET_CONFIGURATION:
            if (header.payload_offset + 4u > packetSize ||
                (packet[header.payload_offset] >> 2u) != LDAC_PROBE_LOCAL_SEID) {
                return 0;
            }
            payload[0] = 0x00u;
            payload[1] = LDAC_PROBE_PEER_ERROR_SEP_IN_USE;
            payloadSize = 2u;
            messageType = AVDTP_MESSAGE_REJECT;
            break;
        case AVDTP_SIGNAL_CLOSE:
            if (header.payload_offset + 1u != packetSize ||
                (packet[header.payload_offset] >> 2u) != LDAC_PROBE_LOCAL_SEID) {
                return 0;
            }
            payloadSize = 0u;
            break;
        default:
            return 0;
    }

    written = avdtp_write_single(response,
                                 responseCapacity,
                                 header.transaction_label,
                                 messageType,
                                 header.signal_id,
                                 payload,
                                 payloadSize);
    if (written == 0u) return 0;
    *responseSize = (DWORD)written;
    return 1;
}

static int exchange_signaling_once(HANDLE device,
                                   uint8_t *outbound,
                                   DWORD outboundSize,
                                   uint8_t *response,
                                   DWORD responseCapacity,
                                   DWORD timeoutMs,
                                   DWORD *responseSize) {
    LDAC_NATIVE_SIGNALING_TRANSFER_REQUEST transfer;
    OVERLAPPED readOverlapped;
    DWORD immediateBytes = 0u;
    DWORD transferred = 0u;
    DWORD error = ERROR_SUCCESS;
    DWORD waitResult;
    BOOL started;
    BOOL completed;

    transfer.Size = sizeof(transfer);
    transfer.TimeoutMs = timeoutMs;
    transfer.Flags = 0u;

    ZeroMemory(&readOverlapped, sizeof(readOverlapped));
    readOverlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (readOverlapped.hEvent == NULL) return 0;

    started = DeviceIoControl(device,
                              IOCTL_LDAC_NATIVE_READ_SIGNALING,
                              &transfer,
                              sizeof(transfer),
                              response,
                              responseCapacity,
                              &immediateBytes,
                              &readOverlapped);
    if (!started && GetLastError() != ERROR_IO_PENDING) {
        error = GetLastError();
        CloseHandle(readOverlapped.hEvent);
        print_win32_error(L"READ_SIGNALING submit", error);
        print_transfer_diagnostics(device);
        return 0;
    }

    print_packet(L"TX", outbound, outboundSize);
    if (!write_signaling(device, outbound, outboundSize, timeoutMs)) {
        (void)CancelIoEx(device, &readOverlapped);
        (void)WaitForSingleObject(readOverlapped.hEvent, INFINITE);
        (void)GetOverlappedResult(device,
                                  &readOverlapped,
                                  &transferred,
                                  FALSE);
        CloseHandle(readOverlapped.hEvent);
        print_transfer_diagnostics(device);
        return 0;
    }

    waitResult = WaitForSingleObject(readOverlapped.hEvent,
                                     timeoutMs + 2000u);
    if (waitResult == WAIT_TIMEOUT) {
        (void)CancelIoEx(device, &readOverlapped);
        (void)WaitForSingleObject(readOverlapped.hEvent, INFINITE);
        (void)GetOverlappedResult(device,
                                  &readOverlapped,
                                  &transferred,
                                  FALSE);
        CloseHandle(readOverlapped.hEvent);
        print_win32_error(L"READ_SIGNALING", ERROR_TIMEOUT);
        print_transfer_diagnostics(device);
        return 0;
    }
    if (waitResult != WAIT_OBJECT_0) {
        error = GetLastError();
        (void)CancelIoEx(device, &readOverlapped);
        (void)WaitForSingleObject(readOverlapped.hEvent, INFINITE);
        CloseHandle(readOverlapped.hEvent);
        print_win32_error(L"READ_SIGNALING wait", error);
        print_transfer_diagnostics(device);
        return 0;
    }

    completed = GetOverlappedResult(device,
                                    &readOverlapped,
                                    &transferred,
                                    FALSE);
    if (!completed) error = GetLastError();
    CloseHandle(readOverlapped.hEvent);
    if (!completed) {
        print_win32_error(L"READ_SIGNALING", error);
        print_transfer_diagnostics(device);
        return 0;
    }
    *responseSize = transferred;
    print_packet(L"RX", response, transferred);
    return 1;
}

static int exchange_signaling(HANDLE device,
                              uint8_t *command,
                              DWORD commandSize,
                              uint8_t *response,
                              DWORD responseCapacity,
                              DWORD timeoutMs,
                              DWORD *responseSize) {
    uint8_t outbound[LDAC_NATIVE_MAX_SIGNALING_TRANSFER];
    DWORD outboundSize = commandSize;
    DWORD peerCommands = 0u;
    avdtp_header header;

    if (command == NULL || commandSize == 0u ||
        commandSize > sizeof(outbound)) {
        return 0;
    }
    memcpy(outbound, command, commandSize);

    for (;;) {
        if (!exchange_signaling_once(device,
                                     outbound,
                                     outboundSize,
                                     response,
                                     responseCapacity,
                                     timeoutMs,
                                     responseSize)) {
            return 0;
        }
        if (avdtp_parse_header(response, *responseSize, &header) != AVDTP_OK ||
            header.packet_type != AVDTP_PACKET_SINGLE ||
            header.message_type != AVDTP_MESSAGE_COMMAND) {
            return 1;
        }
        if (peerCommands >= LDAC_PROBE_MAX_PEER_COMMANDS) {
            fwprintf(stderr,
                     L"Too many inbound AVDTP commands; aborting exchange.\n");
            return 0;
        }
        if (!build_peer_command_response(response,
                                         *responseSize,
                                         outbound,
                                         sizeof(outbound),
                                         &outboundSize)) {
            fwprintf(stderr,
                     L"Unsupported inbound AVDTP command signal 0x%02X; "
                     L"aborting exchange.\n",
                     header.signal_id);
            return 0;
        }
        ++peerCommands;
        wprintf(L"Answering inbound AVDTP command label=%u signal=0x%02X.\n",
                header.transaction_label,
                header.signal_id);
    }
}

static int parse_expected_response(const uint8_t *packet,
                                   size_t packetSize,
                                   uint8_t expectedLabel,
                                   uint8_t expectedSignal,
                                   avdtp_header *header) {
    avdtp_status status = avdtp_parse_header(packet, packetSize, header);
    if (status != AVDTP_OK) {
        fwprintf(stderr, L"Invalid AVDTP response (%d).\n", (int)status);
        return 0;
    }
    if (header->packet_type != AVDTP_PACKET_SINGLE) {
        fwprintf(stderr,
                 L"Fragmented AVDTP responses are not supported by the probe yet.\n");
        return 0;
    }
    if (header->transaction_label != expectedLabel ||
        header->signal_id != expectedSignal) {
        fwprintf(stderr,
                 L"Unexpected AVDTP response label/signal (%u/0x%02X).\n",
                 header->transaction_label,
                 header->signal_id);
        return 0;
    }
    if (header->message_type == AVDTP_MESSAGE_REJECT ||
        header->message_type == AVDTP_MESSAGE_GENERAL_REJECT) {
        unsigned errorCode = packetSize > header->payload_offset
            ? packet[packetSize - 1u]
            : 0u;
        fwprintf(stderr,
                 L"AVDTP command 0x%02X rejected, error 0x%02X.\n",
                 expectedSignal,
                 errorCode);
        return -1;
    }
    if (header->message_type != AVDTP_MESSAGE_ACCEPT) {
        fwprintf(stderr, L"Unexpected AVDTP message type %u.\n",
                 (unsigned)header->message_type);
        return 0;
    }
    return 1;
}

static size_t print_and_collect_audio_sinks(
    const uint8_t *payload,
    size_t payloadSize,
    uint8_t *seids,
    size_t seidCapacity) {
    size_t offset;
    size_t count = 0u;

    if (payload == NULL || seids == NULL || (payloadSize % 2u) != 0u) {
        return 0u;
    }
    wprintf(L"Remote stream endpoints:\n");
    for (offset = 0u; offset < payloadSize; offset += 2u) {
        uint8_t seid = (uint8_t)(payload[offset] >> 2u);
        uint8_t inUse = (uint8_t)((payload[offset] >> 1u) & 1u);
        uint8_t mediaType = (uint8_t)(payload[offset + 1u] >> 4u);
        uint8_t endpointType = (uint8_t)((payload[offset + 1u] >> 3u) & 1u);
        wprintf(L"  SEID %u: media=%u, %ls, %ls\n",
                seid,
                mediaType,
                endpointType != 0u ? L"sink" : L"source",
                inUse != 0u ? L"in use" : L"available");
        if (seid != 0u && inUse == 0u &&
            mediaType == AVDTP_MEDIA_TYPE_AUDIO && endpointType == 1u) {
            if (count < seidCapacity) seids[count++] = seid;
        }
    }
    return count;
}

static const wchar_t *audio_codec_name(uint8_t codecType) {
    switch (codecType) {
        case 0x00u: return L"SBC";
        case 0x01u: return L"MPEG-1/2 Audio";
        case 0x02u: return L"AAC";
        case AVDTP_CODEC_VENDOR: return L"vendor-specific";
        default: return L"unknown";
    }
}

static void print_media_codec_summary(const uint8_t *capabilities,
                                      size_t capabilitiesSize) {
    size_t offset = 0u;

    while (offset < capabilitiesSize) {
        uint8_t category;
        size_t itemSize;
        const uint8_t *item;
        if (capabilitiesSize - offset < 2u) break;
        category = capabilities[offset];
        itemSize = capabilities[offset + 1u];
        offset += 2u;
        if (itemSize > capabilitiesSize - offset) break;
        item = capabilities + offset;
        if (category == AVDTP_SERVICE_MEDIA_CODEC && itemSize >= 2u) {
            uint8_t codecType = item[1];
            wprintf(L"  Media codec: %ls (type 0x%02X)",
                    audio_codec_name(codecType),
                    codecType);
            if (codecType == AVDTP_CODEC_VENDOR && itemSize >= 8u) {
                uint32_t vendorId = (uint32_t)item[2] |
                    ((uint32_t)item[3] << 8u) |
                    ((uint32_t)item[4] << 16u) |
                    ((uint32_t)item[5] << 24u);
                uint16_t codecId = (uint16_t)((uint16_t)item[6] |
                    ((uint16_t)item[7] << 8u));
                wprintf(L", vendor 0x%08lX, codec 0x%04X",
                        (unsigned long)vendorId,
                        codecId);
            }
            wprintf(L"\n");
            return;
        }
        offset += itemSize;
    }
    wprintf(L"  Media codec capability not present.\n");
}

static void print_ldac_capabilities(ldac_capabilities capabilities) {
    int first = 1;

    wprintf(L"LDAC sample rates: ");
    if ((capabilities.sample_rates & LDAC_SF_44100) != 0u) {
        wprintf(L"44.1 kHz"); first = 0;
    }
    if ((capabilities.sample_rates & LDAC_SF_48000) != 0u) {
        wprintf(first ? L"48 kHz" : L", 48 kHz"); first = 0;
    }
    if ((capabilities.sample_rates & LDAC_SF_88200) != 0u) {
        wprintf(first ? L"88.2 kHz" : L", 88.2 kHz"); first = 0;
    }
    if ((capabilities.sample_rates & LDAC_SF_96000) != 0u) {
        wprintf(first ? L"96 kHz" : L", 96 kHz"); first = 0;
    }
    if (first) wprintf(L"none");
    wprintf(L"\nLDAC channel modes: ");
    first = 1;
    if ((capabilities.channel_modes & LDAC_CM_STEREO) != 0u) {
        wprintf(L"stereo"); first = 0;
    }
    if ((capabilities.channel_modes & LDAC_CM_DUAL) != 0u) {
        wprintf(first ? L"dual" : L", dual"); first = 0;
    }
    if ((capabilities.channel_modes & LDAC_CM_MONO) != 0u) {
        wprintf(first ? L"mono" : L", mono"); first = 0;
    }
    if (first) wprintf(L"none");
    wprintf(L"\n");
}

static uint8_t ldac_sample_rate_bit_from_hz(unsigned sampleRateHz) {
    switch (sampleRateHz) {
        case 44100u: return LDAC_SF_44100;
        case 48000u: return LDAC_SF_48000;
        case 88200u: return LDAC_SF_88200;
        case 96000u: return LDAC_SF_96000;
        default: return 0u;
    }
}

static int exchange_seid_command(HANDLE device,
                                 DWORD readCapacity,
                                 uint8_t remoteSeid,
                                 uint8_t signal,
                                 uint8_t *label) {
    uint8_t command[8];
    uint8_t response[LDAC_NATIVE_MAX_SIGNALING_TRANSFER];
    uint8_t payload = (uint8_t)(remoteSeid << 2u);
    DWORD responseSize = 0u;
    size_t commandSize;
    avdtp_header header;
    int parsed;

    commandSize = avdtp_write_single(command,
                                     sizeof(command),
                                     *label,
                                     AVDTP_MESSAGE_COMMAND,
                                     signal,
                                     &payload,
                                     1u);
    if (commandSize == 0u ||
        !exchange_signaling(device,
                            command,
                            (DWORD)commandSize,
                            response,
                            readCapacity,
                            5000u,
                            &responseSize)) {
        return 0;
    }
    parsed = parse_expected_response(response,
                                     responseSize,
                                     *label,
                                     signal,
                                     &header);
    *label = (uint8_t)((*label + 1u) & 0x0Fu);
    return parsed == 1;
}

static int wait_until_stream_sample(const LARGE_INTEGER *start,
                                    const LARGE_INTEGER *frequency,
                                    uint64_t sampleOffset,
                                    unsigned sampleRateHz) {
    LONGLONG target;

    if (start == NULL || frequency == NULL || frequency->QuadPart <= 0 ||
        sampleRateHz == 0u) {
        return 0;
    }
    target = start->QuadPart +
             (LONGLONG)((sampleOffset * (uint64_t)frequency->QuadPart) /
                        sampleRateHz);
    for (;;) {
        LARGE_INTEGER now;
        LONGLONG remaining;
        DWORD sleepMs;

        if (stop_requested()) return 1;
        if (!QueryPerformanceCounter(&now)) return 0;
        remaining = target - now.QuadPart;
        if (remaining <= 0) return 1;
        sleepMs = (DWORD)((remaining * 1000) / frequency->QuadPart);
        if (sleepMs > 1u) {
            Sleep(sleepMs - 1u);
        } else {
            (void)SwitchToThread();
        }
    }
}

static void fill_test_pcm(float *pcm,
                          uint64_t firstFrame,
                          probe_media_mode mediaMode,
                          unsigned sampleRateHz) {
    uint64_t toneStart =
        ((uint64_t)sampleRateHz * LDAC_TEST_TONE_START_MS) /
        1000u;
    uint64_t toneEnd =
        ((uint64_t)sampleRateHz * LDAC_TEST_TONE_END_MS) /
        1000u;
    size_t index;

    for (index = 0u; index < LDAC_ENCODER_PCM_FRAMES_PER_CALL; ++index) {
        uint64_t frame = firstFrame + index;
        float sample = 0.0f;
        if (mediaMode == PROBE_MEDIA_TONE_MQ &&
            frame >= toneStart && frame < toneEnd) {
            double phase = LDAC_TEST_TWO_PI * LDAC_TEST_TONE_HZ *
                           (double)(frame - toneStart) /
                           sampleRateHz;
            sample = (float)(LDAC_TEST_TONE_AMPLITUDE * sin(phase));
        }
        pcm[index * 2u] = sample;
        pcm[index * 2u + 1u] = sample;
    }
}

static int stream_ldac_test(HANDLE device,
                            USHORT outgoingMtu,
                            probe_media_mode mediaMode,
                            unsigned sampleRateHz,
                            wasapi_loopback *loopback,
                            native_pcm_source *endpointSource,
                            ldac_encoder_quality quality,
                            ldac_encoder_channel_mode channelMode,
                            int automaticQuality,
                            uint64_t linkSessionId,
                            int *linkStateReporting) {
    float pcm[LDAC_ENCODER_PCM_FRAMES_PER_CALL *
              LDAC_ENCODER_STEREO_CHANNELS];
    uint8_t encoded[LDAC_ENCODER_MAX_OUTPUT_BYTES];
    uint8_t packet[LDAC_NATIVE_MAX_MEDIA_TRANSFER];
    ldac_encoder *encoder = NULL;
    ldac_encoder_status encoderStatus;
    LARGE_INTEGER streamStart;
    LARGE_INTEGER pacingStart;
    LARGE_INTEGER end;
    LARGE_INTEGER frequency;
    LARGE_INTEGER telemetryStart;
    uint64_t targetSamples;
    uint64_t sentSamples = 0u;
    uint64_t pcmFramesFed = 0u;
    uint64_t encodedBytes = 0u;
    uint64_t transportBytes = 0u;
    uint64_t telemetryEncodedBytes = 0u;
    uint64_t telemetryTransportBytes = 0u;
    uint64_t telemetryWriteTicks = 0u;
    uint64_t telemetryMaxWriteTicks = 0u;
    uint64_t telemetryMaxLagTicks = 0u;
    uint32_t timestamp = 0u;
    uint32_t ssrc = 0x4C444143u ^ GetTickCount();
    uint16_t sequence = 0u;
    size_t encoderPayloadMtu;
    unsigned packetCount = 0u;
    unsigned telemetryPacketCount = 0u;
    unsigned telemetryLatePacketCount = 0u;
    unsigned telemetrySlowWriteCount = 0u;
    uint64_t telemetryEndpointSilenceFrames = 0u;
    uint64_t telemetryLoopbackSilenceFrames = 0u;
    double telemetryPcmPeak = 0.0;
    double telemetryPcmSquareSum = 0.0;
    uint64_t telemetryPcmSampleCount = 0u;
    unsigned samplesPerFrame;
    ldac_abr_controller abrController;
    int pacingAligned = !media_mode_uses_live_source(mediaMode);
    int continuous = media_mode_is_continuous(mediaMode);
    unsigned durationMs = continuous
        ? 0u
        : (mediaMode == PROBE_MEDIA_SYSTEM_MQ
            ? LDAC_TEST_SYSTEM_DURATION_MS
            : (mediaMode == PROBE_MEDIA_TONE_MQ
                ? LDAC_TEST_TONE_DURATION_MS
                : LDAC_TEST_SILENCE_DURATION_MS));
    const wchar_t *streamName = media_mode_uses_native_endpoint(mediaMode)
        ? L"Native LDAC endpoint audio"
        : (media_mode_uses_wasapi(mediaMode)
            ? L"system audio"
            : (mediaMode == PROBE_MEDIA_TONE_MQ ? L"tone" : L"silence"));
    int result = 0;

    if (outgoingMtu <= LDAC_RTP_OVERHEAD) return 0;
    encoderPayloadMtu = outgoingMtu - LDAC_RTP_OVERHEAD;
    if (encoderPayloadMtu < LDAC_ENCODER_MIN_PAYLOAD_MTU) {
        fwprintf(stderr,
                 L"Media MTU %u leaves only %zu bytes for LDAC frames; "
                 L"libldac needs at least %u. No media was sent.\n",
                 outgoingMtu,
                 encoderPayloadMtu,
                 LDAC_ENCODER_MIN_PAYLOAD_MTU);
        return 0;
    }
    if (media_mode_uses_native_endpoint(mediaMode)) {
        native_pcm_source_snapshot snapshot;
        native_pcm_source_status sourceStatus =
            native_pcm_source_get_snapshot(endpointSource, &snapshot);
        if (sourceStatus != NATIVE_PCM_SOURCE_OK) {
            fwprintf(stderr,
                     L"Could not confirm the Native endpoint RUN state "
                     L"before creating the encoder (status %d, Win32 "
                     L"%lu).\n",
                     (int)sourceStatus,
                     native_pcm_source_last_error(endpointSource));
            return 0;
        }
        if (!snapshot.stream_active) {
            wprintf(L"Native endpoint is no longer in RUN; no LDAC encoder "
                    L"was created.\n");
            return 1;
        }
    }
    encoderStatus = ldac_encoder_create_with_channel_mode(
        &encoder,
        encoderPayloadMtu,
        quality,
        sampleRateHz,
        channelMode);
    if (encoderStatus != LDAC_ENCODER_OK || encoder == NULL) {
        fwprintf(stderr,
                 L"libldac initialization failed (status %d).\n",
                 (int)encoderStatus);
        return 0;
    }
    samplesPerFrame = ldac_encoder_samples_per_transport_frame(encoder);
    targetSamples = continuous
        ? 0u
        : ((uint64_t)sampleRateHz * durationMs) / 1000u;
    if (!QueryPerformanceFrequency(&frequency) ||
        !QueryPerformanceCounter(&streamStart)) {
        fwprintf(stderr, L"High-resolution clock is unavailable.\n");
        goto cleanup;
    }
    pacingStart = streamStart;
    telemetryStart = streamStart;
    ldac_abr_init(&abrController, quality);

    if (continuous) {
        wprintf(L"Streaming encoded %ls continuously: %u Hz %ls, %ls%ls "
                L"(%u kbps nominal), media MTU %u. Press Ctrl+C to stop.\n",
                streamName,
                sampleRateHz,
                channel_mode_name(channelMode),
                quality_name(quality),
                automaticQuality ? L"/auto" : L"",
                ldac_encoder_nominal_bitrate_kbps(encoder),
                outgoingMtu);
    } else {
        wprintf(L"Streaming %u ms of encoded %ls: %u Hz %ls, %ls "
                L"(%u kbps nominal), media MTU %u.\n",
                durationMs,
                streamName,
                sampleRateHz,
                channel_mode_name(channelMode),
                quality_name(quality),
                ldac_encoder_nominal_bitrate_kbps(encoder),
                outgoingMtu);
    }
    if (mediaMode == PROBE_MEDIA_TONE_MQ) {
        wprintf(L"Tone window: 500-2500 ms, 440 Hz, -30 dBFS, centered.\n");
    }
    while (continuous || sentSamples < targetSamples) {
        size_t encodedSize = 0u;
        size_t packetSize = 0u;
        uint8_t frameCount = 0u;
        ldac_rtp_status packetStatus;
        LARGE_INTEGER writeStart;
        LARGE_INTEGER writeEnd;
        uint64_t scheduledTicks;
        uint64_t lagTicks;
        uint64_t writeTicks;
        uint64_t packetDurationTicks;

        if (stop_requested()) {
            wprintf(L"Stop requested; releasing the LDAC stream normally.\n");
            result = 1;
            goto cleanup;
        }
        if (media_mode_uses_wasapi(mediaMode)) {
            size_t framesRead = 0u;
            wasapi_loopback_status captureStatus =
                wasapi_loopback_read_f32_stereo(
                    loopback,
                    pcm,
                    LDAC_ENCODER_PCM_FRAMES_PER_CALL,
                    2000u,
                    &framesRead);
            if (captureStatus == WASAPI_LOOPBACK_TIMEOUT ||
                (captureStatus == WASAPI_LOOPBACK_OK &&
                 framesRead != LDAC_ENCODER_PCM_FRAMES_PER_CALL)) {
                // The render endpoint can go idle (for example the player
                // pauses). Treat a capture timeout as silence and keep the
                // LDAC media session alive so playback resumes when audio
                // returns.
                ZeroMemory(pcm, sizeof(pcm));
                framesRead = LDAC_ENCODER_PCM_FRAMES_PER_CALL;
                telemetryLoopbackSilenceFrames += framesRead;
            } else if (captureStatus != WASAPI_LOOPBACK_OK) {
                fwprintf(stderr,
                         L"WASAPI loopback read failed (status %d, "
                         L"HRESULT 0x%08lX, frames %zu). Keep audio "
                         L"playing before starting the test.\n",
                         (int)captureStatus,
                         (unsigned long)wasapi_loopback_last_hresult(loopback),
                         framesRead);
                goto cleanup;
            }
        } else if (media_mode_uses_native_endpoint(mediaMode)) {
            size_t framesRead = 0u;
            native_pcm_source_status sourceStatus =
                native_pcm_source_read_f32_stereo(
                    endpointSource,
                    pcm,
                    LDAC_ENCODER_PCM_FRAMES_PER_CALL,
                    0u,
                    &framesRead);
            if (sourceStatus == NATIVE_PCM_SOURCE_TIMEOUT) {
                native_pcm_source_snapshot snapshot;
                if (native_pcm_source_get_snapshot(
                        endpointSource,
                        &snapshot) == NATIVE_PCM_SOURCE_OK) {
                    if (!snapshot.stream_active) {
                        wprintf(L"Native endpoint left RUN; stopping the "
                                L"LDAC media session.\n");
                        result = 1;
                        goto cleanup;
                    }
                    sourceStatus = native_pcm_source_read_f32_stereo(
                        endpointSource,
                        pcm,
                        LDAC_ENCODER_PCM_FRAMES_PER_CALL,
                        100u,
                        &framesRead);
                }
            }
            if (sourceStatus == NATIVE_PCM_SOURCE_TIMEOUT) {
                ZeroMemory(pcm, sizeof(pcm));
                framesRead = LDAC_ENCODER_PCM_FRAMES_PER_CALL;
                telemetryEndpointSilenceFrames += framesRead;
            } else if (sourceStatus != NATIVE_PCM_SOURCE_OK ||
                       framesRead != LDAC_ENCODER_PCM_FRAMES_PER_CALL) {
                fwprintf(stderr,
                         L"Native endpoint PCM read failed (status %d, "
                         L"Win32 %lu, frames %zu).\n",
                         (int)sourceStatus,
                         native_pcm_source_last_error(endpointSource),
                         framesRead);
                goto cleanup;
            }
        } else {
            fill_test_pcm(pcm,
                          pcmFramesFed,
                          mediaMode,
                          sampleRateHz);
        }
        if (media_mode_uses_native_endpoint(mediaMode)) {
            size_t sampleIndex;
            const size_t pcmSampleCount =
                LDAC_ENCODER_PCM_FRAMES_PER_CALL *
                LDAC_ENCODER_STEREO_CHANNELS;
            for (sampleIndex = 0u;
                 sampleIndex < pcmSampleCount;
                 ++sampleIndex) {
                const double sample = (double)pcm[sampleIndex];
                const double magnitude = fabs(sample);
                if (magnitude > telemetryPcmPeak) {
                    telemetryPcmPeak = magnitude;
                }
                telemetryPcmSquareSum += sample * sample;
            }
            telemetryPcmSampleCount += (uint64_t)pcmSampleCount;
        }
        encoderStatus = ldac_encoder_encode_f32(
            encoder,
            pcm,
            LDAC_ENCODER_PCM_FRAMES_PER_CALL,
            encoded,
            sizeof(encoded),
            &encodedSize,
            &frameCount);
        pcmFramesFed += LDAC_ENCODER_PCM_FRAMES_PER_CALL;
        if (encoderStatus != LDAC_ENCODER_OK) {
            fwprintf(stderr,
                     L"libldac encode failed (status %d, error 0x%X).\n",
                     (int)encoderStatus,
                     ldac_encoder_last_error(encoder));
            goto cleanup;
        }
        if (encodedSize == 0u) continue;
        packetStatus = ldac_rtp_build_unfragmented(
            packet,
            sizeof(packet),
            outgoingMtu,
            sequence,
            timestamp,
            ssrc,
            frameCount,
            encoded,
            encodedSize,
            &packetSize);
        if (packetStatus != LDAC_RTP_OK) {
            fwprintf(stderr,
                     L"RTP/LDAC packetization failed (status %d, "
                     L"encoded %zu bytes, MTU %u).\n",
                     (int)packetStatus,
                     encodedSize,
                     outgoingMtu);
            goto cleanup;
        }
        if (!pacingAligned) {
            LARGE_INTEGER alignNow;
            if (!QueryPerformanceCounter(&alignNow)) goto cleanup;
            pacingStart.QuadPart = alignNow.QuadPart -
                (LONGLONG)((sentSamples *
                    (uint64_t)frequency.QuadPart) / sampleRateHz);
            pacingAligned = 1;
        }
        scheduledTicks = (uint64_t)pacingStart.QuadPart +
            (sentSamples * (uint64_t)frequency.QuadPart) / sampleRateHz;
        if (!wait_until_stream_sample(&pacingStart,
                                      &frequency,
                                      sentSamples,
                                      sampleRateHz)) {
            goto cleanup;
        }
        if (!QueryPerformanceCounter(&writeStart)) goto cleanup;
        lagTicks = writeStart.QuadPart > (LONGLONG)scheduledTicks
            ? (uint64_t)(writeStart.QuadPart - (LONGLONG)scheduledTicks)
            : 0u;
        if (!write_media(device,
                         packet,
                         (DWORD)packetSize,
                         LDAC_NATIVE_DEFAULT_TRANSFER_TIMEOUT_MS)) {
            goto cleanup;
        }
        if (!QueryPerformanceCounter(&writeEnd)) goto cleanup;
        writeTicks = (uint64_t)(writeEnd.QuadPart - writeStart.QuadPart);
        packetDurationTicks =
            ((uint64_t)frameCount * samplesPerFrame *
             (uint64_t)frequency.QuadPart) / sampleRateHz;
        encodedBytes += encodedSize;
        transportBytes += packetSize;
        telemetryEncodedBytes += encodedSize;
        telemetryTransportBytes += packetSize;
        telemetryWriteTicks += writeTicks;
        if (writeTicks > telemetryMaxWriteTicks) {
            telemetryMaxWriteTicks = writeTicks;
        }
        if (lagTicks > telemetryMaxLagTicks) {
            telemetryMaxLagTicks = lagTicks;
        }
        if (lagTicks * 1000000u >
            (uint64_t)frequency.QuadPart * 2000u) {
            telemetryLatePacketCount++;
        }
        if (packetDurationTicks != 0u &&
            writeTicks * 100u >= packetDurationTicks * 80u) {
            telemetrySlowWriteCount++;
        }
        packetCount++;
        telemetryPacketCount++;
        sequence++;
        timestamp += frameCount * samplesPerFrame;
        sentSamples += frameCount * samplesPerFrame;
        if (continuous) {
            LARGE_INTEGER telemetryNow;
            if (!QueryPerformanceCounter(&telemetryNow)) goto cleanup;
            {
                double windowSeconds =
                    (double)(telemetryNow.QuadPart - telemetryStart.QuadPart) /
                    (double)frequency.QuadPart;
                if (windowSeconds >= 1.0) {
                    double elapsedSeconds =
                        (double)(telemetryNow.QuadPart - streamStart.QuadPart) /
                        (double)frequency.QuadPart;
                    double payloadKbps =
                        ((double)telemetryEncodedBytes * 8.0) /
                        windowSeconds / 1000.0;
                    double transportKbps =
                        ((double)telemetryTransportBytes * 8.0) /
                        windowSeconds / 1000.0;
                    double averageWriteMs = telemetryPacketCount != 0u
                        ? ((double)telemetryWriteTicks * 1000.0) /
                          (double)frequency.QuadPart /
                          telemetryPacketCount
                        : 0.0;
                    double maxWriteMs =
                        ((double)telemetryMaxWriteTicks * 1000.0) /
                        (double)frequency.QuadPart;
                    double maxLagMs =
                        ((double)telemetryMaxLagTicks * 1000.0) /
                        (double)frequency.QuadPart;
                    wprintf(L"Live: %.1f kbps LDAC, %.1f kbps with RTP, "
                            L"%u packets/%.2f s, %ls; write avg/max "
                            L"%.2f/%.2f ms, pace lag max %.2f ms, "
                            L"capture-late/slow-write "
                            L"%u/%u, elapsed %.1f s.\n",
                            payloadKbps,
                            transportKbps,
                            telemetryPacketCount,
                            windowSeconds,
                            quality_name(quality),
                            averageWriteMs,
                            maxWriteMs,
                            maxLagMs,
                            telemetryLatePacketCount,
                            telemetrySlowWriteCount,
                            elapsedSeconds);
                    if (media_mode_uses_wasapi(mediaMode) &&
                        telemetryLoopbackSilenceFrames != 0u) {
                        wprintf(L"loopback silence fill %llu frames\n",
                                (unsigned long long)
                                    telemetryLoopbackSilenceFrames);
                    }
                    if (media_mode_uses_native_endpoint(mediaMode)) {
                        native_pcm_source_snapshot snapshot;
                        if (native_pcm_source_get_snapshot(
                                endpointSource,
                                &snapshot) == NATIVE_PCM_SOURCE_OK) {
                            wprintf(L"Source: epoch %llu, %ls, buffer "
                                    L"%u/%u bytes, driver dropped %llu "
                                    L"bytes, silence fill %llu frames",
                                    (unsigned long long)snapshot.stream_epoch,
                                    snapshot.stream_active
                                        ? L"active"
                                        : L"idle",
                                    snapshot.available_bytes,
                                    snapshot.capacity_bytes,
                                    (unsigned long long)
                                        snapshot.total_bytes_dropped,
                                    (unsigned long long)
                                        telemetryEndpointSilenceFrames);
                            if (snapshot.volume_control_available) {
                                wprintf(L", volume %.0f%%%ls",
                                        snapshot.volume_scalar * 100.0f,
                                        snapshot.muted
                                            ? L" (muted)"
                                            : L"");
                            } else {
                                wprintf(L", volume control unavailable");
                            }
                            wprintf(L".\n");
                            if (snapshot.volume_control_available) {
                                const double pcmRms =
                                    telemetryPcmSampleCount != 0u
                                        ? sqrt(telemetryPcmSquareSum /
                                               (double)
                                                   telemetryPcmSampleCount)
                                        : 0.0;
                                const double peakDbfs =
                                    telemetryPcmPeak > 0.000001
                                        ? 20.0 * log10(telemetryPcmPeak)
                                        : -120.0;
                                const double rmsDbfs = pcmRms > 0.000001
                                    ? 20.0 * log10(pcmRms)
                                    : -120.0;
                                const double endpointGain = snapshot.muted
                                    ? 0.0
                                    : pow(10.0,
                                          (double)snapshot.volume_db /
                                              20.0);
                                wprintf(L"Audio level: endpoint %.1f dB, "
                                        L"PCM gain %.6f, post-gain "
                                        L"peak/RMS %.1f/%.1f dBFS.\n",
                                        (double)snapshot.volume_db,
                                        endpointGain,
                                        peakDbfs,
                                        rmsDbfs);
                            }
                        }
                        if (linkStateReporting != NULL &&
                            *linkStateReporting != 0) {
                            native_pcm_source_status linkStatus =
                                native_pcm_source_report_link_state(
                                    endpointSource,
                                    NATIVE_PCM_LINK_CONNECTED,
                                    linkSessionId);
                            if (linkStatus != NATIVE_PCM_SOURCE_OK) {
                                fwprintf(stderr,
                                         L"Native endpoint link-state "
                                         L"heartbeat failed (status %d, "
                                         L"Win32 %lu); audio will "
                                         L"continue.\n",
                                         (int)linkStatus,
                                         native_pcm_source_last_error(
                                             endpointSource));
                                *linkStateReporting = 0;
                            }
                        }
                    }
                    if (automaticQuality) {
                        ldac_abr_window abrWindow;
                        ldac_abr_decision decision;
                        ldac_encoder_quality previousQuality = quality;

                        abrWindow.total_packets = telemetryPacketCount;
                        abrWindow.late_packets = telemetryLatePacketCount;
                        abrWindow.slow_write_packets =
                            telemetrySlowWriteCount;
                        abrWindow.max_schedule_lag_us =
                            (telemetryMaxLagTicks * 1000000u) /
                            (uint64_t)frequency.QuadPart;
                        abrWindow.max_write_us =
                            (telemetryMaxWriteTicks * 1000000u) /
                            (uint64_t)frequency.QuadPart;
                        decision = ldac_abr_update(&abrController, &abrWindow);
                        if (decision.changed) {
                            encoderStatus = ldac_encoder_set_quality(
                                encoder,
                                decision.quality);
                            if (encoderStatus != LDAC_ENCODER_OK) {
                                fwprintf(stderr,
                                         L"ABR quality switch failed "
                                         L"(status %d, error 0x%X).\n",
                                         (int)encoderStatus,
                                         ldac_encoder_last_error(encoder));
                                goto cleanup;
                            }
                            quality = decision.quality;
                            wprintf(L"ABR: %ls -> %ls (%ls).\n",
                                    quality_name(previousQuality),
                                    quality_name(quality),
                                    decision.reason ==
                                            LDAC_ABR_REASON_CONGESTION
                                        ? L"transport congestion"
                                        : L"clean link recovery");
                            pacingStart.QuadPart =
                                telemetryNow.QuadPart -
                                (LONGLONG)(((sentSamples -
                                    ((uint64_t)frameCount * samplesPerFrame)) *
                                    (uint64_t)frequency.QuadPart) /
                                    sampleRateHz);
                        }
                    }
                    telemetryStart = telemetryNow;
                    telemetryEncodedBytes = 0u;
                    telemetryTransportBytes = 0u;
                    telemetryWriteTicks = 0u;
                    telemetryMaxWriteTicks = 0u;
                    telemetryMaxLagTicks = 0u;
                    telemetryPacketCount = 0u;
                    telemetryLatePacketCount = 0u;
                    telemetrySlowWriteCount = 0u;
                    telemetryEndpointSilenceFrames = 0u;
                    telemetryPcmPeak = 0.0;
                    telemetryPcmSquareSum = 0.0;
                    telemetryPcmSampleCount = 0u;
                }
            }
        }
    }
    if (!wait_until_stream_sample(&pacingStart,
                                  &frequency,
                                  sentSamples,
                                  sampleRateHz)) {
        goto cleanup;
    }
    if (!QueryPerformanceCounter(&end)) goto cleanup;
    {
        double elapsedSeconds =
            (double)(end.QuadPart - streamStart.QuadPart) /
            (double)frequency.QuadPart;
        double payloadKbps = elapsedSeconds > 0.0
            ? ((double)encodedBytes * 8.0) / elapsedSeconds / 1000.0
            : 0.0;
        double transportKbps = elapsedSeconds > 0.0
            ? ((double)transportBytes * 8.0) / elapsedSeconds / 1000.0
            : 0.0;
        wprintf(L"%ls stream complete: %u packets, %llu LDAC bytes, "
                L"%.1f kbps payload, %.1f kbps including RTP.\n",
                media_mode_uses_wasapi(mediaMode)
                    ? L"System audio"
                    : (mediaMode == PROBE_MEDIA_TONE_MQ
                        ? L"Tone"
                        : L"Silence"),
                packetCount,
                (unsigned long long)encodedBytes,
                payloadKbps,
                transportKbps);
    }
    result = 1;

cleanup:
    ldac_encoder_destroy(encoder);
    return result;
}

static int configure_ldac_endpoint(
    HANDLE device,
    DWORD readCapacity,
    uint8_t remoteSeid,
    ldac_capabilities remoteCapabilities,
    uint8_t *label,
    probe_media_mode mediaMode,
    ldac_encoder_quality quality,
    ldac_encoder_channel_mode channelMode,
    unsigned preferredSampleRateHz,
    unsigned preferredBitsPerSample,
    int automaticQuality) {
    const ldac_capabilities localCapabilities = {
        LDAC_SF_ALL,
        channel_mode_capability(channelMode)
    };
    uint8_t command[32];
    uint8_t response[LDAC_NATIVE_MAX_SIGNALING_TRANSFER];
    uint8_t configurationPayload[16];
    LDAC_NATIVE_OPEN_SIGNALING_REQUEST mediaOpenRequest;
    LDAC_NATIVE_CHANNEL_INFO mediaChannel;
    DWORD responseSize = 0u;
    DWORD bytes = 0u;
    size_t commandSize;
    size_t configurationSize;
    avdtp_header header;
    ldac_configuration configuration;
    ldac_codec_status codecStatus;
    wasapi_loopback *loopback = NULL;
    native_pcm_source *endpointSource = NULL;
    unsigned streamSampleRateHz = LDAC_TEST_SAMPLE_RATE_HZ;
    int parsed;
    int configured = 0;
    int started = 0;
    int linkStatePropertyAvailable = 0;
    int linkStateReporting = 0;
    uint64_t linkSessionId = 0u;
    int result = 0;

    if (media_mode_uses_wasapi(mediaMode)) {
        wasapi_loopback_status captureStatus =
            wasapi_loopback_create(&loopback);

        if (captureStatus != WASAPI_LOOPBACK_OK || loopback == NULL) {
            fwprintf(stderr,
                     L"Could not initialize WASAPI loopback (status %d).\n",
                     (int)captureStatus);
            goto cleanup;
        }
        streamSampleRateHz = wasapi_loopback_sample_rate_hz(loopback);
        wprintf(L"WASAPI source: %ls, %u Hz, %u channel(s), %u-bit mix "
                L"format.\n",
                wasapi_loopback_device_name(loopback),
                streamSampleRateHz,
                wasapi_loopback_source_channels(loopback),
                wasapi_loopback_bits_per_sample(loopback));
    } else if (media_mode_uses_native_endpoint(mediaMode)) {
        native_pcm_source_status sourceStatus =
            native_pcm_source_create(&endpointSource);
        if (sourceStatus != NATIVE_PCM_SOURCE_OK || endpointSource == NULL) {
            fwprintf(stderr,
                     L"Could not open the Native LDAC virtual endpoint "
                     L"PCM source (status %d).\n",
                     (int)sourceStatus);
            goto cleanup;
        }
        native_pcm_preferred_format appliedFormat;
        sourceStatus = native_pcm_source_set_preferred_format(
            endpointSource,
            preferredSampleRateHz,
            preferredBitsPerSample,
            &appliedFormat);
        if (sourceStatus != NATIVE_PCM_SOURCE_OK) {
            fwprintf(stderr,
                     L"Could not apply the Native endpoint format %u Hz, "
                     L"%u-bit (status %d, Win32 %lu).\n",
                     preferredSampleRateHz,
                     preferredBitsPerSample,
                     (int)sourceStatus,
                     native_pcm_source_last_error(endpointSource));
            goto cleanup;
        }
        streamSampleRateHz = preferredSampleRateHz;
        wprintf(L"Native endpoint source: %ls, %u Hz, 2 channel(s), "
                L"%u-bit PCM.\n",
                native_pcm_source_interface_path(endpointSource),
                streamSampleRateHz,
                preferredBitsPerSample);
        wprintf(L"Native endpoint preferred-format revision: %u.\n",
                appliedFormat.revision);

        {
            ULONGLONG formatDeadline = GetTickCount64() + 5000u;
            native_pcm_source_snapshot snapshot;
            for (;;) {
                sourceStatus = native_pcm_source_get_snapshot(
                    endpointSource, &snapshot);
                if (sourceStatus != NATIVE_PCM_SOURCE_OK ||
                    snapshot.stream_active == 0 ||
                    (snapshot.sample_rate_hz == preferredSampleRateHz &&
                     snapshot.bits_per_sample == preferredBitsPerSample)) {
                    break;
                }
                if (GetTickCount64() >= formatDeadline || stop_requested()) {
                    break;
                }
                Sleep(50u);
            }
            if (sourceStatus != NATIVE_PCM_SOURCE_OK ||
                (snapshot.stream_active != 0 &&
                 (snapshot.sample_rate_hz != preferredSampleRateHz ||
                  snapshot.bits_per_sample != preferredBitsPerSample))) {
                fwprintf(stderr,
                         L"Windows did not reopen the active Native endpoint "
                         L"as %u Hz, %u-bit within five seconds.\n",
                         preferredSampleRateHz,
                         preferredBitsPerSample);
                goto cleanup;
            }
        }

        if (!BCRYPT_SUCCESS(BCryptGenRandom(
                NULL,
                (PUCHAR)&linkSessionId,
                (ULONG)sizeof(linkSessionId),
                BCRYPT_USE_SYSTEM_PREFERRED_RNG)) ||
            linkSessionId == 0u) {
            fwprintf(stderr,
                     L"Could not create a secure Native endpoint session "
                     L"token.\n");
            goto cleanup;
        }
        sourceStatus = native_pcm_source_report_link_state(
            endpointSource,
            NATIVE_PCM_LINK_CONNECTING,
            linkSessionId);
        if (sourceStatus == NATIVE_PCM_SOURCE_OK) {
            linkStatePropertyAvailable = 1;
            linkStateReporting = 1;
            wprintf(L"Native endpoint link state: connecting "
                    L"(session %llu).\n",
                    (unsigned long long)linkSessionId);
        } else if (sourceStatus ==
                   NATIVE_PCM_SOURCE_UNSUPPORTED_PROPERTY) {
            wprintf(L"Native endpoint link-state reporting is unavailable "
                    L"in the installed endpoint driver; audio remains "
                    L"compatible.\n");
        } else {
            fwprintf(stderr,
                     L"Native endpoint link-state reporting could not be "
                     L"started (status %d, Win32 %lu); audio will "
                     L"continue.\n",
                     (int)sourceStatus,
                     native_pcm_source_last_error(endpointSource));
        }
    }

    if (media_mode_uses_live_source(mediaMode)) {
        uint8_t sampleRateBit =
            ldac_sample_rate_bit_from_hz(streamSampleRateHz);
        if (sampleRateBit == 0u) {
            fwprintf(stderr,
                     L"The PCM source rate %u Hz needs resampling; "
                     L"supported rates are 44.1/48/88.2/96 kHz.\n",
                     streamSampleRateHz);
            goto cleanup;
        }
        if ((remoteCapabilities.sample_rates & sampleRateBit) == 0u) {
            fwprintf(stderr,
                     L"XM5 does not advertise the PCM source rate %u Hz.\n",
                     streamSampleRateHz);
            goto cleanup;
        }
    }

    codecStatus = ldac_choose_configuration(localCapabilities,
                                             remoteCapabilities,
                                             streamSampleRateHz,
                                             &configuration);
    if (codecStatus != LDAC_CODEC_OK) {
        fwprintf(stderr,
                 L"No common %u Hz-preferred LDAC configuration "
                 L"(status %d).\n",
                 streamSampleRateHz,
                 (int)codecStatus);
        goto cleanup;
    }
    if (ldac_sample_rate_to_hz(configuration.sample_rate) !=
        streamSampleRateHz) {
        fwprintf(stderr,
                 L"LDAC selected %u Hz but the PCM source is %u Hz; "
                 L"refusing an unresampled stream.\n",
                 ldac_sample_rate_to_hz(configuration.sample_rate),
                 streamSampleRateHz);
        goto cleanup;
    }
    if (configuration.channel_mode != channel_mode_capability(channelMode)) {
        fwprintf(stderr,
                 L"LDAC selected channel mode 0x%02X but %ls was requested.\n",
                 configuration.channel_mode,
                 channel_mode_name(channelMode));
        goto cleanup;
    }
    configurationSize = ldac_build_set_configuration_payload(
        configurationPayload,
        sizeof(configurationPayload),
        remoteSeid,
        1u,
        configuration);
    if (configurationSize == 0u) goto cleanup;

    wprintf(L"Testing LDAC SET_CONFIGURATION: remote SEID %u, local SEID 1, "
            L"%u Hz, %ls.\n",
            remoteSeid,
            ldac_sample_rate_to_hz(configuration.sample_rate),
            channel_mode_name(channelMode));
    commandSize = avdtp_write_single(command,
                                     sizeof(command),
                                     *label,
                                     AVDTP_MESSAGE_COMMAND,
                                     AVDTP_SIGNAL_SET_CONFIGURATION,
                                     configurationPayload,
                                     configurationSize);
    if (commandSize == 0u ||
        !exchange_signaling(device,
                            command,
                            (DWORD)commandSize,
                            response,
                            readCapacity,
                            5000u,
                            &responseSize)) {
        goto cleanup;
    }
    parsed = parse_expected_response(response,
                                     responseSize,
                                     *label,
                                     AVDTP_SIGNAL_SET_CONFIGURATION,
                                     &header);
    *label = (uint8_t)((*label + 1u) & 0x0Fu);
    if (parsed != 1) goto cleanup;
    configured = 1;
    wprintf(L"XM5 accepted LDAC SET_CONFIGURATION.\n");

    if (mediaMode != PROBE_MEDIA_NONE) {
        wprintf(L"Sending AVDTP OPEN for LDAC SEID %u...\n", remoteSeid);
        if (!exchange_seid_command(device,
                                   readCapacity,
                                   remoteSeid,
                                   AVDTP_SIGNAL_OPEN,
                                   label)) {
            goto cleanup;
        }
        wprintf(L"XM5 accepted AVDTP OPEN. Establishing Media L2CAP...\n");

        mediaOpenRequest.Size = sizeof(mediaOpenRequest);
        mediaOpenRequest.TimeoutMs = LDAC_NATIVE_DEFAULT_OPEN_TIMEOUT_MS;
        mediaOpenRequest.PreferredMtu = LDAC_TEST_MEDIA_MTU;
        mediaOpenRequest.Reserved = 0u;
        ZeroMemory(&mediaChannel, sizeof(mediaChannel));
        if (!run_ioctl(device,
                       IOCTL_LDAC_NATIVE_OPEN_MEDIA,
                       &mediaOpenRequest,
                       sizeof(mediaOpenRequest),
                       &mediaChannel,
                       sizeof(mediaChannel),
                       mediaOpenRequest.TimeoutMs + 2000u,
                       &bytes)) {
            print_win32_error(L"OPEN_MEDIA", GetLastError());
            goto cleanup;
        }
        if (bytes < sizeof(mediaChannel) ||
            mediaChannel.Size != sizeof(mediaChannel) ||
            mediaChannel.State != LDAC_NATIVE_CHANNEL_CONNECTED ||
            mediaChannel.OutgoingMtu == 0u ||
            mediaChannel.OutgoingMtu > LDAC_NATIVE_MAX_MEDIA_TRANSFER) {
            fwprintf(stderr, L"OPEN_MEDIA returned invalid channel data.\n");
            goto cleanup;
        }
        wprintf(L"Media L2CAP connected: incoming MTU %u, outgoing MTU %u.\n",
                mediaChannel.IncomingMtu,
                mediaChannel.OutgoingMtu);

        if (media_mode_streams_audio(mediaMode)) {
            const wchar_t *kind = media_mode_uses_native_endpoint(mediaMode)
                ? L"Native LDAC endpoint audio"
                : (media_mode_uses_wasapi(mediaMode)
                    ? L"system audio"
                    : (mediaMode == PROBE_MEDIA_TONE_MQ
                        ? L"tone"
                        : L"silence"));
            wprintf(L"Sending AVDTP START before the controlled %ls "
                    L"stream...\n",
                    kind);
        } else {
            wprintf(L"Sending AVDTP START without media payload...\n");
        }
        if (media_mode_uses_wasapi(mediaMode)) {
            wasapi_loopback_status captureStatus =
                wasapi_loopback_start(loopback);
            if (captureStatus != WASAPI_LOOPBACK_OK) {
                fwprintf(stderr,
                         L"Could not start WASAPI loopback (status %d, "
                         L"HRESULT 0x%08lX).\n",
                         (int)captureStatus,
                         (unsigned long)wasapi_loopback_last_hresult(loopback));
                goto cleanup;
            }
        }
        if (!exchange_seid_command(device,
                                   readCapacity,
                                   remoteSeid,
                                   AVDTP_SIGNAL_START,
                                   label)) {
            goto cleanup;
        }
        wprintf(L"XM5 accepted START; the LDAC Media transport is ready.\n");
        started = 1;
        if (linkStateReporting != 0) {
            native_pcm_source_status linkStatus =
                native_pcm_source_report_link_state(
                    endpointSource,
                    NATIVE_PCM_LINK_CONNECTED,
                    linkSessionId);
            if (linkStatus != NATIVE_PCM_SOURCE_OK) {
                fwprintf(stderr,
                         L"Native endpoint connected-state report failed "
                         L"(status %d, Win32 %lu); audio will continue.\n",
                         (int)linkStatus,
                         native_pcm_source_last_error(endpointSource));
                linkStateReporting = 0;
            }
        }
        if (media_mode_streams_audio(mediaMode) &&
            !stream_ldac_test(device,
                              mediaChannel.OutgoingMtu,
                              mediaMode,
                              streamSampleRateHz,
                              loopback,
                              endpointSource,
                              quality,
                              channelMode,
                              automaticQuality,
                              linkSessionId,
                              &linkStateReporting)) {
            goto cleanup;
        }
    }
    result = 1;

cleanup:
    if (linkStatePropertyAvailable != 0 && started != 0) {
        (void)native_pcm_source_report_link_state(
            endpointSource,
            NATIVE_PCM_LINK_STOPPING,
            linkSessionId);
    }
    if (configured != 0 && started != 0) {
        wprintf(L"Gracefully stopping the test stream...\n");
        if (exchange_seid_command(device,
                                  readCapacity,
                                  remoteSeid,
                                  AVDTP_SIGNAL_SUSPEND,
                                  label)) {
            wprintf(L"XM5 accepted SUSPEND. Closing the stream...\n");
            if (exchange_seid_command(device,
                                      readCapacity,
                                      remoteSeid,
                                      AVDTP_SIGNAL_CLOSE,
                                      label)) {
                wprintf(L"XM5 accepted CLOSE; the test stream was released "
                        L"normally.\n");
                configured = 0;
            } else {
                result = 0;
            }
        } else {
            result = 0;
        }
    }
    if (configured != 0) {
        wprintf(L"Using AVDTP ABORT for test cleanup.\n");
        if (!exchange_seid_command(device,
                                   readCapacity,
                                   remoteSeid,
                                   AVDTP_SIGNAL_ABORT,
                                   label)) {
            result = 0;
        } else {
            wprintf(L"XM5 accepted ABORT; the test stream was released.\n");
        }
    }
    if (linkStatePropertyAvailable != 0) {
        (void)native_pcm_source_report_link_state(
            endpointSource,
            NATIVE_PCM_LINK_DISCONNECTED,
            linkSessionId);
    }
    wasapi_loopback_stop(loopback);
    wasapi_loopback_destroy(loopback);
    native_pcm_source_destroy(endpointSource);
    return result;
}

static int query_capabilities(HANDLE device,
                              const LDAC_NATIVE_CHANNEL_INFO *channel,
                              int configure,
                              probe_media_mode mediaMode,
                              ldac_encoder_quality quality,
                              ldac_encoder_channel_mode channelMode,
                              unsigned preferredSampleRateHz,
                              unsigned preferredBitsPerSample,
                              int automaticQuality) {
    uint8_t command[16];
    uint8_t response[LDAC_NATIVE_MAX_SIGNALING_TRANSFER];
    uint8_t remoteSeids[AVDTP_MAX_REMOTE_SEIDS];
    uint8_t seidPayload;
    uint8_t remoteSeid;
    uint8_t label = 1u;
    size_t sinkCount;
    size_t sinkIndex;
    DWORD responseSize = 0u;
    DWORD readCapacity = channel->IncomingMtu;
    size_t commandSize;
    avdtp_header header;
    int parsed;
    ldac_capabilities capabilities;
    ldac_codec_status codecStatus;

    commandSize = avdtp_write_single(command,
                                     sizeof(command),
                                     0u,
                                     AVDTP_MESSAGE_COMMAND,
                                     AVDTP_SIGNAL_DISCOVER,
                                     NULL,
                                     0u);
    if (commandSize == 0u ||
        !exchange_signaling(device,
                            command,
                            (DWORD)commandSize,
                            response,
                            readCapacity,
                            5000u,
                            &responseSize)) {
        return 0;
    }
    parsed = parse_expected_response(response,
                                     responseSize,
                                     0u,
                                     AVDTP_SIGNAL_DISCOVER,
                                     &header);
    if (parsed != 1 || header.payload_offset > responseSize) return 0;
    sinkCount = print_and_collect_audio_sinks(
        response + header.payload_offset,
        responseSize - header.payload_offset,
        remoteSeids,
        AVDTP_MAX_REMOTE_SEIDS);
    if (sinkCount == 0u) {
        fwprintf(stderr, L"No available remote audio sink endpoint found.\n");
        return 0;
    }
    for (sinkIndex = 0u; sinkIndex < sinkCount; ++sinkIndex) {
        uint8_t signal = AVDTP_SIGNAL_GET_ALL_CAPABILITIES;
        remoteSeid = remoteSeids[sinkIndex];

retry_capabilities:
        wprintf(L"Querying capabilities for audio sink SEID %u...\n",
                remoteSeid);
        seidPayload = (uint8_t)(remoteSeid << 2u);
        commandSize = avdtp_write_single(command,
                                         sizeof(command),
                                         label,
                                         AVDTP_MESSAGE_COMMAND,
                                         signal,
                                         &seidPayload,
                                         1u);
        if (commandSize == 0u ||
            !exchange_signaling(device,
                                command,
                                (DWORD)commandSize,
                                response,
                                readCapacity,
                                5000u,
                                &responseSize)) {
            return 0;
        }
        parsed = parse_expected_response(response,
                                         responseSize,
                                         label,
                                         signal,
                                         &header);
        label = (uint8_t)((label + 1u) & 0x0Fu);
        if (parsed == -1 && signal == AVDTP_SIGNAL_GET_ALL_CAPABILITIES) {
            wprintf(L"GET_ALL_CAPABILITIES rejected for SEID %u; "
                    L"retrying legacy GET_CAPABILITIES.\n",
                    remoteSeid);
            signal = AVDTP_SIGNAL_GET_CAPABILITIES;
            goto retry_capabilities;
        }
        if (parsed == -1) {
            wprintf(L"Skipping SEID %u because its capability query was rejected.\n",
                    remoteSeid);
            continue;
        }
        if (parsed != 1 || header.payload_offset > responseSize) return 0;

        print_media_codec_summary(response + header.payload_offset,
                                  responseSize - header.payload_offset);
        codecStatus = ldac_find_in_service_capabilities(
            response + header.payload_offset,
            responseSize - header.payload_offset,
            &capabilities);
        if (codecStatus == LDAC_CODEC_OK) {
            wprintf(L"Selected LDAC audio sink SEID: %u\n", remoteSeid);
            print_ldac_capabilities(capabilities);
            if (configure != 0) {
                return configure_ldac_endpoint(device,
                                               readCapacity,
                                               remoteSeid,
                                               capabilities,
                                               &label,
                                               mediaMode,
                                               quality,
                                               channelMode,
                                               preferredSampleRateHz,
                                               preferredBitsPerSample,
                                               automaticQuality);
            }
            return 1;
        }
        if (codecStatus != LDAC_CODEC_NOT_FOUND) {
            fwprintf(stderr,
                     L"Invalid LDAC capability on SEID %u (status %d).\n",
                     remoteSeid,
                     (int)codecStatus);
            return 0;
        }
        wprintf(L"  SEID %u does not advertise LDAC; trying the next sink.\n",
                remoteSeid);
    }
    fwprintf(stderr, L"No LDAC audio sink endpoint was found.\n");
    return 0;
}

static int hold_signaling_channel(unsigned durationSeconds) {
    const ULONGLONG deadline = GetTickCount64() +
        (ULONGLONG)durationSeconds * 1000u;
    wprintf(L"Signaling channel hold active for up to %u second(s).\n",
            durationSeconds);
    while (GetTickCount64() < deadline) {
        if (InterlockedCompareExchange(&g_cancelled, 0, 0) != 0) {
            return 0;
        }
        if (g_stop_event != NULL &&
            WaitForSingleObject(g_stop_event, 0u) == WAIT_OBJECT_0) {
            wprintf(L"Signaling channel hold stop event observed.\n");
            return 1;
        }
        Sleep(50u);
    }
    wprintf(L"Signaling channel hold completed at its bounded limit.\n");
    return 1;
}

static int run_discover(HANDLE device,
                        int configure,
                        probe_media_mode mediaMode,
                        ldac_encoder_quality quality,
                        ldac_encoder_channel_mode channelMode,
                        unsigned preferredSampleRateHz,
                        unsigned preferredBitsPerSample,
                        int automaticQuality,
                        unsigned holdSignalingSeconds) {
    LDAC_NATIVE_OPEN_SIGNALING_REQUEST openRequest;
    LDAC_NATIVE_CHANNEL_INFO channel;
    DWORD bytes = 0u;
    int result = 0;

    openRequest.Size = sizeof(openRequest);
    openRequest.TimeoutMs = LDAC_NATIVE_DEFAULT_OPEN_TIMEOUT_MS;
    openRequest.PreferredMtu = 672u;
    openRequest.Reserved = 0u;
    if (!open_signaling_with_retry(device,
                                   &openRequest,
                                   &channel,
                                   &bytes)) {
        DWORD openError = GetLastError();
        print_open_diagnostics(device);
        print_win32_error(L"OPEN_SIGNALING", openError);
        return 0;
    }
    if (bytes < sizeof(channel) || channel.Size != sizeof(channel) ||
        channel.State != LDAC_NATIVE_CHANNEL_CONNECTED ||
        channel.IncomingMtu == 0u || channel.OutgoingMtu == 0u ||
        channel.IncomingMtu > LDAC_NATIVE_MAX_SIGNALING_TRANSFER) {
        fwprintf(stderr, L"OPEN_SIGNALING returned invalid channel data.\n");
        goto close_channel;
    }
    wprintf(L"Signaling connected: incoming MTU %u, outgoing MTU %u\n",
            channel.IncomingMtu,
            channel.OutgoingMtu);
    result = query_capabilities(device,
                                &channel,
                                configure,
                                mediaMode,
                                quality,
                                channelMode,
                                preferredSampleRateHz,
                                preferredBitsPerSample,
                                automaticQuality);
    if (result != 0 && holdSignalingSeconds != 0u &&
        !hold_signaling_channel(holdSignalingSeconds)) {
        result = 0;
    }

close_channel:
    if (!run_ioctl(device,
                   IOCTL_LDAC_NATIVE_CLOSE_CHANNELS,
                   NULL,
                   0u,
                   NULL,
                   0u,
                   7000u,
                   &bytes)) {
        print_win32_error(L"CLOSE_CHANNELS", GetLastError());
        result = 0;
    } else {
        wprintf(L"Signaling channel closed.\n");
    }
    return result;
}

int wmain(int argc, wchar_t **argv) {
    LDAC_NATIVE_DEVICE_INFO deviceInfo;
    int discover = 0;
    int configure = 0;
    probe_media_mode mediaMode = PROBE_MEDIA_NONE;
    ldac_encoder_quality quality = LDAC_ENCODER_QUALITY_MQ;
    ldac_encoder_channel_mode channelMode = LDAC_ENCODER_CHANNEL_STEREO;
    unsigned preferredSampleRateHz = 48000u;
    unsigned preferredBitsPerSample = 16u;
    unsigned holdSignalingSeconds = 0u;
    const wchar_t *stopEventName = NULL;
    int automaticQuality = 0;
    int openDiagnostics = 0;
    int index;
    int result = 1;

    (void)setvbuf(stdout, NULL, _IONBF, 0u);
    (void)setvbuf(stderr, NULL, _IONBF, 0u);

    for (index = 1; index < argc; ++index) {
        if (wcscmp(argv[index], L"--discover") == 0) {
            discover = 1;
        } else if (wcscmp(argv[index], L"--configure") == 0) {
            discover = 1;
            configure = 1;
        } else if (wcscmp(argv[index], L"--media-session") == 0) {
            discover = 1;
            configure = 1;
            mediaMode = PROBE_MEDIA_EMPTY_SESSION;
        } else if (wcscmp(argv[index], L"--stream-silence") == 0) {
            discover = 1;
            configure = 1;
            mediaMode = PROBE_MEDIA_SILENCE_MQ;
        } else if (wcscmp(argv[index],
                          L"--stream-silence-continuous") == 0) {
            discover = 1;
            configure = 1;
            mediaMode = PROBE_MEDIA_SILENCE_CONTINUOUS_MQ;
        } else if (wcscmp(argv[index], L"--stream-tone") == 0) {
            discover = 1;
            configure = 1;
            mediaMode = PROBE_MEDIA_TONE_MQ;
        } else if (wcscmp(argv[index], L"--stream-system") == 0) {
            discover = 1;
            configure = 1;
            mediaMode = PROBE_MEDIA_SYSTEM_MQ;
        } else if (wcscmp(argv[index], L"--play-system") == 0) {
            discover = 1;
            configure = 1;
            mediaMode = PROBE_MEDIA_SYSTEM_CONTINUOUS_MQ;
        } else if (wcscmp(argv[index], L"--play-endpoint") == 0) {
            discover = 1;
            configure = 1;
            mediaMode = PROBE_MEDIA_ENDPOINT_CONTINUOUS;
        } else if (wcscmp(argv[index], L"--quality") == 0) {
            if (index + 1 >= argc ||
                !parse_quality(argv[index + 1],
                               &quality,
                               &automaticQuality)) {
                fwprintf(stderr,
                         L"--quality requires mq, sq, hq, or auto.\n");
                return 2;
            }
            index++;
        } else if (wcscmp(argv[index], L"--channel-mode") == 0) {
            if (index + 1 >= argc ||
                !parse_channel_mode(argv[index + 1], &channelMode)) {
                fwprintf(stderr,
                         L"--channel-mode requires stereo, dual, or mono.\n");
                return 2;
            }
            index++;
        } else if (wcscmp(argv[index], L"--sample-rate") == 0) {
            wchar_t *end = NULL;
            unsigned long parsed;
            if (index + 1 >= argc) {
                fwprintf(stderr,
                         L"--sample-rate requires 44100, 48000, 88200, "
                         L"or 96000.\n");
                return 2;
            }
            parsed = wcstoul(argv[index + 1], &end, 10);
            if (end == argv[index + 1] || end == NULL || *end != L'\0' ||
                (parsed != 44100u && parsed != 48000u &&
                 parsed != 88200u && parsed != 96000u)) {
                fwprintf(stderr,
                         L"--sample-rate requires 44100, 48000, 88200, "
                         L"or 96000.\n");
                return 2;
            }
            preferredSampleRateHz = (unsigned)parsed;
            index++;
        } else if (wcscmp(argv[index], L"--bits") == 0) {
            if (index + 1 >= argc ||
                (wcscmp(argv[index + 1], L"16") != 0 &&
                 wcscmp(argv[index + 1], L"24") != 0)) {
                fwprintf(stderr, L"--bits requires 16 or 24.\n");
                return 2;
            }
            preferredBitsPerSample = wcscmp(argv[index + 1], L"24") == 0
                ? 24u
                : 16u;
            index++;
        } else if (wcscmp(argv[index], L"--stop-event") == 0) {
            if (index + 1 >= argc || argv[index + 1][0] == L'\0') {
                fwprintf(stderr,
                         L"--stop-event requires a Windows event name.\n");
                return 2;
            }
            stopEventName = argv[index + 1];
            index++;
        } else if (wcscmp(argv[index], L"--open-attempts") == 0) {
            wchar_t *end = NULL;
            unsigned long attempts;
            if (index + 1 >= argc || argv[index + 1][0] == L'\0') {
                fwprintf(stderr, L"--open-attempts requires 1-20.\n");
                return 2;
            }
            attempts = wcstoul(argv[index + 1], &end, 10);
            if (end == argv[index + 1] || end == NULL || *end != L'\0' ||
                attempts < 1u || attempts > LDAC_OPEN_SIGNALING_MAX_ATTEMPTS) {
                fwprintf(stderr, L"--open-attempts requires 1-20.\n");
                return 2;
            }
            g_open_signaling_max_attempts = (DWORD)attempts;
            index++;
        } else if (wcscmp(argv[index], L"--hold-signaling-seconds") == 0) {
            wchar_t *end = NULL;
            unsigned long seconds;
            if (index + 1 >= argc || argv[index + 1][0] == L'\0') {
                fwprintf(stderr,
                         L"--hold-signaling-seconds requires 15-300.\n");
                return 2;
            }
            seconds = wcstoul(argv[index + 1], &end, 10);
            if (end == argv[index + 1] || end == NULL || *end != L'\0' ||
                seconds < 15u || seconds > 300u) {
                fwprintf(stderr,
                         L"--hold-signaling-seconds requires 15-300.\n");
                return 2;
            }
            holdSignalingSeconds = (unsigned)seconds;
            index++;
        } else if (wcscmp(argv[index], L"--info") == 0) {
            continue;
        } else if (wcscmp(argv[index], L"--open-diagnostics") == 0) {
            openDiagnostics = 1;
        } else if (wcscmp(argv[index], L"--help") == 0 ||
                   wcscmp(argv[index], L"-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fwprintf(stderr, L"Unknown option: %ls\n", argv[index]);
            print_usage(argv[0]);
            return 2;
        }
    }

    if (automaticQuality && !media_mode_is_continuous(mediaMode)) {
        fwprintf(stderr,
                 L"--quality auto requires --play-system or "
                 L"--play-endpoint.\n");
        return 2;
    }
    if (openDiagnostics && discover) {
        fwprintf(stderr,
                 L"--open-diagnostics cannot be combined with an operation that submits Bluetooth requests.\n");
        return 2;
    }
    if (holdSignalingSeconds != 0u &&
        (discover == 0 || configure != 0 || mediaMode != PROBE_MEDIA_NONE)) {
        fwprintf(stderr,
                 L"--hold-signaling-seconds requires capability-only "
                 L"--discover without configuration or media.\n");
        return 2;
    }

    if (stopEventName != NULL) {
        g_stop_event = OpenEventW(SYNCHRONIZE, FALSE, stopEventName);
        if (g_stop_event == NULL) {
            print_win32_error(L"Open stop event", GetLastError());
            return 2;
        }
    }
    (void)SetConsoleCtrlHandler(console_control_handler, TRUE);
    g_device = open_transport_interface();
    if (g_device == INVALID_HANDLE_VALUE) {
        print_win32_error(L"Open native LDAC transport", GetLastError());
        fwprintf(stderr,
                 L"The test driver must be installed and bound before probing.\n");
        if (g_stop_event != NULL) CloseHandle(g_stop_event);
        g_stop_event = NULL;
        return 3;
    }

    if (!print_driver_info(g_device, &deviceInfo)) goto cleanup;
    result = 0;
    if (openDiagnostics) {
        result = print_open_diagnostics(g_device) ? 0 : 6;
        goto cleanup;
    }
    if (discover != 0) {
        ULONG requiredFlags = LDAC_NATIVE_DEVICE_INFO_PROFILE_READY |
                              LDAC_NATIVE_DEVICE_INFO_REMOTE_READY |
                              LDAC_NATIVE_DEVICE_INFO_LOCAL_READY |
                              LDAC_NATIVE_DEVICE_INFO_INBOUND_SIGNALING_READY;
        if ((deviceInfo.Flags & requiredFlags) != requiredFlags) {
            fwprintf(stderr, L"Bluetooth profile information is not ready.\n");
            result = 4;
        } else if (!run_discover(g_device,
                                 configure,
                                 mediaMode,
                                 quality,
                                 channelMode,
                                 preferredSampleRateHz,
                                 preferredBitsPerSample,
                                 automaticQuality,
                                 holdSignalingSeconds)) {
            result = 5;
        }
    }

cleanup:
    CloseHandle(g_device);
    g_device = INVALID_HANDLE_VALUE;
    if (g_stop_event != NULL) CloseHandle(g_stop_event);
    g_stop_event = NULL;
    if (InterlockedCompareExchange(&g_cancelled, 0, 0) != 0 &&
        InterlockedCompareExchange(&g_stop_event_requested, 0, 0) == 0) {
        return 130;
    }
    return result;
}
