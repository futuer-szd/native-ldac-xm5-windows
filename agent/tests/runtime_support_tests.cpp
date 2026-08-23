#include "../runtime_support.h"
#include "nativeldac_direct_pdo_media_abi.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

int Fail(const wchar_t* message) {
    OutputDebugStringW(message);
    return 1;
}

}  // namespace

int wmain() {
    wchar_t temporary_root[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, temporary_root) == 0) {
        return Fail(L"GetTempPathW failed.");
    }
    const std::filesystem::path test_root =
        std::filesystem::path(temporary_root) /
        (L"NativeLdacAgentTests-" +
         std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));
    std::error_code error;
    std::filesystem::create_directories(test_root, error);
    if (error) {
        return Fail(L"Could not create the test directory.");
    }

    int result = 0;
    {
        native_ldac::agent::RotatingLog log;
        const std::filesystem::path log_path = test_root / L"probe.log";
        if (!log.Open(log_path.wstring(), 64, 2)) {
            result = Fail(L"Could not open rotating log.");
        } else {
            const std::string first(40, 'A');
            const std::string second(40, 'B');
            const std::string third(40, 'C');
            if (!log.Write(first.data(), first.size()) ||
                !log.Write(second.data(), second.size()) ||
                !log.Write(third.data(), third.size())) {
                result = Fail(L"Could not write rotating log.");
            }
            log.Close();
            if (!std::filesystem::exists(log_path) ||
                !std::filesystem::exists(log_path.wstring() + L".1") ||
                !std::filesystem::exists(log_path.wstring() + L".2") ||
                std::filesystem::exists(log_path.wstring() + L".3")) {
                result = Fail(L"Rotating log backups are incorrect.");
            }
        }
    }

    const std::filesystem::path state_path = test_root / L"state.json";
    native_ldac::agent::StateSnapshot state;
    state.state = L"probe_running";
    state.quality = L"hq";
    state.agent_pid = GetCurrentProcessId();
    state.probe_pid = 1234;
    state.generation = 2;
    state.last_probe_exit_code = 5;
    state.retry_delay_ms = 2000;
    state.config_enabled = false;
    state.config_revision = 7;
    if (result == 0 &&
        !native_ldac::agent::WriteStateAtomically(state_path.wstring(),
                                                  state)) {
        result = Fail(L"Atomic state write failed.");
    }
    if (result == 0) {
        std::ifstream stream(state_path, std::ios::binary);
        const std::string contents((std::istreambuf_iterator<char>(stream)),
                                   std::istreambuf_iterator<char>());
        if (contents.find("\"state\": \"probe_running\"") ==
                std::string::npos ||
            contents.find("\"probe_pid\": 1234") ==
                std::string::npos ||
            contents.find("\"config_enabled\": false") ==
                std::string::npos ||
            contents.find("\"config_revision\": 7") ==
                std::string::npos) {
            result = Fail(L"Atomic state contents are incorrect.");
        }
    }

    const std::filesystem::path config_path = test_root / L"config.json";
    native_ldac::agent::AgentConfig config;
    DWORD config_error = ERROR_SUCCESS;
    if (result == 0 &&
        native_ldac::agent::ReadAgentConfig(config_path.wstring(),
                                             &config,
                                             &config_error) !=
            native_ldac::agent::ConfigReadResult::Missing) {
        result = Fail(L"Missing agent config was not detected.");
    }
    if (result == 0) {
        std::ofstream stream(config_path, std::ios::binary);
        stream << "{\"version\":1,\"revision\":42,"
                  "\"enabled\":false,\"quality\":\"auto\"}";
        stream.close();
        if (native_ldac::agent::ReadAgentConfig(config_path.wstring(),
                                                 &config,
                                                 &config_error) !=
                native_ldac::agent::ConfigReadResult::Loaded ||
            config.enabled || config.quality != L"auto" ||
            config.channel_mode != L"stereo" ||
            config.sample_rate != 48000u ||
            config.bits_per_sample != 16u ||
            config.revision != 42u || config_error != ERROR_SUCCESS) {
            result = Fail(L"Valid agent config was not parsed.");
        }
    }
    if (result == 0) {
        std::ofstream stream(config_path,
                             std::ios::binary | std::ios::trunc);
        stream << "{\"version\":3,\"revision\":44,"
                  "\"enabled\":true,\"quality\":\"sq\","
                  "\"channel_mode\":\"mono\","
                  "\"sample_rate\":96000,\"bits_per_sample\":24}";
        stream.close();
        if (native_ldac::agent::ReadAgentConfig(config_path.wstring(),
                                                 &config,
                                                 &config_error) !=
                native_ldac::agent::ConfigReadResult::Loaded ||
            config.sample_rate != 96000u ||
            config.bits_per_sample != 24u || config.revision != 44u) {
            result = Fail(L"Version 3 endpoint format was not parsed.");
        }
    }
    if (result == 0) {
        std::ofstream stream(config_path,
                             std::ios::binary | std::ios::trunc);
        stream << "{\"version\":2,\"revision\":43,"
                  "\"enabled\":true,\"quality\":\"hq\","
                  "\"channel_mode\":\"dual\"}";
        stream.close();
        if (native_ldac::agent::ReadAgentConfig(config_path.wstring(),
                                                 &config,
                                                 &config_error) !=
                native_ldac::agent::ConfigReadResult::Loaded ||
            !config.enabled || config.quality != L"hq" ||
            config.channel_mode != L"dual" || config.revision != 43u) {
            result = Fail(L"Version 2 channel mode was not parsed.");
        }
    }
    if (result == 0) {
        std::ofstream stream(config_path,
                             std::ios::binary | std::ios::trunc);
        stream << "{\"version\":3,\"revision\":45,"
                  "\"enabled\":true,\"quality\":\"invalid\"}";
        stream.close();
        if (native_ldac::agent::ReadAgentConfig(config_path.wstring(),
                                                 &config,
                                                 &config_error) !=
                native_ldac::agent::ConfigReadResult::Invalid ||
            config_error != ERROR_INVALID_DATA) {
            result = Fail(L"Invalid agent config was accepted.");
        }
    }

    if (result == 0 &&
        (native_ldac::agent::ReconnectDelayMs(0, 3) != 2000 ||
         native_ldac::agent::ReconnectDelayMs(1, 3) != 2000 ||
         native_ldac::agent::ReconnectDelayMs(2, 3) != 4000 ||
         native_ldac::agent::ReconnectDelayMs(0, 5) != 30000 ||
         native_ldac::agent::ReconnectDelayMs(5, 5) != 30000)) {
        result = Fail(L"Reconnect cooldown policy is incorrect.");
    }

    if (result == 0 &&
        (!native_ldac::agent::IsXm5DeviceName(L"WH-1000XM5") ||
         !native_ldac::agent::IsXm5DeviceName(L"wh-1000xm5") ||
         native_ldac::agent::IsXm5DeviceName(L"WH-1000XM4") ||
         native_ldac::agent::IsXm5DeviceName(nullptr))) {
        result = Fail(L"XM5 Bluetooth name matching is incorrect.");
    }

    DWORD bluetooth_query_error = ERROR_SUCCESS;
    const native_ldac::agent::Xm5ConnectionState bluetooth_state =
        native_ldac::agent::QueryXm5Connection(&bluetooth_query_error);
    if (result == 0 &&
        ((bluetooth_state ==
              native_ldac::agent::Xm5ConnectionState::QueryFailed &&
          bluetooth_query_error == ERROR_SUCCESS) ||
         (bluetooth_state !=
              native_ldac::agent::Xm5ConnectionState::QueryFailed &&
          bluetooth_query_error != ERROR_SUCCESS))) {
        result = Fail(L"XM5 Bluetooth connection query status is inconsistent.");
    }

    DWORD transport_query_error = ERROR_SUCCESS;
    const native_ldac::agent::LdacTransportState transport_state =
        native_ldac::agent::QueryLdacTransport(&transport_query_error);
    if (result == 0 &&
        ((transport_state ==
              native_ldac::agent::LdacTransportState::QueryFailed &&
          transport_query_error == ERROR_SUCCESS) ||
         (transport_state ==
              native_ldac::agent::LdacTransportState::Ready &&
          transport_query_error != ERROR_SUCCESS))) {
        result = Fail(L"LDAC transport query status is inconsistent.");
    }

    DWORD direct_transport_error = ERROR_SUCCESS;
    const native_ldac::agent::LdacTransportState direct_transport_state =
        native_ldac::agent::QueryDirectPdoTransport(
            &direct_transport_error);
    if (result == 0 &&
        ((direct_transport_state ==
              native_ldac::agent::LdacTransportState::QueryFailed &&
          direct_transport_error == ERROR_SUCCESS) ||
         (direct_transport_state ==
              native_ldac::agent::LdacTransportState::Ready &&
          direct_transport_error != ERROR_SUCCESS) ||
         (direct_transport_state ==
              native_ldac::agent::LdacTransportState::Faulted &&
          direct_transport_error != ERROR_SUCCESS))) {
        result = Fail(L"Direct-PDO transport query status is inconsistent.");
    }

    DWORD pcm_query_error = ERROR_SUCCESS;
    const native_ldac::agent::NativePcmRunState pcm_state =
        native_ldac::agent::QueryNativePcmRunState(&pcm_query_error);
    if (result == 0 &&
        ((pcm_state == native_ldac::agent::NativePcmRunState::QueryFailed &&
          pcm_query_error == ERROR_SUCCESS) ||
         (pcm_state != native_ldac::agent::NativePcmRunState::QueryFailed &&
          pcm_query_error != ERROR_SUCCESS))) {
        result = Fail(L"Native PCM RUN query status is inconsistent.");
    }

    if (result == 0 &&
        (native_ldac::agent::PlanInstalledPresence(
             native_ldac::agent::Xm5ConnectionState::Disconnected,
             native_ldac::agent::LdacTransportState::Ready,
             true) != native_ldac::agent::Xm5PresenceAction::Wait ||
          native_ldac::agent::PlanInstalledPresence(
             native_ldac::agent::Xm5ConnectionState::QueryFailed,
             native_ldac::agent::LdacTransportState::Ready,
             false) != native_ldac::agent::Xm5PresenceAction::Wait ||
          native_ldac::agent::PlanInstalledPresence(
             native_ldac::agent::Xm5ConnectionState::Connected,
             native_ldac::agent::LdacTransportState::Unavailable,
             false) != native_ldac::agent::Xm5PresenceAction::Wait ||
          native_ldac::agent::PlanInstalledPresence(
             native_ldac::agent::Xm5ConnectionState::Connected,
             native_ldac::agent::LdacTransportState::Ready,
             true) != native_ldac::agent::Xm5PresenceAction::Settle ||
          native_ldac::agent::PlanInstalledPresence(
             native_ldac::agent::Xm5ConnectionState::Connected,
             native_ldac::agent::LdacTransportState::Ready,
             false) != native_ldac::agent::Xm5PresenceAction::StartProbe)) {
        result = Fail(L"Installed XM5 presence action is incorrect.");
    }

    native_ldac::agent::LegacyReconnectGate reconnect_gate;
    if (result == 0 &&
        native_ldac::agent::ObserveLegacyReconnectGate(
            &reconnect_gate,
            native_ldac::agent::Xm5ConnectionState::Connected,
            native_ldac::agent::LdacTransportState::Ready) !=
            native_ldac::agent::LegacyReconnectAction::
                AllowCurrentConnection) {
        result = Fail(L"An unarmed legacy reconnect gate blocked startup.");
    }
    native_ldac::agent::ArmLegacyReconnectGate(&reconnect_gate);
    if (result == 0 &&
        (native_ldac::agent::ObserveLegacyReconnectGate(
             &reconnect_gate,
             native_ldac::agent::Xm5ConnectionState::Connected,
             native_ldac::agent::LdacTransportState::Ready) !=
             native_ldac::agent::LegacyReconnectAction::
                 WaitForTransportAbsent ||
         !reconnect_gate.requires_fresh_transport ||
         reconnect_gate.transport_absence_observed)) {
        result = Fail(L"A stale ready transport bypassed the reconnect gate.");
    }
    if (result == 0 &&
        native_ldac::agent::ObserveLegacyReconnectGate(
            &reconnect_gate,
            native_ldac::agent::Xm5ConnectionState::Disconnected,
            native_ldac::agent::LdacTransportState::Ready) !=
            native_ldac::agent::LegacyReconnectAction::
                WaitForTransportAbsent) {
        result = Fail(L"A public disconnect was mistaken for PDO removal.");
    }
    if (result == 0 &&
        (native_ldac::agent::ObserveLegacyReconnectGate(
             &reconnect_gate,
             native_ldac::agent::Xm5ConnectionState::Disconnected,
             native_ldac::agent::LdacTransportState::Unavailable) !=
             native_ldac::agent::LegacyReconnectAction::
                 WaitForFreshConnection ||
         !reconnect_gate.transport_absence_observed)) {
        result = Fail(L"Definitive legacy transport removal was not recorded.");
    }
    if (result == 0 &&
        native_ldac::agent::ObserveLegacyReconnectGate(
            &reconnect_gate,
            native_ldac::agent::Xm5ConnectionState::QueryFailed,
            native_ldac::agent::LdacTransportState::QueryFailed) !=
            native_ldac::agent::LegacyReconnectAction::
                WaitForFreshConnection) {
        result = Fail(L"An uncertain reconnect state bypassed the gate.");
    }
    if (result == 0 &&
        (native_ldac::agent::ObserveLegacyReconnectGate(
             &reconnect_gate,
             native_ldac::agent::Xm5ConnectionState::Connected,
             native_ldac::agent::LdacTransportState::Ready) !=
             native_ldac::agent::LegacyReconnectAction::
                 AllowCurrentConnection ||
         reconnect_gate.requires_fresh_transport ||
         reconnect_gate.transport_absence_observed)) {
        result = Fail(L"A fresh legacy transport generation was not released.");
    }
    native_ldac::agent::ArmLegacyReconnectGate(&reconnect_gate);
    if (result == 0 && reconnect_gate.transport_absence_observed) {
        result = Fail(L"Rearming retained a previous absence observation.");
    }

    if (result == 0 &&
        (native_ldac::agent::PlanInstalledPresence(
             native_ldac::agent::Xm5ConnectionState::Connected,
             native_ldac::agent::LdacTransportState::Faulted,
             false,
             false) != native_ldac::agent::Xm5PresenceAction::Wait ||
         native_ldac::agent::PlanInstalledPresence(
             native_ldac::agent::Xm5ConnectionState::Connected,
             native_ldac::agent::LdacTransportState::Faulted,
             true,
             true) != native_ldac::agent::Xm5PresenceAction::Settle ||
         native_ldac::agent::PlanInstalledPresence(
             native_ldac::agent::Xm5ConnectionState::Connected,
             native_ldac::agent::LdacTransportState::Faulted,
             false,
             true) !=
             native_ldac::agent::Xm5PresenceAction::RecoverTransport)) {
        result = Fail(L"Direct-PDO recovery presence gate is incorrect.");
    }

    native_ldac::agent::DirectPdoTransportInfo recovery_info;
    recovery_info.state = native_ldac::agent::LdacTransportState::Faulted;
    recovery_info.failure_reason = NldDirectPdoFailureMediaTimeout;
    if (result == 0 &&
        !native_ldac::agent::CanRecoverDirectPdoTransport(recovery_info,
                                                          false)) {
        result = Fail(L"Media-timeout recovery was not allowed.");
    }
    recovery_info.failure_reason = NldDirectPdoFailureRemoteDisconnect;
    if (result == 0 &&
        (native_ldac::agent::CanRecoverDirectPdoTransport(recovery_info,
                                                          false) ||
         !native_ldac::agent::CanRecoverDirectPdoTransport(recovery_info,
                                                           true))) {
        result = Fail(L"Remote-disconnect recovery edge gate is incorrect.");
    }
    recovery_info.failure_reason = NldDirectPdoFailureBackend;
    if (result == 0 &&
        (native_ldac::agent::CanRecoverDirectPdoTransport(recovery_info,
                                                          false) ||
         !native_ldac::agent::CanRecoverDirectPdoTransport(recovery_info,
                                                           true))) {
        result = Fail(L"Backend recovery edge gate is incorrect.");
    }

    native_ldac::agent::DirectPdoTransportInfo direct_info;
    direct_info.state = native_ldac::agent::LdacTransportState::Ready;
    direct_info.media_state = NldDirectPdoMediaIdle;
    if (result == 0 &&
        native_ldac::agent::PlanDirectPdoPresence(
            native_ldac::agent::Xm5ConnectionState::Disconnected,
            direct_info,
            false) != native_ldac::agent::Xm5PresenceAction::StartProbe) {
        result = Fail(L"Direct-PDO ready state incorrectly depended on the "
                      L"public Bluetooth connected flag.");
    }
    direct_info.state = native_ldac::agent::LdacTransportState::Faulted;
    direct_info.failure_reason = NldDirectPdoFailureMediaTimeout;
    if (result == 0 &&
        native_ldac::agent::PlanDirectPdoPresence(
            native_ldac::agent::Xm5ConnectionState::Disconnected,
            direct_info,
            true) !=
            native_ldac::agent::Xm5PresenceAction::RecoverTransport) {
        result = Fail(L"Direct-PDO media-timeout recovery depended on the "
                      L"public Bluetooth connected flag.");
    }
    direct_info.failure_reason = NldDirectPdoFailureRemoteDisconnect;
    if (result == 0 &&
        (native_ldac::agent::PlanDirectPdoPresence(
             native_ldac::agent::Xm5ConnectionState::Disconnected,
             direct_info,
             true) != native_ldac::agent::Xm5PresenceAction::Wait ||
         native_ldac::agent::PlanDirectPdoPresence(
             native_ldac::agent::Xm5ConnectionState::Connected,
             direct_info,
             true) !=
             native_ldac::agent::Xm5PresenceAction::RecoverTransport)) {
        result = Fail(L"Direct-PDO remote-disconnect recovery lost its "
                      L"reconnect edge gate.");
    }

    if (result == 0 &&
        (native_ldac::agent::PlanInstalledMediaDemand(
             native_ldac::agent::Xm5ConnectionState::Disconnected,
             native_ldac::agent::LdacTransportState::Ready,
             native_ldac::agent::NativePcmRunState::Running) !=
             native_ldac::agent::MediaDemandAction::WaitForDevice ||
         native_ldac::agent::PlanInstalledMediaDemand(
             native_ldac::agent::Xm5ConnectionState::Connected,
             native_ldac::agent::LdacTransportState::Faulted,
             native_ldac::agent::NativePcmRunState::Running) !=
             native_ldac::agent::MediaDemandAction::WaitForDevice ||
         native_ldac::agent::PlanInstalledMediaDemand(
             native_ldac::agent::Xm5ConnectionState::Connected,
             native_ldac::agent::LdacTransportState::Ready,
             native_ldac::agent::NativePcmRunState::Idle) !=
             native_ldac::agent::MediaDemandAction::WaitForAudio ||
         native_ldac::agent::PlanInstalledMediaDemand(
             native_ldac::agent::Xm5ConnectionState::Connected,
             native_ldac::agent::LdacTransportState::Ready,
             native_ldac::agent::NativePcmRunState::Running) !=
             native_ldac::agent::MediaDemandAction::StartEngine)) {
        result = Fail(L"Installed media demand gate is incorrect.");
    }

    direct_info.state = native_ldac::agent::LdacTransportState::Ready;
    direct_info.media_state = NldDirectPdoMediaIdle;
    if (result == 0 &&
        native_ldac::agent::PlanDirectPdoMediaDemand(
            direct_info,
            native_ldac::agent::NativePcmRunState::Idle) !=
            native_ldac::agent::MediaDemandAction::WaitForAudio) {
        result = Fail(L"Idle Direct-PDO endpoint incorrectly armed engine.");
    }
    direct_info.media_state = NldDirectPdoMediaOpen;
    if (result == 0 &&
        native_ldac::agent::PlanDirectPdoMediaDemand(
            direct_info,
            native_ldac::agent::NativePcmRunState::Idle) !=
            native_ldac::agent::MediaDemandAction::StartEngine) {
        result = Fail(L"Acquired Direct-PDO session did not pre-arm engine.");
    }
    direct_info.media_state = NldDirectPdoMediaIdle;
    if (result == 0 &&
        native_ldac::agent::PlanDirectPdoMediaDemand(
            direct_info,
            native_ldac::agent::NativePcmRunState::Running) !=
            native_ldac::agent::MediaDemandAction::StartEngine) {
        result = Fail(L"WaveRT RUN did not start Direct-PDO engine.");
    }
    direct_info.state = native_ldac::agent::LdacTransportState::Faulted;
    if (result == 0 &&
        native_ldac::agent::PlanDirectPdoMediaDemand(
            direct_info,
            native_ldac::agent::NativePcmRunState::Running) !=
            native_ldac::agent::MediaDemandAction::WaitForDevice) {
        result = Fail(L"Faulted Direct-PDO state started media engine.");
    }

    HANDLE job = native_ldac::agent::CreateKillOnCloseJob();
    if (result == 0 && job == nullptr) {
        result = Fail(L"Kill-on-close Job Object creation failed.");
    }
    if (job != nullptr) {
        CloseHandle(job);
    }

    std::filesystem::remove_all(test_root, error);
    return result;
}
