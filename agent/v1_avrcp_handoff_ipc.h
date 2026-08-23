#pragma once

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <string>

namespace native_ldac::agent {

// Result of waiting for the handoff/restore done event.
enum class V1AvrcpHandoffWaitResult {
    Ok,
    TimedOut,
    Failed,
};

// Daily-host side of the AVRCP owner handoff IPC (design section 4).
//
// The daily host (normal user) signals the elevated handoff host through
// fixed named events and a small atomic JSON state file. The handoff host
// performs the exact 0x110E PDO switch and signals back. When disabled
// (default) every call is a no-op success so the existing ObserveOnly /
// volume-sync path is unchanged.
class V1AvrcpHandoffIpc {
public:
    explicit V1AvrcpHandoffIpc(bool enabled,
                               std::wstring name_suffix = std::wstring());
    ~V1AvrcpHandoffIpc();

    V1AvrcpHandoffIpc(const V1AvrcpHandoffIpc&) = delete;
    V1AvrcpHandoffIpc& operator=(const V1AvrcpHandoffIpc&) = delete;

    bool enabled() const { return enabled_; }
    bool valid() const { return valid_; }

    // Requests the handoff host to switch the AVRCP PDO to the observer
    // function driver and waits for the done signal (bounded).
    bool RequestHandoff(std::uint64_t acl_generation,
                        std::uint32_t restart_count,
                        DWORD* error);
    V1AvrcpHandoffWaitResult WaitHandoffDone(DWORD timeout_ms, DWORD* error);

    // Requests the handoff host to restore Microsoft AVRCP after the media
    // period and waits for the done signal (bounded).
    bool RequestRestore(std::uint64_t acl_generation,
                        std::uint32_t restart_count,
                        DWORD* error);
    V1AvrcpHandoffWaitResult WaitRestoreDone(DWORD timeout_ms, DWORD* error);

    // Handoff-host side: wait for a request from the daily host. Returns
    // Ok when the request event fired, TimedOut after timeout_ms.
    V1AvrcpHandoffWaitResult WaitForHandoffRequest(DWORD timeout_ms,
                                                   DWORD* error);
    V1AvrcpHandoffWaitResult WaitForRestoreRequest(DWORD timeout_ms,
                                                   DWORD* error);
    // Waits for either request event; on Ok sets is_handoff for the fired
    // request. Used by the resident handoff host without polling.
    V1AvrcpHandoffWaitResult WaitForAnyRequest(DWORD timeout_ms,
                                               DWORD* error,
                                               bool* is_handoff);
    void ResetHandoffRequest();
    void ResetRestoreRequest();

    // Reads the generation/restart_count recorded by the daily host in the
    // state file so the completion write keeps the same session identity.
    bool ReadRequestInfo(std::uint64_t* generation,
                         std::uint32_t* restart_count,
                         DWORD* error);

    // Handoff-host side: record the completed owner state and signal the
    // daily host's done event.
    bool SignalHandoffCompleted(std::uint64_t acl_generation,
                                std::uint32_t restart_count,
                                const wchar_t* error_text,
                                DWORD* error);
    bool SignalRestoreCompleted(std::uint64_t acl_generation,
                                std::uint32_t restart_count,
                                const wchar_t* error_text,
                                DWORD* error);

    void Close();

private:
    bool Open();
    bool WriteState(const wchar_t* state,
                    std::uint64_t acl_generation,
                    std::uint32_t restart_count,
                    const wchar_t* error_text,
                    bool completed,
                    DWORD* error);
    bool ReadState(std::wstring* state,
                   std::uint64_t* generation,
                   std::uint32_t* restart_count,
                   std::wstring* error_text,
                   DWORD* error) const;
    bool SignalRequest(bool handoff, DWORD* error);
    V1AvrcpHandoffWaitResult WaitDone(bool handoff,
                                      DWORD timeout_ms,
                                      DWORD* error);

    bool enabled_ = false;
    bool valid_ = false;
    std::wstring name_suffix_;
    std::wstring state_path_;
    HANDLE handoff_request_ = nullptr;
    HANDLE handoff_done_ = nullptr;
    HANDLE restore_request_ = nullptr;
    HANDLE restore_done_ = nullptr;
    std::uint64_t expected_handoff_generation_ = 0u;
    std::uint32_t expected_handoff_restart_count_ = 0u;
    std::uint64_t expected_restore_generation_ = 0u;
    std::uint32_t expected_restore_restart_count_ = 0u;
};

}  // namespace native_ldac::agent
