#include "v1_engine_ready_host.h"

#include <filesystem>
#include <vector>

namespace native_ldac::agent {
namespace {

std::wstring QuoteCommandLineArgument(const std::wstring& argument) {
    if (argument.empty()) {
        return L"\"\"";
    }
    if (argument.find_first_of(L" \t\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring quoted = L"\"";
    size_t backslashes = 0u;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'\"') {
            quoted.append(backslashes * 2u + 1u, L'\\');
            quoted.push_back(L'\"');
            backslashes = 0u;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0u;
        quoted.push_back(character);
    }
    quoted.append(backslashes * 2u, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

HANDLE CreateKillOnCloseJob() {
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) {
        return nullptr;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION information{};
    information.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job,
                                 JobObjectExtendedLimitInformation,
                                 &information,
                                 sizeof(information))) {
        const DWORD error = GetLastError();
        CloseHandle(job);
        SetLastError(error);
        return nullptr;
    }
    return job;
}

void StoreError(DWORD value, DWORD* error) {
    if (error != nullptr) {
        *error = value;
    }
}

bool PollEvent(HANDLE event, bool* signaled, DWORD* error) {
    if (event == nullptr || signaled == nullptr) {
        StoreError(ERROR_INVALID_PARAMETER, error);
        return false;
    }
    const DWORD wait = WaitForSingleObject(event, 0u);
    if (wait == WAIT_OBJECT_0) {
        *signaled = true;
        return true;
    }
    if (wait == WAIT_TIMEOUT) {
        *signaled = false;
        return true;
    }
    StoreError(GetLastError(), error);
    return false;
}

}  // namespace

V1EngineReadyHost::~V1EngineReadyHost() {
    Close();
}

bool V1EngineReadyHost::Start(const std::wstring& executable,
                              std::uint64_t generation,
                              bool ignore_stop,
                              DWORD* error) {
    return StartInternal(executable,
                         generation,
                         ignore_stop,
                         false,
                         true,
                         L"",
                         L"hq",
                         L"stereo",
                         48000u,
                         16u,
                         error);
}

bool V1EngineReadyHost::StartTransportWorker(
    const std::wstring& executable,
    std::uint64_t generation,
    bool ignore_stop,
    DWORD* error,
    bool apply_endpoint_volume,
    const std::wstring& quality,
    const std::wstring& channel_mode,
    unsigned sample_rate_hz,
    unsigned bits_per_sample) {
    return StartInternal(executable,
                         generation,
                         ignore_stop,
                         true,
                         apply_endpoint_volume,
                         L"",
                         quality,
                         channel_mode,
                         sample_rate_hz,
                         bits_per_sample,
                         error);
}

bool V1EngineReadyHost::StartTransportDiscoveryWorker(
    const std::wstring& executable,
    std::uint64_t generation,
    const std::wstring& result_path,
    DWORD* error,
    bool apply_endpoint_volume,
    const std::wstring& quality,
    const std::wstring& channel_mode,
    unsigned sample_rate_hz,
    unsigned bits_per_sample) {
    if (result_path.empty()) {
        StoreError(ERROR_INVALID_PARAMETER, error);
        return false;
    }
    return StartInternal(executable,
                         generation,
                         false,
                         true,
                         apply_endpoint_volume,
                         result_path,
                         quality,
                         channel_mode,
                         sample_rate_hz,
                         bits_per_sample,
                         error);
}

bool V1EngineReadyHost::StartInternal(const std::wstring& executable,
                                      std::uint64_t generation,
                                      bool ignore_stop,
                                      bool transport_worker,
                                      bool apply_endpoint_volume,
                                      const std::wstring& transport_result_path,
                                      const std::wstring& quality,
                                      const std::wstring& channel_mode,
                                      unsigned sample_rate_hz,
                                      unsigned bits_per_sample,
                                      DWORD* error) {
    Close();
    if (executable.empty() ||
        (_wcsicmp(quality.c_str(), L"hq") != 0 &&
         _wcsicmp(quality.c_str(), L"sq") != 0 &&
         _wcsicmp(quality.c_str(), L"mq") != 0) ||
        (_wcsicmp(channel_mode.c_str(), L"stereo") != 0 &&
         _wcsicmp(channel_mode.c_str(), L"dual") != 0 &&
         _wcsicmp(channel_mode.c_str(), L"mono") != 0) ||
        (sample_rate_hz != 44100u && sample_rate_hz != 48000u &&
         sample_rate_hz != 88200u && sample_rate_hz != 96000u) ||
        (bits_per_sample != 16u && bits_per_sample != 24u) ||
        GetFileAttributesW(executable.c_str()) == INVALID_FILE_ATTRIBUTES) {
        StoreError(ERROR_FILE_NOT_FOUND, error);
        return false;
    }

    const std::wstring event_base =
        L"Local\\NativeLdac.V1.Engine." +
        std::to_wstring(GetCurrentProcessId()) + L"." +
        std::to_wstring(generation) + L"." +
        std::to_wstring(GetTickCount64());
    const std::wstring ready_name = event_base + L".ready";
    const std::wstring stop_name = event_base + L".stop";
    ready_event_ = CreateEventW(nullptr, TRUE, FALSE, ready_name.c_str());
    if (ready_event_ == nullptr) {
        StoreError(GetLastError(), error);
        Close();
        return false;
    }
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, stop_name.c_str());
    if (stop_event_ == nullptr) {
        StoreError(GetLastError(), error);
        Close();
        return false;
    }
    std::wstring transport_open_name;
    std::wstring capabilities_discovered_name;
    std::wstring media_started_name;
    std::wstring media_stopped_name;
    std::wstring media_failed_name;
    std::wstring retryable_open_failure_name;
    std::wstring graceful_transport_stop_name;
    std::wstring cancel_transport_name;
    std::wstring single_gain_ready_name;
    const bool discovery_worker = !transport_result_path.empty();
    if (transport_worker) {
        transport_open_name = event_base + L".transport-open";
        capabilities_discovered_name =
            event_base + L".capabilities-discovered";
        media_started_name = event_base + L".media-started";
        media_stopped_name = event_base + L".media-stopped";
        media_failed_name = event_base + L".media-failed";
        if (discovery_worker) {
            retryable_open_failure_name =
                event_base + L".retryable-open-failure";
        }
        graceful_transport_stop_name =
            event_base + L".graceful-transport-stop";
        cancel_transport_name = event_base + L".cancel-transport";
        if (!apply_endpoint_volume) {
            single_gain_ready_name = event_base + L".single-gain-ready";
        }
        transport_open_event_ = CreateEventW(
            nullptr, TRUE, FALSE, transport_open_name.c_str());
        capabilities_discovered_event_ = CreateEventW(
            nullptr, TRUE, FALSE, capabilities_discovered_name.c_str());
        media_started_event_ = CreateEventW(
            nullptr, TRUE, FALSE, media_started_name.c_str());
        media_stopped_event_ = CreateEventW(
            nullptr, TRUE, FALSE, media_stopped_name.c_str());
        media_failed_event_ = CreateEventW(
            nullptr, TRUE, FALSE, media_failed_name.c_str());
        if (discovery_worker) {
            retryable_open_failure_event_ = CreateEventW(
                nullptr, TRUE, FALSE,
                retryable_open_failure_name.c_str());
        }
        graceful_transport_stop_event_ = CreateEventW(
            nullptr, TRUE, FALSE, graceful_transport_stop_name.c_str());
        cancel_transport_event_ = CreateEventW(
            nullptr, TRUE, FALSE, cancel_transport_name.c_str());
        if (!apply_endpoint_volume) {
            // Start fail-safe with endpoint gain applied. The parent signals
            // this only after AVRCP is fully ready and the XM5 initial volume
            // has been adopted.
            single_gain_ready_event_ = CreateEventW(
                nullptr, TRUE, FALSE, single_gain_ready_name.c_str());
        }
        if (transport_open_event_ == nullptr ||
            capabilities_discovered_event_ == nullptr ||
            media_started_event_ == nullptr ||
            media_stopped_event_ == nullptr ||
            media_failed_event_ == nullptr ||
            (discovery_worker &&
             retryable_open_failure_event_ == nullptr) ||
            graceful_transport_stop_event_ == nullptr ||
            cancel_transport_event_ == nullptr ||
            (!apply_endpoint_volume &&
             single_gain_ready_event_ == nullptr)) {
            StoreError(GetLastError(), error);
            Close();
            return false;
        }
    }
    job_ = CreateKillOnCloseJob();
    if (job_ == nullptr) {
        StoreError(GetLastError(), error);
        Close();
        return false;
    }

    std::wstring command_line = QuoteCommandLineArgument(executable) +
        L" --ready-event " + QuoteCommandLineArgument(ready_name) +
        L" --stop-event " + QuoteCommandLineArgument(stop_name);
    if (transport_worker) {
        command_line +=
            L" --transport-open-event " +
                QuoteCommandLineArgument(transport_open_name) +
            L" --capabilities-discovered-event " +
                QuoteCommandLineArgument(capabilities_discovered_name) +
            L" --media-started-event " +
                QuoteCommandLineArgument(media_started_name) +
            L" --media-stopped-event " +
                QuoteCommandLineArgument(media_stopped_name) +
            L" --media-failed-event " +
                QuoteCommandLineArgument(media_failed_name) +
            L" --graceful-transport-stop-event " +
                QuoteCommandLineArgument(graceful_transport_stop_name) +
            L" --cancel-transport-event " +
                QuoteCommandLineArgument(cancel_transport_name);
            // A requested single-gain path starts fail-safe with endpoint
            // gain enabled. The worker switches dynamically only after the
            // parent proves the AVRCP control channel is ready.
            if (!apply_endpoint_volume) {
                command_line += L" --single-gain-ready-event " +
                    QuoteCommandLineArgument(single_gain_ready_name);
            }
        if (discovery_worker) {
            command_line += L" --retryable-open-failure-event " +
                QuoteCommandLineArgument(retryable_open_failure_name);
        }
        if (!transport_result_path.empty()) {
            command_line += L" --session-result " +
                QuoteCommandLineArgument(transport_result_path) +
                L" --session-generation " +
                std::to_wstring(generation) + L" --quality " +
                QuoteCommandLineArgument(quality) + L" --channel-mode " +
                QuoteCommandLineArgument(channel_mode);
        } else if (quality != L"hq") {
            command_line += L" --quality " +
                QuoteCommandLineArgument(quality);
        }
        if (transport_result_path.empty() && channel_mode != L"stereo") {
            command_line += L" --channel-mode " +
                QuoteCommandLineArgument(channel_mode);
        }
        command_line += L" --sample-rate " +
            std::to_wstring(sample_rate_hz) + L" --bits " +
            std::to_wstring(bits_per_sample);
    }
    if (ignore_stop) {
        command_line += L" --ignore-stop";
    }
    std::vector<wchar_t> mutable_command(command_line.begin(),
                                         command_line.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process_information{};
    const std::filesystem::path executable_path(executable);
    const std::wstring working_directory =
        executable_path.parent_path().wstring();
    if (!CreateProcessW(
            executable.c_str(),
            mutable_command.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT |
                CREATE_SUSPENDED,
            nullptr,
            working_directory.empty() ? nullptr : working_directory.c_str(),
            &startup,
            &process_information)) {
        StoreError(GetLastError(), error);
        Close();
        return false;
    }

    DWORD failure = ERROR_SUCCESS;
    if (!AssignProcessToJobObject(job_, process_information.hProcess)) {
        failure = GetLastError();
    } else if (ResumeThread(process_information.hThread) ==
               static_cast<DWORD>(-1)) {
        failure = GetLastError();
    }
    CloseHandle(process_information.hThread);
    if (failure != ERROR_SUCCESS) {
        (void)TerminateProcess(process_information.hProcess, failure);
        CloseHandle(process_information.hProcess);
        StoreError(failure, error);
        Close();
        return false;
    }

    process_ = process_information.hProcess;
    process_id_ = process_information.dwProcessId;
    ready_observed_ = false;
    transport_worker_enabled_ = transport_worker;
    transport_open_authorized_ = false;
    capabilities_discovered_observed_ = false;
    media_started_observed_ = false;
    media_stopped_observed_ = false;
    media_failed_observed_ = false;
    retryable_open_failure_observed_ = false;
    last_transport_stop_acknowledged_ = false;
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1EngineReadyHost::AuthorizeTransportOpen(DWORD* error) {
    if (!transport_worker_enabled_ || process_ == nullptr ||
        transport_open_event_ == nullptr) {
        StoreError(ERROR_INVALID_STATE, error);
        return false;
    }
    if (transport_open_authorized_) {
        StoreError(ERROR_ALREADY_EXISTS, error);
        return false;
    }
    if (!SetEvent(transport_open_event_)) {
        StoreError(GetLastError(), error);
        return false;
    }
    transport_open_authorized_ = true;
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1EngineReadyHost::SetSingleGainReady(bool ready, DWORD* error) {
    if (single_gain_ready_event_ == nullptr || process_ == nullptr) {
        StoreError(ERROR_INVALID_STATE, error);
        return false;
    }
    const BOOL updated = ready
        ? SetEvent(single_gain_ready_event_)
        : ResetEvent(single_gain_ready_event_);
    if (!updated) {
        StoreError(GetLastError(), error);
        return false;
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1EngineReadyHost::PollTransportEvent(
    V1TransportWorkerEvent* event,
    DWORD* error) {
    if (event == nullptr || !transport_worker_enabled_ ||
        process_ == nullptr) {
        StoreError(ERROR_INVALID_STATE, error);
        return false;
    }
    *event = V1TransportWorkerEvent::None;
    bool signaled = false;
    if (retryable_open_failure_event_ != nullptr &&
        !retryable_open_failure_observed_) {
        if (!PollEvent(retryable_open_failure_event_, &signaled, error)) {
            return false;
        }
        if (signaled) {
            retryable_open_failure_observed_ = true;
            *event = V1TransportWorkerEvent::RetryableOpenFailure;
        }
    }
    if (*event == V1TransportWorkerEvent::None &&
        !media_failed_observed_) {
        if (!PollEvent(media_failed_event_, &signaled, error)) {
            return false;
        }
        if (signaled) {
            media_failed_observed_ = true;
            *event = V1TransportWorkerEvent::MediaFailed;
        }
    }
    if (*event == V1TransportWorkerEvent::None &&
        !capabilities_discovered_observed_) {
        if (!PollEvent(capabilities_discovered_event_, &signaled, error)) {
            return false;
        }
        if (signaled) {
            capabilities_discovered_observed_ = true;
            *event = V1TransportWorkerEvent::CapabilitiesDiscovered;
        }
    }
    if (*event == V1TransportWorkerEvent::None &&
        !media_started_observed_) {
        if (!PollEvent(media_started_event_, &signaled, error)) {
            return false;
        }
        if (signaled) {
            media_started_observed_ = true;
            *event = V1TransportWorkerEvent::MediaStarted;
        }
    }
    if (*event == V1TransportWorkerEvent::None &&
        !media_stopped_observed_) {
        if (!PollEvent(media_stopped_event_, &signaled, error)) {
            return false;
        }
        if (signaled) {
            media_stopped_observed_ = true;
            *event = V1TransportWorkerEvent::MediaStopped;
        }
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1EngineReadyHost::PollReady(bool* ready, DWORD* error) {
    if (ready == nullptr || ready_event_ == nullptr || process_ == nullptr) {
        StoreError(ERROR_INVALID_STATE, error);
        return false;
    }
    const DWORD wait = WaitForSingleObject(ready_event_, 0u);
    if (wait == WAIT_OBJECT_0) {
        ready_observed_ = true;
        *ready = true;
        StoreError(ERROR_SUCCESS, error);
        return true;
    }
    if (wait == WAIT_TIMEOUT) {
        *ready = false;
        StoreError(ERROR_SUCCESS, error);
        return true;
    }
    StoreError(GetLastError(), error);
    return false;
}

bool V1EngineReadyHost::PollExited(bool* exited,
                                   DWORD* exit_code,
                                   DWORD* error) const {
    if (exited == nullptr || process_ == nullptr) {
        StoreError(ERROR_INVALID_STATE, error);
        return false;
    }
    const DWORD wait = WaitForSingleObject(process_, 0u);
    if (wait == WAIT_TIMEOUT) {
        *exited = false;
        StoreError(ERROR_SUCCESS, error);
        return true;
    }
    if (wait != WAIT_OBJECT_0) {
        StoreError(GetLastError(), error);
        return false;
    }
    DWORD code = 0u;
    if (!GetExitCodeProcess(process_, &code)) {
        StoreError(GetLastError(), error);
        return false;
    }
    *exited = true;
    if (exit_code != nullptr) {
        *exit_code = code;
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1EngineReadyHost::Stop(DWORD timeout_ms,
                             DWORD* exit_code,
                             DWORD* error) {
    return Stop(V1EngineStopMode::LocalOnly,
                timeout_ms,
                exit_code,
                error);
}

bool V1EngineReadyHost::Stop(V1EngineStopMode mode,
                             DWORD timeout_ms,
                             DWORD* exit_code,
                             DWORD* error) {
    if (process_ == nullptr) {
        StoreError(ERROR_SUCCESS, error);
        return true;
    }
    HANDLE mode_event = nullptr;
    if (mode == V1EngineStopMode::GracefulTransport) {
        mode_event = graceful_transport_stop_event_;
    } else if (mode == V1EngineStopMode::CancelTransport) {
        mode_event = cancel_transport_event_;
    }
    if (mode != V1EngineStopMode::LocalOnly &&
        (!transport_worker_enabled_ || mode_event == nullptr)) {
        StoreError(ERROR_INVALID_STATE, error);
        return false;
    }
    if (mode_event != nullptr && !SetEvent(mode_event)) {
        const DWORD failure = GetLastError();
        Close();
        StoreError(failure, error);
        return false;
    }
    if (stop_event_ == nullptr || !SetEvent(stop_event_)) {
        const DWORD failure = stop_event_ == nullptr
                                  ? ERROR_INVALID_STATE
                                  : GetLastError();
        Close();
        StoreError(failure, error);
        return false;
    }
    const DWORD wait = WaitForSingleObject(process_, timeout_ms);
    if (wait != WAIT_OBJECT_0) {
        const DWORD failure = wait == WAIT_TIMEOUT
                                  ? WAIT_TIMEOUT
                                  : GetLastError();
        Close();
        StoreError(failure, error);
        return false;
    }
    if (mode != V1EngineStopMode::LocalOnly) {
        if (media_stopped_event_ == nullptr ||
            WaitForSingleObject(media_stopped_event_, 0u) !=
                WAIT_OBJECT_0) {
            Close();
            StoreError(ERROR_INVALID_DATA, error);
            return false;
        }
        media_stopped_observed_ = true;
        last_transport_stop_acknowledged_ = true;
    }
    DWORD code = 0u;
    if (!GetExitCodeProcess(process_, &code)) {
        const DWORD failure = GetLastError();
        Close();
        StoreError(failure, error);
        return false;
    }
    if (exit_code != nullptr) {
        *exit_code = code;
    }
    Close();
    StoreError(ERROR_SUCCESS, error);
    return code == 0u;
}

void V1EngineReadyHost::Close() {
    if (job_ != nullptr) {
        CloseHandle(job_);
        job_ = nullptr;
    }
    if (process_ != nullptr) {
        (void)WaitForSingleObject(process_, 5000u);
        CloseHandle(process_);
        process_ = nullptr;
    }
    if (stop_event_ != nullptr) {
        CloseHandle(stop_event_);
        stop_event_ = nullptr;
    }
    if (cancel_transport_event_ != nullptr) {
        CloseHandle(cancel_transport_event_);
        cancel_transport_event_ = nullptr;
    }
    if (single_gain_ready_event_ != nullptr) {
        CloseHandle(single_gain_ready_event_);
        single_gain_ready_event_ = nullptr;
    }
    if (graceful_transport_stop_event_ != nullptr) {
        CloseHandle(graceful_transport_stop_event_);
        graceful_transport_stop_event_ = nullptr;
    }
    if (media_failed_event_ != nullptr) {
        CloseHandle(media_failed_event_);
        media_failed_event_ = nullptr;
    }
    if (retryable_open_failure_event_ != nullptr) {
        CloseHandle(retryable_open_failure_event_);
        retryable_open_failure_event_ = nullptr;
    }
    if (media_stopped_event_ != nullptr) {
        CloseHandle(media_stopped_event_);
        media_stopped_event_ = nullptr;
    }
    if (media_started_event_ != nullptr) {
        CloseHandle(media_started_event_);
        media_started_event_ = nullptr;
    }
    if (capabilities_discovered_event_ != nullptr) {
        CloseHandle(capabilities_discovered_event_);
        capabilities_discovered_event_ = nullptr;
    }
    if (transport_open_event_ != nullptr) {
        CloseHandle(transport_open_event_);
        transport_open_event_ = nullptr;
    }
    if (ready_event_ != nullptr) {
        CloseHandle(ready_event_);
        ready_event_ = nullptr;
    }
    process_id_ = 0u;
    ready_observed_ = false;
    transport_worker_enabled_ = false;
    transport_open_authorized_ = false;
    capabilities_discovered_observed_ = false;
    media_started_observed_ = false;
    media_stopped_observed_ = false;
    media_failed_observed_ = false;
    retryable_open_failure_observed_ = false;
}

}  // namespace native_ldac::agent
