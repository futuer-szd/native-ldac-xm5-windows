#pragma once

#define NOMINMAX
#include <windows.h>
#include <ks.h>

#include <cstdint>
#include <string>

#include "nativeldac_pcm_abi.h"

namespace native_ldac::agent {

class V1EndpointPresenceSink {
public:
    V1EndpointPresenceSink() = default;
    V1EndpointPresenceSink(const V1EndpointPresenceSink&) = delete;
    V1EndpointPresenceSink& operator=(const V1EndpointPresenceSink&) = delete;
    ~V1EndpointPresenceSink();

    bool Open(DWORD* error);
    bool OpenForInstanceId(const std::wstring& instance_id,
                           DWORD* error);
    void Close();
    bool Set(bool present,
             std::uint64_t presence_generation,
             DWORD* error);
    bool Query(NATIVE_LDAC_PRESENCE_STATE* state,
               DWORD* error) const;
    bool QueryRenderActive(bool* active,
                           std::uint64_t* stream_epoch,
                           DWORD* error) const;

private:
    HANDLE device_ = INVALID_HANDLE_VALUE;
};

}  // namespace native_ldac::agent
