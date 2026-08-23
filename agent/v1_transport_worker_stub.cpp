#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cwchar>

namespace {

struct Options {
    const wchar_t* ready_event = nullptr;
    const wchar_t* stop_event = nullptr;
    const wchar_t* transport_open_event = nullptr;
    const wchar_t* capabilities_discovered_event = nullptr;
    const wchar_t* media_started_event = nullptr;
    const wchar_t* media_stopped_event = nullptr;
    const wchar_t* media_failed_event = nullptr;
    const wchar_t* graceful_transport_stop_event = nullptr;
    const wchar_t* cancel_transport_event = nullptr;
    const wchar_t* single_gain_ready_event = nullptr;
    bool fail_after_open = false;
    bool ignore_stop = false;
};

bool ParseNamedEvent(int argc,
                     wchar_t** argv,
                     int* index,
                     const wchar_t** value) {
    if (*index + 1 >= argc || argv[*index + 1][0] == L'\0') {
        return false;
    }
    *value = argv[++(*index)];
    return true;
}

bool ParseOptions(int argc, wchar_t** argv, Options* options) {
    if (options == nullptr) {
        return false;
    }
    for (int index = 1; index < argc; ++index) {
        if (std::wcscmp(argv[index], L"--ready-event") == 0) {
            if (!ParseNamedEvent(argc, argv, &index,
                                 &options->ready_event)) return false;
        } else if (std::wcscmp(argv[index], L"--stop-event") == 0) {
            if (!ParseNamedEvent(argc, argv, &index,
                                 &options->stop_event)) return false;
        } else if (std::wcscmp(
                       argv[index], L"--transport-open-event") == 0) {
            if (!ParseNamedEvent(argc, argv, &index,
                                 &options->transport_open_event)) return false;
        } else if (std::wcscmp(
                       argv[index],
                       L"--capabilities-discovered-event") == 0) {
            if (!ParseNamedEvent(
                    argc,
                    argv,
                    &index,
                    &options->capabilities_discovered_event)) return false;
        } else if (std::wcscmp(
                       argv[index], L"--media-started-event") == 0) {
            if (!ParseNamedEvent(argc, argv, &index,
                                 &options->media_started_event)) return false;
        } else if (std::wcscmp(
                       argv[index], L"--media-stopped-event") == 0) {
            if (!ParseNamedEvent(argc, argv, &index,
                                 &options->media_stopped_event)) return false;
        } else if (std::wcscmp(
                       argv[index], L"--media-failed-event") == 0) {
            if (!ParseNamedEvent(argc, argv, &index,
                                 &options->media_failed_event)) return false;
        } else if (std::wcscmp(
                       argv[index],
                       L"--graceful-transport-stop-event") == 0) {
            if (!ParseNamedEvent(
                    argc,
                    argv,
                    &index,
                    &options->graceful_transport_stop_event)) return false;
        } else if (std::wcscmp(
                       argv[index], L"--cancel-transport-event") == 0) {
            if (!ParseNamedEvent(argc, argv, &index,
                                 &options->cancel_transport_event)) return false;
        } else if (std::wcscmp(
                       argv[index], L"--single-gain-ready-event") == 0) {
            if (!ParseNamedEvent(argc, argv, &index,
                                 &options->single_gain_ready_event)) return false;
        } else if (std::wcscmp(argv[index], L"--fail-after-open") == 0) {
            options->fail_after_open = true;
        } else if (std::wcscmp(argv[index], L"--ignore-stop") == 0) {
            options->ignore_stop = true;
        } else {
            return false;
        }
    }
    return options->ready_event != nullptr &&
           options->stop_event != nullptr &&
           options->transport_open_event != nullptr &&
           options->capabilities_discovered_event != nullptr &&
           options->media_started_event != nullptr &&
           options->media_stopped_event != nullptr &&
           options->media_failed_event != nullptr &&
           options->graceful_transport_stop_event != nullptr &&
           options->cancel_transport_event != nullptr;
}

void PrintUsage() {
    std::wprintf(
        L"Usage: v1_transport_worker_stub.exe --ready-event <name> "
        L"--stop-event <name> --transport-open-event <name> "
        L"--capabilities-discovered-event <name> "
        L"--media-started-event <name> --media-stopped-event <name> "
        L"--media-failed-event <name> "
        L"--graceful-transport-stop-event <name> "
        L"--cancel-transport-event <name> "
        L"[--single-gain-ready-event <name>] "
        L"[--fail-after-open] [--ignore-stop]\n");
}

HANDLE OpenSignal(const wchar_t* name) {
    return OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
}

HANDLE OpenWait(const wchar_t* name) {
    return OpenEventW(SYNCHRONIZE, FALSE, name);
}

void CloseIfValid(HANDLE value) {
    if (value != nullptr) {
        CloseHandle(value);
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 &&
        (std::wcscmp(argv[1], L"--help") == 0 ||
         std::wcscmp(argv[1], L"-h") == 0)) {
        PrintUsage();
        return 0;
    }
    Options options;
    if (!ParseOptions(argc, argv, &options)) {
        PrintUsage();
        return 2;
    }
#ifdef V1_TRANSPORT_WORKER_STUB_FAIL_AFTER_OPEN
    options.fail_after_open = true;
#endif

    HANDLE ready = OpenSignal(options.ready_event);
    HANDLE stop = OpenWait(options.stop_event);
    HANDLE transport_open = OpenWait(options.transport_open_event);
    HANDLE capabilities_discovered = OpenSignal(
        options.capabilities_discovered_event);
    HANDLE media_started = OpenSignal(options.media_started_event);
    HANDLE media_stopped = OpenSignal(options.media_stopped_event);
    HANDLE media_failed = OpenSignal(options.media_failed_event);
    HANDLE graceful_stop = OpenWait(
        options.graceful_transport_stop_event);
    HANDLE cancel_transport = OpenWait(options.cancel_transport_event);
    if (ready == nullptr || stop == nullptr || transport_open == nullptr ||
        capabilities_discovered == nullptr ||
        media_started == nullptr || media_stopped == nullptr ||
        media_failed == nullptr || graceful_stop == nullptr ||
        cancel_transport == nullptr) {
        CloseIfValid(cancel_transport);
        CloseIfValid(graceful_stop);
        CloseIfValid(media_failed);
        CloseIfValid(media_stopped);
        CloseIfValid(media_started);
        CloseIfValid(transport_open);
        CloseIfValid(capabilities_discovered);
        CloseIfValid(stop);
        CloseIfValid(ready);
        return 3;
    }

    int result = 0;
    if (!SetEvent(ready)) {
        result = 4;
        goto cleanup;
    }
    HANDLE initial_waits[] = {stop, transport_open};
    const DWORD initial = WaitForMultipleObjects(
        ARRAYSIZE(initial_waits), initial_waits, FALSE, 600000u);
    if (initial == WAIT_OBJECT_0) {
        result = 0;
        goto cleanup;
    }
    if (initial != WAIT_OBJECT_0 + 1u) {
        result = 5;
        goto cleanup;
    }
    if (options.fail_after_open) {
        result = SetEvent(media_failed) ? 20 : 6;
        goto cleanup;
    }
    if (!SetEvent(capabilities_discovered)) {
        result = 11;
        goto cleanup;
    }
    if (!SetEvent(media_started)) {
        result = 7;
        goto cleanup;
    }
    if (options.ignore_stop) {
        (void)WaitForSingleObject(GetCurrentProcess(), INFINITE);
    }
    if (WaitForSingleObject(stop, 600000u) != WAIT_OBJECT_0) {
        result = 8;
        goto cleanup;
    }
    if (WaitForSingleObject(cancel_transport, 0u) != WAIT_OBJECT_0 &&
        WaitForSingleObject(graceful_stop, 0u) != WAIT_OBJECT_0) {
        result = 9;
        goto cleanup;
    }
    if (!SetEvent(media_stopped)) {
        result = 10;
    }

cleanup:
    CloseHandle(cancel_transport);
    CloseHandle(graceful_stop);
    CloseHandle(media_failed);
    CloseHandle(media_stopped);
    CloseHandle(media_started);
    CloseHandle(transport_open);
    CloseHandle(capabilities_discovered);
    CloseHandle(stop);
    CloseHandle(ready);
    return result;
}
