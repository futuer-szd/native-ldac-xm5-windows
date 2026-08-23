// SPDX-License-Identifier: Apache-2.0
#pragma once

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <string>

namespace native_ldac::agent {

enum class V1DailyQuality : std::uint32_t { Hq = 0, Sq = 1, Mq = 2 };

enum class V1DailyConfigStatus : std::uint32_t {
    Accepted = 0,
    Rejected = 1,
    Invalid = 2,
    StaleRevision = 3,
    Error = 4,
};

#pragma pack(push, 1)
struct V1DailyConfigRequest {
    std::uint32_t magic = 0x31434C4Eu;  // NLC1
    std::uint16_t version = 1u;
    std::uint16_t message_type = 1u;
    std::uint32_t message_bytes = sizeof(V1DailyConfigRequest);
    std::uint64_t revision = 0u;
    std::uint32_t quality = static_cast<std::uint32_t>(V1DailyQuality::Hq);
    std::uint32_t reserved = 0u;
};

struct V1DailyConfigResponse {
    std::uint32_t magic = 0x31434C4Eu;
    std::uint16_t version = 1u;
    std::uint16_t message_type = 2u;
    std::uint32_t message_bytes = sizeof(V1DailyConfigResponse);
    std::uint32_t status = static_cast<std::uint32_t>(
        V1DailyConfigStatus::Error);
    std::uint64_t requested_revision = 0u;
    std::uint64_t applied_revision = 0u;
    std::uint32_t error = ERROR_SUCCESS;
    std::uint32_t reserved = 0u;
};
#pragma pack(pop)

static_assert(sizeof(V1DailyConfigRequest) == 28u);
static_assert(sizeof(V1DailyConfigResponse) == 40u);

bool ValidateV1DailyConfigRequest(const V1DailyConfigRequest& request);
bool IsV1DailyQuality(std::uint32_t value);
const wchar_t* V1DailyQualityName(V1DailyQuality quality);
bool ParseV1DailyQuality(const std::wstring& value,
                         V1DailyQuality* quality);

class V1DailyConfigServer {
public:
    V1DailyConfigServer() = default;
    V1DailyConfigServer(const V1DailyConfigServer&) = delete;
    V1DailyConfigServer& operator=(const V1DailyConfigServer&) = delete;
    ~V1DailyConfigServer();

    bool Start(const std::wstring& pipe_name,
               const std::wstring& persistence_path,
               V1DailyQuality default_quality,
               std::uint64_t default_revision,
               DWORD* error);
    void Stop();
    bool TakeAccepted(V1DailyConfigRequest* request);
    bool MarkApplied(std::uint64_t revision, DWORD* error);
    V1DailyQuality requested_quality() const;
    std::uint64_t requested_revision() const;
    V1DailyQuality applied_quality() const;
    std::uint64_t applied_revision() const;
    std::uint32_t rejected_count() const;
    DWORD last_error() const;
    const std::wstring& pipe_name() const { return pipe_name_; }

private:
    static DWORD WINAPI ThreadMain(void* context);
    void Run();
    bool LoadPersisted(DWORD* error);
    bool Persist(const V1DailyConfigRequest& request, DWORD* error);
    V1DailyConfigResponse Handle(const V1DailyConfigRequest& request);

    HANDLE stop_event_ = nullptr;
    HANDLE thread_ = nullptr;
    mutable CRITICAL_SECTION lock_{};
    bool lock_initialized_ = false;
    std::wstring pipe_name_;
    std::wstring persistence_path_;
    V1DailyConfigRequest requested_{};
    V1DailyConfigRequest applied_{};
    V1DailyConfigRequest pending_{};
    bool pending_available_ = false;
    std::uint32_t rejected_count_ = 0u;
    DWORD last_error_ = ERROR_SUCCESS;
};

}  // namespace native_ldac::agent
