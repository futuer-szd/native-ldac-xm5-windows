cmake_minimum_required(VERSION 3.20)
project(native_ldac_windows VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)

set(LIBLDAC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ldacBT/libldac)
if(NOT EXISTS ${LIBLDAC_ROOT}/inc/ldacBT.h)
    message(FATAL_ERROR
        "libldac sources are missing from vendor/ldacBT/libldac")
endif()

add_library(ldacBT_enc STATIC
    ${LIBLDAC_ROOT}/src/ldaclib.c
    ${LIBLDAC_ROOT}/src/ldacBT.c
)
target_include_directories(ldacBT_enc PUBLIC ${LIBLDAC_ROOT}/inc)
if(MSVC)
    target_include_directories(ldacBT_enc PRIVATE cmake/win_compat)
    target_compile_options(ldacBT_enc PRIVATE
        /FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/win_compat/msvc_compat.h
        /wd4005 /wd4018 /wd4101 /wd4133 /wd4146 /wd4244 /wd4267
    )
endif()

add_library(ldac_native_protocol STATIC
    protocol/src/avdtp.c
    protocol/src/avrcp.c
    protocol/src/ldac_codec.c
    protocol/src/rtp_ldac.c
)
target_include_directories(ldac_native_protocol PUBLIC protocol/include)
target_compile_features(ldac_native_protocol PUBLIC c_std_11)

add_library(ldac_native_encoder STATIC
    engine/src/abr_controller.c
    engine/src/ldac_encoder.c
)
target_include_directories(ldac_native_encoder PUBLIC engine/include)
target_compile_features(ldac_native_encoder PUBLIC c_std_11)
target_link_libraries(ldac_native_encoder PUBLIC ldacBT_enc)

add_library(nativeldac_pcm_owner_contract STATIC
    audio-endpoint/Source/Main/nativeldac_pcm_owner_contract.c)
target_include_directories(nativeldac_pcm_owner_contract PUBLIC
    audio-endpoint/Source/Inc)
target_compile_features(nativeldac_pcm_owner_contract PUBLIC c_std_11)

add_library(nativeldac_presence_owner_contract STATIC
    audio-endpoint/Source/Main/nativeldac_presence_owner_contract.c)
target_include_directories(nativeldac_presence_owner_contract PUBLIC
    audio-endpoint/Source/Inc)
target_compile_features(nativeldac_presence_owner_contract PUBLIC c_std_11)

if(WIN32)
    add_library(nativeldac_avrcp_event_queue_contract STATIC
        avrcp-sideband/src/nativeldac_avrcp_event_queue_contract.c)
    target_include_directories(nativeldac_avrcp_event_queue_contract PUBLIC
        avrcp-sideband/include)
    target_compile_features(nativeldac_avrcp_event_queue_contract PUBLIC
        c_std_11)

    add_library(nativeldac_avrcp_filter_trace_contract STATIC
        avrcp-filter/src/nativeldac_avrcp_filter_trace_contract.c)
    target_include_directories(nativeldac_avrcp_filter_trace_contract PUBLIC
        avrcp-filter/include)
    target_compile_features(nativeldac_avrcp_filter_trace_contract PUBLIC
        c_std_11)
endif()

add_library(ldac_direct_pdo_contract STATIC
    direct-pdo/src/nativeldac_avrcp_control.c
    direct-pdo/src/nativeldac_avdtp_transaction.c
    direct-pdo/src/nativeldac_bth_indication_contract.c
    direct-pdo/src/nativeldac_bth_transfer_contract.c
    direct-pdo/src/nativeldac_direct_pdo_arbiter_contract.c
    direct-pdo/src/nativeldac_direct_pdo_control_abi.c
    direct-pdo/src/nativeldac_direct_pdo_media_abi.c
    direct-pdo/src/nativeldac_media_watchdog_contract.c
    direct-pdo/src/nativeldac_direct_pdo_contract.c
    direct-pdo/src/nativeldac_direct_pdo_diagnostic_contract.c
    direct-pdo/src/nativeldac_direct_pdo_dispatch_contract.c
    direct-pdo/src/nativeldac_direct_pdo_preemption_contract.c
    direct-pdo/src/nativeldac_bth_owner_contract.c
    direct-pdo/src/nativeldac_bth_address_contract.c
    direct-pdo/src/nativeldac_bth_signaling_contract.c
)
target_include_directories(ldac_direct_pdo_contract PUBLIC
    direct-pdo/include
    protocol/include)
target_compile_features(ldac_direct_pdo_contract PUBLIC c_std_11)

if(MSVC)
    target_compile_options(ldac_native_protocol PRIVATE /W4 /WX)
    target_compile_options(ldac_native_encoder PRIVATE /W4 /WX)
    target_compile_options(nativeldac_pcm_owner_contract PRIVATE /W4 /WX)
    target_compile_options(nativeldac_presence_owner_contract PRIVATE /W4 /WX)
    if(WIN32)
        target_compile_options(nativeldac_avrcp_event_queue_contract PRIVATE
            /W4 /WX)
        target_compile_options(nativeldac_avrcp_filter_trace_contract PRIVATE
            /W4 /WX)
    endif()
    target_compile_options(ldac_direct_pdo_contract PRIVATE /W4 /WX)
else()
    target_compile_options(ldac_native_protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(ldac_native_encoder PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(nativeldac_pcm_owner_contract PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(nativeldac_presence_owner_contract PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
    if(WIN32)
        target_compile_options(nativeldac_avrcp_event_queue_contract PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(nativeldac_avrcp_filter_trace_contract PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    target_compile_options(ldac_direct_pdo_contract PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

if(WIN32)
    add_library(native_pcm_source STATIC
        engine/windows/native_pcm_source.cpp
    )
    target_include_directories(native_pcm_source
        PUBLIC engine/include
        PRIVATE audio-endpoint/Source/Inc)
    target_compile_features(native_pcm_source PUBLIC cxx_std_17)
    target_link_libraries(native_pcm_source PUBLIC ole32 setupapi)
    if(MSVC)
        target_compile_options(native_pcm_source PRIVATE /W4 /WX /utf-8 /EHsc)
    endif()

    add_library(direct_pdo_media_sink STATIC
        engine/windows/direct_pdo_media_sink.cpp)
    target_include_directories(direct_pdo_media_sink
        PUBLIC engine/include direct-pdo/include)
    target_compile_features(direct_pdo_media_sink PUBLIC cxx_std_17)
    target_link_libraries(direct_pdo_media_sink PUBLIC setupapi)
    if(MSVC)
        target_compile_options(direct_pdo_media_sink PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_library(wasapi_loopback STATIC
        engine/windows/wasapi_loopback.cpp
    )
    target_include_directories(wasapi_loopback PUBLIC engine/include)
    target_compile_features(wasapi_loopback PUBLIC cxx_std_17)
    target_link_libraries(wasapi_loopback PUBLIC ole32)
    if(MSVC)
        target_compile_options(wasapi_loopback PRIVATE /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(transport_probe tools/transport_probe.c)
    target_include_directories(transport_probe PRIVATE driver/inc)
    target_link_libraries(transport_probe PRIVATE
        ldac_native_protocol
        ldac_native_encoder
        native_pcm_source
        wasapi_loopback
        bcrypt
        setupapi
    )
    if(MSVC)
        target_compile_options(transport_probe PRIVATE /W4 /WX /utf-8)
    endif()

    add_executable(ldac_direct_engine
        engine/windows/ldac_direct_engine.cpp)
    target_link_libraries(ldac_direct_engine PRIVATE
        ldac_native_encoder
        ldac_native_protocol
        native_pcm_source
        direct_pdo_media_sink)
    target_compile_features(ldac_direct_engine PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(ldac_direct_engine PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(audio_endpoint_probe tools/audio_endpoint_probe.c)
    target_include_directories(audio_endpoint_probe PRIVATE
        audio-endpoint/Source/Inc
        direct-pdo/include)
    target_link_libraries(audio_endpoint_probe PRIVATE setupapi)
    if(MSVC)
        target_compile_options(audio_endpoint_probe PRIVATE /W4 /WX /utf-8)
    endif()

    add_executable(endpoint_volume_probe tools/endpoint_volume_probe.cpp)
    target_compile_features(endpoint_volume_probe PRIVATE cxx_std_17)
    target_link_libraries(endpoint_volume_probe PRIVATE ole32)
    if(MSVC)
        target_compile_options(endpoint_volume_probe PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_swd_child_probe tools/v1_swd_child_probe.cpp)
    target_compile_features(v1_swd_child_probe PRIVATE cxx_std_17)
    target_link_libraries(v1_swd_child_probe PRIVATE onecoreuap ole32)
    if(MSVC)
        target_compile_options(v1_swd_child_probe PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_swd_endpoint_host
        tools/v1_swd_endpoint_host.cpp
        agent/v1_endpoint_presence_sink.cpp)
    target_include_directories(v1_swd_endpoint_host PRIVATE
        agent
        audio-endpoint/Source/Inc)
    target_compile_features(v1_swd_endpoint_host PRIVATE cxx_std_17)
    target_link_libraries(v1_swd_endpoint_host PRIVATE
        onecoreuap ole32 setupapi)
    if(MSVC)
        target_compile_options(v1_swd_endpoint_host PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(xm5_connection_probe
        tools/xm5_connection_probe.cpp
        agent/xm5_acl_event.cpp
        agent/runtime_support.cpp)
    target_include_directories(xm5_connection_probe PRIVATE
        agent
        driver/inc
        audio-endpoint/Source/Inc
        direct-pdo/include)
    target_compile_features(xm5_connection_probe PRIVATE cxx_std_17)
    target_link_libraries(xm5_connection_probe PRIVATE
        bthprops cfgmgr32 ole32 setupapi user32)
    if(MSVC)
        target_compile_options(xm5_connection_probe PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_presence_agent
        agent/v1_avrcp_bootstrap_play.cpp
        agent/v1_avrcp_filter_host.cpp
        agent/v1_avrcp_filter_decoder.cpp
        agent/v1_avrcp_handoff_ipc.cpp
        agent/v1_avrcp_observer_host.cpp
        agent/v1_avrcp_action_executor.cpp
        agent/v1_avrcp_windows_sink.cpp
        agent/v1_native_ldac_volume_endpoint_selector.cpp
        agent/v1_avrcp_action_mapper.cpp
        agent/v1_media_session_eligibility.cpp
        agent/v1_media_session_monitor.cpp
        agent/v1_hfp_capture_monitor.cpp
        agent/v1_hfp_lifecycle_adapter.cpp
        agent/v1_hfp_render_endpoint_selector.cpp
        agent/v1_hfp_render_endpoint_monitor.cpp
        agent/v1_hfp_shadow_state.cpp
        agent/v1_hfp_switch_state.cpp
        agent/avrcp_absolute_volume_state.cpp
        agent/avrcp_passthrough_state.cpp
        agent/v1_daily_instance.cpp
        agent/v1_daily_config_ipc.cpp
        agent/v1_engine_ready_host.cpp
        agent/v1_endpoint_presence_sink.cpp
        agent/v1_presence_agent.cpp
        agent/v1_lifecycle.cpp
        agent/v1_render_demand_tracker.cpp
        agent/v1_transport_open_stability.cpp
        agent/xm5_acl_event.cpp
        agent/xm5_acl_watcher.cpp)
    target_include_directories(v1_presence_agent PRIVATE
        agent
        audio-endpoint/Source/Inc
        avrcp-sideband/include
        avrcp-filter/include
        protocol/include)
    target_compile_features(v1_presence_agent PRIVATE cxx_std_17)
    target_link_libraries(v1_presence_agent PRIVATE
        advapi32 bthprops setupapi user32 ole32 windowsapp
        ldac_native_protocol)
    if(MSVC)
        target_compile_options(v1_presence_agent PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(endpoint_format_control tools/endpoint_format_control.cpp)
    target_include_directories(endpoint_format_control PRIVATE
        audio-endpoint/Source/Inc)
    target_compile_features(endpoint_format_control PRIVATE cxx_std_17)
    target_link_libraries(endpoint_format_control PRIVATE ole32)
    if(MSVC)
        target_compile_options(endpoint_format_control PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_engine_ready_stub
        agent/v1_engine_ready_stub.cpp)
    target_compile_features(v1_engine_ready_stub PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(v1_engine_ready_stub PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_transport_worker_stub
        agent/v1_transport_worker_stub.cpp)
    target_compile_features(v1_transport_worker_stub PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_worker_stub PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_transport_worker_fail_stub
        agent/v1_transport_worker_stub.cpp)
    target_compile_definitions(v1_transport_worker_fail_stub PRIVATE
        V1_TRANSPORT_WORKER_STUB_FAIL_AFTER_OPEN=1)
    target_compile_features(v1_transport_worker_fail_stub PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_worker_fail_stub PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_library(v1_transport_session STATIC
        agent/v1_transport_session.cpp)
    target_include_directories(v1_transport_session PUBLIC
        agent protocol/include)
    target_link_libraries(v1_transport_session PUBLIC
        ldac_native_protocol)
    target_compile_features(v1_transport_session PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_session PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_library(v1_transport_driver_backend STATIC
        agent/v1_transport_driver_backend.cpp)
    target_include_directories(v1_transport_driver_backend PUBLIC
        agent driver/inc protocol/include)
    target_link_libraries(v1_transport_driver_backend PUBLIC
        v1_transport_session setupapi)
    target_compile_features(v1_transport_driver_backend PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_driver_backend PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_library(v1_transport_configuration_session STATIC
        agent/v1_transport_configuration_session.cpp)
    target_include_directories(v1_transport_configuration_session PUBLIC
        agent protocol/include engine/include)
    target_link_libraries(v1_transport_configuration_session PUBLIC
        ldac_native_protocol)
    target_compile_features(v1_transport_configuration_session PUBLIC
        cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_configuration_session PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_library(v1_transport_configuration_driver_backend STATIC
        agent/v1_transport_configuration_driver_backend.cpp)
    target_include_directories(
        v1_transport_configuration_driver_backend PUBLIC
        agent driver/inc protocol/include)
    target_link_libraries(
        v1_transport_configuration_driver_backend PUBLIC
        v1_transport_configuration_session
        v1_transport_driver_backend)
    target_compile_features(
        v1_transport_configuration_driver_backend PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(
            v1_transport_configuration_driver_backend PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_library(v1_transport_silence_session STATIC
        agent/v1_transport_silence_session.cpp)
    target_include_directories(v1_transport_silence_session PUBLIC
        agent protocol/include engine/include)
    target_link_libraries(v1_transport_silence_session PUBLIC
        ldac_native_protocol ldac_native_encoder)
    target_compile_features(v1_transport_silence_session PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_silence_session PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_library(v1_transport_silence_driver_backend STATIC
        agent/v1_transport_silence_driver_backend.cpp)
    target_include_directories(v1_transport_silence_driver_backend PUBLIC
        agent driver/inc protocol/include engine/include)
    target_link_libraries(v1_transport_silence_driver_backend PUBLIC
        v1_transport_silence_session
        v1_transport_configuration_driver_backend
        v1_transport_driver_backend)
    target_compile_features(v1_transport_silence_driver_backend PUBLIC
        cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_silence_driver_backend PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_library(v1_linked_stereo_block_limiter STATIC
        agent/v1_linked_stereo_block_limiter.cpp)
    target_include_directories(v1_linked_stereo_block_limiter PUBLIC agent)
    target_compile_features(v1_linked_stereo_block_limiter PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(v1_linked_stereo_block_limiter PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_library(v1_sent_frame_fade_envelope STATIC
        agent/v1_sent_frame_fade_envelope.cpp)
    target_include_directories(v1_sent_frame_fade_envelope PUBLIC agent)
    target_compile_features(v1_sent_frame_fade_envelope PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(v1_sent_frame_fade_envelope PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_library(v1_transport_pcm_session STATIC
        agent/v1_transport_pcm_session.cpp)
    target_include_directories(v1_transport_pcm_session PUBLIC
        agent protocol/include engine/include)
    target_link_libraries(v1_transport_pcm_session PUBLIC
        v1_transport_silence_session
        ldac_native_protocol ldac_native_encoder
        v1_linked_stereo_block_limiter
        v1_sent_frame_fade_envelope)
    target_compile_features(v1_transport_pcm_session PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_pcm_session PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_library(v1_transport_pcm_source_adapter STATIC
        agent/v1_transport_pcm_source_adapter.cpp)
    target_include_directories(v1_transport_pcm_source_adapter PUBLIC
        agent engine/include)
    target_link_libraries(v1_transport_pcm_source_adapter PUBLIC
        v1_transport_pcm_session native_pcm_source)
    target_compile_features(v1_transport_pcm_source_adapter PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_pcm_source_adapter PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_avrcp_action_executor
        tools/v1_avrcp_action_executor.cpp
        agent/v1_avrcp_action_executor.cpp
        agent/v1_avrcp_action_mapper.cpp
        agent/v1_media_session_eligibility.cpp
        agent/v1_media_session_monitor.cpp
        agent/avrcp_passthrough_state.cpp
        agent/avrcp_absolute_volume_state.cpp
        agent/v1_avrcp_windows_sink.cpp
        agent/v1_native_ldac_volume_endpoint_selector.cpp)
    target_include_directories(v1_avrcp_action_executor PRIVATE
        agent avrcp-sideband/include audio-endpoint/Source/Inc)
    target_compile_features(v1_avrcp_action_executor PRIVATE cxx_std_17)
    target_link_libraries(v1_avrcp_action_executor PRIVATE
        ole32 setupapi windowsapp)
    if(MSVC)
        target_compile_options(v1_avrcp_action_executor PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_library(v1_avrcp_filter_decoder STATIC
        agent/v1_avrcp_filter_decoder.cpp)
    target_include_directories(v1_avrcp_filter_decoder PUBLIC
        agent protocol/include)
    target_link_libraries(v1_avrcp_filter_decoder PUBLIC
        ldac_native_protocol)
    target_compile_features(v1_avrcp_filter_decoder PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(v1_avrcp_filter_decoder PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_transport_discovery_worker
        agent/v1_transport_discovery_worker.cpp)
    target_link_libraries(v1_transport_discovery_worker PRIVATE
        v1_transport_driver_backend)
    target_compile_features(v1_transport_discovery_worker PRIVATE
        cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_discovery_worker PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_transport_discovery_mock_worker
        agent/v1_transport_discovery_worker.cpp)
    target_compile_definitions(v1_transport_discovery_mock_worker PRIVATE
        V1_TRANSPORT_DISCOVERY_MOCK_BACKEND=1)
    target_link_libraries(v1_transport_discovery_mock_worker PRIVATE
        v1_transport_session)
    target_compile_features(v1_transport_discovery_mock_worker PRIVATE
        cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_discovery_mock_worker PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_transport_discovery_retryable_mock_worker
        agent/v1_transport_discovery_worker.cpp)
    target_compile_definitions(
        v1_transport_discovery_retryable_mock_worker PRIVATE
        V1_TRANSPORT_DISCOVERY_MOCK_BACKEND=1
        V1_TRANSPORT_DISCOVERY_MOCK_RETRYABLE_OPEN=1)
    target_link_libraries(
        v1_transport_discovery_retryable_mock_worker PRIVATE
        v1_transport_session)
    target_compile_features(
        v1_transport_discovery_retryable_mock_worker PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(
            v1_transport_discovery_retryable_mock_worker PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_transport_configuration_worker
        agent/v1_transport_configuration_worker.cpp)
    target_link_libraries(v1_transport_configuration_worker PRIVATE
        v1_transport_configuration_driver_backend)
    target_compile_features(v1_transport_configuration_worker PRIVATE
        cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_configuration_worker PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_transport_configuration_mock_worker
        agent/v1_transport_configuration_worker.cpp)
    target_compile_definitions(
        v1_transport_configuration_mock_worker PRIVATE
        V1_TRANSPORT_CONFIGURATION_MOCK_BACKEND=1)
    target_link_libraries(v1_transport_configuration_mock_worker PRIVATE
        v1_transport_configuration_session)
    target_compile_features(
        v1_transport_configuration_mock_worker PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(
            v1_transport_configuration_mock_worker PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_transport_silence_worker
        agent/v1_transport_silence_worker.cpp)
    target_link_libraries(v1_transport_silence_worker PRIVATE
        v1_transport_silence_driver_backend)
    target_compile_features(v1_transport_silence_worker PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_silence_worker PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_transport_silence_mock_worker
        agent/v1_transport_silence_worker.cpp)
    target_compile_definitions(v1_transport_silence_mock_worker PRIVATE
        V1_TRANSPORT_SILENCE_MOCK_BACKEND=1)
    target_link_libraries(v1_transport_silence_mock_worker PRIVATE
        v1_transport_silence_session)
    target_compile_features(v1_transport_silence_mock_worker PRIVATE
        cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_silence_mock_worker PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_transport_pcm_worker
        agent/v1_transport_pcm_worker.cpp)
    target_link_libraries(v1_transport_pcm_worker PRIVATE
        v1_transport_pcm_session
        v1_transport_pcm_source_adapter
        v1_transport_silence_driver_backend)
    target_compile_features(v1_transport_pcm_worker PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_pcm_worker PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_transport_audible_worker
        agent/v1_transport_audible_worker.cpp)
    target_link_libraries(v1_transport_audible_worker PRIVATE
        v1_transport_pcm_session
        v1_transport_pcm_source_adapter
        v1_transport_silence_driver_backend)
    target_compile_features(v1_transport_audible_worker PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_audible_worker PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_transport_recognizable_worker
        agent/v1_transport_recognizable_worker.cpp)
    target_link_libraries(v1_transport_recognizable_worker PRIVATE
        v1_transport_pcm_session
        v1_transport_pcm_source_adapter
        v1_transport_silence_driver_backend)
    target_compile_features(v1_transport_recognizable_worker PRIVATE
        cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_recognizable_worker PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_transport_stability_worker
        agent/v1_transport_stability_worker.cpp)
    target_link_libraries(v1_transport_stability_worker PRIVATE
        v1_transport_pcm_session
        v1_transport_pcm_source_adapter
        v1_transport_silence_driver_backend)
    target_compile_features(v1_transport_stability_worker PRIVATE
        cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_stability_worker PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_transport_linked_limiter_worker
        agent/v1_transport_linked_limiter_worker.cpp)
    target_link_libraries(v1_transport_linked_limiter_worker PRIVATE
        v1_transport_pcm_session
        v1_transport_pcm_source_adapter
        v1_transport_silence_driver_backend)
    target_compile_features(v1_transport_linked_limiter_worker PRIVATE
        cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_linked_limiter_worker PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_transport_fidelity_bridge_worker
        agent/v1_transport_fidelity_bridge_worker.cpp)
    target_link_libraries(v1_transport_fidelity_bridge_worker PRIVATE
        v1_transport_pcm_session
        v1_transport_pcm_source_adapter
        v1_transport_silence_driver_backend)
    target_compile_features(v1_transport_fidelity_bridge_worker PRIVATE
        cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_fidelity_bridge_worker PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_transport_normal_stop_worker
        agent/v1_transport_normal_stop_worker.cpp)
    target_link_libraries(v1_transport_normal_stop_worker PRIVATE
        v1_transport_pcm_session
        v1_transport_pcm_source_adapter
        v1_transport_silence_driver_backend)
    target_compile_features(v1_transport_normal_stop_worker PRIVATE
        cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_normal_stop_worker PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_transport_daily_worker
        agent/v1_transport_daily_worker.cpp)
    target_link_libraries(v1_transport_daily_worker PRIVATE
        v1_transport_pcm_session
        v1_transport_pcm_source_adapter
        v1_transport_silence_driver_backend)
    target_compile_features(v1_transport_daily_worker PRIVATE
        cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_daily_worker PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_transport_pcm_mock_worker
        agent/v1_transport_pcm_worker.cpp)
    target_compile_definitions(v1_transport_pcm_mock_worker PRIVATE
        V1_TRANSPORT_PCM_MOCK_BACKEND=1)
    target_link_libraries(v1_transport_pcm_mock_worker PRIVATE
        v1_transport_pcm_session)
    target_compile_features(v1_transport_pcm_mock_worker PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(v1_transport_pcm_mock_worker PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_pcm_encode_engine
        engine/windows/v1_pcm_encode_engine.cpp)
    target_link_libraries(v1_pcm_encode_engine PRIVATE
        ldac_native_encoder
        native_pcm_source)
    target_compile_features(v1_pcm_encode_engine PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(v1_pcm_encode_engine PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(avrcp_transport_probe tools/avrcp_transport_probe.cpp)
    target_compile_features(avrcp_transport_probe PRIVATE cxx_std_17)
    target_link_libraries(avrcp_transport_probe PRIVATE setupapi)
    if(MSVC)
        target_compile_options(avrcp_transport_probe PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_avrcp_observer_probe
        tools/v1_avrcp_observer_probe.cpp)
    target_include_directories(v1_avrcp_observer_probe PRIVATE
        avrcp-sideband/include)
    target_compile_features(v1_avrcp_observer_probe PRIVATE cxx_std_17)
    target_link_libraries(v1_avrcp_observer_probe PRIVATE setupapi)
    if(MSVC)
        target_compile_options(v1_avrcp_observer_probe PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(v1_avrcp_filter_probe
        tools/v1_avrcp_filter_probe.cpp)
    target_include_directories(v1_avrcp_filter_probe PRIVATE
        avrcp-filter/include
        agent
        protocol/include)
    target_link_libraries(v1_avrcp_filter_probe PRIVATE
        v1_avrcp_filter_decoder)
    target_compile_features(v1_avrcp_filter_probe PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(v1_avrcp_filter_probe PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(xm5_media_control_observer
        tools/xm5_media_control_observer.cpp
        agent/xm5_media_control_event.cpp)
    target_include_directories(xm5_media_control_observer PRIVATE agent)
    target_compile_features(xm5_media_control_observer PRIVATE cxx_std_17)
    target_link_libraries(xm5_media_control_observer PRIVATE hid user32)
    if(MSVC)
        target_compile_options(xm5_media_control_observer PRIVATE
            /W4 /WX /utf-8 /EHsc)
    endif()

    add_executable(ldac_agent WIN32
        agent/ldac_agent.cpp
        agent/runtime_support.cpp)
    target_include_directories(ldac_agent PRIVATE
        agent
        driver/inc
        audio-endpoint/Source/Inc
        direct-pdo/include)
    target_compile_features(ldac_agent PRIVATE cxx_std_17)
    target_link_libraries(ldac_agent PRIVATE
        bcrypt shell32 bthprops setupapi)
    if(MSVC)
        target_compile_options(ldac_agent PRIVATE /W4 /WX /utf-8 /EHsc)
    endif()
endif()

if(BUILD_TESTING)
    add_executable(protocol_tests protocol/tests/protocol_tests.c)
    target_link_libraries(protocol_tests PRIVATE ldac_native_protocol)
    if(MSVC)
        target_compile_options(protocol_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(protocol_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME protocol_tests COMMAND protocol_tests)

    add_executable(avrcp_tests protocol/tests/avrcp_tests.c)
    target_link_libraries(avrcp_tests PRIVATE ldac_native_protocol)
    if(MSVC)
        target_compile_options(avrcp_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(avrcp_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME avrcp_tests COMMAND avrcp_tests)

    if(WIN32)
        add_executable(nativeldac_avrcp_event_queue_contract_tests
            avrcp-sideband/tests/nativeldac_avrcp_event_queue_contract_tests.c)
        target_link_libraries(
            nativeldac_avrcp_event_queue_contract_tests PRIVATE
            nativeldac_avrcp_event_queue_contract)
        if(MSVC)
            target_compile_options(
                nativeldac_avrcp_event_queue_contract_tests PRIVATE /W4 /WX)
        endif()
        add_test(NAME nativeldac_avrcp_event_queue_contract_tests
            COMMAND nativeldac_avrcp_event_queue_contract_tests)

        add_executable(nativeldac_avrcp_filter_trace_contract_tests
            avrcp-filter/tests/nativeldac_avrcp_filter_trace_contract_tests.c)
        target_link_libraries(
            nativeldac_avrcp_filter_trace_contract_tests PRIVATE
            nativeldac_avrcp_filter_trace_contract)
        if(MSVC)
            target_compile_options(
                nativeldac_avrcp_filter_trace_contract_tests PRIVATE /W4 /WX)
        endif()
        add_test(NAME nativeldac_avrcp_filter_trace_contract_tests
            COMMAND nativeldac_avrcp_filter_trace_contract_tests)
    endif()

    add_executable(ldac_encoder_tests engine/tests/ldac_encoder_tests.c)
    target_link_libraries(ldac_encoder_tests PRIVATE
        ldac_native_encoder
        ldac_native_protocol
    )
    if(MSVC)
        target_compile_options(ldac_encoder_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(ldac_encoder_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME ldac_encoder_tests COMMAND ldac_encoder_tests)

    add_executable(abr_controller_tests engine/tests/abr_controller_tests.c)
    target_link_libraries(abr_controller_tests PRIVATE ldac_native_encoder)
    if(MSVC)
        target_compile_options(abr_controller_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(abr_controller_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME abr_controller_tests COMMAND abr_controller_tests)

    add_executable(nativeldac_pcm_owner_contract_tests
        audio-endpoint/tests/nativeldac_pcm_owner_contract_tests.c)
    target_link_libraries(nativeldac_pcm_owner_contract_tests PRIVATE
        nativeldac_pcm_owner_contract)
    if(MSVC)
        target_compile_options(nativeldac_pcm_owner_contract_tests PRIVATE
            /W4 /WX)
    else()
        target_compile_options(nativeldac_pcm_owner_contract_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME nativeldac_pcm_owner_contract_tests
        COMMAND nativeldac_pcm_owner_contract_tests)

    add_executable(nativeldac_presence_owner_contract_tests
        audio-endpoint/tests/nativeldac_presence_owner_contract_tests.c)
    target_link_libraries(nativeldac_presence_owner_contract_tests PRIVATE
        nativeldac_presence_owner_contract)
    if(MSVC)
        target_compile_options(nativeldac_presence_owner_contract_tests PRIVATE
            /W4 /WX)
    else()
        target_compile_options(nativeldac_presence_owner_contract_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME nativeldac_presence_owner_contract_tests
        COMMAND nativeldac_presence_owner_contract_tests)

    add_executable(direct_pdo_contract_tests
        direct-pdo/tests/direct_pdo_contract_tests.c)
    target_link_libraries(direct_pdo_contract_tests PRIVATE
        ldac_direct_pdo_contract)
    if(MSVC)
        target_compile_options(direct_pdo_contract_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(direct_pdo_contract_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME direct_pdo_contract_tests COMMAND direct_pdo_contract_tests)

    add_executable(avrcp_control_tests
        direct-pdo/tests/avrcp_control_tests.c)
    target_link_libraries(avrcp_control_tests PRIVATE
        ldac_direct_pdo_contract
        ldac_native_protocol)
    if(MSVC)
        target_compile_options(avrcp_control_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(avrcp_control_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME avrcp_control_tests COMMAND avrcp_control_tests)

    add_executable(direct_pdo_arbiter_contract_tests
        direct-pdo/tests/direct_pdo_arbiter_contract_tests.c)
    target_link_libraries(direct_pdo_arbiter_contract_tests PRIVATE
        ldac_direct_pdo_contract)
    if(MSVC)
        target_compile_options(direct_pdo_arbiter_contract_tests PRIVATE
            /W4 /WX)
    else()
        target_compile_options(direct_pdo_arbiter_contract_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME direct_pdo_arbiter_contract_tests
        COMMAND direct_pdo_arbiter_contract_tests)

    add_executable(direct_pdo_control_abi_tests
        direct-pdo/tests/direct_pdo_control_abi_tests.c)
    target_link_libraries(direct_pdo_control_abi_tests PRIVATE
        ldac_direct_pdo_contract)
    if(MSVC)
        target_compile_options(direct_pdo_control_abi_tests PRIVATE
            /W4 /WX)
    else()
        target_compile_options(direct_pdo_control_abi_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME direct_pdo_control_abi_tests
        COMMAND direct_pdo_control_abi_tests)

    add_executable(direct_pdo_media_abi_tests
        direct-pdo/tests/direct_pdo_media_abi_tests.c)
    target_link_libraries(direct_pdo_media_abi_tests PRIVATE
        ldac_direct_pdo_contract)
    if(MSVC)
        target_compile_options(direct_pdo_media_abi_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(direct_pdo_media_abi_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME direct_pdo_media_abi_tests
        COMMAND direct_pdo_media_abi_tests)

    add_executable(media_watchdog_contract_tests
        direct-pdo/tests/media_watchdog_contract_tests.c)
    target_link_libraries(media_watchdog_contract_tests PRIVATE
        ldac_direct_pdo_contract)
    if(MSVC)
        target_compile_options(media_watchdog_contract_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(media_watchdog_contract_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME media_watchdog_contract_tests
        COMMAND media_watchdog_contract_tests)

    add_executable(direct_pdo_dispatch_contract_tests
        direct-pdo/tests/direct_pdo_dispatch_contract_tests.c)
    target_link_libraries(direct_pdo_dispatch_contract_tests PRIVATE
        ldac_direct_pdo_contract)
    if(MSVC)
        target_compile_options(direct_pdo_dispatch_contract_tests PRIVATE
            /W4 /WX)
    else()
        target_compile_options(direct_pdo_dispatch_contract_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME direct_pdo_dispatch_contract_tests
        COMMAND direct_pdo_dispatch_contract_tests)

    add_executable(direct_pdo_preemption_contract_tests
        direct-pdo/tests/direct_pdo_preemption_contract_tests.c)
    target_link_libraries(direct_pdo_preemption_contract_tests PRIVATE
        ldac_direct_pdo_contract)
    if(MSVC)
        target_compile_options(direct_pdo_preemption_contract_tests PRIVATE
            /W4 /WX)
    else()
        target_compile_options(direct_pdo_preemption_contract_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME direct_pdo_preemption_contract_tests
        COMMAND direct_pdo_preemption_contract_tests)

    add_executable(direct_pdo_diagnostic_contract_tests
        direct-pdo/tests/direct_pdo_diagnostic_contract_tests.c)
    target_link_libraries(direct_pdo_diagnostic_contract_tests PRIVATE
        ldac_direct_pdo_contract)
    if(MSVC)
        target_compile_options(direct_pdo_diagnostic_contract_tests PRIVATE
            /W4 /WX)
    else()
        target_compile_options(direct_pdo_diagnostic_contract_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME direct_pdo_diagnostic_contract_tests
        COMMAND direct_pdo_diagnostic_contract_tests)

    add_executable(avdtp_transaction_tests
        direct-pdo/tests/avdtp_transaction_tests.c)
    target_link_libraries(avdtp_transaction_tests PRIVATE
        ldac_direct_pdo_contract)
    if(MSVC)
        target_compile_options(avdtp_transaction_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(avdtp_transaction_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME avdtp_transaction_tests COMMAND avdtp_transaction_tests)

    add_executable(bth_indication_contract_tests
        direct-pdo/tests/bth_indication_contract_tests.c)
    target_link_libraries(bth_indication_contract_tests PRIVATE
        ldac_direct_pdo_contract)
    if(MSVC)
        target_compile_options(bth_indication_contract_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(bth_indication_contract_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME bth_indication_contract_tests
        COMMAND bth_indication_contract_tests)

    add_executable(bth_transfer_contract_tests
        direct-pdo/tests/bth_transfer_contract_tests.c)
    target_link_libraries(bth_transfer_contract_tests PRIVATE
        ldac_direct_pdo_contract)
    if(MSVC)
        target_compile_options(bth_transfer_contract_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(bth_transfer_contract_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME bth_transfer_contract_tests
        COMMAND bth_transfer_contract_tests)

    add_executable(bth_owner_contract_tests
        direct-pdo/tests/bth_owner_contract_tests.c)
    target_link_libraries(bth_owner_contract_tests PRIVATE
        ldac_direct_pdo_contract)
    if(MSVC)
        target_compile_options(bth_owner_contract_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(bth_owner_contract_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME bth_owner_contract_tests COMMAND bth_owner_contract_tests)

    add_executable(bth_address_contract_tests
        direct-pdo/tests/bth_address_contract_tests.c)
    target_link_libraries(bth_address_contract_tests PRIVATE
        ldac_direct_pdo_contract)
    if(MSVC)
        target_compile_options(bth_address_contract_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(bth_address_contract_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME bth_address_contract_tests COMMAND bth_address_contract_tests)

    add_executable(bth_signaling_contract_tests
        direct-pdo/tests/bth_signaling_contract_tests.c)
    target_link_libraries(bth_signaling_contract_tests PRIVATE
        ldac_direct_pdo_contract)
    if(MSVC)
        target_compile_options(bth_signaling_contract_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(bth_signaling_contract_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME bth_signaling_contract_tests
        COMMAND bth_signaling_contract_tests)

    if(WIN32)
        add_executable(wasapi_loopback_tests
            engine/tests/wasapi_loopback_tests.c)
        target_link_libraries(wasapi_loopback_tests PRIVATE wasapi_loopback)
        if(MSVC)
            target_compile_options(wasapi_loopback_tests PRIVATE /W4 /WX /utf-8)
        endif()
        add_test(NAME wasapi_loopback_tests COMMAND wasapi_loopback_tests)

        add_executable(ioctl_abi_tests driver/tests/ioctl_abi_tests.c)
        target_include_directories(ioctl_abi_tests PRIVATE driver/inc)
        if(MSVC)
            target_compile_options(ioctl_abi_tests PRIVATE /W4 /WX)
        endif()
        add_test(NAME ioctl_abi_tests COMMAND ioctl_abi_tests)

        add_executable(audio_endpoint_abi_tests
            audio-endpoint/tests/pcm_abi_tests.c)
        target_include_directories(audio_endpoint_abi_tests PRIVATE
            audio-endpoint/Source/Inc)
        if(MSVC)
            target_compile_options(audio_endpoint_abi_tests PRIVATE /W4 /WX)
        endif()
        add_test(NAME audio_endpoint_abi_tests COMMAND audio_endpoint_abi_tests)

        add_executable(native_pcm_source_tests
            engine/tests/native_pcm_source_tests.cpp)
        target_link_libraries(native_pcm_source_tests PRIVATE
            native_pcm_source)
        if(MSVC)
            target_compile_options(native_pcm_source_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME native_pcm_source_tests COMMAND native_pcm_source_tests)
        set_tests_properties(native_pcm_source_tests
            PROPERTIES SKIP_RETURN_CODE 77)

        add_executable(native_pcm_volume_logic_tests
            engine/tests/native_pcm_volume_logic_tests.cpp)
        target_include_directories(native_pcm_volume_logic_tests PRIVATE
            engine/windows)
        target_compile_features(native_pcm_volume_logic_tests PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(native_pcm_volume_logic_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME native_pcm_volume_logic_tests
            COMMAND native_pcm_volume_logic_tests)

        add_test(NAME transport_probe_help COMMAND transport_probe --help)
        add_test(NAME ldac_direct_engine_help
            COMMAND ldac_direct_engine --help)
        add_test(NAME transport_probe_quality_hq
            COMMAND transport_probe --quality hq --help)
        add_test(NAME transport_probe_quality_auto
            COMMAND transport_probe --play-system --quality auto --help)
        add_test(NAME transport_probe_endpoint_quality_auto
            COMMAND transport_probe --play-endpoint --quality auto --help)
        add_test(NAME transport_probe_stop_event_help
            COMMAND transport_probe --stop-event test-event --help)
        add_test(NAME transport_probe_channel_mode_mono
            COMMAND transport_probe --channel-mode mono --help)
        add_test(NAME transport_probe_channel_mode_invalid
            COMMAND transport_probe --channel-mode invalid)
        add_test(NAME transport_probe_sample_rate_96000
            COMMAND transport_probe --sample-rate 96000 --bits 24 --help)
        add_test(NAME transport_probe_sample_rate_invalid
            COMMAND transport_probe --sample-rate 32000)
        add_test(NAME transport_probe_bits_invalid
            COMMAND transport_probe --bits 32)
        add_test(NAME transport_probe_quality_invalid
            COMMAND transport_probe --quality invalid)
        add_test(NAME transport_probe_stop_event_missing
            COMMAND transport_probe --stop-event)
        add_test(NAME transport_probe_auto_requires_play
            COMMAND transport_probe --quality auto)
        add_test(NAME transport_probe_open_attempts_invalid
            COMMAND transport_probe --open-attempts 0)
        add_test(NAME transport_probe_open_attempts_one
            COMMAND transport_probe --open-attempts 1 --help)
        add_test(NAME transport_probe_signaling_hold_help
            COMMAND transport_probe --discover
                --hold-signaling-seconds 60 --help)
        add_test(NAME transport_probe_signaling_hold_invalid
            COMMAND transport_probe --discover
                --hold-signaling-seconds 0)
        add_test(NAME transport_probe_signaling_hold_requires_discover
            COMMAND transport_probe --hold-signaling-seconds 60)
        add_test(NAME transport_probe_signaling_hold_rejects_configure
            COMMAND transport_probe --configure
                --hold-signaling-seconds 60)
        add_test(NAME audio_endpoint_probe_help
            COMMAND audio_endpoint_probe --help)
        add_test(NAME endpoint_format_control_help
            COMMAND endpoint_format_control --help)
        add_test(NAME direct_pdo_runtime_status_text
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File
                    ${CMAKE_CURRENT_SOURCE_DIR}/direct-pdo/tests/runtime_status_text_tests.ps1)
        add_test(NAME direct_pdo_hardware_suspension
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File
                    ${CMAKE_CURRENT_SOURCE_DIR}/direct-pdo/tests/hardware_suspension_tests.ps1)
        add_test(NAME endpoint_volume_probe_help
            COMMAND endpoint_volume_probe --help)
        add_test(NAME v1_swd_child_probe_help
            COMMAND v1_swd_child_probe --help)
        add_test(NAME v1_swd_child_probe_requires_confirmation
            COMMAND v1_swd_child_probe --create)
        set_tests_properties(v1_swd_child_probe_requires_confirmation
            PROPERTIES WILL_FAIL TRUE)
        add_test(NAME v1_swd_endpoint_host_help
            COMMAND v1_swd_endpoint_host --help)
        add_test(NAME v1_swd_endpoint_host_requires_confirmation
            COMMAND v1_swd_endpoint_host --create)
        set_tests_properties(v1_swd_endpoint_host_requires_confirmation
            PROPERTIES WILL_FAIL TRUE)
        add_test(NAME native_ldac_volume_step_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File
                    ${CMAKE_CURRENT_SOURCE_DIR}/audio-endpoint/tests/native_ldac_volume_step_policy_tests.ps1)
        add_test(NAME xm5_connection_probe_help
            COMMAND xm5_connection_probe --help)
        add_test(NAME xm5_connection_probe_acl_timeout_invalid
            COMMAND xm5_connection_probe --wait-acl-connect 0)
        set_tests_properties(xm5_connection_probe_acl_timeout_invalid
            PROPERTIES WILL_FAIL TRUE)
        add_test(NAME avrcp_transport_probe_help
            COMMAND avrcp_transport_probe --help)
        add_test(NAME v1_avrcp_observer_probe_help
            COMMAND v1_avrcp_observer_probe --help)
        add_test(NAME v1_avrcp_filter_probe_help
            COMMAND v1_avrcp_filter_probe --help)
        add_test(NAME v1_avrcp_filter_probe_wait_invalid
            COMMAND v1_avrcp_filter_probe
                --wait-for-first-request-seconds 0)
        set_tests_properties(v1_avrcp_filter_probe_wait_invalid
            PROPERTIES WILL_FAIL TRUE)
        add_test(NAME xm5_media_control_observer_help
            COMMAND xm5_media_control_observer --help)
        add_executable(ldac_agent_probe_stub agent/tests/probe_stub.cpp)
        if(MSVC)
            target_compile_options(ldac_agent_probe_stub PRIVATE
                /W4 /WX /utf-8)
        endif()
        add_executable(ldac_agent_runtime_tests
            agent/tests/runtime_support_tests.cpp
            agent/runtime_support.cpp)
        target_include_directories(ldac_agent_runtime_tests PRIVATE
            agent driver/inc audio-endpoint/Source/Inc direct-pdo/include)
        target_compile_features(ldac_agent_runtime_tests PRIVATE cxx_std_17)
        target_link_libraries(ldac_agent_runtime_tests PRIVATE
            bthprops setupapi)
        if(MSVC)
            target_compile_options(ldac_agent_runtime_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME ldac_agent_runtime_tests
            COMMAND ldac_agent_runtime_tests)
        add_executable(v1_lifecycle_tests
            agent/tests/v1_lifecycle_tests.cpp
            agent/v1_lifecycle.cpp
            agent/xm5_acl_event.cpp)
        target_include_directories(v1_lifecycle_tests PRIVATE agent)
        target_compile_features(v1_lifecycle_tests PRIVATE cxx_std_17)
        target_link_libraries(v1_lifecycle_tests PRIVATE user32)
        if(MSVC)
            target_compile_options(v1_lifecycle_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_lifecycle_tests COMMAND v1_lifecycle_tests)
        add_executable(v1_daily_instance_tests
            agent/tests/v1_daily_instance_tests.cpp
            agent/v1_daily_instance.cpp)
        target_include_directories(v1_daily_instance_tests PRIVATE agent)
        target_compile_features(v1_daily_instance_tests PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(v1_daily_instance_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_daily_instance_tests
            COMMAND v1_daily_instance_tests)
        add_executable(v1_daily_config_ipc_tests
            agent/tests/v1_daily_config_ipc_tests.cpp
            agent/v1_daily_config_ipc.cpp)
        target_include_directories(v1_daily_config_ipc_tests PRIVATE agent)
        target_compile_features(v1_daily_config_ipc_tests PRIVATE cxx_std_17)
        target_link_libraries(v1_daily_config_ipc_tests PRIVATE advapi32)
        if(MSVC)
            target_compile_options(v1_daily_config_ipc_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_daily_config_ipc_tests
            COMMAND v1_daily_config_ipc_tests)
        add_executable(v1_lifecycle_stress_tests
            agent/tests/v1_lifecycle_stress_tests.cpp
            agent/v1_lifecycle.cpp)
        target_include_directories(v1_lifecycle_stress_tests PRIVATE agent)
        target_compile_features(v1_lifecycle_stress_tests PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(v1_lifecycle_stress_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_lifecycle_stress_tests
            COMMAND v1_lifecycle_stress_tests)
        add_executable(v1_multigeneration_lifecycle_scenarios_tests
            agent/tests/v1_multigeneration_lifecycle_scenarios_tests.cpp
            agent/v1_lifecycle.cpp)
        target_include_directories(
            v1_multigeneration_lifecycle_scenarios_tests PRIVATE agent)
        target_compile_features(
            v1_multigeneration_lifecycle_scenarios_tests PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(
                v1_multigeneration_lifecycle_scenarios_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_multigeneration_lifecycle_scenarios_tests
            COMMAND v1_multigeneration_lifecycle_scenarios_tests)
        add_executable(avrcp_absolute_volume_state_tests
            agent/tests/avrcp_absolute_volume_state_tests.cpp
            agent/avrcp_absolute_volume_state.cpp)
        target_include_directories(avrcp_absolute_volume_state_tests PRIVATE
            agent)
        target_compile_features(avrcp_absolute_volume_state_tests PRIVATE
            cxx_std_17)
        if(MSVC)
            target_compile_options(avrcp_absolute_volume_state_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME avrcp_absolute_volume_state_tests
            COMMAND avrcp_absolute_volume_state_tests)
        add_executable(avrcp_absolute_volume_observe_log_tests
            agent/tests/avrcp_absolute_volume_observe_log_tests.cpp
            agent/avrcp_absolute_volume_observe_log.cpp
            agent/avrcp_absolute_volume_state.cpp)
        target_include_directories(
            avrcp_absolute_volume_observe_log_tests PRIVATE agent)
        target_compile_features(
            avrcp_absolute_volume_observe_log_tests PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(
                avrcp_absolute_volume_observe_log_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME avrcp_absolute_volume_observe_log_tests
            COMMAND avrcp_absolute_volume_observe_log_tests)
        add_executable(avrcp_absolute_volume_observe_json_tests
            agent/tests/avrcp_absolute_volume_observe_json_tests.cpp
            agent/avrcp_absolute_volume_observe_json.cpp)
        target_include_directories(
            avrcp_absolute_volume_observe_json_tests PRIVATE agent)
        target_compile_features(
            avrcp_absolute_volume_observe_json_tests PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(
                avrcp_absolute_volume_observe_json_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME avrcp_absolute_volume_observe_json_tests
            COMMAND avrcp_absolute_volume_observe_json_tests)
        add_executable(avrcp_passthrough_state_tests
            agent/tests/avrcp_passthrough_state_tests.cpp
            agent/avrcp_passthrough_state.cpp)
        target_include_directories(avrcp_passthrough_state_tests PRIVATE
            agent)
        target_compile_features(avrcp_passthrough_state_tests PRIVATE
            cxx_std_17)
        if(MSVC)
            target_compile_options(avrcp_passthrough_state_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME avrcp_passthrough_state_tests
            COMMAND avrcp_passthrough_state_tests)
        add_executable(xm5_media_control_event_tests
            agent/tests/xm5_media_control_event_tests.cpp
            agent/xm5_media_control_event.cpp)
        target_include_directories(xm5_media_control_event_tests PRIVATE
            agent)
        target_compile_features(xm5_media_control_event_tests PRIVATE
            cxx_std_17)
        if(MSVC)
            target_compile_options(xm5_media_control_event_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME xm5_media_control_event_tests
            COMMAND xm5_media_control_event_tests)
        add_executable(v1_avrcp_action_mapper_tests
            agent/tests/v1_avrcp_action_mapper_tests.cpp
            agent/v1_avrcp_action_mapper.cpp
            agent/v1_media_session_eligibility.cpp
            agent/avrcp_passthrough_state.cpp
            agent/avrcp_absolute_volume_state.cpp
            agent/xm5_media_control_event.cpp)
        target_include_directories(v1_avrcp_action_mapper_tests PRIVATE
            agent)
        target_compile_features(v1_avrcp_action_mapper_tests PRIVATE
            cxx_std_17)
        if(MSVC)
            target_compile_options(v1_avrcp_action_mapper_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_avrcp_action_mapper_tests
            COMMAND v1_avrcp_action_mapper_tests)
        add_executable(v1_avrcp_bootstrap_play_tests
            agent/tests/v1_avrcp_bootstrap_play_tests.cpp
            agent/v1_avrcp_bootstrap_play.cpp)
        target_include_directories(v1_avrcp_bootstrap_play_tests PRIVATE
            agent)
        target_compile_features(v1_avrcp_bootstrap_play_tests PRIVATE
            cxx_std_17)
        if(MSVC)
            target_compile_options(v1_avrcp_bootstrap_play_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_avrcp_bootstrap_play_tests
            COMMAND v1_avrcp_bootstrap_play_tests)
        add_executable(v1_native_ldac_volume_endpoint_selector_tests
            agent/tests/v1_native_ldac_volume_endpoint_selector_tests.cpp
            agent/v1_native_ldac_volume_endpoint_selector.cpp)
        target_include_directories(
            v1_native_ldac_volume_endpoint_selector_tests PRIVATE agent)
        target_compile_features(
            v1_native_ldac_volume_endpoint_selector_tests PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(
                v1_native_ldac_volume_endpoint_selector_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_native_ldac_volume_endpoint_selector_tests
            COMMAND v1_native_ldac_volume_endpoint_selector_tests)
        add_executable(v1_avrcp_action_executor_tests
            agent/tests/v1_avrcp_action_executor_tests.cpp
            agent/v1_avrcp_action_executor.cpp
            agent/v1_avrcp_action_mapper.cpp
            agent/v1_media_session_eligibility.cpp
            agent/avrcp_passthrough_state.cpp
            agent/avrcp_absolute_volume_state.cpp)
        target_include_directories(v1_avrcp_action_executor_tests PRIVATE
            agent)
        target_compile_features(v1_avrcp_action_executor_tests PRIVATE
            cxx_std_17)
        if(MSVC)
            target_compile_options(v1_avrcp_action_executor_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_avrcp_action_executor_tests
            COMMAND v1_avrcp_action_executor_tests)
        add_executable(v1_avrcp_filter_decoder_tests
            agent/tests/v1_avrcp_filter_decoder_tests.cpp)
        target_link_libraries(v1_avrcp_filter_decoder_tests PRIVATE
            v1_avrcp_filter_decoder)
        target_compile_features(v1_avrcp_filter_decoder_tests PRIVATE
            cxx_std_17)
        if(MSVC)
            target_compile_options(v1_avrcp_filter_decoder_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_avrcp_filter_decoder_tests
            COMMAND v1_avrcp_filter_decoder_tests)
        add_executable(v1_avrcp_observer_host_tests
            agent/tests/v1_avrcp_observer_host_tests.cpp
            agent/v1_avrcp_observer_host.cpp
            agent/v1_avrcp_action_executor.cpp
            agent/v1_avrcp_windows_sink.cpp
            agent/v1_native_ldac_volume_endpoint_selector.cpp
            agent/v1_avrcp_action_mapper.cpp
            agent/v1_media_session_eligibility.cpp
            agent/avrcp_passthrough_state.cpp
            agent/avrcp_absolute_volume_state.cpp)
        target_include_directories(v1_avrcp_observer_host_tests PRIVATE
            agent avrcp-sideband/include audio-endpoint/Source/Inc)
        target_compile_features(v1_avrcp_observer_host_tests PRIVATE
            cxx_std_17)
        target_link_libraries(v1_avrcp_observer_host_tests PRIVATE
            ole32 setupapi user32)
        if(MSVC)
            target_compile_options(v1_avrcp_observer_host_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_avrcp_observer_host_tests
            COMMAND v1_avrcp_observer_host_tests)

        add_executable(v1_avrcp_handoff_state_tests
            agent/tests/v1_avrcp_handoff_state_tests.cpp
            agent/v1_avrcp_handoff_state.cpp)
        target_include_directories(v1_avrcp_handoff_state_tests PRIVATE
            agent)
        target_compile_features(v1_avrcp_handoff_state_tests PRIVATE
            cxx_std_17)
        if(MSVC)
            target_compile_options(v1_avrcp_handoff_state_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_avrcp_handoff_state_tests
            COMMAND v1_avrcp_handoff_state_tests)

        add_executable(v1_hfp_switch_state_tests
            agent/tests/v1_hfp_switch_state_tests.cpp
            agent/v1_hfp_switch_state.cpp)
        target_include_directories(v1_hfp_switch_state_tests PRIVATE agent)
        target_compile_features(v1_hfp_switch_state_tests PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(v1_hfp_switch_state_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_hfp_switch_state_tests
            COMMAND v1_hfp_switch_state_tests)

        add_executable(v1_hfp_capture_monitor_tests
            agent/tests/v1_hfp_capture_monitor_tests.cpp
            agent/v1_hfp_capture_monitor.cpp)
        target_include_directories(v1_hfp_capture_monitor_tests PRIVATE
            agent
            audio-endpoint/Source/Inc)
        target_compile_features(v1_hfp_capture_monitor_tests PRIVATE
            cxx_std_17)
        target_link_libraries(v1_hfp_capture_monitor_tests PRIVATE ole32)
        if(MSVC)
            target_compile_options(v1_hfp_capture_monitor_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_hfp_capture_monitor_tests
            COMMAND v1_hfp_capture_monitor_tests)

        add_executable(v1_hfp_shadow_state_tests
            agent/tests/v1_hfp_shadow_state_tests.cpp
            agent/v1_hfp_shadow_state.cpp
            agent/v1_hfp_switch_state.cpp)
        target_include_directories(v1_hfp_shadow_state_tests PRIVATE agent)
        target_compile_features(v1_hfp_shadow_state_tests PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(v1_hfp_shadow_state_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_hfp_shadow_state_tests
            COMMAND v1_hfp_shadow_state_tests)

        add_executable(v1_hfp_lifecycle_adapter_tests
            agent/tests/v1_hfp_lifecycle_adapter_tests.cpp
            agent/v1_hfp_lifecycle_adapter.cpp
            agent/v1_hfp_switch_state.cpp)
        target_include_directories(v1_hfp_lifecycle_adapter_tests PRIVATE
            agent)
        target_compile_features(v1_hfp_lifecycle_adapter_tests PRIVATE
            cxx_std_17)
        if(MSVC)
            target_compile_options(v1_hfp_lifecycle_adapter_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_hfp_lifecycle_adapter_tests
            COMMAND v1_hfp_lifecycle_adapter_tests)

        add_executable(v1_hfp_render_endpoint_selector_tests
            agent/tests/v1_hfp_render_endpoint_selector_tests.cpp
            agent/v1_hfp_render_endpoint_selector.cpp)
        target_include_directories(v1_hfp_render_endpoint_selector_tests
            PRIVATE agent)
        target_compile_features(v1_hfp_render_endpoint_selector_tests
            PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(v1_hfp_render_endpoint_selector_tests
                PRIVATE /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_hfp_render_endpoint_selector_tests
            COMMAND v1_hfp_render_endpoint_selector_tests)

        add_executable(v1_hfp_render_endpoint_monitor_tests
            agent/tests/v1_hfp_render_endpoint_monitor_tests.cpp
            agent/v1_hfp_render_endpoint_monitor.cpp
            agent/v1_hfp_render_endpoint_selector.cpp)
        target_include_directories(v1_hfp_render_endpoint_monitor_tests
            PRIVATE agent audio-endpoint/Source/Inc)
        target_compile_features(v1_hfp_render_endpoint_monitor_tests
            PRIVATE cxx_std_17)
        target_link_libraries(v1_hfp_render_endpoint_monitor_tests PRIVATE
            ole32)
        if(MSVC)
            target_compile_options(v1_hfp_render_endpoint_monitor_tests
                PRIVATE /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_hfp_render_endpoint_monitor_tests
            COMMAND v1_hfp_render_endpoint_monitor_tests)

        add_executable(v1_media_session_eligibility_tests
            agent/tests/v1_media_session_eligibility_tests.cpp
            agent/v1_media_session_eligibility.cpp)
        target_include_directories(v1_media_session_eligibility_tests PRIVATE
            agent)
        target_compile_features(v1_media_session_eligibility_tests PRIVATE
            cxx_std_17)
        if(MSVC)
            target_compile_options(v1_media_session_eligibility_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_media_session_eligibility_tests
            COMMAND v1_media_session_eligibility_tests)
        add_executable(v1_avrcp_handoff_ipc_tests
            agent/tests/v1_avrcp_handoff_ipc_tests.cpp
            agent/v1_avrcp_handoff_ipc.cpp)
        target_include_directories(v1_avrcp_handoff_ipc_tests PRIVATE
            agent)
        target_compile_features(v1_avrcp_handoff_ipc_tests PRIVATE
            cxx_std_17)
        if(MSVC)
        target_compile_options(v1_avrcp_handoff_ipc_tests PRIVATE
            /W4 /WX /utf-8 /EHsc)
        endif()
        target_link_libraries(v1_avrcp_handoff_ipc_tests PRIVATE advapi32)
        add_test(NAME v1_avrcp_handoff_ipc_tests
            COMMAND v1_avrcp_handoff_ipc_tests)
        add_executable(v1_render_demand_tracker_tests
            agent/tests/v1_render_demand_tracker_tests.cpp
            agent/v1_render_demand_tracker.cpp)
        target_include_directories(v1_render_demand_tracker_tests PRIVATE
            agent)
        target_compile_features(v1_render_demand_tracker_tests PRIVATE
            cxx_std_17)
        if(MSVC)
            target_compile_options(v1_render_demand_tracker_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_render_demand_tracker_tests
            COMMAND v1_render_demand_tracker_tests)
        add_executable(v1_transport_open_stability_tests
            agent/tests/v1_transport_open_stability_tests.cpp
            agent/v1_transport_open_stability.cpp)
        target_include_directories(v1_transport_open_stability_tests PRIVATE
            agent)
        target_compile_features(v1_transport_open_stability_tests PRIVATE
            cxx_std_17)
        if(MSVC)
            target_compile_options(v1_transport_open_stability_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_transport_open_stability_tests
            COMMAND v1_transport_open_stability_tests)
        add_executable(v1_engine_ready_host_tests
            agent/tests/v1_engine_ready_host_tests.cpp
            agent/v1_engine_ready_host.cpp)
        target_include_directories(v1_engine_ready_host_tests PRIVATE
            agent)
        target_compile_features(v1_engine_ready_host_tests PRIVATE
            cxx_std_17)
        if(MSVC)
            target_compile_options(v1_engine_ready_host_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_engine_ready_host_tests
            COMMAND v1_engine_ready_host_tests
                $<TARGET_FILE:v1_engine_ready_stub>
                $<TARGET_FILE:v1_transport_worker_stub>
                $<TARGET_FILE:v1_transport_worker_fail_stub>
                $<TARGET_FILE:v1_transport_discovery_mock_worker>
                $<TARGET_FILE:v1_transport_discovery_retryable_mock_worker>
                $<TARGET_FILE:v1_transport_configuration_mock_worker>
                $<TARGET_FILE:v1_transport_silence_mock_worker>
                $<TARGET_FILE:v1_transport_pcm_mock_worker>)
        set_tests_properties(v1_engine_ready_host_tests PROPERTIES
            TIMEOUT 20)
        add_executable(v1_transport_session_tests
            agent/tests/v1_transport_session_tests.cpp)
        target_link_libraries(v1_transport_session_tests PRIVATE
            v1_transport_session)
        target_compile_features(v1_transport_session_tests PRIVATE
            cxx_std_17)
        if(MSVC)
            target_compile_options(v1_transport_session_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_transport_session_tests
            COMMAND v1_transport_session_tests)
        add_executable(v1_transport_configuration_session_tests
            agent/tests/v1_transport_configuration_session_tests.cpp)
        target_link_libraries(
            v1_transport_configuration_session_tests PRIVATE
            v1_transport_configuration_session)
        target_compile_features(
            v1_transport_configuration_session_tests PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(
                v1_transport_configuration_session_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_transport_configuration_session_tests
            COMMAND v1_transport_configuration_session_tests)
        add_executable(v1_transport_silence_session_tests
            agent/tests/v1_transport_silence_session_tests.cpp)
        target_link_libraries(v1_transport_silence_session_tests PRIVATE
            v1_transport_silence_session)
        target_compile_features(v1_transport_silence_session_tests PRIVATE
            cxx_std_17)
        if(MSVC)
            target_compile_options(v1_transport_silence_session_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_transport_silence_session_tests
            COMMAND v1_transport_silence_session_tests)
        add_executable(v1_transport_pcm_session_tests
            agent/tests/v1_transport_pcm_session_tests.cpp)
        target_link_libraries(v1_transport_pcm_session_tests PRIVATE
            v1_transport_pcm_session)
        target_compile_features(v1_transport_pcm_session_tests PRIVATE
            cxx_std_17)
        if(MSVC)
            target_compile_options(v1_transport_pcm_session_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_transport_pcm_session_tests
            COMMAND v1_transport_pcm_session_tests)
        add_executable(v1_linked_stereo_block_limiter_tests
            agent/tests/v1_linked_stereo_block_limiter_tests.cpp)
        target_link_libraries(v1_linked_stereo_block_limiter_tests PRIVATE
            v1_linked_stereo_block_limiter)
        target_compile_features(v1_linked_stereo_block_limiter_tests PRIVATE
            cxx_std_17)
        if(MSVC)
            target_compile_options(v1_linked_stereo_block_limiter_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_linked_stereo_block_limiter_tests
            COMMAND v1_linked_stereo_block_limiter_tests)
        add_executable(v1_sent_frame_fade_envelope_tests
            agent/tests/v1_sent_frame_fade_envelope_tests.cpp)
        target_link_libraries(v1_sent_frame_fade_envelope_tests PRIVATE
            v1_sent_frame_fade_envelope)
        target_compile_features(v1_sent_frame_fade_envelope_tests PRIVATE
            cxx_std_17)
        if(MSVC)
            target_compile_options(v1_sent_frame_fade_envelope_tests PRIVATE
                /W4 /WX /utf-8 /EHsc)
        endif()
        add_test(NAME v1_sent_frame_fade_envelope_tests
            COMMAND v1_sent_frame_fade_envelope_tests)
        add_test(NAME v1_engine_ready_stub_help
            COMMAND v1_engine_ready_stub --help)
        add_test(NAME v1_transport_worker_stub_help
            COMMAND v1_transport_worker_stub --help)
        add_test(NAME v1_transport_discovery_worker_help
            COMMAND v1_transport_discovery_worker --help)
        add_test(NAME v1_transport_discovery_mock_worker_help
            COMMAND v1_transport_discovery_mock_worker --help)
        add_test(NAME v1_transport_configuration_worker_help
            COMMAND v1_transport_configuration_worker --help)
        add_test(NAME v1_transport_configuration_mock_worker_help
            COMMAND v1_transport_configuration_mock_worker --help)
        add_test(NAME v1_transport_silence_worker_help
            COMMAND v1_transport_silence_worker --help)
        add_test(NAME v1_transport_silence_mock_worker_help
            COMMAND v1_transport_silence_mock_worker --help)
        add_test(NAME v1_transport_pcm_worker_help
            COMMAND v1_transport_pcm_worker --help)
        add_test(NAME v1_transport_audible_worker_help
            COMMAND v1_transport_audible_worker --help)
        add_test(NAME v1_transport_recognizable_worker_help
            COMMAND v1_transport_recognizable_worker --help)
        add_test(NAME v1_transport_stability_worker_help
            COMMAND v1_transport_stability_worker --help)
        add_test(NAME v1_transport_linked_limiter_worker_help
            COMMAND v1_transport_linked_limiter_worker --help)
        add_test(NAME v1_transport_fidelity_bridge_worker_help
            COMMAND v1_transport_fidelity_bridge_worker --help)
        add_test(NAME v1_transport_normal_stop_worker_help
            COMMAND v1_transport_normal_stop_worker --help)
        add_test(NAME v1_transport_daily_worker_help
            COMMAND v1_transport_daily_worker --help)
        add_test(NAME v1_transport_pcm_mock_worker_help
            COMMAND v1_transport_pcm_mock_worker --help)
        add_test(NAME v1_pcm_encode_engine_help
            COMMAND v1_pcm_encode_engine --help)
        add_test(NAME v1_presence_agent_help
            COMMAND v1_presence_agent --help)
        add_test(NAME v1_presence_agent_quality_requires_daily
            COMMAND v1_presence_agent --quality sq)
        set_tests_properties(v1_presence_agent_quality_requires_daily
            PROPERTIES WILL_FAIL TRUE TIMEOUT 10)
        add_test(NAME v1_presence_agent_quality_invalid
            COMMAND v1_presence_agent --quality auto)
        set_tests_properties(v1_presence_agent_quality_invalid
            PROPERTIES WILL_FAIL TRUE TIMEOUT 10)
        add_test(NAME v1_presence_agent_stop_rejects_quality
            COMMAND v1_presence_agent --stop-daily --quality mq)
        set_tests_properties(v1_presence_agent_stop_rejects_quality
            PROPERTIES WILL_FAIL TRUE TIMEOUT 10)
        add_test(NAME v1_render_demand_requires_endpoint_sink
            COMMAND v1_presence_agent
                --run-for-ms 1000
                --state v1-invalid-render-demand-state.json
                --observe-render-demand)
        set_tests_properties(v1_render_demand_requires_endpoint_sink
            PROPERTIES WILL_FAIL TRUE TIMEOUT 10)
        add_test(NAME v1_engine_ready_requires_stub
            COMMAND v1_presence_agent
                --run-for-ms 1000
                --state v1-invalid-engine-ready-state.json
                --endpoint-presence
                --observe-render-demand
                --observe-engine-ready)
        set_tests_properties(v1_engine_ready_requires_stub
            PROPERTIES WILL_FAIL TRUE TIMEOUT 10)
        add_test(NAME v1_hfp_transport_switch_requires_daily
            COMMAND v1_presence_agent
                --run-for-ms 1000
                --state v1-invalid-hfp-switch-state.json
                --hfp-transport-switch)
        set_tests_properties(v1_hfp_transport_switch_requires_daily
            PROPERTIES WILL_FAIL TRUE TIMEOUT 10)
        add_test(NAME v1_hfp_transport_switch_rejected_by_stop
            COMMAND v1_presence_agent
                --stop-daily
                --hfp-transport-switch)
        set_tests_properties(v1_hfp_transport_switch_rejected_by_stop
            PROPERTIES WILL_FAIL TRUE TIMEOUT 10)
        add_test(NAME ldac_agent_smoke
            COMMAND ldac_agent
                --probe $<TARGET_FILE:ldac_agent_probe_stub>
                --once
                --run-for-ms 500
                --instance-suffix ctest
                --log ${CMAKE_CURRENT_BINARY_DIR}/ldac-agent-smoke.log
                --probe-log
                    ${CMAKE_CURRENT_BINARY_DIR}/ldac-agent-smoke-probe.log
                --state
                    ${CMAKE_CURRENT_BINARY_DIR}/ldac-agent-smoke-state.json)
        set_tests_properties(ldac_agent_smoke PROPERTIES TIMEOUT 20)
        add_test(NAME ldac_agent_reconnect
            COMMAND ldac_agent
                --probe $<TARGET_FILE:ldac_agent_probe_stub>
                --run-for-ms 3000
                --instance-suffix ctest-reconnect
                --log ${CMAKE_CURRENT_BINARY_DIR}/ldac-agent-reconnect.log
                --probe-log
                    ${CMAKE_CURRENT_BINARY_DIR}/ldac-agent-reconnect-probe.log
                --state
                    ${CMAKE_CURRENT_BINARY_DIR}/ldac-agent-reconnect-state.json)
        set_tests_properties(ldac_agent_reconnect PROPERTIES TIMEOUT 20)
        add_test(NAME ldac_agent_control
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/control_test.ps1
                -Agent $<TARGET_FILE:ldac_agent>
                -Probe $<TARGET_FILE:ldac_agent_probe_stub>
                -LogRoot ${CMAKE_CURRENT_BINARY_DIR})
        set_tests_properties(ldac_agent_control PROPERTIES TIMEOUT 20)
        add_test(NAME ldac_agent_containment
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/containment_test.ps1
                -Agent $<TARGET_FILE:ldac_agent>
                -Probe $<TARGET_FILE:ldac_agent_probe_stub>
                -LogRoot ${CMAKE_CURRENT_BINARY_DIR})
        set_tests_properties(ldac_agent_containment PROPERTIES TIMEOUT 20)
        add_test(NAME ldac_agent_config_reload
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/config_reload_test.ps1
                -Agent $<TARGET_FILE:ldac_agent>
                -Probe $<TARGET_FILE:ldac_agent_probe_stub>
                -LogRoot ${CMAKE_CURRENT_BINARY_DIR})
        set_tests_properties(ldac_agent_config_reload PROPERTIES TIMEOUT 25)
        add_test(NAME ldac_agent_legacy_installed_rejects_probe_override
            COMMAND ldac_agent
                --installed-legacy
                --probe $<TARGET_FILE:ldac_agent_probe_stub>)
        set_tests_properties(ldac_agent_legacy_installed_rejects_probe_override
            PROPERTIES WILL_FAIL TRUE TIMEOUT 10)
        add_test(NAME ldac_agent_retired_direct_installed_mode_fails_closed
            COMMAND ldac_agent --installed)
        set_tests_properties(ldac_agent_retired_direct_installed_mode_fails_closed
            PROPERTIES WILL_FAIL TRUE TIMEOUT 10)
        add_test(NAME ldac_agent_legacy_install_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/legacy_install_policy_tests.ps1)
        set_tests_properties(ldac_agent_legacy_install_policy
            PROPERTIES TIMEOUT 10)
        add_test(NAME legacy_candidate_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/legacy_candidate_policy_tests.ps1)
        set_tests_properties(legacy_candidate_policy PROPERTIES TIMEOUT 10)
        add_test(NAME legacy_open_diagnostic
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/legacy_open_diagnostic_tests.ps1)
        set_tests_properties(legacy_open_diagnostic PROPERTIES TIMEOUT 10)
        add_test(NAME legacy_power_cycle_evidence
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/legacy_power_cycle_evidence_tests.ps1)
        set_tests_properties(legacy_power_cycle_evidence PROPERTIES TIMEOUT 10)
        add_test(NAME v1_observer_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_observer_policy_tests.ps1)
        set_tests_properties(v1_observer_policy PROPERTIES TIMEOUT 10)
        add_test(NAME v1_presence_agent_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_presence_agent_policy_tests.ps1)
        set_tests_properties(v1_presence_agent_policy PROPERTIES TIMEOUT 10)
        add_test(NAME v1_endpoint_presence_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_endpoint_presence_policy_tests.ps1)
        set_tests_properties(v1_endpoint_presence_policy PROPERTIES TIMEOUT 10)
        add_test(NAME v1_endpoint_presence_gate_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_endpoint_presence_gate_policy_tests.ps1)
        set_tests_properties(v1_endpoint_presence_gate_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_render_demand_observer_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_render_demand_observer_policy_tests.ps1)
        set_tests_properties(v1_render_demand_observer_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_engine_ready_observer_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_engine_ready_observer_policy_tests.ps1)
        set_tests_properties(v1_engine_ready_observer_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_pcm_encode_observer_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_pcm_encode_observer_policy_tests.ps1)
        set_tests_properties(v1_pcm_encode_observer_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_transport_worker_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_transport_worker_policy_tests.ps1)
        set_tests_properties(v1_transport_worker_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_transport_discovery_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_transport_discovery_policy_tests.ps1)
        set_tests_properties(v1_transport_discovery_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_transport_configuration_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_transport_configuration_policy_tests.ps1)
        set_tests_properties(v1_transport_configuration_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_transport_silence_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_transport_silence_policy_tests.ps1)
        set_tests_properties(v1_transport_silence_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_transport_silence_evidence
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_transport_silence_evidence_tests.ps1)
        set_tests_properties(v1_transport_silence_evidence PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_transport_pcm_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_transport_pcm_policy_tests.ps1)
        set_tests_properties(v1_transport_pcm_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_transport_pcm_evidence
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_transport_pcm_evidence_tests.ps1)
        set_tests_properties(v1_transport_pcm_evidence PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_transport_audible_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_transport_audible_policy_tests.ps1)
        set_tests_properties(v1_transport_audible_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_transport_audible_evidence
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_transport_audible_evidence_tests.ps1)
        set_tests_properties(v1_transport_audible_evidence PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_transport_recognizable_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_transport_recognizable_policy_tests.ps1)
        set_tests_properties(v1_transport_recognizable_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_transport_recognizable_evidence
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_transport_recognizable_evidence_tests.ps1)
        set_tests_properties(v1_transport_recognizable_evidence PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_transport_stability_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_transport_stability_policy_tests.ps1)
        set_tests_properties(v1_transport_stability_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_transport_stability_evidence
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_transport_stability_evidence_tests.ps1)
        set_tests_properties(v1_transport_stability_evidence PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_stability_completion_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_stability_completion_policy_tests.ps1)
        set_tests_properties(v1_stability_completion_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_linked_limiter_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_linked_limiter_policy_tests.ps1)
        set_tests_properties(v1_linked_limiter_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_linked_limiter_evidence
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_linked_limiter_evidence_tests.ps1)
        set_tests_properties(v1_linked_limiter_evidence PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_linked_limiter_completion_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_linked_limiter_completion_policy_tests.ps1)
        set_tests_properties(v1_linked_limiter_completion_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_fidelity_bridge_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_fidelity_bridge_policy_tests.ps1)
        set_tests_properties(v1_fidelity_bridge_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_fidelity_bridge_evidence
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_fidelity_bridge_evidence_tests.ps1)
        set_tests_properties(v1_fidelity_bridge_evidence PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_normal_stop_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_normal_stop_policy_tests.ps1)
        set_tests_properties(v1_normal_stop_policy PROPERTIES TIMEOUT 10)
        add_test(NAME v1_daily_worker_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_daily_worker_policy_tests.ps1)
        set_tests_properties(v1_daily_worker_policy PROPERTIES TIMEOUT 10)
        add_test(NAME v1_daily_host_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_daily_host_policy_tests.ps1)
        set_tests_properties(v1_daily_host_policy PROPERTIES TIMEOUT 10)
        add_test(NAME v1_daily_host_common
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_daily_host_common_tests.ps1)
        set_tests_properties(v1_daily_host_common PROPERTIES TIMEOUT 10)
        add_test(NAME v1_daily_packaging_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_daily_packaging_policy_tests.ps1)
        set_tests_properties(v1_daily_packaging_policy PROPERTIES TIMEOUT 10)
        add_test(NAME v1_golden_checkpoint_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_golden_checkpoint_policy_tests.ps1)
        set_tests_properties(v1_golden_checkpoint_policy PROPERTIES TIMEOUT 10)
        add_test(NAME v1_volume_sync_topology
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_volume_sync_topology_tests.ps1)
        set_tests_properties(v1_volume_sync_topology PROPERTIES TIMEOUT 10)
        add_test(NAME v1_swd_child_probe_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_swd_child_probe_policy_tests.ps1)
        set_tests_properties(v1_swd_child_probe_policy PROPERTIES TIMEOUT 10)
        add_test(NAME v1_swd_child_topology_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_swd_child_topology_policy_tests.ps1)
        set_tests_properties(v1_swd_child_topology_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_swd_child_topology_evidence
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_swd_child_topology_evidence_tests.ps1)
        set_tests_properties(v1_swd_child_topology_evidence PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_swd_child_topology_completion_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_swd_child_topology_completion_policy_tests.ps1)
        set_tests_properties(v1_swd_child_topology_completion_policy
            PROPERTIES TIMEOUT 10)
        add_test(NAME v1_swd_endpoint_candidate_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_swd_endpoint_candidate_policy_tests.ps1)
        set_tests_properties(v1_swd_endpoint_candidate_policy
            PROPERTIES TIMEOUT 10)
        add_test(NAME v1_swd_endpoint_binding_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_swd_endpoint_binding_policy_tests.ps1)
        set_tests_properties(v1_swd_endpoint_binding_policy
            PROPERTIES TIMEOUT 10)
        add_test(NAME v1_swd_endpoint_binding_evidence
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_swd_endpoint_binding_evidence_tests.ps1)
        set_tests_properties(v1_swd_endpoint_binding_evidence
            PROPERTIES TIMEOUT 10)
        add_test(NAME v1_swd_endpoint_binding_rollback_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_swd_endpoint_binding_rollback_policy_tests.ps1)
        set_tests_properties(v1_swd_endpoint_binding_rollback_policy
            PROPERTIES TIMEOUT 10)
        add_test(NAME v1_swd_volume_observation_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_swd_volume_observation_policy_tests.ps1)
        set_tests_properties(v1_swd_volume_observation_policy
            PROPERTIES TIMEOUT 10)
        add_test(NAME v1_swd_volume_observation_evidence
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_swd_volume_observation_evidence_tests.ps1)
        set_tests_properties(v1_swd_volume_observation_evidence
            PROPERTIES TIMEOUT 10)
        add_test(NAME v1_avrcp_observer_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_avrcp_observer_policy_tests.ps1)
        set_tests_properties(v1_avrcp_observer_policy
            PROPERTIES TIMEOUT 10)
        add_test(NAME v1_avrcp_filter_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_avrcp_filter_policy_tests.ps1)
        set_tests_properties(v1_avrcp_filter_policy
            PROPERTIES TIMEOUT 10)
        add_test(NAME v1_avrcp_filter_gate_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_avrcp_filter_gate_policy_tests.ps1)
        set_tests_properties(v1_avrcp_filter_gate_policy
            PROPERTIES TIMEOUT 10)
        add_test(NAME v1_avrcp_write_path_gate_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_avrcp_write_path_gate_policy_tests.ps1)
        set_tests_properties(v1_avrcp_write_path_gate_policy
            PROPERTIES TIMEOUT 10)
        add_test(NAME v1_avrcp_audible_sync_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_avrcp_audible_sync_policy_tests.ps1)
        set_tests_properties(v1_avrcp_audible_sync_policy
            PROPERTIES TIMEOUT 10)
        add_test(NAME v1_avrcp_handoff_host_install_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_avrcp_handoff_host_install_policy_tests.ps1)
        set_tests_properties(v1_avrcp_handoff_host_install_policy
            PROPERTIES TIMEOUT 10)
        add_test(NAME v1_avrcp_idle_activation_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_avrcp_idle_activation_policy_tests.ps1)
        set_tests_properties(v1_avrcp_idle_activation_policy
            PROPERTIES TIMEOUT 10)
        add_test(NAME v1_avrcp_resident_install_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_avrcp_resident_install_policy_tests.ps1)
        set_tests_properties(v1_avrcp_resident_install_policy
            PROPERTIES TIMEOUT 10)
        add_test(NAME v1_avrcp_bootstrap_diagnostic_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_avrcp_bootstrap_diagnostic_policy_tests.ps1)
        set_tests_properties(v1_avrcp_bootstrap_diagnostic_policy
            PROPERTIES TIMEOUT 10)
        add_test(NAME v1_inbound_signaling_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_inbound_signaling_policy_tests.ps1)
        set_tests_properties(v1_inbound_signaling_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_inbound_signaling_gate_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_inbound_signaling_gate_policy_tests.ps1)
        set_tests_properties(v1_inbound_signaling_gate_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_inbound_pnp_rundown_gate_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_inbound_pnp_rundown_gate_policy_tests.ps1)
        set_tests_properties(v1_inbound_pnp_rundown_gate_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_normal_stop_evidence
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_normal_stop_evidence_tests.ps1)
        set_tests_properties(v1_normal_stop_evidence PROPERTIES TIMEOUT 10)
        add_test(NAME v1_normal_stop_resume_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_normal_stop_resume_policy_tests.ps1)
        set_tests_properties(v1_normal_stop_resume_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_playback_disconnect_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_playback_disconnect_policy_tests.ps1)
        set_tests_properties(v1_playback_disconnect_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_playback_disconnect_evidence
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_playback_disconnect_evidence_tests.ps1)
        set_tests_properties(v1_playback_disconnect_evidence PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_playback_reconnect_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_playback_reconnect_policy_tests.ps1)
        set_tests_properties(v1_playback_reconnect_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_playback_reconnect_evidence
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_playback_reconnect_evidence_tests.ps1)
        set_tests_properties(v1_playback_reconnect_evidence PROPERTIES
            TIMEOUT 15)
        add_test(NAME v1_lifecycle_soak_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_lifecycle_soak_policy_tests.ps1)
        set_tests_properties(v1_lifecycle_soak_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_lifecycle_soak_evidence
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_lifecycle_soak_evidence_tests.ps1)
        set_tests_properties(v1_lifecycle_soak_evidence PROPERTIES
            TIMEOUT 20)
        add_test(NAME v1_lifecycle_soak_completion_policy
            COMMAND pwsh.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_lifecycle_soak_completion_policy_tests.ps1)
        set_tests_properties(v1_lifecycle_soak_completion_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_discovery_handoff_gate_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_discovery_handoff_gate_policy_tests.ps1)
        set_tests_properties(v1_discovery_handoff_gate_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME v1_reboot_discovery_evidence
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/v1_reboot_discovery_evidence_tests.ps1)
        set_tests_properties(v1_reboot_discovery_evidence PROPERTIES
            TIMEOUT 10)
        add_test(NAME native_pcm_active_format_policy
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/agent/tests/native_pcm_active_format_policy_tests.ps1)
        set_tests_properties(native_pcm_active_format_policy PROPERTIES
            TIMEOUT 10)
        add_test(NAME ldac_agent_direct_trial_requires_bounds
            COMMAND ldac_agent --direct-pdo-trial)
        set_tests_properties(ldac_agent_direct_trial_requires_bounds
            PROPERTIES WILL_FAIL TRUE TIMEOUT 10)
        add_test(NAME ldac_agent_direct_trial_rejects_probe_override
            COMMAND ldac_agent
                --direct-pdo-trial
                --probe $<TARGET_FILE:ldac_agent_probe_stub>
                --run-for-ms 1000
                --instance-suffix ctest-direct
                --log direct-agent.log
                --probe-log direct-probe.log
                --state direct-state.json)
        set_tests_properties(ldac_agent_direct_trial_rejects_probe_override
            PROPERTIES WILL_FAIL TRUE TIMEOUT 10)
        set_tests_properties(transport_probe_quality_invalid
            PROPERTIES WILL_FAIL TRUE)
        set_tests_properties(transport_probe_stop_event_missing
            PROPERTIES WILL_FAIL TRUE)
        set_tests_properties(transport_probe_auto_requires_play
            PROPERTIES WILL_FAIL TRUE)
        set_tests_properties(transport_probe_open_attempts_invalid
            PROPERTIES WILL_FAIL TRUE)
        set_tests_properties(transport_probe_signaling_hold_invalid
            PROPERTIES WILL_FAIL TRUE)
        set_tests_properties(transport_probe_signaling_hold_requires_discover
            PROPERTIES WILL_FAIL TRUE)
        set_tests_properties(transport_probe_signaling_hold_rejects_configure
            PROPERTIES WILL_FAIL TRUE)
        set_tests_properties(transport_probe_channel_mode_invalid
            PROPERTIES WILL_FAIL TRUE)
        set_tests_properties(transport_probe_sample_rate_invalid
            PROPERTIES WILL_FAIL TRUE)
        set_tests_properties(transport_probe_bits_invalid
            PROPERTIES WILL_FAIL TRUE)
    endif()
endif()
