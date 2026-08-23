#include "../v1_engine_ready_host.h"

#include <array>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

const wchar_t* FindOptionValue(int argc,
                               wchar_t** argv,
                               const wchar_t* option) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::wcscmp(argv[index], option) == 0) {
            return argv[index + 1];
        }
    }
    return nullptr;
}

bool HasOption(int argc, wchar_t** argv, const wchar_t* option) {
    for (int index = 1; index < argc; ++index) {
        if (std::wcscmp(argv[index], option) == 0) {
            return true;
        }
    }
    return false;
}

std::uint64_t ParseGeneration(const wchar_t* ready_event_name) {
    if (ready_event_name == nullptr) {
        return 0u;
    }
    const std::wstring name(ready_event_name);
    const std::size_t suffix = name.rfind(L".ready");
    if (suffix == std::wstring::npos || suffix == 0u) {
        return 0u;
    }
    const std::size_t tick_separator = name.rfind(L'.', suffix - 1u);
    if (tick_separator == std::wstring::npos || tick_separator == 0u) {
        return 0u;
    }
    const std::size_t generation_separator =
        name.rfind(L'.', tick_separator - 1u);
    if (generation_separator == std::wstring::npos ||
        generation_separator + 1u == tick_separator) {
        return 0u;
    }
    const std::wstring text = name.substr(
        generation_separator + 1u,
        tick_separator - generation_separator - 1u);
    wchar_t* end = nullptr;
    const unsigned long long value = std::wcstoull(text.c_str(), &end, 10);
    return end != text.c_str() && end != nullptr && *end == L'\0'
               ? static_cast<std::uint64_t>(value)
               : 0u;
}

HANDLE OpenTestEvent(const wchar_t* name) {
    return name == nullptr
               ? nullptr
               : OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE,
                            FALSE,
                            name);
}

void CloseTestEvent(HANDLE* handle) {
    if (*handle != nullptr) {
        CloseHandle(*handle);
        *handle = nullptr;
    }
}

int RunGenerationProvenanceStub(int argc, wchar_t** argv) {
    const wchar_t* ready_name =
        FindOptionValue(argc, argv, L"--ready-event");
    const wchar_t* stop_name =
        FindOptionValue(argc, argv, L"--stop-event");
    const std::uint64_t generation = ParseGeneration(ready_name);
    if (generation == 0u || stop_name == nullptr) {
        return 101;
    }

    HANDLE ready = OpenTestEvent(ready_name);
    HANDLE stop = OpenTestEvent(stop_name);
    HANDLE transport_open = OpenTestEvent(
        FindOptionValue(argc, argv, L"--transport-open-event"));
    HANDLE capabilities = OpenTestEvent(
        FindOptionValue(argc, argv, L"--capabilities-discovered-event"));
    HANDLE media_started = OpenTestEvent(
        FindOptionValue(argc, argv, L"--media-started-event"));
    HANDLE media_stopped = OpenTestEvent(
        FindOptionValue(argc, argv, L"--media-stopped-event"));
    HANDLE graceful_stop = OpenTestEvent(
        FindOptionValue(argc, argv, L"--graceful-transport-stop-event"));
    HANDLE cancel_transport = OpenTestEvent(
        FindOptionValue(argc, argv, L"--cancel-transport-event"));
    const bool transport_worker = transport_open != nullptr;

    int result = 0;
    if (ready == nullptr || stop == nullptr ||
        (transport_worker &&
         (capabilities == nullptr || media_started == nullptr ||
          media_stopped == nullptr || graceful_stop == nullptr ||
          cancel_transport == nullptr))) {
        result = 102;
        goto cleanup;
    }

    // Generation ending in 2 is intentionally much slower. The parent can
    // observe generation 1's ready event while generation 2 is alive but has
    // not signaled its own ready event yet.
    Sleep(generation % 10u == 2u ? 2000u : 100u);
    if (!SetEvent(ready)) {
        result = 103;
        goto cleanup;
    }
    if (!transport_worker) {
        if (HasOption(argc, argv, L"--ignore-stop")) {
            (void)WaitForSingleObject(GetCurrentProcess(), INFINITE);
        }
        result = WaitForSingleObject(stop, 10000u) == WAIT_OBJECT_0
                     ? 0
                     : 104;
        goto cleanup;
    }

    {
        HANDLE waits[] = {stop, transport_open};
        const DWORD wait = WaitForMultipleObjects(
            ARRAYSIZE(waits), waits, FALSE, 10000u);
        if (wait == WAIT_OBJECT_0) {
            result = 0;
            goto cleanup;
        }
        if (wait != WAIT_OBJECT_0 + 1u ||
            !SetEvent(capabilities) || !SetEvent(media_started)) {
            result = 105;
            goto cleanup;
        }
    }
    if (WaitForSingleObject(stop, 10000u) != WAIT_OBJECT_0 ||
        (WaitForSingleObject(graceful_stop, 0u) != WAIT_OBJECT_0 &&
         WaitForSingleObject(cancel_transport, 0u) != WAIT_OBJECT_0) ||
        !SetEvent(media_stopped)) {
        result = 106;
    }

cleanup:
    CloseTestEvent(&cancel_transport);
    CloseTestEvent(&graceful_stop);
    CloseTestEvent(&media_stopped);
    CloseTestEvent(&media_started);
    CloseTestEvent(&capabilities);
    CloseTestEvent(&transport_open);
    CloseTestEvent(&stop);
    CloseTestEvent(&ready);
    return result;
}

int Fail(const char* message, DWORD error = ERROR_SUCCESS) {
    std::fprintf(stderr, "%s (Win32 %lu)\n", message, error);
    return 1;
}

int FailUnexpectedTransportEvent(
    const char* message,
    native_ldac::agent::V1TransportWorkerEvent event) {
    std::fprintf(stderr,
                 "%s (transport event %u)\n",
                 message,
                 static_cast<unsigned>(event));
    return 1;
}

bool WaitReady(native_ldac::agent::V1EngineReadyHost* host,
               DWORD timeout_ms,
               DWORD* error) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    while (GetTickCount64() < deadline) {
        bool ready = false;
        if (!host->PollReady(&ready, error)) {
            return false;
        }
        if (ready) {
            return true;
        }
        Sleep(10u);
    }
    *error = WAIT_TIMEOUT;
    return false;
}

bool WaitTransportEvent(
    native_ldac::agent::V1EngineReadyHost* host,
    native_ldac::agent::V1TransportWorkerEvent expected,
    DWORD timeout_ms,
    DWORD* error) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    while (GetTickCount64() < deadline) {
        native_ldac::agent::V1TransportWorkerEvent event =
            native_ldac::agent::V1TransportWorkerEvent::None;
        if (!host->PollTransportEvent(&event, error)) {
            return false;
        }
        if (event == expected) {
            return true;
        }
        if (event != native_ldac::agent::V1TransportWorkerEvent::None) {
            *error = ERROR_INVALID_DATA;
            return false;
        }
        Sleep(10u);
    }
    *error = WAIT_TIMEOUT;
    return false;
}

bool PollNoTransportEvent(native_ldac::agent::V1EngineReadyHost* host,
                          DWORD* error) {
    native_ldac::agent::V1TransportWorkerEvent event =
        native_ldac::agent::V1TransportWorkerEvent::None;
    return host->PollTransportEvent(&event, error) &&
           event == native_ldac::agent::V1TransportWorkerEvent::None;
}

int VerifyGenerationProvenance(const std::wstring& self_path) {
    constexpr std::uint64_t kOldReadyGeneration = 91001u;
    constexpr std::uint64_t kNewReadyGeneration = 91002u;
    constexpr std::uint64_t kOldTransportGeneration = 92003u;
    constexpr std::uint64_t kNewTransportGeneration = 92004u;
    DWORD error = ERROR_SUCCESS;
    DWORD exit_code = MAXDWORD;

    native_ldac::agent::V1EngineReadyHost old_ready;
    native_ldac::agent::V1EngineReadyHost new_ready;
    if (!old_ready.Start(self_path,
                         kOldReadyGeneration,
                         false,
                         &error) ||
        !new_ready.Start(self_path,
                         kNewReadyGeneration,
                         false,
                         &error) ||
        old_ready.process_id() == new_ready.process_id() ||
        !WaitReady(&old_ready, 1500u, &error)) {
        return Fail("Could not stage overlapping ready generations.", error);
    }
    bool ready = true;
    if (!new_ready.PollReady(&ready, &error) || ready ||
        new_ready.ready_observed()) {
        return Fail("An old generation ready event reached the new host.",
                    error);
    }
    bool exited = true;
    if (!old_ready.PollExited(&exited, nullptr, &error) || exited ||
        !new_ready.PollExited(&exited, nullptr, &error) || exited) {
        return Fail("Overlapping generation child exited prematurely.", error);
    }
    if (!old_ready.Stop(2000u, &exit_code, &error) || exit_code != 0u ||
        !WaitReady(&new_ready, 3000u, &error) ||
        !new_ready.Stop(2000u, &exit_code, &error) || exit_code != 0u) {
        return Fail("Overlapping ready generations did not stop cleanly.",
                    error);
    }

    native_ldac::agent::V1EngineReadyHost old_transport;
    native_ldac::agent::V1EngineReadyHost new_transport;
    if (!old_transport.StartTransportWorker(
            self_path, kOldTransportGeneration, false, &error) ||
        !new_transport.StartTransportWorker(
            self_path, kNewTransportGeneration, false, &error) ||
        !WaitReady(&old_transport, 1500u, &error) ||
        !WaitReady(&new_transport, 1500u, &error) ||
        !old_transport.AuthorizeTransportOpen(&error) ||
        !WaitTransportEvent(
            &old_transport,
            native_ldac::agent::V1TransportWorkerEvent::
                CapabilitiesDiscovered,
            1500u,
            &error) ||
        !WaitTransportEvent(
            &old_transport,
            native_ldac::agent::V1TransportWorkerEvent::MediaStarted,
            1500u,
            &error) ||
        !PollNoTransportEvent(&new_transport, &error)) {
        return Fail("Old transport events reached the new generation.", error);
    }
    if (!old_transport.Stop(
            native_ldac::agent::V1EngineStopMode::GracefulTransport,
            2000u,
            &exit_code,
            &error) ||
        exit_code != 0u ||
        !old_transport.last_transport_stop_acknowledged() ||
        !PollNoTransportEvent(&new_transport, &error) ||
        new_transport.last_transport_stop_acknowledged() ||
        new_transport.transport_open_authorized()) {
        return Fail("Old MediaStopped acknowledgement crossed generations.",
                    error);
    }
    if (!new_transport.AuthorizeTransportOpen(&error) ||
        !WaitTransportEvent(
            &new_transport,
            native_ldac::agent::V1TransportWorkerEvent::
                CapabilitiesDiscovered,
            1500u,
            &error) ||
        !WaitTransportEvent(
            &new_transport,
            native_ldac::agent::V1TransportWorkerEvent::MediaStarted,
            1500u,
            &error) ||
        !new_transport.Stop(
            native_ldac::agent::V1EngineStopMode::GracefulTransport,
            2000u,
            &exit_code,
            &error) ||
        exit_code != 0u ||
        !new_transport.last_transport_stop_acknowledged()) {
        return Fail("New generation transport did not remain independent.",
                    error);
    }
    return 0;
}

int VerifyMultiGenerationHostStress(const std::wstring& self_path) {
    constexpr std::uint64_t kGenerationBase = 93000u;
    constexpr unsigned kRounds = 8u;
    DWORD baseline_handles = 0u;
    if (!GetProcessHandleCount(GetCurrentProcess(), &baseline_handles)) {
        return Fail("Could not read the host handle baseline.",
                    GetLastError());
    }

    for (unsigned round = 0u; round < kRounds; ++round) {
        const std::uint64_t old_generation =
            kGenerationBase + static_cast<std::uint64_t>(round) * 10u + 1u;
        const std::uint64_t new_generation = old_generation + 1u;
        native_ldac::agent::V1EngineReadyHost old_host;
        native_ldac::agent::V1EngineReadyHost new_host;
        DWORD error = ERROR_SUCCESS;
        if (!old_host.StartTransportWorker(
                self_path, old_generation, false, &error) ||
            !new_host.StartTransportWorker(
                self_path, new_generation, false, &error)) {
            return Fail("Could not start a multi-generation host pair.",
                        error);
        }
        HANDLE old_process = OpenProcess(
            SYNCHRONIZE, FALSE, old_host.process_id());
        HANDLE new_process = OpenProcess(
            SYNCHRONIZE, FALSE, new_host.process_id());
        if (old_process == nullptr || new_process == nullptr) {
            const DWORD failure = GetLastError();
            if (old_process != nullptr) CloseHandle(old_process);
            if (new_process != nullptr) CloseHandle(new_process);
            return Fail("Could not observe a generation child.", failure);
        }

        if (!WaitReady(&old_host, 1500u, &error)) {
            CloseHandle(new_process);
            CloseHandle(old_process);
            return Fail("Old generation did not become ready.", error);
        }
        bool new_ready = true;
        if (!new_host.PollReady(&new_ready, &error)) {
            CloseHandle(new_process);
            CloseHandle(old_process);
            return Fail("Could not poll the delayed new generation ready event.",
                        error);
        }
        if (new_ready || new_host.ready_observed()) {
            CloseHandle(new_process);
            CloseHandle(old_process);
            return Fail("New generation became ready before its delay.",
                        ERROR_INVALID_DATA);
        }
        if (!old_host.AuthorizeTransportOpen(&error)) {
            CloseHandle(new_process);
            CloseHandle(old_process);
            return Fail("Old generation OPEN authorization failed.", error);
        }
        if (!WaitTransportEvent(
                &old_host,
                native_ldac::agent::V1TransportWorkerEvent::
                    CapabilitiesDiscovered,
                1500u,
                &error)) {
            CloseHandle(new_process);
            CloseHandle(old_process);
            return Fail("Old generation capabilities event failed.", error);
        }
        if (!WaitTransportEvent(
                &old_host,
                native_ldac::agent::V1TransportWorkerEvent::MediaStarted,
                1500u,
                &error)) {
            CloseHandle(new_process);
            CloseHandle(old_process);
            return Fail("Old generation media-start event failed.", error);
        }
        native_ldac::agent::V1TransportWorkerEvent unexpected =
            native_ldac::agent::V1TransportWorkerEvent::None;
        if (!new_host.PollTransportEvent(&unexpected, &error)) {
            CloseHandle(new_process);
            CloseHandle(old_process);
            return Fail("Could not poll new-host transport isolation.",
                        error);
        }
        if (unexpected !=
            native_ldac::agent::V1TransportWorkerEvent::None) {
            CloseHandle(new_process);
            CloseHandle(old_process);
            return FailUnexpectedTransportEvent(
                "New host observed an old-generation transport event.",
                unexpected);
        }

        const auto old_stop_mode =
            (round & 1u) == 0u
                ? native_ldac::agent::V1EngineStopMode::GracefulTransport
                : native_ldac::agent::V1EngineStopMode::CancelTransport;
        DWORD exit_code = MAXDWORD;
        if (!old_host.Stop(
                old_stop_mode, 2000u, &exit_code, &error) ||
            exit_code != 0u || old_host.active() ||
            old_host.process_id() != 0u ||
            !old_host.last_transport_stop_acknowledged() ||
            WaitForSingleObject(old_process, 0u) != WAIT_OBJECT_0 ||
            !PollNoTransportEvent(&new_host, &error)) {
            CloseHandle(new_process);
            CloseHandle(old_process);
            return Fail("Old generation stop did not converge locally.",
                        error);
        }
        CloseHandle(old_process);
        old_process = nullptr;

        if (!WaitReady(&new_host, 3000u, &error) ||
            !new_host.AuthorizeTransportOpen(&error) ||
            !WaitTransportEvent(
                &new_host,
                native_ldac::agent::V1TransportWorkerEvent::
                    CapabilitiesDiscovered,
                1500u,
                &error) ||
            !WaitTransportEvent(
                &new_host,
                native_ldac::agent::V1TransportWorkerEvent::MediaStarted,
                1500u,
                &error)) {
            CloseHandle(new_process);
            return Fail("New generation did not start independently.",
                        error);
        }
        const auto new_stop_mode =
            old_stop_mode ==
                    native_ldac::agent::V1EngineStopMode::GracefulTransport
                ? native_ldac::agent::V1EngineStopMode::CancelTransport
                : native_ldac::agent::V1EngineStopMode::GracefulTransport;
        if (!new_host.Stop(
                new_stop_mode, 2000u, &exit_code, &error) ||
            exit_code != 0u || new_host.active() ||
            new_host.process_id() != 0u ||
            !new_host.last_transport_stop_acknowledged() ||
            WaitForSingleObject(new_process, 0u) != WAIT_OBJECT_0) {
            CloseHandle(new_process);
            return Fail("New generation stop did not converge locally.",
                        error);
        }
        CloseHandle(new_process);
        new_process = nullptr;

        DWORD current_handles = 0u;
        if (!GetProcessHandleCount(GetCurrentProcess(), &current_handles) ||
            current_handles != baseline_handles) {
            return Fail("A multi-generation round leaked a host handle.",
                        GetLastError());
        }
    }
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc > 1 && std::wcscmp(argv[1], L"--ready-event") == 0) {
        return RunGenerationProvenanceStub(argc, argv);
    }
    if (argc != 9) {
        return Fail(
            "Expected ready, transport, failing transport, discovery, and "
            "retryable discovery, configuration, silence, and PCM mock worker paths.");
    }
    const std::wstring stub_path(argv[1]);
    const std::wstring transport_stub_path(argv[2]);
    const std::wstring failing_transport_stub_path(argv[3]);
    const std::wstring discovery_mock_worker_path(argv[4]);
    const std::wstring retryable_discovery_mock_worker_path(argv[5]);
    const std::wstring configuration_mock_worker_path(argv[6]);
    const std::wstring silence_mock_worker_path(argv[7]);
    const std::wstring pcm_mock_worker_path(argv[8]);

    std::array<wchar_t, 32768> self_path_buffer{};
    const DWORD self_path_size = GetModuleFileNameW(
        nullptr,
        self_path_buffer.data(),
        static_cast<DWORD>(self_path_buffer.size()));
    if (self_path_size == 0u ||
        static_cast<std::size_t>(self_path_size) >=
            self_path_buffer.size() ||
        VerifyGenerationProvenance(
            std::wstring(self_path_buffer.data(), self_path_size)) != 0 ||
        VerifyMultiGenerationHostStress(
            std::wstring(self_path_buffer.data(), self_path_size)) != 0) {
        return Fail("Generation provenance tests failed.", GetLastError());
    }

    DWORD error = ERROR_SUCCESS;
    native_ldac::agent::V1EngineReadyHost host;
    if (!host.Start(stub_path, 1u, false, &error)) {
        return Fail("Could not start the graceful stub.", error);
    }
    if (!WaitReady(&host, 2000u, &error)) {
        return Fail("Graceful stub did not become ready.", error);
    }
    DWORD exit_code = MAXDWORD;
    if (!host.Stop(2000u, &exit_code, &error) || exit_code != 0u ||
        host.active()) {
        return Fail("Graceful stub did not stop cleanly.", error);
    }

    HANDLE observer = nullptr;
    {
        native_ldac::agent::V1EngineReadyHost contained;
        if (!contained.Start(stub_path, 2u, true, &error)) {
            return Fail("Could not start the containment stub.", error);
        }
        if (!WaitReady(&contained, 2000u, &error)) {
            return Fail("Containment stub did not become ready.", error);
        }
        observer = OpenProcess(SYNCHRONIZE,
                               FALSE,
                               contained.process_id());
        if (observer == nullptr) {
            return Fail("Could not observe the contained child.",
                        GetLastError());
        }
    }
    const DWORD containment_wait = WaitForSingleObject(observer, 5000u);
    CloseHandle(observer);
    if (containment_wait != WAIT_OBJECT_0) {
        return Fail("Closing the host did not terminate its Job child.");
    }

    native_ldac::agent::V1EngineReadyHost single_gain_fail_safe;
    if (!single_gain_fail_safe.StartTransportWorker(
            transport_stub_path, 13u, false, &error, false) ||
        !WaitReady(&single_gain_fail_safe, 3000u, &error) ||
        !single_gain_fail_safe.single_gain_fail_safe_enabled() ||
        !single_gain_fail_safe.SetSingleGainReady(true, &error) ||
        !single_gain_fail_safe.SetSingleGainReady(false, &error)) {
        return Fail("Single-gain readiness event was not controllable.",
                    error);
    }
    exit_code = MAXDWORD;
    if (!single_gain_fail_safe.Stop(2000u, &exit_code, &error) ||
        exit_code != 0u || single_gain_fail_safe.active()) {
        return Fail("Single-gain fail-safe worker did not stop cleanly.",
                    error);
    }

    native_ldac::agent::V1EngineReadyHost graceful;
    if (!graceful.StartTransportWorker(
            transport_stub_path, 3u, false, &error) ||
        !WaitReady(&graceful, 2000u, &error) ||
        !graceful.AuthorizeTransportOpen(&error) ||
        !WaitTransportEvent(
            &graceful,
            native_ldac::agent::V1TransportWorkerEvent::
                CapabilitiesDiscovered,
            2000u,
            &error) ||
        !WaitTransportEvent(
            &graceful,
            native_ldac::agent::V1TransportWorkerEvent::MediaStarted,
            2000u,
            &error)) {
        return Fail("Graceful transport worker did not start media.", error);
    }
    if (graceful.AuthorizeTransportOpen(&error) ||
        error != ERROR_ALREADY_EXISTS) {
        return Fail("A transport worker accepted a second OPEN authorization.",
                    error);
    }
    exit_code = MAXDWORD;
    if (!graceful.Stop(
            native_ldac::agent::V1EngineStopMode::GracefulTransport,
            2000u,
            &exit_code,
            &error) ||
        exit_code != 0u || graceful.active() ||
        !graceful.last_transport_stop_acknowledged()) {
        return Fail("Graceful transport worker did not acknowledge stop.",
                    error);
    }

    native_ldac::agent::V1EngineReadyHost cancelled;
    if (!cancelled.StartTransportWorker(
            transport_stub_path, 4u, false, &error) ||
        !WaitReady(&cancelled, 2000u, &error) ||
        !cancelled.AuthorizeTransportOpen(&error) ||
        !WaitTransportEvent(
            &cancelled,
            native_ldac::agent::V1TransportWorkerEvent::
                CapabilitiesDiscovered,
            2000u,
            &error) ||
        !WaitTransportEvent(
            &cancelled,
            native_ldac::agent::V1TransportWorkerEvent::MediaStarted,
            2000u,
            &error)) {
        return Fail("Cancellable transport worker did not start media.", error);
    }
    exit_code = MAXDWORD;
    if (!cancelled.Stop(
            native_ldac::agent::V1EngineStopMode::CancelTransport,
            2000u,
            &exit_code,
            &error) ||
        exit_code != 0u || cancelled.active() ||
        !cancelled.last_transport_stop_acknowledged()) {
        return Fail("Cancelled transport worker did not acknowledge stop.",
                    error);
    }

    native_ldac::agent::V1EngineReadyHost failed;
    if (!failed.StartTransportWorker(
            failing_transport_stub_path, 5u, false, &error) ||
        !WaitReady(&failed, 2000u, &error) ||
        !failed.AuthorizeTransportOpen(&error) ||
        !WaitTransportEvent(
            &failed,
            native_ldac::agent::V1TransportWorkerEvent::MediaFailed,
            2000u,
            &error)) {
        return Fail("Failing transport worker did not publish failure.", error);
    }
    bool exited = false;
    exit_code = 0u;
    const ULONGLONG failure_deadline = GetTickCount64() + 2000u;
    while (!exited && GetTickCount64() < failure_deadline) {
        if (!failed.PollExited(&exited, &exit_code, &error)) {
            return Fail("Could not observe failing transport worker.", error);
        }
        if (!exited) Sleep(10u);
    }
    if (!exited || exit_code != 20u) {
        return Fail("Failing transport worker exit code changed.", exit_code);
    }
    failed.Close();

    HANDLE transport_observer = nullptr;
    {
        native_ldac::agent::V1EngineReadyHost transport_contained;
        if (!transport_contained.StartTransportWorker(
                transport_stub_path, 6u, true, &error) ||
            !WaitReady(&transport_contained, 2000u, &error) ||
            !transport_contained.AuthorizeTransportOpen(&error) ||
            !WaitTransportEvent(
                &transport_contained,
                native_ldac::agent::V1TransportWorkerEvent::
                    CapabilitiesDiscovered,
                2000u,
                &error) ||
            !WaitTransportEvent(
                &transport_contained,
                native_ldac::agent::V1TransportWorkerEvent::MediaStarted,
                2000u,
                &error)) {
            return Fail("Contained transport worker did not start.", error);
        }
        transport_observer = OpenProcess(
            SYNCHRONIZE, FALSE, transport_contained.process_id());
        if (transport_observer == nullptr) {
            return Fail("Could not observe contained transport worker.",
                        GetLastError());
        }
    }
    const DWORD transport_containment_wait =
        WaitForSingleObject(transport_observer, 5000u);
    CloseHandle(transport_observer);
    if (transport_containment_wait != WAIT_OBJECT_0) {
        return Fail("Closing the host did not contain transport worker.");
    }

    wchar_t temporary_directory[MAX_PATH] = {};
    if (GetTempPathW(ARRAYSIZE(temporary_directory), temporary_directory) ==
        0u) {
        return Fail("Could not resolve the temporary directory.",
                    GetLastError());
    }

    // A physical ACL disconnect can stop a configuration worker before the
    // parent authorizes transport OPEN. Local-only stop must still converge;
    // the worker has not entered a transport session yet and therefore does
    // not need a cancel/graceful transport event to acknowledge the stop.
    const std::filesystem::path early_stop_result =
        std::filesystem::path(temporary_directory) /
        (L"native-ldac-v1-configuration-early-stop-" +
         std::to_wstring(GetCurrentProcessId()) + L".json");
    (void)DeleteFileW(early_stop_result.c_str());
    native_ldac::agent::V1EngineReadyHost early_stop;
    if (!early_stop.StartTransportDiscoveryWorker(
            configuration_mock_worker_path,
            41u,
            early_stop_result.wstring(),
            &error) ||
        !WaitReady(&early_stop, 2000u, &error)) {
        return Fail("Early-stop configuration worker did not become ready.",
                    error);
    }
    exit_code = MAXDWORD;
    if (!early_stop.Stop(2000u, &exit_code, &error) ||
        exit_code != 0u || early_stop.active()) {
        return Fail("Pre-authorization configuration worker did not stop "
                    "cleanly.",
                    error);
    }
    (void)DeleteFileW(early_stop_result.c_str());

    const std::filesystem::path discovery_result =
        std::filesystem::path(temporary_directory) /
        (L"native-ldac-v1-discovery-" +
         std::to_wstring(GetCurrentProcessId()) + L".json");
    (void)DeleteFileW(discovery_result.c_str());
    native_ldac::agent::V1EngineReadyHost discovery;
    if (!discovery.StartTransportDiscoveryWorker(
            discovery_mock_worker_path,
            7u,
            discovery_result.wstring(),
            &error) ||
        !WaitReady(&discovery, 2000u, &error) ||
        !discovery.AuthorizeTransportOpen(&error) ||
        !WaitTransportEvent(
            &discovery,
            native_ldac::agent::V1TransportWorkerEvent::
                CapabilitiesDiscovered,
            2000u,
            &error)) {
        return Fail("Capability discovery worker did not complete.", error);
    }
    native_ldac::agent::V1TransportWorkerEvent unexpected =
        native_ldac::agent::V1TransportWorkerEvent::None;
    if (!discovery.PollTransportEvent(&unexpected, &error) ||
        unexpected != native_ldac::agent::V1TransportWorkerEvent::None) {
        return Fail("Discovery-only worker published a media event.", error);
    }
    exit_code = MAXDWORD;
    if (!discovery.Stop(
            native_ldac::agent::V1EngineStopMode::CancelTransport,
            2000u,
            &exit_code,
            &error) ||
        exit_code != 0u || discovery.active() ||
        !discovery.last_transport_stop_acknowledged()) {
        return Fail("Discovery-only worker did not acknowledge cleanup.",
                    error);
    }
    std::ifstream result_stream(discovery_result);
    std::ostringstream result_text;
    result_text << result_stream.rdbuf();
    const std::string result_json = result_text.str();
    (void)DeleteFileW(discovery_result.c_str());
    if (result_json.find("\"disposition\": \"succeeded\"") ==
            std::string::npos ||
        result_json.find("\"remote_seid\": 3") == std::string::npos ||
        result_json.find("\"signaling_exchanges\": 2") ==
            std::string::npos ||
        result_json.find("\"close_succeeded\": true") ==
            std::string::npos) {
        return Fail("Discovery-only worker result contract changed.");
    }

    const std::filesystem::path retryable_result =
        std::filesystem::path(temporary_directory) /
        (L"native-ldac-v1-discovery-retryable-" +
         std::to_wstring(GetCurrentProcessId()) + L".json");
    (void)DeleteFileW(retryable_result.c_str());
    native_ldac::agent::V1EngineReadyHost retryable;
    if (!retryable.StartTransportDiscoveryWorker(
            retryable_discovery_mock_worker_path,
            8u,
            retryable_result.wstring(),
            &error) ||
        !WaitReady(&retryable, 2000u, &error) ||
        !retryable.AuthorizeTransportOpen(&error) ||
        !WaitTransportEvent(
            &retryable,
            native_ldac::agent::V1TransportWorkerEvent::
                RetryableOpenFailure,
            2000u,
            &error)) {
        return Fail("Retryable OPEN worker did not publish its distinct event.",
                    error);
    }
    exit_code = MAXDWORD;
    if (!retryable.Stop(
            native_ldac::agent::V1EngineStopMode::CancelTransport,
            2000u,
            &exit_code,
            &error) ||
        exit_code != 0u || retryable.active() ||
        !retryable.last_transport_stop_acknowledged()) {
        return Fail("Retryable OPEN worker did not acknowledge cleanup.",
                    error);
    }
    std::ifstream retryable_stream(retryable_result);
    std::ostringstream retryable_text;
    retryable_text << retryable_stream.rdbuf();
    const std::string retryable_json = retryable_text.str();
    (void)DeleteFileW(retryable_result.c_str());
    if (retryable_json.find("\"stage\": 1") == std::string::npos ||
        retryable_json.find("\"backend_error\": 71") ==
            std::string::npos ||
        retryable_json.find("\"signaling_exchanges\": 0") ==
            std::string::npos ||
        retryable_json.find(
            "\"strictly_retryable_open_failure\": true") ==
            std::string::npos) {
        return Fail("Retryable OPEN result contract changed.");
    }

    const std::filesystem::path configuration_result =
        std::filesystem::path(temporary_directory) /
        (L"native-ldac-v1-configuration-" +
         std::to_wstring(GetCurrentProcessId()) + L".json");
    (void)DeleteFileW(configuration_result.c_str());
    native_ldac::agent::V1EngineReadyHost configuration;
    if (!configuration.StartTransportDiscoveryWorker(
            configuration_mock_worker_path,
            9u,
            configuration_result.wstring(),
            &error) ||
        !WaitReady(&configuration, 2000u, &error) ||
        !configuration.AuthorizeTransportOpen(&error) ||
        !WaitTransportEvent(
            &configuration,
            native_ldac::agent::V1TransportWorkerEvent::
                CapabilitiesDiscovered,
            2000u,
            &error)) {
        return Fail("Zero-packet configuration worker did not complete.",
                    error);
    }
    unexpected = native_ldac::agent::V1TransportWorkerEvent::None;
    if (!configuration.PollTransportEvent(&unexpected, &error) ||
        unexpected != native_ldac::agent::V1TransportWorkerEvent::None) {
        return Fail("Configuration worker published a media-start event.",
                    error);
    }
    exit_code = MAXDWORD;
    if (!configuration.Stop(
            native_ldac::agent::V1EngineStopMode::CancelTransport,
            2000u,
            &exit_code,
            &error) ||
        exit_code != 0u || configuration.active() ||
        !configuration.last_transport_stop_acknowledged()) {
        return Fail("Configuration worker did not acknowledge cleanup.",
                    error);
    }
    std::ifstream configuration_stream(configuration_result);
    std::ostringstream configuration_text;
    configuration_text << configuration_stream.rdbuf();
    const std::string configuration_json = configuration_text.str();
    (void)DeleteFileW(configuration_result.c_str());
    if (configuration_json.find(
            "\"set_configuration_accepted\": true") ==
            std::string::npos ||
        configuration_json.find("\"avdtp_open_accepted\": true") ==
            std::string::npos ||
        configuration_json.find("\"media_opened\": true") ==
            std::string::npos ||
        configuration_json.find("\"avdtp_close_accepted\": true") ==
            std::string::npos ||
        configuration_json.find("\"media_start_commands\": 0") ==
            std::string::npos ||
        configuration_json.find("\"media_packets_written\": 0") ==
            std::string::npos) {
        return Fail("Zero-packet configuration result contract changed.");
    }

    const std::filesystem::path silence_result =
        std::filesystem::path(temporary_directory) /
        (L"native-ldac-v1-silence-" +
         std::to_wstring(GetCurrentProcessId()) + L".json");
    (void)DeleteFileW(silence_result.c_str());
    native_ldac::agent::V1EngineReadyHost silence;
    if (!silence.StartTransportDiscoveryWorker(
            silence_mock_worker_path, 10u, silence_result.wstring(), &error) ||
        !WaitReady(&silence, 2000u, &error) ||
        !silence.AuthorizeTransportOpen(&error) ||
        !WaitTransportEvent(&silence,
            native_ldac::agent::V1TransportWorkerEvent::CapabilitiesDiscovered,
            2000u, &error)) {
        return Fail("Silence-burst worker did not complete.", error);
    }
    exit_code = MAXDWORD;
    if (!silence.Stop(native_ldac::agent::V1EngineStopMode::CancelTransport,
            2000u, &exit_code, &error) || exit_code != 0u ||
        !silence.last_transport_stop_acknowledged()) {
        return Fail("Silence-burst worker did not acknowledge cleanup.", error);
    }
    std::ifstream silence_stream(silence_result);
    std::ostringstream silence_text; silence_text << silence_stream.rdbuf();
    const std::string silence_json = silence_text.str();
    (void)DeleteFileW(silence_result.c_str());
    if (silence_json.find("\"avdtp_start_accepted\": true") ==
            std::string::npos ||
        silence_json.find("\"media_packets_written\": 4") ==
            std::string::npos ||
        silence_json.find("\"avdtp_suspend_accepted\": true") ==
            std::string::npos ||
        silence_json.find("\"avdtp_close_accepted\": true") ==
            std::string::npos) {
        return Fail("Silence-burst result contract changed.");
    }

    const std::filesystem::path pcm_result =
        std::filesystem::path(temporary_directory) /
        (L"native-ldac-v1-pcm-burst-" +
         std::to_wstring(GetCurrentProcessId()) + L".json");
    (void)DeleteFileW(pcm_result.c_str());
    native_ldac::agent::V1EngineReadyHost pcm;
    if (!pcm.StartTransportDiscoveryWorker(
            pcm_mock_worker_path, 11u, pcm_result.wstring(), &error) ||
        !WaitReady(&pcm, 2000u, &error) ||
        !pcm.AuthorizeTransportOpen(&error) ||
        !WaitTransportEvent(&pcm,
            native_ldac::agent::V1TransportWorkerEvent::MediaStarted,
            3000u, &error) ||
        !WaitTransportEvent(&pcm,
            native_ldac::agent::V1TransportWorkerEvent::CapabilitiesDiscovered,
            3000u, &error)) {
        return Fail("Low-gain PCM worker did not complete.", error);
    }
    exit_code = MAXDWORD;
    if (!pcm.Stop(native_ldac::agent::V1EngineStopMode::GracefulTransport,
            2000u, &exit_code, &error) || exit_code != 0u ||
        !pcm.last_transport_stop_acknowledged()) {
        return Fail("Low-gain PCM worker did not acknowledge cleanup.", error);
    }
    std::ifstream pcm_stream(pcm_result);
    std::ostringstream pcm_text;
    pcm_text << pcm_stream.rdbuf();
    const std::string pcm_json = pcm_text.str();
    (void)DeleteFileW(pcm_result.c_str());
    if (pcm_json.find("\"audible_pcm_confirmed_before_open\": true") ==
            std::string::npos ||
        pcm_json.find("\"open_diagnostic_query_attempts\": 0") ==
            std::string::npos ||
        pcm_json.find("\"open_diagnostic_query_error\": 0") ==
            std::string::npos ||
        pcm_json.find("\"open_diagnostic_query_bytes\": 0") ==
            std::string::npos ||
        pcm_json.find("\"open_diagnostic_available\": false") ==
            std::string::npos ||
        pcm_json.find(
            "\"open_diagnostic_remote_no_resources\": false") ==
            std::string::npos ||
        pcm_json.find("\"maximum_gain_scalar\": 0.01000000") ==
            std::string::npos ||
        pcm_json.find("\"target_duration_ms\": 10000") ==
            std::string::npos ||
        pcm_json.find("\"completed_full_duration\": true") ==
            std::string::npos ||
        pcm_json.find("\"consumer_lease_released\": true") ==
            std::string::npos ||
        pcm_json.find("\"avdtp_suspend_accepted\": true") ==
            std::string::npos ||
        pcm_json.find("\"avdtp_close_accepted\": true") ==
            std::string::npos) {
        return Fail("Low-gain PCM result contract changed.");
    }

    std::printf("V1 engine/transport bounded-media host tests passed.\n");
    return 0;
}
