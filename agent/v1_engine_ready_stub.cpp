#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cwchar>

namespace {

struct Options {
    const wchar_t* ready_event = nullptr;
    const wchar_t* stop_event = nullptr;
    bool ignore_stop = false;
};

bool ParseOptions(int argc, wchar_t** argv, Options* options) {
    if (options == nullptr) {
        return false;
    }
    for (int index = 1; index < argc; ++index) {
        if (std::wcscmp(argv[index], L"--ready-event") == 0 &&
            index + 1 < argc) {
            options->ready_event = argv[++index];
        } else if (std::wcscmp(argv[index], L"--stop-event") == 0 &&
                   index + 1 < argc) {
            options->stop_event = argv[++index];
        } else if (std::wcscmp(argv[index], L"--ignore-stop") == 0) {
            options->ignore_stop = true;
        } else {
            return false;
        }
    }
    return options->ready_event != nullptr &&
           options->ready_event[0] != L'\0' &&
           options->stop_event != nullptr &&
           options->stop_event[0] != L'\0';
}

void PrintUsage() {
    std::wprintf(
        L"Usage: v1_engine_ready_stub.exe --ready-event <name> "
        L"--stop-event <name> [--ignore-stop]\n");
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

    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE,
                              FALSE,
                              options.ready_event);
    if (ready == nullptr) {
        return 3;
    }
    HANDLE stop = OpenEventW(SYNCHRONIZE,
                             FALSE,
                             options.stop_event);
    if (stop == nullptr) {
        CloseHandle(ready);
        return 4;
    }
    if (!SetEvent(ready)) {
        CloseHandle(stop);
        CloseHandle(ready);
        return 5;
    }

    const DWORD wait = options.ignore_stop
                           ? WaitForSingleObject(GetCurrentProcess(), INFINITE)
                           : WaitForSingleObject(stop, 600000u);
    CloseHandle(stop);
    CloseHandle(ready);
    return wait == WAIT_OBJECT_0 ? 0 : 6;
}
