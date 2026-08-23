// SPDX-License-Identifier: Apache-2.0
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cwchar>

#include "ldac_native/ldac_encoder.h"
#include "ldac_native/native_pcm_source.h"

namespace {

constexpr int kExitSourceCreateBase = 0x100;
constexpr int kExitSourceSnapshotBase = 0x200;
constexpr int kExitConsumerLeaseBase = 0x280;
constexpr int kExitEncoderCreateBase = 0x300;
constexpr int kExitSourceReadBase = 0x400;
constexpr int kExitPartialRead = 0x480;
constexpr int kExitEncodeBase = 0x500;
constexpr int kExitReadyEventBase = 0x600;

int StatusExitCode(int base, int status) {
    return base + (status < 0 ? -status : status);
}

struct Options {
    const wchar_t* ready_event_name = nullptr;
    const wchar_t* stop_event_name = nullptr;
};

bool ParseOptions(int argc, wchar_t** argv, Options* options) {
    if (options == nullptr) {
        return false;
    }
    for (int index = 1; index < argc; ++index) {
        if (std::wcscmp(argv[index], L"--ready-event") == 0 &&
            index + 1 < argc) {
            options->ready_event_name = argv[++index];
        } else if (std::wcscmp(argv[index], L"--stop-event") == 0 &&
                   index + 1 < argc) {
            options->stop_event_name = argv[++index];
        } else {
            return false;
        }
    }
    return options->ready_event_name != nullptr &&
           options->ready_event_name[0] != L'\0' &&
           options->stop_event_name != nullptr &&
           options->stop_event_name[0] != L'\0';
}

void PrintUsage() {
    std::wprintf(
        L"Usage: v1_pcm_encode_engine.exe --ready-event <name> "
        L"--stop-event <name>\n");
}

bool StopRequested(HANDLE stop_event) {
    return WaitForSingleObject(stop_event, 0u) == WAIT_OBJECT_0;
}

int Run(HANDLE ready_event, HANDLE stop_event) {
    native_pcm_source* source = nullptr;
    ldac_encoder* encoder = nullptr;
    native_pcm_source_snapshot snapshot{};
    float pcm[LDAC_ENCODER_PCM_FRAMES_PER_CALL *
              LDAC_ENCODER_STEREO_CHANNELS]{};
    std::uint8_t encoded[LDAC_ENCODER_MAX_OUTPUT_BYTES]{};
    int result = 5;
    const std::uint64_t consumer_generation =
        (static_cast<std::uint64_t>(GetCurrentProcessId()) << 32u) |
        (GetTickCount64() & 0xFFFFFFFFu);

    const native_pcm_source_status source_result =
        native_pcm_source_create(&source);
    if (source_result != NATIVE_PCM_SOURCE_OK || source == nullptr) {
        return StatusExitCode(kExitSourceCreateBase,
                              static_cast<int>(source_result));
    }
    const native_pcm_source_status snapshot_result =
        native_pcm_source_get_snapshot(source, &snapshot);
    if (snapshot_result != NATIVE_PCM_SOURCE_OK) {
        result = StatusExitCode(kExitSourceSnapshotBase,
                                static_cast<int>(snapshot_result));
        goto cleanup;
    }
    if (!snapshot.stream_active) {
        result = kExitSourceSnapshotBase;
        goto cleanup;
    }
    const native_pcm_source_status lease_result =
        native_pcm_source_acquire_consumer(source, consumer_generation);
    if (lease_result != NATIVE_PCM_SOURCE_OK) {
        result = StatusExitCode(kExitConsumerLeaseBase,
                                static_cast<int>(lease_result));
        goto cleanup;
    }
    const ldac_encoder_status create_result =
        ldac_encoder_create(&encoder,
                            LDAC_ENCODER_MAX_OUTPUT_BYTES,
                            LDAC_ENCODER_QUALITY_HQ,
                            snapshot.sample_rate_hz);
    if (create_result != LDAC_ENCODER_OK ||
        encoder == nullptr) {
        result = StatusExitCode(kExitEncoderCreateBase,
                                static_cast<int>(create_result));
        goto cleanup;
    }

    for (;;) {
        if (StopRequested(stop_event)) {
            result = 0;
            break;
        }
        size_t frames_read = 0u;
        const native_pcm_source_status read_result =
            native_pcm_source_read_f32_stereo(
                source,
                pcm,
                LDAC_ENCODER_PCM_FRAMES_PER_CALL,
                100u,
                &frames_read);
        if (read_result == NATIVE_PCM_SOURCE_TIMEOUT) {
            continue;
        }
        if (read_result != NATIVE_PCM_SOURCE_OK) {
            result = StatusExitCode(kExitSourceReadBase,
                                    static_cast<int>(read_result));
            break;
        }
        if (frames_read != LDAC_ENCODER_PCM_FRAMES_PER_CALL) {
            result = kExitPartialRead;
            break;
        }
        size_t encoded_size = 0u;
        std::uint8_t frame_count = 0u;
        const ldac_encoder_status encode_result = ldac_encoder_encode_f32(
                encoder,
                pcm,
                frames_read,
                encoded,
                sizeof(encoded),
                &encoded_size,
                &frame_count);
        if (encode_result != LDAC_ENCODER_OK) {
            result = StatusExitCode(kExitEncodeBase,
                                    static_cast<int>(encode_result));
            break;
        }
        if (encoded_size != 0u && frame_count != 0u) {
            if (!SetEvent(ready_event)) {
                result = kExitReadyEventBase +
                         static_cast<int>(GetLastError() & 0xFFu);
                break;
            }
        }
    }

cleanup:
    ldac_encoder_destroy(encoder);
    (void)native_pcm_source_release_consumer(source);
    native_pcm_source_destroy(source);
    return result;
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
    HANDLE ready_event = OpenEventW(EVENT_MODIFY_STATE,
                                    FALSE,
                                    options.ready_event_name);
    if (ready_event == nullptr) {
        return 3;
    }
    HANDLE stop_event = OpenEventW(SYNCHRONIZE,
                                   FALSE,
                                   options.stop_event_name);
    if (stop_event == nullptr) {
        CloseHandle(ready_event);
        return 4;
    }
    const int result = Run(ready_event, stop_event);
    CloseHandle(stop_event);
    CloseHandle(ready_event);
    return result;
}
