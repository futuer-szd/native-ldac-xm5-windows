#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <ks.h>
#include <ksmedia.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "nativeldac_pcm_abi.h"
#include "nativeldac_direct_pdo_public.h"

static const GUID g_audio_category = { STATIC_KSCATEGORY_AUDIO };
static const GUID g_pcm_property_set = { STATIC_KSPROPSETID_NativeLdacPcm };
static const GUID g_direct_pdo_property_set = {
    STATIC_KSPROPSETID_NativeLdacDirectPdo
};
static volatile LONG g_stop_requested = 0;

static BOOL WINAPI console_handler(DWORD control_type)
{
    if (control_type == CTRL_C_EVENT || control_type == CTRL_BREAK_EVENT ||
        control_type == CTRL_CLOSE_EVENT) {
        InterlockedExchange(&g_stop_requested, 1);
        return TRUE;
    }
    return FALSE;
}

static BOOL query_property(
    HANDLE device,
    ULONG property_id,
    void *output,
    DWORD output_size,
    DWORD *bytes_returned)
{
    KSPROPERTY property;

    ZeroMemory(&property, sizeof(property));
    property.Set = g_pcm_property_set;
    property.Id = property_id;
    property.Flags = KSPROPERTY_TYPE_GET;

    return DeviceIoControl(
        device,
        IOCTL_KS_PROPERTY,
        &property,
        (DWORD)sizeof(property),
        output,
        output_size,
        bytes_returned,
        NULL);
}

static BOOL query_direct_pdo_status(
    HANDLE device,
    NLD_DIRECT_PDO_MEDIA_STATUS_V1 *status)
{
    KSPROPERTY property;
    DWORD bytes_returned;

    if (status == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ZeroMemory(&property, sizeof(property));
    property.Set = g_direct_pdo_property_set;
    property.Id = NldDirectPdoPropertyMediaStatus;
    property.Flags = KSPROPERTY_TYPE_GET;
    ZeroMemory(status, sizeof(*status));
    bytes_returned = 0;
    if (!DeviceIoControl(
            device,
            IOCTL_KS_PROPERTY,
            &property,
            (DWORD)sizeof(property),
            status,
            (DWORD)sizeof(*status),
            &bytes_returned,
            NULL)) {
        return FALSE;
    }
    if (bytes_returned < sizeof(*status) ||
        status->Size != sizeof(*status)) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    return TRUE;
}

static const wchar_t *direct_pdo_state_name(ULONG state)
{
    switch (state) {
    case NldDirectPdoMediaOffline:
        return L"offline";
    case NldDirectPdoMediaIdle:
        return L"idle";
    case NldDirectPdoMediaOpen:
        return L"open";
    case NldDirectPdoMediaStreaming:
        return L"streaming";
    case NldDirectPdoMediaStopping:
        return L"stopping";
    case NldDirectPdoMediaFaulted:
        return L"faulted";
    default:
        return L"unknown";
    }
}

static const wchar_t *direct_pdo_failure_name(ULONG reason)
{
    switch (reason) {
    case NldDirectPdoFailureNone:
        return L"none";
    case NldDirectPdoFailureRemoteDisconnect:
        return L"remote-disconnect";
    case NldDirectPdoFailureMediaTimeout:
        return L"media-timeout";
    case NldDirectPdoFailureBackend:
        return L"backend";
    default:
        return L"unknown";
    }
}

static const wchar_t *direct_pdo_backend_action_name(ULONG action)
{
    switch (action) {
    case NldDirectPdoMediaBackendActionNone:
        return L"none";
    case NldDirectPdoMediaBackendActionOpen:
        return L"open";
    case NldDirectPdoMediaBackendActionStart:
        return L"start";
    case NldDirectPdoMediaBackendActionSuspend:
        return L"suspend";
    case NldDirectPdoMediaBackendActionClose:
        return L"close";
    case NldDirectPdoMediaBackendActionCancelAndClose:
        return L"cancel-and-close";
    default:
        return L"unknown";
    }
}

static const wchar_t *direct_pdo_protocol_phase_name(ULONG phase)
{
    switch (phase) {
    case NldDirectPdoProtocolPhaseNone:
        return L"none";
    case NldDirectPdoProtocolPhaseWrite:
        return L"write";
    case NldDirectPdoProtocolPhaseRead:
        return L"read";
    case NldDirectPdoProtocolPhaseHandle:
        return L"handle";
    case NldDirectPdoProtocolPhaseOpenMedia:
        return L"media-open";
    case NldDirectPdoProtocolPhaseComplete:
        return L"complete";
    default:
        return L"unknown";
    }
}

static const wchar_t *avdtp_signal_name(ULONG signal_id)
{
    switch (signal_id) {
    case 0x01u: /* DISCOVER */
        return L"DISCOVER";
    case 0x02u: /* GET_CAPABILITIES */
        return L"GET_CAPABILITIES";
    case 0x03u: /* SET_CONFIGURATION */
        return L"SET_CONFIGURATION";
    case 0x06u: /* OPEN */
        return L"OPEN";
    case 0x07u: /* START */
        return L"START";
    case 0x08u: /* CLOSE */
        return L"CLOSE";
    case 0x09u: /* SUSPEND */
        return L"SUSPEND";
    case 0x0Cu: /* GET_ALL_CAPABILITIES */
        return L"GET_ALL_CAPABILITIES";
    default:
        return L"unknown";
    }
}

static void print_direct_pdo_status(
    const NLD_DIRECT_PDO_MEDIA_STATUS_V1 *status)
{
    ULONG protocol_phase =
        (status->Flags & NLD_DIRECT_PDO_MEDIA_STATUS_PROTOCOL_PHASE_MASK) >>
        NLD_DIRECT_PDO_MEDIA_STATUS_PROTOCOL_PHASE_SHIFT;
    ULONG commands_completed =
        (status->Flags & NLD_DIRECT_PDO_MEDIA_STATUS_COMMANDS_COMPLETED_MASK) >>
        NLD_DIRECT_PDO_MEDIA_STATUS_COMMANDS_COMPLETED_SHIFT;
    ULONG signal_id =
        (status->Flags & NLD_DIRECT_PDO_MEDIA_STATUS_SIGNAL_ID_MASK) >>
        NLD_DIRECT_PDO_MEDIA_STATUS_SIGNAL_ID_SHIFT;

    wprintf(
        L"Direct-PDO Media ABI %lu: %ls (%lu), flags 0x%08lX.\n",
        status->Version,
        direct_pdo_state_name(status->State),
        status->State,
        status->Flags);
    wprintf(
        L"Session generation %lu, media generation %lu, outgoing MTU %lu, open attempts %lu.\n",
        status->SessionGeneration,
        status->MediaGeneration,
        status->OutgoingMtu,
        (status->Flags &
         NLD_DIRECT_PDO_MEDIA_STATUS_OPEN_ATTEMPTS_MASK) >>
            NLD_DIRECT_PDO_MEDIA_STATUS_OPEN_ATTEMPTS_SHIFT);
    wprintf(
        L"Failure: %ls (%lu), last media status 0x%08lX; packets %llu, bytes %llu.\n",
        direct_pdo_failure_name(status->FailureReason),
        status->FailureReason,
        (ULONG)status->LastStatus,
        status->PacketsAccepted,
        status->BytesAccepted);
    wprintf(
        L"Backend: %ls (%lu), action status 0x%08lX, signaling open status 0x%08lX.\n",
        direct_pdo_backend_action_name(status->LastBackendAction),
        status->LastBackendAction,
        (ULONG)status->LastBackendStatus,
        (ULONG)status->LastSignalingOpenStatus);
    wprintf(
        L"Backend activity: %ls.\n",
        (status->Flags & NLD_DIRECT_PDO_MEDIA_STATUS_BACKEND_ACTIVE) != 0
            ? L"active"
            : L"idle");
    wprintf(
        L"AVDTP: %ls, signal %ls (0x%02lX), %lu command(s) completed.\n",
        direct_pdo_protocol_phase_name(protocol_phase),
        avdtp_signal_name(signal_id),
        signal_id,
        commands_completed);
}

static BOOL set_property(
    HANDLE device,
    ULONG property_id,
    void *value,
    DWORD value_size)
{
    KSPROPERTY property;
    DWORD bytes_returned;

    ZeroMemory(&property, sizeof(property));
    property.Set = g_pcm_property_set;
    property.Id = property_id;
    property.Flags = KSPROPERTY_TYPE_SET;
    bytes_returned = 0;
    return DeviceIoControl(
        device,
        IOCTL_KS_PROPERTY,
        &property,
        (DWORD)sizeof(property),
        value,
        value_size,
        &bytes_returned,
        NULL);
}

static BOOL is_ldac_diagnostic_path(const wchar_t *path);

static HANDLE open_native_ldac_wave_interface(
    wchar_t *path,
    size_t path_count,
    NATIVE_LDAC_PCM_INFO *info,
    BOOL require_direct_pdo)
{
    HDEVINFO device_info;
    SP_DEVICE_INTERFACE_DATA interface_data;
    DWORD index;
    DWORD final_error;
    DWORD candidate_error;
    HANDLE result;

    if (path == NULL || path_count == 0 || info == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }

    path[0] = L'\0';
    result = INVALID_HANDLE_VALUE;
    candidate_error = ERROR_SUCCESS;
    device_info = SetupDiGetClassDevsW(
        &g_audio_category,
        NULL,
        NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (device_info == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
    }

    ZeroMemory(&interface_data, sizeof(interface_data));
    interface_data.cbSize = sizeof(interface_data);

    for (index = 0;
         SetupDiEnumDeviceInterfaces(
             device_info, NULL, &g_audio_category, index, &interface_data);
         ++index) {
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail;
        DWORD required_size;
        DWORD bytes_returned;
        HANDLE candidate;

        required_size = 0;
        (void)SetupDiGetDeviceInterfaceDetailW(
            device_info,
            &interface_data,
            NULL,
            0,
            &required_size,
            NULL);
        if (required_size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            continue;
        }

        detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, required_size);
        if (detail == NULL) {
            SetLastError(ERROR_OUTOFMEMORY);
            break;
        }
        detail->cbSize = sizeof(*detail);

        if (!SetupDiGetDeviceInterfaceDetailW(
                device_info,
                &interface_data,
                detail,
                required_size,
                NULL,
                NULL)) {
            HeapFree(GetProcessHeap(), 0, detail);
            continue;
        }

        candidate = CreateFileW(
            detail->DevicePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
        if (candidate == INVALID_HANDLE_VALUE) {
            if (is_ldac_diagnostic_path(detail->DevicePath)) {
                DWORD open_error = GetLastError();

                if (candidate_error == ERROR_SUCCESS ||
                    open_error == ERROR_ACCESS_DENIED) {
                    candidate_error = open_error;
                }
            }
            HeapFree(GetProcessHeap(), 0, detail);
            continue;
        }

        ZeroMemory(info, sizeof(*info));
        bytes_returned = 0;
        if (query_property(
                candidate,
                NativeLdacPcmPropertyInfo,
                info,
                (DWORD)sizeof(*info),
                &bytes_returned) &&
            bytes_returned >= sizeof(*info) &&
            info->AbiVersion == NATIVE_LDAC_PCM_ABI_VERSION) {
            NLD_DIRECT_PDO_MEDIA_STATUS_V1 direct_status;

            if (require_direct_pdo &&
                (!query_direct_pdo_status(candidate, &direct_status) ||
                 direct_status.Version !=
                     NLD_DIRECT_PDO_MEDIA_ABI_VERSION)) {
                CloseHandle(candidate);
                HeapFree(GetProcessHeap(), 0, detail);
                continue;
            }
            if (wcslen(detail->DevicePath) + 1 <= path_count) {
                (void)wcscpy_s(path, path_count, detail->DevicePath);
                result = candidate;
            } else {
                CloseHandle(candidate);
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
            }
            HeapFree(GetProcessHeap(), 0, detail);
            break;
        }

        CloseHandle(candidate);
        HeapFree(GetProcessHeap(), 0, detail);
    }

    final_error = GetLastError();
    if (result == INVALID_HANDLE_VALUE) {
        if (candidate_error != ERROR_SUCCESS) {
            final_error = candidate_error;
        } else if (final_error == ERROR_NO_MORE_ITEMS) {
            final_error = ERROR_NOT_FOUND;
        }
    }
    SetupDiDestroyDeviceInfoList(device_info);
    if (result == INVALID_HANDLE_VALUE) {
        SetLastError(final_error);
    }
    return result;
}

static BOOL is_ldac_diagnostic_path(const wchar_t *path)
{
    if (path == NULL) {
        return FALSE;
    }
    return wcsstr(path, L"root#media#") != NULL ||
        wcsstr(path, L"ROOT#MEDIA#") != NULL ||
        wcsstr(path, L"vid&0002054c_pid&0df0") != NULL ||
        wcsstr(path, L"VID&0002054C_PID&0DF0") != NULL;
}

static int scan_native_ldac_interfaces(void)
{
    HDEVINFO device_info;
    SP_DEVICE_INTERFACE_DATA interface_data;
    DWORD index;
    unsigned matches;

    device_info = SetupDiGetClassDevsW(
        &g_audio_category,
        NULL,
        NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (device_info == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"Audio interface enumeration failed (Win32 %lu).\n",
                 GetLastError());
        return 3;
    }

    matches = 0u;
    ZeroMemory(&interface_data, sizeof(interface_data));
    interface_data.cbSize = sizeof(interface_data);
    for (index = 0;
         SetupDiEnumDeviceInterfaces(
             device_info, NULL, &g_audio_category, index, &interface_data);
         ++index) {
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail;
        DWORD required_size;
        DWORD error;
        DWORD bytes_returned;
        HANDLE candidate;
        NATIVE_LDAC_PCM_INFO info;
        NLD_DIRECT_PDO_MEDIA_STATUS_V1 direct_status;

        required_size = 0u;
        (void)SetupDiGetDeviceInterfaceDetailW(
            device_info, &interface_data, NULL, 0u, &required_size, NULL);
        if (required_size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            continue;
        }
        detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, required_size);
        if (detail == NULL) {
            SetupDiDestroyDeviceInfoList(device_info);
            return 3;
        }
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(
                device_info,
                &interface_data,
                detail,
                required_size,
                NULL,
                NULL) ||
            !is_ldac_diagnostic_path(detail->DevicePath)) {
            HeapFree(GetProcessHeap(), 0, detail);
            continue;
        }

        matches++;
        wprintf(L"Interface candidate: %ls\n", detail->DevicePath);
        candidate = CreateFileW(
            detail->DevicePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
        if (candidate == INVALID_HANDLE_VALUE) {
            wprintf(L"  open: failed (Win32 %lu)\n", GetLastError());
            HeapFree(GetProcessHeap(), 0, detail);
            continue;
        }
        wprintf(L"  open: success\n");

        ZeroMemory(&info, sizeof(info));
        bytes_returned = 0u;
        if (!query_property(
                candidate,
                NativeLdacPcmPropertyInfo,
                &info,
                (DWORD)sizeof(info),
                &bytes_returned)) {
            error = GetLastError();
            wprintf(L"  PCM ABI: failed (Win32 %lu)\n", error);
        } else {
            wprintf(L"  PCM ABI: %lu, %lu byte(s) returned\n",
                    info.AbiVersion,
                    bytes_returned);
        }

        if (!query_direct_pdo_status(candidate, &direct_status)) {
            error = GetLastError();
            wprintf(L"  Direct-PDO ABI: failed (Win32 %lu)\n", error);
        } else {
            wprintf(L"  Direct-PDO ABI: %lu, state %lu\n",
                    direct_status.Version,
                    direct_status.State);
        }
        CloseHandle(candidate);
        HeapFree(GetProcessHeap(), 0, detail);
    }
    SetupDiDestroyDeviceInfoList(device_info);
    if (matches == 0u) {
        wprintf(L"No present XM5 or legacy Native LDAC audio interfaces were enumerated.\n");
    }
    return 0;
}

static void print_info(const NATIVE_LDAC_PCM_INFO *info)
{
    const wchar_t *active;
    const wchar_t *continuity;

    active = (info->Flags & NATIVE_LDAC_PCM_FLAG_STREAM_ACTIVE) != 0
        ? L"active"
        : L"idle";
    continuity = (info->Flags & NATIVE_LDAC_PCM_FLAG_DISCONTINUITY) != 0
        ? L", discontinuity"
        : L"";

    wprintf(
        L"PCM ABI %lu: %lu Hz, %hu channel(s), %hu-bit, block %lu bytes.\n",
        info->AbiVersion,
        info->SampleRate,
        info->Channels,
        info->BitsPerSample,
        info->BlockAlign);
    wprintf(
        L"Stream %ls%ls: epoch %llu, buffer %lu/%lu bytes.\n",
        active,
        continuity,
        info->StreamEpoch,
        info->AvailableBytes,
        info->CapacityBytes);
    wprintf(
        L"Totals: written %llu, read %llu, dropped %llu bytes.\n",
        info->TotalBytesWritten,
        info->TotalBytesRead,
        info->TotalBytesDropped);
}

static const wchar_t *link_state_name(ULONG state)
{
    switch (state) {
    case NativeLdacLinkStateDisconnected:
        return L"disconnected";
    case NativeLdacLinkStateConnecting:
        return L"connecting";
    case NativeLdacLinkStateConnected:
        return L"connected";
    case NativeLdacLinkStateStopping:
        return L"stopping";
    default:
        return L"invalid";
    }
}

static void print_link_state(const NATIVE_LDAC_LINK_STATE *state)
{
    ULONGLONG now_100ns;
    ULONGLONG age_ms;

    now_100ns = GetTickCount64() * 10000u;
    age_ms = now_100ns >= state->UpdatedInterruptTime100ns
        ? (now_100ns - state->UpdatedInterruptTime100ns) / 10000u
        : 0u;
    wprintf(
        L"Link %ls: session %llu, update %llu, age %llu ms.\n",
        link_state_name(state->State),
        state->SessionId,
        state->UpdateSequence,
        age_ms);
}

static const wchar_t *presence_state_name(ULONG state)
{
    return state == NativeLdacPresencePresent ? L"present" : L"absent";
}

static BOOL query_physical_presence(
    HANDLE device,
    NATIVE_LDAC_PRESENCE_STATE *state)
{
    DWORD bytes_returned;

    ZeroMemory(state, sizeof(*state));
    bytes_returned = 0;
    if (!query_property(
            device,
            NativeLdacPcmPropertyPhysicalPresence,
            state,
            (DWORD)sizeof(*state),
            &bytes_returned)) {
        return FALSE;
    }
    if (bytes_returned < sizeof(*state) ||
        state->Size != sizeof(*state) ||
        state->AbiVersion != NATIVE_LDAC_PRESENCE_STATE_ABI_VERSION ||
        state->Flags != NATIVE_LDAC_PRESENCE_STATE_FLAG_NONE ||
        state->State > NativeLdacPresencePresent) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    return TRUE;
}

static void print_physical_presence(
    const NATIVE_LDAC_PRESENCE_STATE *state)
{
    ULONGLONG now_100ns;
    ULONGLONG age_ms;

    now_100ns = GetTickCount64() * 10000u;
    age_ms = now_100ns >= state->UpdatedInterruptTime100ns
        ? (now_100ns - state->UpdatedInterruptTime100ns) / 10000u
        : 0u;
    wprintf(
        L"Physical presence %ls: generation %llu, update %llu, "
        L"age %llu ms.\n",
        presence_state_name(state->State),
        state->PresenceGeneration,
        state->UpdateSequence,
        age_ms);
}

static BOOL query_consumer_lease(
    HANDLE device,
    NATIVE_LDAC_PCM_CONSUMER_LEASE *lease)
{
    DWORD bytes_returned;

    ZeroMemory(lease, sizeof(*lease));
    bytes_returned = 0;
    if (!query_property(
            device,
            NativeLdacPcmPropertyConsumerLease,
            lease,
            (DWORD)sizeof(*lease),
            &bytes_returned)) {
        return FALSE;
    }
    if (bytes_returned < sizeof(*lease) ||
        lease->Size != sizeof(*lease) ||
        lease->AbiVersion !=
            NATIVE_LDAC_PCM_CONSUMER_LEASE_ABI_VERSION ||
        lease->Flags != NATIVE_LDAC_PCM_CONSUMER_LEASE_FLAG_NONE ||
        lease->State > NativeLdacPcmConsumerAcquired) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    return TRUE;
}

static void print_consumer_lease(
    const NATIVE_LDAC_PCM_CONSUMER_LEASE *lease)
{
    wprintf(
        L"PCM consumer lease %ls: generation %llu.\n",
        lease->State == NativeLdacPcmConsumerAcquired
            ? L"acquired"
            : L"released",
        lease->ConsumerGeneration);
}

static BOOL query_preferred_format(
    HANDLE device,
    NATIVE_LDAC_PREFERRED_FORMAT *format)
{
    DWORD bytes_returned;

    ZeroMemory(format, sizeof(*format));
    bytes_returned = 0;
    if (!query_property(
            device,
            NativeLdacPcmPropertyPreferredFormat,
            format,
            (DWORD)sizeof(*format),
            &bytes_returned)) {
        return FALSE;
    }
    if (bytes_returned < sizeof(*format) ||
        format->Size != sizeof(*format) ||
        format->AbiVersion != NATIVE_LDAC_FORMAT_ABI_VERSION ||
        format->Flags != NATIVE_LDAC_FORMAT_FLAG_NONE) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    return TRUE;
}

static void print_preferred_format(
    const NATIVE_LDAC_PREFERRED_FORMAT *format)
{
    wprintf(
        L"Preferred format: %lu Hz, %lu-bit, revision %lu; "
        L"rate mask 0x%08lX, bit mask 0x%08lX.\n",
        format->SampleRate,
        format->BitsPerSample,
        format->Revision,
        format->SupportedSampleRates,
        format->SupportedBitsPerSample);
}

static BOOL query_link_state(
    HANDLE device,
    NATIVE_LDAC_LINK_STATE *state)
{
    DWORD bytes_returned;

    ZeroMemory(state, sizeof(*state));
    bytes_returned = 0;
    if (!query_property(
            device,
            NativeLdacPcmPropertyLinkState,
            state,
            (DWORD)sizeof(*state),
            &bytes_returned)) {
        return FALSE;
    }
    if (bytes_returned < sizeof(*state) ||
        state->Size != sizeof(*state) ||
        state->AbiVersion != NATIVE_LDAC_LINK_STATE_ABI_VERSION ||
        state->Flags != NATIVE_LDAC_LINK_STATE_FLAG_NONE ||
        state->State > NativeLdacLinkStateStopping) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    return TRUE;
}

static int monitor_link_state(HANDLE device, unsigned duration_seconds)
{
    NATIVE_LDAC_LINK_STATE state;
    ULONGLONG start_tick;
    ULONGLONG last_sequence;

    if (!query_link_state(device, &state)) {
        fwprintf(
            stderr,
            L"LINK STATE failed (Win32 %lu). The installed endpoint "
            L"driver may predate this optional ABI.\n",
            GetLastError());
        return 4;
    }
    print_link_state(&state);
    last_sequence = state.UpdateSequence;
    start_tick = GetTickCount64();

    while (InterlockedCompareExchange(&g_stop_requested, 0, 0) == 0 &&
           GetTickCount64() - start_tick <
               (ULONGLONG)duration_seconds * 1000u) {
        Sleep(50);
        if (!query_link_state(device, &state)) {
            fwprintf(
                stderr,
                L"LINK STATE failed while monitoring (Win32 %lu).\n",
                GetLastError());
            return 4;
        }
        if (state.UpdateSequence != last_sequence) {
            print_link_state(&state);
            last_sequence = state.UpdateSequence;
        }
    }

    wprintf(L"Link-state monitor complete.\n");
    return 0;
}

static int monitor_pcm(HANDLE device, unsigned duration_seconds)
{
    enum { READ_PAYLOAD_BYTES = 16384 };
    unsigned char *buffer;
    ULONGLONG start_tick;
    ULONGLONG next_report_tick;
    ULONGLONG total_bytes;
    ULONGLONG total_samples;
    double sum_squares;
    double peak;
    int exit_code;

    buffer = (unsigned char *)HeapAlloc(
        GetProcessHeap(),
        HEAP_ZERO_MEMORY,
        sizeof(NATIVE_LDAC_PCM_READ_HEADER) + READ_PAYLOAD_BYTES);
    if (buffer == NULL) {
        fwprintf(stderr, L"Could not allocate the PCM read buffer.\n");
        return 3;
    }

    start_tick = GetTickCount64();
    next_report_tick = start_tick + 1000;
    total_bytes = 0;
    total_samples = 0;
    sum_squares = 0.0;
    peak = 0;
    exit_code = 0;

    while (InterlockedCompareExchange(&g_stop_requested, 0, 0) == 0) {
        PNATIVE_LDAC_PCM_READ_HEADER result;
        unsigned char *samples;
        DWORD output_size;
        DWORD bytes_returned;
        DWORD sample_count;
        DWORD bytes_per_sample;
        DWORD i;
        ULONGLONG now;

        output_size = (DWORD)(
            sizeof(NATIVE_LDAC_PCM_READ_HEADER) + READ_PAYLOAD_BYTES);
        bytes_returned = 0;
        ZeroMemory(buffer, sizeof(NATIVE_LDAC_PCM_READ_HEADER));
        if (!query_property(
                device,
                NativeLdacPcmPropertyRead,
                buffer,
                output_size,
                &bytes_returned)) {
            fwprintf(
                stderr,
                L"PCM READ failed (Win32 %lu).\n",
                GetLastError());
            exit_code = 4;
            break;
        }
        if (bytes_returned < sizeof(NATIVE_LDAC_PCM_READ_HEADER)) {
            fwprintf(stderr, L"PCM READ returned a truncated header.\n");
            exit_code = 4;
            break;
        }

        result = (PNATIVE_LDAC_PCM_READ_HEADER)buffer;
        if (result->BytesReturned >
            bytes_returned - sizeof(NATIVE_LDAC_PCM_READ_HEADER)) {
            fwprintf(stderr, L"PCM READ returned an invalid byte count.\n");
            exit_code = 4;
            break;
        }

        if (result->InfoBeforeRead.Channels == 0u ||
            result->InfoBeforeRead.BlockAlign == 0u ||
            result->InfoBeforeRead.BlockAlign %
                result->InfoBeforeRead.Channels != 0u) {
            fwprintf(stderr, L"PCM READ returned an unsupported format.\n");
            exit_code = 4;
            break;
        }
        bytes_per_sample = result->InfoBeforeRead.BlockAlign /
            result->InfoBeforeRead.Channels;
        if ((bytes_per_sample != 2u && bytes_per_sample != 3u &&
             bytes_per_sample != 4u) ||
            result->BytesReturned % result->InfoBeforeRead.BlockAlign != 0u) {
            fwprintf(stderr, L"PCM READ returned an unsupported format.\n");
            exit_code = 4;
            break;
        }
        samples = buffer + sizeof(NATIVE_LDAC_PCM_READ_HEADER);
        sample_count = result->BytesReturned / bytes_per_sample;
        for (i = 0; i < sample_count; ++i) {
            double sample;
            double magnitude;
            DWORD offset;

            offset = i * bytes_per_sample;
            if (bytes_per_sample == 2u) {
                unsigned value;
                int signed_value;
                value = (unsigned)samples[offset] |
                    ((unsigned)samples[offset + 1u] << 8u);
                signed_value = (value & 0x8000u) != 0u
                    ? (int)(value | 0xFFFF0000u)
                    : (int)value;
                sample = (double)signed_value / 32768.0;
            } else if (bytes_per_sample == 3u) {
                unsigned value;
                int signed_value;
                value = (unsigned)samples[offset] |
                    ((unsigned)samples[offset + 1u] << 8u) |
                    ((unsigned)samples[offset + 2u] << 16u);
                signed_value = (value & 0x00800000u) != 0u
                    ? (int)(value | 0xFF000000u)
                    : (int)value;
                sample = (double)signed_value / 8388608.0;
            } else if (result->InfoBeforeRead.BitsPerSample == 24u) {
                int signed_value;
                memcpy(&signed_value, samples + offset, sizeof(signed_value));
                signed_value >>= 8;
                sample = (double)signed_value / 8388608.0;
            } else {
                fwprintf(stderr, L"PCM READ returned an unsupported format.\n");
                exit_code = 4;
                break;
            }
            magnitude = sample < 0.0 ? -sample : sample;
            if (magnitude > peak) {
                peak = magnitude;
            }
            sum_squares += sample * sample;
        }
        total_bytes += result->BytesReturned;
        total_samples += sample_count;

        now = GetTickCount64();
        if (now >= next_report_tick) {
            wprintf(
                L"Live: epoch %llu, %ls, read %llu bytes, buffer %lu -> %lu, "
                L"dropped %llu, peak %.1f%%.\n",
                result->InfoBeforeRead.StreamEpoch,
                (result->InfoBeforeRead.Flags & NATIVE_LDAC_PCM_FLAG_STREAM_ACTIVE)
                    ? L"active"
                    : L"idle",
                total_bytes,
                result->InfoBeforeRead.AvailableBytes,
                result->AvailableBytesAfterRead,
                result->InfoBeforeRead.TotalBytesDropped,
                100.0 * peak);
            next_report_tick = now + 1000;
        }

        if (now - start_tick >= (ULONGLONG)duration_seconds * 1000) {
            break;
        }
        Sleep(5);
    }

    if (total_samples != 0) {
        double rms;

        rms = sqrt(sum_squares / (double)total_samples);
        wprintf(
            L"Monitor complete: %llu PCM bytes, peak %.1f%%, RMS %.2f%%.\n",
            total_bytes,
            100.0 * peak,
            100.0 * rms);
    } else {
        wprintf(L"Monitor complete: no PCM samples were read.\n");
    }

    HeapFree(GetProcessHeap(), 0, buffer);
    return exit_code;
}

static void print_usage(const wchar_t *program)
{
    wprintf(L"Native LDAC audio endpoint PCM probe\n\n");
    wprintf(
        L"Usage: %ls [--info | --monitor [seconds] | --link-state | "
        L"--monitor-link [seconds] | --presence | --consumer-lease | "
        L"--direct-status | --format | "
        L"--set-format rate bits | --scan-interfaces]\n\n",
        program);
    wprintf(L"  --info      Show the endpoint PCM ABI and buffer state (default).\n");
    wprintf(L"  --monitor   Read and meter PCM for 10 seconds by default.\n");
    wprintf(L"  --link-state  Show the reported LDAC transport state.\n");
    wprintf(L"  --monitor-link  Show link-state updates for 10 seconds.\n");
    wprintf(L"  --presence  Show the independent physical-presence lease.\n");
    wprintf(L"  --consumer-lease  Show the independent PCM reader lease.\n");
    wprintf(L"  --direct-status  Read the Direct-PDO Media ABI and fault state.\n");
    wprintf(L"  --scan-interfaces  Diagnose present Native LDAC KS interfaces.\n");
    wprintf(L"  --format    Show the preferred endpoint format.\n");
    wprintf(L"  --set-format  Set 44100/48000/88200/96000 Hz and 16/24-bit, "
            L"then notify Windows.\n");
    wprintf(L"  --help      Show this help.\n");
}

int wmain(int argc, wchar_t **argv)
{
    wchar_t interface_path[1024];
    NATIVE_LDAC_PCM_INFO info;
    unsigned duration_seconds;
    BOOL monitor;
    BOOL link_state;
    BOOL physical_presence;
    BOOL consumer_lease;
    BOOL monitor_link;
    BOOL direct_status;
    BOOL preferred_format;
    BOOL set_preferred_format;
    BOOL scan_interfaces;
    ULONG preferred_sample_rate;
    ULONG preferred_bits;
    HANDLE device;
    DWORD bytes_returned;
    int result;

    monitor = FALSE;
    link_state = FALSE;
    physical_presence = FALSE;
    consumer_lease = FALSE;
    monitor_link = FALSE;
    direct_status = FALSE;
    preferred_format = FALSE;
    set_preferred_format = FALSE;
    scan_interfaces = FALSE;
    preferred_sample_rate = 0;
    preferred_bits = 0;
    duration_seconds = 10;
    if (argc >= 2) {
        if (wcscmp(argv[1], L"--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (wcscmp(argv[1], L"--monitor") == 0 ||
            wcscmp(argv[1], L"--monitor-link") == 0) {
            wchar_t *end;
            unsigned long parsed;

            monitor = wcscmp(argv[1], L"--monitor") == 0;
            monitor_link = !monitor;
            if (argc >= 3) {
                end = NULL;
                parsed = wcstoul(argv[2], &end, 10);
                if (end == argv[2] || *end != L'\0' || parsed == 0 || parsed > 600) {
                    fwprintf(stderr, L"Monitor duration must be 1 to 600 seconds.\n");
                    return 2;
                }
                duration_seconds = (unsigned)parsed;
            }
            if (argc > 3) {
                print_usage(argv[0]);
                return 2;
            }
        } else if (wcscmp(argv[1], L"--link-state") == 0 && argc == 2) {
            link_state = TRUE;
        } else if (wcscmp(argv[1], L"--presence") == 0 && argc == 2) {
            physical_presence = TRUE;
        } else if (wcscmp(argv[1], L"--consumer-lease") == 0 && argc == 2) {
            consumer_lease = TRUE;
        } else if (wcscmp(argv[1], L"--direct-status") == 0 && argc == 2) {
            direct_status = TRUE;
        } else if (wcscmp(argv[1], L"--scan-interfaces") == 0 && argc == 2) {
            scan_interfaces = TRUE;
        } else if (wcscmp(argv[1], L"--format") == 0 && argc == 2) {
            preferred_format = TRUE;
        } else if (wcscmp(argv[1], L"--set-format") == 0 && argc == 4) {
            wchar_t *rate_end;
            wchar_t *bits_end;
            unsigned long parsed_rate;
            unsigned long parsed_bits;

            rate_end = NULL;
            bits_end = NULL;
            parsed_rate = wcstoul(argv[2], &rate_end, 10);
            parsed_bits = wcstoul(argv[3], &bits_end, 10);
            if (rate_end == argv[2] || *rate_end != L'\0' ||
                bits_end == argv[3] || *bits_end != L'\0' ||
                (parsed_rate != 44100 && parsed_rate != 48000 &&
                 parsed_rate != 88200 && parsed_rate != 96000) ||
                (parsed_bits != 16 && parsed_bits != 24)) {
                fwprintf(stderr,
                         L"Format must use 44100/48000/88200/96000 Hz "
                         L"and 16/24 bits.\n");
                return 2;
            }
            set_preferred_format = TRUE;
            preferred_sample_rate = (ULONG)parsed_rate;
            preferred_bits = (ULONG)parsed_bits;
        } else if (wcscmp(argv[1], L"--info") != 0 || argc > 2) {
            print_usage(argv[0]);
            return 2;
        }
    }

    if (scan_interfaces) {
        return scan_native_ldac_interfaces();
    }

    ZeroMemory(&info, sizeof(info));
    device = open_native_ldac_wave_interface(
        interface_path,
        ARRAYSIZE(interface_path),
        &info,
        direct_status);
    if (device == INVALID_HANDLE_VALUE) {
        fwprintf(
            stderr,
            L"Native LDAC WaveSpeaker PCM interface was not found (Win32 %lu).\n",
            GetLastError());
        return 3;
    }

    wprintf(L"Interface: %ls\n", interface_path);
    print_info(&info);

    result = 0;
    if (monitor || monitor_link) {
        (void)SetConsoleCtrlHandler(console_handler, TRUE);
        result = monitor
            ? monitor_pcm(device, duration_seconds)
            : monitor_link_state(device, duration_seconds);
    } else if (link_state) {
        NATIVE_LDAC_LINK_STATE state;

        if (!query_link_state(device, &state)) {
            fwprintf(
                stderr,
                L"LINK STATE failed (Win32 %lu). The installed endpoint "
                L"driver may predate this optional ABI.\n",
                GetLastError());
            result = 4;
        } else {
            print_link_state(&state);
        }
    } else if (physical_presence) {
        NATIVE_LDAC_PRESENCE_STATE state;

        if (!query_physical_presence(device, &state)) {
            fwprintf(
                stderr,
                L"PHYSICAL PRESENCE failed (Win32 %lu). The installed "
                L"endpoint driver may predate this optional ABI.\n",
                GetLastError());
            result = 4;
        } else {
            print_physical_presence(&state);
        }
    } else if (consumer_lease) {
        NATIVE_LDAC_PCM_CONSUMER_LEASE lease;

        if (!query_consumer_lease(device, &lease)) {
            fwprintf(
                stderr,
                L"PCM CONSUMER LEASE failed (Win32 %lu). The installed "
                L"endpoint driver may predate this optional ABI.\n",
                GetLastError());
            result = 4;
        } else {
            print_consumer_lease(&lease);
        }
    } else if (direct_status) {
        NLD_DIRECT_PDO_MEDIA_STATUS_V1 status;

        if (!query_direct_pdo_status(device, &status)) {
            fwprintf(
                stderr,
                L"DIRECT-PDO STATUS failed (Win32 %lu). The installed "
                L"endpoint may not be the coordinated Direct-PDO driver.\n",
                GetLastError());
            result = 4;
        } else if (status.Version != NLD_DIRECT_PDO_MEDIA_ABI_VERSION) {
            fwprintf(
                stderr,
                L"DIRECT-PDO STATUS reported ABI %lu; this probe requires "
                L"ABI %lu.\n",
                status.Version,
                NLD_DIRECT_PDO_MEDIA_ABI_VERSION);
            result = 5;
        } else {
            print_direct_pdo_status(&status);
        }
    } else if (preferred_format || set_preferred_format) {
        NATIVE_LDAC_PREFERRED_FORMAT format;

        if (set_preferred_format) {
            ZeroMemory(&format, sizeof(format));
            format.Size = sizeof(format);
            format.AbiVersion = NATIVE_LDAC_FORMAT_ABI_VERSION;
            format.SampleRate = preferred_sample_rate;
            format.BitsPerSample = preferred_bits;
            format.Flags = NATIVE_LDAC_FORMAT_FLAG_NONE;
            if (!set_property(
                    device,
                    NativeLdacPcmPropertyPreferredFormat,
                    &format,
                    (DWORD)sizeof(format))) {
                fwprintf(stderr,
                         L"SET FORMAT failed (Win32 %lu).\n",
                         GetLastError());
                result = 4;
            }
        }
        if (result == 0) {
            if (!query_preferred_format(device, &format)) {
                fwprintf(stderr,
                         L"FORMAT failed (Win32 %lu). The installed endpoint "
                         L"driver may predate this ABI.\n",
                         GetLastError());
                result = 4;
            } else {
                print_preferred_format(&format);
            }
        }
    } else {
        bytes_returned = 0;
        ZeroMemory(&info, sizeof(info));
        if (!query_property(
                device,
                NativeLdacPcmPropertyInfo,
                &info,
                (DWORD)sizeof(info),
                &bytes_returned)) {
            fwprintf(stderr, L"PCM INFO failed (Win32 %lu).\n", GetLastError());
            result = 4;
        }
    }

    CloseHandle(device);
    return result;
}
