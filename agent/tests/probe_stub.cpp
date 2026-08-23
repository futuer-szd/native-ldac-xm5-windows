#define UNICODE
#define _UNICODE

#include <windows.h>

#include <cstdio>
#include <cwchar>

int wmain(int argc, wchar_t** argv) {
    const wchar_t* stop_event_name = nullptr;
    const wchar_t* open_attempts = nullptr;
    const wchar_t* quality = nullptr;
    const wchar_t* channel_mode = nullptr;
    const wchar_t* sample_rate = nullptr;
    const wchar_t* bits = nullptr;
    (void)setvbuf(stdout, nullptr, _IONBF, 0u);
    for (int index = 1; index < argc; ++index) {
        if (wcscmp(argv[index], L"--stop-event") == 0 &&
            index + 1 < argc) {
            stop_event_name = argv[++index];
        } else if (wcscmp(argv[index], L"--open-attempts") == 0 &&
                   index + 1 < argc) {
            open_attempts = argv[++index];
        } else if (wcscmp(argv[index], L"--quality") == 0 &&
                   index + 1 < argc) {
            quality = argv[++index];
        } else if (wcscmp(argv[index], L"--channel-mode") == 0 &&
                   index + 1 < argc) {
            channel_mode = argv[++index];
        } else if (wcscmp(argv[index], L"--sample-rate") == 0 &&
                   index + 1 < argc) {
            sample_rate = argv[++index];
        } else if (wcscmp(argv[index], L"--bits") == 0 &&
                   index + 1 < argc) {
            bits = argv[++index];
        }
    }
    if (stop_event_name == nullptr) {
        fwprintf(stderr, L"Missing --stop-event.\n");
        return 2;
    }
    if (open_attempts == nullptr || wcscmp(open_attempts, L"1") != 0) {
        fwprintf(stderr, L"Agent must constrain signaling open to one attempt.\n");
        return 5;
    }
    if (quality == nullptr) {
        fwprintf(stderr, L"Missing --quality.\n");
        return 6;
    }
    if (channel_mode == nullptr) {
        fwprintf(stderr, L"Missing --channel-mode.\n");
        return 7;
    }
    if (sample_rate == nullptr || bits == nullptr) {
        fwprintf(stderr, L"Missing endpoint format.\n");
        return 8;
    }

    HANDLE stop_event = OpenEventW(SYNCHRONIZE,
                                   FALSE,
                                   stop_event_name);
    if (stop_event == nullptr) {
        fwprintf(stderr, L"Could not open stop event.\n");
        return 3;
    }
    wprintf(L"Agent probe stub started.\n");
    wprintf(L"Agent probe stub quality: %ls.\n", quality);
    wprintf(L"Agent probe stub channel mode: %ls.\n", channel_mode);
    wprintf(L"Agent probe stub format: %ls Hz, %ls-bit.\n",
            sample_rate,
            bits);
    if (wcsstr(stop_event_name, L".1.ctest-reconnect") != nullptr) {
        wprintf(L"Agent probe stub simulating an initial connection failure.\n");
        CloseHandle(stop_event);
        return 4;
    }
    const DWORD wait = WaitForSingleObject(stop_event, 10000);
    CloseHandle(stop_event);
    if (wait == WAIT_OBJECT_0) {
        wprintf(L"Agent probe stub stopped gracefully.\n");
        return 0;
    }
    fwprintf(stderr, L"Agent probe stub timed out.\n");
    return 4;
}
