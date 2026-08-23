// SPDX-License-Identifier: Apache-2.0
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cwchar>

#include "ldac_native/direct_pdo_media_sink.h"
#include "ldac_native/ldac_encoder.h"
#include "ldac_native/native_pcm_source.h"
#include "ldac_native/rtp_ldac.h"

namespace {

HANDLE g_stop_event = nullptr;
volatile LONG g_cancelled = 0;

BOOL WINAPI ConsoleHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
        type == CTRL_CLOSE_EVENT || type == CTRL_LOGOFF_EVENT ||
        type == CTRL_SHUTDOWN_EVENT) {
        InterlockedExchange(&g_cancelled, 1);
        return TRUE;
    }
    return FALSE;
}

bool StopRequested() {
    return InterlockedCompareExchange(&g_cancelled, 0, 0) != 0 ||
           (g_stop_event != nullptr &&
            WaitForSingleObject(g_stop_event, 0) == WAIT_OBJECT_0);
}

bool ParseQuality(const wchar_t* value, ldac_encoder_quality* quality) {
    if (value == nullptr || quality == nullptr) return false;
    if (_wcsicmp(value, L"hq") == 0 || _wcsicmp(value, L"auto") == 0) {
        *quality = LDAC_ENCODER_QUALITY_HQ;
        return true;
    }
    if (_wcsicmp(value, L"sq") == 0) {
        *quality = LDAC_ENCODER_QUALITY_SQ;
        return true;
    }
    if (_wcsicmp(value, L"mq") == 0) {
        *quality = LDAC_ENCODER_QUALITY_MQ;
        return true;
    }
    return false;
}

bool ParseChannelMode(const wchar_t* value,
                      ldac_encoder_channel_mode* mode) {
    if (value == nullptr || mode == nullptr) return false;
    if (_wcsicmp(value, L"stereo") == 0) {
        *mode = LDAC_ENCODER_CHANNEL_STEREO;
        return true;
    }
    if (_wcsicmp(value, L"dual") == 0) {
        *mode = LDAC_ENCODER_CHANNEL_DUAL;
        return true;
    }
    if (_wcsicmp(value, L"mono") == 0) {
        *mode = LDAC_ENCODER_CHANNEL_MONO;
        return true;
    }
    return false;
}

bool WaitUntilSample(const LARGE_INTEGER& start,
                     const LARGE_INTEGER& frequency,
                     std::uint64_t sample_offset,
                     unsigned sample_rate_hz) {
    const LONGLONG target = start.QuadPart +
        static_cast<LONGLONG>((sample_offset *
            static_cast<std::uint64_t>(frequency.QuadPart)) /
            sample_rate_hz);
    for (;;) {
        if (StopRequested()) return false;
        LARGE_INTEGER now{};
        if (!QueryPerformanceCounter(&now)) return false;
        const LONGLONG remaining = target - now.QuadPart;
        if (remaining <= 0) return true;
        const DWORD sleep_ms = static_cast<DWORD>(
            (remaining * 1000) / frequency.QuadPart);
        if (sleep_ms > 1u) {
            Sleep(sleep_ms - 1u);
        } else {
            (void)SwitchToThread();
        }
    }
}

int Run(ldac_encoder_quality quality,
        ldac_encoder_channel_mode channel_mode) {
    native_pcm_source* source = nullptr;
    direct_pdo_media_sink* sink = nullptr;
    ldac_encoder* encoder = nullptr;
    native_pcm_source_snapshot source_status{};
    NLD_DIRECT_PDO_MEDIA_STATUS_V1 media_status{};
    wchar_t interface_path[NATIVE_PCM_SOURCE_PATH_CAPACITY]{};
    float pcm[LDAC_ENCODER_PCM_FRAMES_PER_CALL *
              LDAC_ENCODER_STEREO_CHANNELS]{};
    std::uint8_t encoded[LDAC_ENCODER_MAX_OUTPUT_BYTES]{};
    std::uint8_t packet[NLD_DIRECT_PDO_MEDIA_MAX_PACKET_SIZE]{};
    std::uint16_t sequence = 0;
    std::uint32_t timestamp = 0;
    const std::uint32_t ssrc = 0x4C444143u ^ GetTickCount();
    std::uint64_t packet_count = 0;
    std::uint64_t encoded_bytes = 0;
    std::uint64_t sent_samples = 0;
    LARGE_INTEGER pacing_start{};
    LARGE_INTEGER pacing_frequency{};
    native_pcm_source_status source_result = NATIVE_PCM_SOURCE_NOT_FOUND;
    int result = 5;

    if (direct_pdo_media_sink_create_first(&sink,
                                           interface_path,
                                           NATIVE_PCM_SOURCE_PATH_CAPACITY) !=
        DIRECT_PDO_MEDIA_SINK_OK) {
        std::fwprintf(stderr,
                      L"Could not locate Direct-PDO media interface.\n");
        goto cleanup;
    }
    source_result = native_pcm_source_create_for_interface(interface_path,
                                                            &source);
    if (source_result != NATIVE_PCM_SOURCE_OK || source == nullptr) {
        std::fwprintf(stderr,
                      L"Could not open Native LDAC PCM source (status %d).\n",
                      static_cast<int>(source_result));
        goto cleanup;
    }
    std::wprintf(L"Direct-PDO engine armed; waiting for WaveRT RUN.\n");
    for (;;) {
        if (StopRequested()) {
            result = 130;
            goto cleanup;
        }
        source_result = native_pcm_source_get_snapshot(source, &source_status);
        if (source_result != NATIVE_PCM_SOURCE_OK) {
            std::fwprintf(stderr,
                          L"Could not query Native LDAC PCM state "
                          L"(status %d).\n",
                          static_cast<int>(source_result));
            goto cleanup;
        }
        if (source_status.stream_active) break;
        Sleep(50u);
    }
    {
        const ULONGLONG deadline = GetTickCount64() + 10000u;
        for (;;) {
            const direct_pdo_media_sink_status status =
                direct_pdo_media_sink_get_status(sink, &media_status);
            if (status == DIRECT_PDO_MEDIA_SINK_OK &&
                media_status.State == NldDirectPdoMediaStreaming &&
                media_status.MediaGeneration != 0u &&
                media_status.OutgoingMtu > LDAC_RTP_OVERHEAD) {
                break;
            }
            if (StopRequested()) {
                result = 130;
                goto cleanup;
            }
            if (GetTickCount64() >= deadline) {
                std::fwprintf(stderr,
                              L"Direct-PDO Media channel did not reach "
                              L"START within ten seconds.\n");
                goto cleanup;
            }
            Sleep(20u);
        }
    }
    if (!QueryPerformanceFrequency(&pacing_frequency) ||
        !QueryPerformanceCounter(&pacing_start)) {
        std::fwprintf(stderr, L"High-resolution clock is unavailable.\n");
        goto cleanup;
    }

    {
        const size_t payload_mtu =
            media_status.OutgoingMtu - LDAC_RTP_OVERHEAD;
        const ldac_encoder_status status =
            ldac_encoder_create_with_channel_mode(
                &encoder,
                payload_mtu,
                quality,
                source_status.sample_rate_hz,
                channel_mode);
        if (status != LDAC_ENCODER_OK || encoder == nullptr) {
            std::fwprintf(stderr,
                          L"Could not create LDAC encoder (status %d).\n",
                          static_cast<int>(status));
            goto cleanup;
        }
    }
    std::wprintf(L"Direct-PDO LDAC engine started: %u Hz, %u-bit, MTU %u, "
                 L"generation %u.\n",
                 source_status.sample_rate_hz,
                 source_status.bits_per_sample,
                 media_status.OutgoingMtu,
                 media_status.MediaGeneration);

    for (;;) {
        size_t frames_read = 0;
        size_t encoded_size = 0;
        size_t packet_size = 0;
        std::uint8_t frame_count = 0;
        if (StopRequested()) {
            result = 130;
            break;
        }
        source_result = native_pcm_source_read_f32_stereo(
            source,
            pcm,
            LDAC_ENCODER_PCM_FRAMES_PER_CALL,
            100u,
            &frames_read);
        if (source_result == NATIVE_PCM_SOURCE_TIMEOUT) {
            if (native_pcm_source_get_snapshot(source, &source_status) ==
                    NATIVE_PCM_SOURCE_OK &&
                !source_status.stream_active) {
                std::wprintf(L"WaveRT left RUN; Direct-PDO encoder stopped.\n");
                result = 0;
                break;
            }
            continue;
        }
        if (source_result != NATIVE_PCM_SOURCE_OK ||
            frames_read != LDAC_ENCODER_PCM_FRAMES_PER_CALL) {
            std::fwprintf(stderr,
                          L"Native PCM read failed (status %d, frames %zu).\n",
                          static_cast<int>(source_result),
                          frames_read);
            break;
        }
        const ldac_encoder_status encode_result = ldac_encoder_encode_f32(
            encoder,
            pcm,
            frames_read,
            encoded,
            sizeof(encoded),
            &encoded_size,
            &frame_count);
        if (encode_result != LDAC_ENCODER_OK) {
            std::fwprintf(stderr,
                          L"LDAC encode failed (status %d).\n",
                          static_cast<int>(encode_result));
            break;
        }
        if (encoded_size == 0u) continue;
        if (ldac_rtp_build_unfragmented(packet,
                                        sizeof(packet),
                                        media_status.OutgoingMtu,
                                        sequence,
                                        timestamp,
                                        ssrc,
                                        frame_count,
                                        encoded,
                                        encoded_size,
                                        &packet_size) != LDAC_RTP_OK) {
            std::fwprintf(stderr, L"LDAC RTP packetization failed.\n");
            break;
        }
        if (!WaitUntilSample(pacing_start,
                             pacing_frequency,
                             sent_samples,
                             source_status.sample_rate_hz)) {
            result = StopRequested() ? 130 : 5;
            break;
        }
        const direct_pdo_media_sink_status write_result =
            direct_pdo_media_sink_write(sink,
                                        media_status.MediaGeneration,
                                        packet,
                                        packet_size);
        if (write_result != DIRECT_PDO_MEDIA_SINK_OK) {
            std::fwprintf(stderr,
                          L"Direct-PDO media write stopped (status %d, "
                          L"Win32 %lu).\n",
                          static_cast<int>(write_result),
                          direct_pdo_media_sink_last_error(sink));
            break;
        }
        sequence++;
        const unsigned packet_samples = frame_count *
            ldac_encoder_samples_per_transport_frame(encoder);
        timestamp += packet_samples;
        sent_samples += packet_samples;
        packet_count++;
        encoded_bytes += encoded_size;
        if ((packet_count % 200u) == 0u) {
            std::wprintf(L"Direct: %llu packets, %llu LDAC bytes.\n",
                         static_cast<unsigned long long>(packet_count),
                         static_cast<unsigned long long>(encoded_bytes));
        }
    }

cleanup:
    ldac_encoder_destroy(encoder);
    direct_pdo_media_sink_destroy(sink);
    native_pcm_source_destroy(source);
    return result;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    ldac_encoder_quality quality = LDAC_ENCODER_QUALITY_HQ;
    ldac_encoder_channel_mode channel_mode = LDAC_ENCODER_CHANNEL_STEREO;
    for (int index = 1; index < argc; ++index) {
        if (_wcsicmp(argv[index], L"--quality") == 0) {
            if (++index >= argc || !ParseQuality(argv[index], &quality)) {
                return 2;
            }
        } else if (_wcsicmp(argv[index], L"--channel-mode") == 0) {
            if (++index >= argc ||
                !ParseChannelMode(argv[index], &channel_mode)) {
                return 2;
            }
        } else if (_wcsicmp(argv[index], L"--stop-event") == 0) {
            if (++index >= argc || argv[index][0] == L'\0') return 2;
            g_stop_event = OpenEventW(SYNCHRONIZE, FALSE, argv[index]);
            if (g_stop_event == nullptr) return 2;
        } else if (_wcsicmp(argv[index], L"--help") == 0 ||
                   _wcsicmp(argv[index], L"-h") == 0) {
            std::wprintf(L"Usage: %ls [--quality mq|sq|hq|auto] "
                         L"[--channel-mode stereo|dual|mono] "
                         L"[--stop-event name]\n",
                         argv[0]);
            return 0;
        } else {
            return 2;
        }
    }
    if (channel_mode != LDAC_ENCODER_CHANNEL_STEREO) {
        std::fwprintf(stderr,
                      L"Direct-PDO milestone currently supports stereo "
                      L"only.\n");
        if (g_stop_event != nullptr) CloseHandle(g_stop_event);
        g_stop_event = nullptr;
        return 2;
    }
    (void)SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    const int result = Run(quality, channel_mode);
    if (g_stop_event != nullptr) CloseHandle(g_stop_event);
    g_stop_event = nullptr;
    return result;
}
