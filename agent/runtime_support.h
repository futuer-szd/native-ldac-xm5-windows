#pragma once

#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace native_ldac::agent {

enum class Xm5ConnectionState {
    Connected,
    Disconnected,
    QueryFailed,
};

enum class LdacTransportState {
    Ready,
    Faulted,
    Unavailable,
    QueryFailed,
};

struct DirectPdoTransportInfo {
    LdacTransportState state = LdacTransportState::Unavailable;
    unsigned long media_state = 0;
    unsigned long session_generation = 0;
    unsigned long failure_reason = 0;
    unsigned long flags = 0;
};

enum class NativePcmRunState {
    Running,
    Idle,
    Unavailable,
    QueryFailed,
};

enum class MediaDemandAction {
    WaitForDevice,
    WaitForAudio,
    StartEngine,
};

enum class Xm5PresenceAction {
    Wait,
    Settle,
    RecoverTransport,
    StartProbe,
};

enum class LegacyReconnectAction {
    AllowCurrentConnection,
    WaitForTransportAbsent,
    WaitForFreshConnection,
};

struct LegacyReconnectGate {
    bool requires_fresh_transport = false;
    bool transport_absence_observed = false;
};

enum class ConfigReadResult {
    Loaded,
    Missing,
    Invalid,
};

struct AgentConfig {
    bool enabled = true;
    std::wstring quality = L"hq";
    std::wstring channel_mode = L"stereo";
    unsigned int sample_rate = 48000;
    unsigned int bits_per_sample = 16;
    std::uint64_t revision = 0;
};

class RotatingLog {
public:
    RotatingLog() = default;
    RotatingLog(const RotatingLog&) = delete;
    RotatingLog& operator=(const RotatingLog&) = delete;
    ~RotatingLog();

    bool Open(const std::wstring& path,
              std::uint64_t max_bytes,
              unsigned int backup_count);
    bool Write(const void* data, std::size_t size);
    void Flush() const;
    void Close();

private:
    bool OpenCurrent();
    bool Rotate();
    std::wstring BackupPath(unsigned int index) const;

    std::wstring path_;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::uint64_t current_size_ = 0;
    std::uint64_t max_bytes_ = 0;
    unsigned int backup_count_ = 0;
};

struct StateSnapshot {
    std::wstring state;
    std::wstring quality;
    DWORD agent_pid = 0;
    DWORD probe_pid = 0;
    unsigned int generation = 0;
    DWORD last_probe_exit_code = 0;
    DWORD retry_delay_ms = 0;
    bool config_enabled = true;
    std::uint64_t config_revision = 0;
};

bool WriteStateAtomically(const std::wstring& path,
                          const StateSnapshot& snapshot);

DWORD ReconnectDelayMs(unsigned int failure_count,
                       DWORD probe_exit_code);

bool IsXm5DeviceName(const wchar_t* name);
Xm5ConnectionState QueryXm5Connection(DWORD* query_error);
LdacTransportState QueryLdacTransport(DWORD* query_error);
LdacTransportState QueryDirectPdoTransport(DWORD* query_error);
LdacTransportState QueryDirectPdoTransport(
    DirectPdoTransportInfo* info,
    DWORD* query_error);
bool RequestDirectPdoRecovery(const DirectPdoTransportInfo& info,
                              DWORD* request_error);
bool CanRecoverDirectPdoTransport(const DirectPdoTransportInfo& info,
                                  bool disconnect_observed);
NativePcmRunState QueryNativePcmRunState(DWORD* query_error);
MediaDemandAction PlanInstalledMediaDemand(
    Xm5ConnectionState state,
    LdacTransportState transport_state,
    NativePcmRunState pcm_state);
MediaDemandAction PlanDirectPdoMediaDemand(
    const DirectPdoTransportInfo& info,
    NativePcmRunState pcm_state);
Xm5PresenceAction PlanInstalledPresence(Xm5ConnectionState state,
                                        LdacTransportState transport_state,
                                        bool state_changed,
                                        bool recovery_allowed = false);
Xm5PresenceAction PlanDirectPdoPresence(
    Xm5ConnectionState state,
    const DirectPdoTransportInfo& info,
    bool recovery_allowed);
void ArmLegacyReconnectGate(LegacyReconnectGate* gate);
LegacyReconnectAction ObserveLegacyReconnectGate(
    LegacyReconnectGate* gate,
    Xm5ConnectionState state,
    LdacTransportState transport_state);
ConfigReadResult ReadAgentConfig(const std::wstring& path,
                                 AgentConfig* config,
                                 DWORD* read_error);

HANDLE CreateKillOnCloseJob();

}  // namespace native_ldac::agent
