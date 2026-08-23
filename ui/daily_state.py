"""Strict, read-only UI adapter for the current V1 daily host.

This module never signals the daily host, writes configuration, opens a
driver, or performs PnP work.  A future writable UI must use a separately
authenticated IPC contract instead of extending this file with side effects.
"""

from __future__ import annotations

from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
from typing import Any


MAX_DAILY_JSON_BYTES = 64 * 1024
VALID_HOST_STATES = frozenset({"armed", "present", "absent", "stopped"})
VALID_PRESENCE = frozenset({"present", "absent"})
VALID_RENDER_DEMAND = frozenset({"running", "idle"})
VALID_PLAYBACK = frozenset({"playing", "paused", "stopped", "absent"})
VALID_CONFIG_PIPE = re.compile(
    r"^NativeLdac\.V1\.Config\.[A-Za-z0-9._-]{1,64}$"
)


@dataclass(frozen=True)
class DailyState:
    host_process_id: int
    state: str
    physical_presence: str
    render_demand: str
    acl_generation: int
    connected_events: int
    disconnected_events: int
    child_processes_started: int
    transport_worker_sequence: int
    engine_exit_events: int
    engine_start_failures: int
    engine_ready_timeouts: int
    engine_stop_failures: int
    engine_unexpected_exits: int
    media_started_events: int
    media_stopped_events: int
    media_failed_events: int
    avrcp_volume_sync_enabled: bool
    avrcp_observer_active: bool
    avrcp_observer_poll_failures: int
    avrcp_pc_playback: str
    hfp_capture_monitor_ready: bool
    hfp_capture_matched: bool
    hfp_capture_active: bool
    hfp_render_bridge_ready: bool
    hfp_suspended: bool
    endpoint_presence_failures: int
    render_query_failures: int
    config_pipe: str
    requested_quality: str
    applied_quality: str
    requested_config_revision: int
    applied_config_revision: int
    config_rejected_count: int
    config_last_error: int


@dataclass(frozen=True)
class DailyTransportResult:
    disposition: str
    backend_error: int
    sample_rate_hz: int
    bits_per_sample: int
    channel_mode: int
    media_packets_written: int
    media_bytes_written: int
    actual_duration_ms: int
    encoder_quality: str | None
    nominal_ldac_bitrate_kbps: int | None

    @property
    def transport_kbps(self) -> float | None:
        if self.actual_duration_ms <= 0:
            return None
        return (
            self.media_bytes_written * 8.0 / self.actual_duration_ms
        )


@dataclass(frozen=True)
class DailyPresentation:
    status: str
    severity: str
    connection: str
    playback: str
    volume: str
    hfp: str
    details: str
    quality: str
    last_format: str
    last_bitrate: str


def daily_runtime_root(local_app_data: str | Path | None = None) -> Path:
    root = local_app_data
    if root is None:
        root = os.environ.get("LOCALAPPDATA")
    if not root:
        raise OSError("LOCALAPPDATA is unavailable")
    return Path(root) / "NativeLdac" / "V1"


def daily_state_path(
    local_app_data: str | Path | None = None,
    override: str | Path | None = None,
) -> Path:
    selected = override or os.environ.get("NATIVE_LDAC_V1_STATE_PATH")
    if selected:
        return Path(selected)
    return daily_runtime_root(local_app_data) / "state" / "daily-state.json"


def daily_transport_result_path(
    local_app_data: str | Path | None = None,
    override: str | Path | None = None,
) -> Path:
    selected = override or os.environ.get("NATIVE_LDAC_V1_RESULT_PATH")
    if selected:
        return Path(selected)
    return daily_runtime_root(local_app_data) / "results" / "latest-session.json"


def daily_ui_log_root(local_app_data: str | Path | None = None) -> Path:
    return daily_runtime_root(local_app_data) / "logs" / "ui"


def _load_object(path: Path) -> dict[str, Any]:
    size = path.stat().st_size
    if size <= 0 or size > MAX_DAILY_JSON_BYTES:
        raise ValueError("daily JSON size is invalid")
    with path.open("r", encoding="utf-8-sig") as stream:
        payload = json.load(stream)
    if not isinstance(payload, dict):
        raise ValueError("daily JSON must contain an object")
    return payload


def _required(payload: dict[str, Any], field: str) -> Any:
    if field not in payload:
        raise ValueError(f"{field} is required")
    return payload[field]


def _integer(payload: dict[str, Any], field: str) -> int:
    value = _required(payload, field)
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise ValueError(f"{field} must be a non-negative integer")
    return value


def _boolean(payload: dict[str, Any], field: str) -> bool:
    value = _required(payload, field)
    if not isinstance(value, bool):
        raise ValueError(f"{field} must be boolean")
    return value


def _enum(payload: dict[str, Any], field: str, allowed: frozenset[str]) -> str:
    value = _required(payload, field)
    if not isinstance(value, str) or value not in allowed:
        raise ValueError(f"{field} is invalid")
    return value


def load_daily_state(path: Path) -> DailyState:
    payload = _load_object(Path(path))
    if (
        payload.get("schema_version") != 1
        or payload.get("mode") != "daily"
        or payload.get("daily_mode") is not True
    ):
        raise ValueError("unsupported V1 daily state schema")
    config_pipe = payload.get("config_pipe")
    requested_quality = payload.get("requested_quality")
    applied_quality = payload.get("applied_quality")
    if (not isinstance(config_pipe, str) or
            VALID_CONFIG_PIPE.fullmatch(config_pipe) is None):
        raise ValueError("daily config pipe is invalid")
    if requested_quality not in {"HQ", "SQ", "MQ"} or \
            applied_quality not in {"HQ", "SQ", "MQ"}:
        raise ValueError("daily quality state is invalid")
    return DailyState(
        host_process_id=_integer(payload, "host_process_id"),
        state=_enum(payload, "state", VALID_HOST_STATES),
        physical_presence=_enum(
            payload, "physical_presence", VALID_PRESENCE
        ),
        render_demand=_enum(payload, "render_demand", VALID_RENDER_DEMAND),
        acl_generation=_integer(payload, "acl_generation"),
        connected_events=_integer(payload, "connected_events"),
        disconnected_events=_integer(payload, "disconnected_events"),
        child_processes_started=_integer(payload, "child_processes_started"),
        transport_worker_sequence=_integer(
            payload, "transport_worker_sequence"
        ),
        engine_exit_events=_integer(payload, "engine_exit_events"),
        engine_start_failures=_integer(payload, "engine_start_failures"),
        engine_ready_timeouts=_integer(payload, "engine_ready_timeouts"),
        engine_stop_failures=_integer(payload, "engine_stop_failures"),
        engine_unexpected_exits=_integer(payload, "engine_unexpected_exits"),
        media_started_events=_integer(payload, "media_started_events"),
        media_stopped_events=_integer(payload, "media_stopped_events"),
        media_failed_events=_integer(payload, "media_failed_events"),
        avrcp_volume_sync_enabled=_boolean(
            payload, "avrcp_volume_sync_enabled"
        ),
        avrcp_observer_active=_boolean(payload, "avrcp_observer_active"),
        avrcp_observer_poll_failures=_integer(
            payload, "avrcp_observer_poll_failures"
        ),
        avrcp_pc_playback=_enum(
            payload, "avrcp_pc_playback", VALID_PLAYBACK
        ),
        hfp_capture_monitor_ready=_boolean(
            payload, "hfp_capture_monitor_ready"
        ),
        hfp_capture_matched=_boolean(payload, "hfp_capture_matched"),
        hfp_capture_active=_boolean(payload, "hfp_capture_active"),
        hfp_render_bridge_ready=_boolean(
            payload, "hfp_render_bridge_ready"
        ),
        hfp_suspended=_boolean(payload, "hfp_suspended"),
        endpoint_presence_failures=_integer(
            payload, "endpoint_presence_failures"
        ),
        render_query_failures=_integer(payload, "render_query_failures"),
        config_pipe=config_pipe,
        requested_quality=requested_quality,
        applied_quality=applied_quality,
        requested_config_revision=_integer(
            payload, "requested_config_revision"
        ),
        applied_config_revision=_integer(
            payload, "applied_config_revision"
        ),
        config_rejected_count=_integer(payload, "config_rejected_count"),
        config_last_error=_integer(payload, "config_last_error"),
    )


def load_daily_transport_result(path: Path) -> DailyTransportResult:
    payload = _load_object(Path(path))
    if payload.get("schema_version") != 1:
        raise ValueError("unsupported daily transport result schema")
    disposition = _required(payload, "disposition")
    if not isinstance(disposition, str):
        raise ValueError("disposition must be a string")
    return DailyTransportResult(
        disposition=disposition,
        backend_error=_integer(payload, "backend_error"),
        sample_rate_hz=_integer(payload, "sample_rate_hz"),
        bits_per_sample=_integer(payload, "bits_per_sample"),
        channel_mode=_integer(payload, "channel_mode"),
        media_packets_written=_integer(payload, "media_packets_written"),
        media_bytes_written=_integer(payload, "media_bytes_written"),
        actual_duration_ms=_integer(payload, "actual_duration_ms"),
        encoder_quality=(
            payload.get("encoder_quality")
            if payload.get("encoder_quality") in {"HQ", "SQ", "MQ"}
            else None
        ),
        nominal_ldac_bitrate_kbps=(
            _integer(payload, "nominal_ldac_bitrate_kbps")
            if "nominal_ldac_bitrate_kbps" in payload
            else None
        ),
    )


def derive_daily_presentation(
    state: DailyState,
    transport: DailyTransportResult | None = None,
) -> DailyPresentation:
    failures = (
        state.engine_start_failures
        + state.engine_ready_timeouts
        + state.engine_stop_failures
        + state.engine_unexpected_exits
        + state.media_failed_events
        + state.avrcp_observer_poll_failures
        + state.endpoint_presence_failures
        + state.render_query_failures
    )
    worker_active = state.child_processes_started > state.engine_exit_events
    if failures:
        status, severity = "后台检测到故障", "error"
    elif state.state == "stopped":
        status, severity = "后台已停止", "idle"
    elif state.physical_presence == "absent":
        status, severity = "等待 XM5 连接", "idle"
    elif state.hfp_suspended or state.hfp_capture_active:
        status, severity = "HFP 切换观察中", "warning"
    elif state.avrcp_pc_playback == "paused":
        status, severity = "已暂停，等待继续播放", "ok"
    elif worker_active and state.media_started_events > 0:
        status, severity = "Native LDAC 正在播放", "ok"
    elif state.render_demand == "running":
        status, severity = "正在准备 LDAC", "warning"
    else:
        status, severity = "XM5 已连接", "ok"

    connection = (
        f"已连接 · ACL {state.acl_generation}"
        if state.physical_presence == "present"
        else "未连接"
    )
    playback_labels = {
        "playing": "正在播放",
        "paused": "已暂停",
        "stopped": "已停止",
        "absent": "无媒体会话",
    }
    playback = playback_labels[state.avrcp_pc_playback]
    if not state.avrcp_volume_sync_enabled:
        volume = "统一音量未启用"
    elif state.avrcp_observer_active:
        volume = "统一音量已就绪"
    else:
        volume = "Windows 增益保护中"
    if state.hfp_capture_active:
        hfp = "XM5 麦克风正在使用"
    elif state.hfp_capture_matched:
        hfp = "XM5 麦克风可用"
    elif state.hfp_capture_monitor_ready:
        hfp = "HFP 未激活"
    else:
        hfp = "HFP 监视器未就绪"
    details = (
        f"workers {state.child_processes_started} · "
        f"media {state.media_started_events}/{state.media_failed_events} · "
        f"connect {state.connected_events}/{state.disconnected_events}"
    )
    if state.requested_config_revision != state.applied_config_revision:
        details += (
            f" · quality {state.applied_quality}→"
            f"{state.requested_quality} pending"
        )
    elif state.config_last_error:
        details += f" · config error {state.config_last_error}"
    last_format = "最近会话：—"
    last_bitrate = "—"
    if transport is not None:
        channels = {1: "stereo", 2: "dual", 4: "mono"}.get(
            transport.channel_mode, f"mode {transport.channel_mode}"
        )
        last_format = (
            f"最近会话：{transport.sample_rate_hz / 1000:g} kHz · "
            f"{transport.bits_per_sample}-bit · {channels}"
        )
        bitrate = transport.transport_kbps
        if bitrate is not None:
            last_bitrate = f"{bitrate:.0f} kbps"
    return DailyPresentation(
        status=status,
        severity=severity,
        connection=connection,
        playback=playback,
        volume=volume,
        hfp=hfp,
        details=details,
        quality=(
            transport.encoder_quality
            if transport is not None and transport.encoder_quality
            else state.applied_quality
        ),
        last_format=last_format,
        last_bitrate=last_bitrate,
    )
