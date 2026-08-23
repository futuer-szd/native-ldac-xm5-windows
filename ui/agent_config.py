"""Versioned Native LDAC agent configuration and state helpers."""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
from typing import Any


VALID_QUALITIES = frozenset({"mq", "sq", "hq", "auto"})
VALID_CHANNEL_MODES = frozenset({"stereo", "dual", "mono"})
VALID_SAMPLE_RATES = frozenset({44100, 48000, 88200, 96000})
VALID_BITS_PER_SAMPLE = frozenset({16, 24})
VALID_STOP_REASONS = frozenset({
    "render-stop", "acl-disconnect", "fault"
})
VALID_LIFECYCLE_OUTCOMES = frozenset({
    "completed", "graceful-stop", "cancelled", "faulted"
})


@dataclass(frozen=True)
class AgentConfig:
    enabled: bool = True
    quality: str = "hq"
    revision: int = 0
    channel_mode: str = "stereo"
    sample_rate: int = 48000
    bits_per_sample: int = 16


@dataclass(frozen=True)
class AgentTelemetry:
    """Optional, read-only transport diagnostics published with state.

    The installed agent's version 1/2 state files predate these fields.  A
    missing value therefore means "not published", not zero or false.
    """

    transport_policy: str | None = None
    transport_policy_version: int | None = None
    acl_generation: int | None = None
    render_demand: str | None = None
    open_attempts: int | None = None
    maximum_open_attempts: int | None = None
    retryable_failures: int | None = None
    retries_scheduled: int | None = None
    retry_reasons: tuple[str, ...] = ()
    pcm_prepare_attempts: int | None = None
    pcm_epoch_restarts: int | None = None
    consumer_lease_acquire_count: int | None = None
    consumer_lease_release_count: int | None = None
    consumer_lease_acquired: bool | None = None
    consumer_lease_released: bool | None = None
    maximum_output_peak_ceiling: float | None = None
    maximum_unlimited_post_gain_peak: float | None = None
    maximum_post_gain_peak: float | None = None
    limited_output_samples: int | None = None
    stop_reason: str | None = None
    graceful_stop_actions: int | None = None
    cancel_actions: int | None = None
    media_duration_ms: int | None = None
    final_attempt_archived: bool | None = None
    resources_released: bool | None = None
    lifecycle_outcome: str | None = None


@dataclass(frozen=True)
class AgentState:
    state: str
    quality: str
    agent_pid: int
    probe_pid: int
    generation: int
    config_enabled: bool
    config_revision: int
    telemetry: AgentTelemetry = AgentTelemetry()


@dataclass(frozen=True)
class V1TrialResult:
    """Read-only view of one explicitly selected V1 trial result file."""

    path: Path
    transport_passed: bool | None
    telemetry: AgentTelemetry


@dataclass(frozen=True)
class V1TrialComparison:
    """Aggregate of only the explicitly supplied V1 trial result paths."""

    results: tuple[V1TrialResult, ...]
    passed_count: int
    failed_count: int
    unknown_count: int
    graceful_stop_actions: int | None
    cancel_actions: int | None
    minimum_media_duration_ms: int | None
    maximum_media_duration_ms: int | None
    stop_reasons: tuple[str, ...]
    lifecycle_outcomes: tuple[str, ...]
    all_final_attempts_archived: bool | None
    all_resources_released: bool | None


def native_root(local_app_data: str | Path | None = None) -> Path:
    root = local_app_data
    if root is None:
        root = os.environ.get("LOCALAPPDATA")
    if not root:
        raise OSError("LOCALAPPDATA is unavailable")
    return Path(root) / "NativeLdac"


def config_path(local_app_data: str | Path | None = None) -> Path:
    return native_root(local_app_data) / "config.json"


def state_path(local_app_data: str | Path | None = None) -> Path:
    return native_root(local_app_data) / "logs" / "state.json"


def probe_log_path(local_app_data: str | Path | None = None) -> Path:
    return native_root(local_app_data) / "logs" / "probe.log"


def _require_int(value: Any, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise ValueError(f"{field} must be a non-negative integer")
    return value


def _require_bool(value: Any, field: str) -> bool:
    if not isinstance(value, bool):
        raise ValueError(f"{field} must be boolean")
    return value


def _optional_int(payload: dict[str, Any], *fields: str) -> int | None:
    for field in fields:
        if field in payload:
            return _require_int(payload[field], field)
    return None


def _optional_float(payload: dict[str, Any], *fields: str) -> float | None:
    for field in fields:
        if field not in payload:
            continue
        value = payload[field]
        if (isinstance(value, bool) or not isinstance(value, (int, float)) or
                not float("-inf") < float(value) < float("inf") or
                value < 0):
            raise ValueError(f"{field} must be a finite non-negative number")
        return float(value)
    return None


def _optional_bool(payload: dict[str, Any], *fields: str) -> bool | None:
    for field in fields:
        if field in payload:
            return _require_bool(payload[field], field)
    return None


def _optional_string(payload: dict[str, Any], *fields: str) -> str | None:
    for field in fields:
        if field in payload:
            value = payload[field]
            if not isinstance(value, str):
                raise ValueError(f"{field} must be a string")
            return value
    return None


def _optional_enum(
    payload: dict[str, Any], allowed: frozenset[str], *fields: str
) -> str | None:
    value = _optional_string(payload, *fields)
    if value is not None and value not in allowed:
        field = next(field for field in fields if field in payload)
        raise ValueError(f"{field} is invalid")
    return value


def _optional_string_tuple(
    payload: dict[str, Any], *fields: str
) -> tuple[str, ...]:
    for field in fields:
        if field not in payload:
            continue
        value = payload[field]
        if isinstance(value, str):
            return (value,)
        if (not isinstance(value, list) or
                any(not isinstance(item, str) for item in value)):
            raise ValueError(f"{field} must be a string or string array")
        return tuple(value)
    return ()


def _parse_telemetry(payload: dict[str, Any]) -> AgentTelemetry:
    nested = payload.get("telemetry", {})
    if not isinstance(nested, dict):
        raise ValueError("telemetry must be an object")

    # Flat fields match the bounded V1 state/session artifacts.  The nested
    # object lets a future resident agent add the same diagnostics without
    # changing the stable top-level state contract.  Flat values win when a
    # producer temporarily publishes both during migration.
    values = dict(nested)
    values.update({key: value for key, value in payload.items()
                   if key != "telemetry"})
    return AgentTelemetry(
        transport_policy=_optional_string(values, "transport_policy"),
        transport_policy_version=_optional_int(
            values, "transport_policy_version"
        ),
        acl_generation=_optional_int(values, "acl_generation"),
        render_demand=_optional_string(values, "render_demand"),
        open_attempts=_optional_int(
            values,
            "open_attempts",
            "transport_open_attempts_for_generation",
            "transport_open_actions",
        ),
        maximum_open_attempts=_optional_int(
            values,
            "maximum_open_attempts",
            "maximum_transport_open_attempts",
        ),
        retryable_failures=_optional_int(
            values, "retryable_failures", "transport_retryable_failures"
        ),
        retries_scheduled=_optional_int(
            values, "retries_scheduled", "transport_retries_scheduled"
        ),
        retry_reasons=_optional_string_tuple(
            values, "retry_reasons", "transport_retry_reasons"
        ),
        pcm_prepare_attempts=_optional_int(values, "pcm_prepare_attempts"),
        pcm_epoch_restarts=_optional_int(values, "pcm_epoch_restarts"),
        consumer_lease_acquire_count=_optional_int(
            values, "consumer_lease_acquire_count"
        ),
        consumer_lease_release_count=_optional_int(
            values, "consumer_lease_release_count"
        ),
        consumer_lease_acquired=_optional_bool(
            values, "consumer_lease_acquired"
        ),
        consumer_lease_released=_optional_bool(
            values, "consumer_lease_released"
        ),
        maximum_output_peak_ceiling=_optional_float(
            values, "maximum_output_peak_ceiling"
        ),
        maximum_unlimited_post_gain_peak=_optional_float(
            values, "maximum_unlimited_post_gain_peak"
        ),
        maximum_post_gain_peak=_optional_float(
            values, "maximum_post_gain_peak"
        ),
        limited_output_samples=_optional_int(values, "limited_output_samples"),
        stop_reason=_optional_enum(
            values, VALID_STOP_REASONS, "stop_reason"
        ),
        graceful_stop_actions=_optional_int(
            values, "graceful_stop_actions", "transport_graceful_stop_actions"
        ),
        cancel_actions=_optional_int(
            values, "cancel_actions", "transport_cancel_actions"
        ),
        media_duration_ms=_optional_int(
            values, "media_duration_ms", "actual_duration_ms"
        ),
        final_attempt_archived=_optional_bool(
            values, "final_attempt_archived"
        ),
        resources_released=_optional_bool(values, "resources_released"),
        lifecycle_outcome=_optional_enum(
            values, VALID_LIFECYCLE_OUTCOMES, "lifecycle_outcome"
        ),
    )


def _parse_config(payload: Any) -> AgentConfig:
    if (not isinstance(payload, dict) or
            payload.get("version") not in (1, 2, 3)):
        raise ValueError("unsupported agent config version")
    version = payload["version"]
    enabled = payload.get("enabled")
    quality = payload.get("quality")
    if not isinstance(enabled, bool):
        raise ValueError("enabled must be boolean")
    if not isinstance(quality, str) or quality not in VALID_QUALITIES:
        raise ValueError("quality is invalid")
    revision = _require_int(payload.get("revision"), "revision")
    if version >= 2 and "channel_mode" not in payload:
        raise ValueError("channel_mode is missing")
    channel_mode = payload.get("channel_mode", "stereo")
    if (not isinstance(channel_mode, str) or
            channel_mode not in VALID_CHANNEL_MODES):
        raise ValueError("channel_mode is invalid")
    if (version == 3 and
            ("sample_rate" not in payload or
             "bits_per_sample" not in payload)):
        raise ValueError("endpoint format is missing")
    sample_rate = payload.get("sample_rate", 48000)
    bits_per_sample = payload.get("bits_per_sample", 16)
    if sample_rate not in VALID_SAMPLE_RATES:
        raise ValueError("sample_rate is invalid")
    if bits_per_sample not in VALID_BITS_PER_SAMPLE:
        raise ValueError("bits_per_sample is invalid")
    return AgentConfig(
        enabled=enabled,
        quality=quality,
        revision=revision,
        channel_mode=channel_mode,
        sample_rate=sample_rate,
        bits_per_sample=bits_per_sample,
    )


def load_config(path: Path, default_quality: str = "hq") -> AgentConfig:
    if default_quality not in VALID_QUALITIES:
        raise ValueError("default quality is invalid")
    try:
        with path.open("r", encoding="utf-8-sig") as stream:
            return _parse_config(json.load(stream))
    except FileNotFoundError:
        return AgentConfig(quality=default_quality)


def save_config(path: Path, config: AgentConfig) -> None:
    if (config.quality not in VALID_QUALITIES or
            config.channel_mode not in VALID_CHANNEL_MODES or
            config.sample_rate not in VALID_SAMPLE_RATES or
            config.bits_per_sample not in VALID_BITS_PER_SAMPLE or
            config.revision < 0):
        raise ValueError("agent config is invalid")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f"{path.name}.tmp.{os.getpid()}")
    payload = {
        "version": 3,
        "revision": config.revision,
        "enabled": config.enabled,
        "quality": config.quality,
        "channel_mode": config.channel_mode,
        "sample_rate": config.sample_rate,
        "bits_per_sample": config.bits_per_sample,
    }
    try:
        with temporary.open("w", encoding="utf-8", newline="\n") as stream:
            json.dump(payload, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def update_config(
    path: Path,
    *,
    enabled: bool | None = None,
    quality: str | None = None,
    channel_mode: str | None = None,
    sample_rate: int | None = None,
    bits_per_sample: int | None = None,
) -> AgentConfig:
    try:
        current = load_config(path)
    except ValueError:
        current = AgentConfig()
    updated = AgentConfig(
        enabled=current.enabled if enabled is None else enabled,
        quality=current.quality if quality is None else quality,
        revision=current.revision + 1,
        channel_mode=(
            current.channel_mode if channel_mode is None else channel_mode
        ),
        sample_rate=(
            current.sample_rate if sample_rate is None else sample_rate
        ),
        bits_per_sample=(
            current.bits_per_sample
            if bits_per_sample is None else bits_per_sample
        ),
    )
    save_config(path, updated)
    return updated


def load_state(path: Path) -> AgentState:
    with path.open("r", encoding="utf-8-sig") as stream:
        payload = json.load(stream)
    if not isinstance(payload, dict) or payload.get("version") not in (1, 2):
        raise ValueError("unsupported agent state version")
    quality = payload.get("quality")
    state = payload.get("state")
    if not isinstance(state, str) or not isinstance(quality, str):
        raise ValueError("agent state strings are invalid")
    return AgentState(
        state=state,
        quality=quality,
        agent_pid=_require_int(payload.get("agent_pid"), "agent_pid"),
        probe_pid=_require_int(payload.get("probe_pid"), "probe_pid"),
        generation=_require_int(payload.get("generation"), "generation"),
        config_enabled=_require_bool(
            payload.get("config_enabled", True), "config_enabled"
        ),
        config_revision=_require_int(
            payload.get("config_revision", 0), "config_revision"
        ),
        telemetry=_parse_telemetry(payload),
    )


def load_telemetry_snapshot(path: Path) -> AgentTelemetry:
    """Read optional telemetry without conflating legacy and V1 state ABIs.

    The resident legacy agent state uses ``version`` and carries process and
    configuration identity.  Bounded V1 observers use ``schema_version`` and
    deliberately do not carry that identity.  Both can publish compatible
    diagnostic field names, but only :func:`load_state` may drive agent-mode
    lifecycle decisions in the UI.
    """

    with path.open("r", encoding="utf-8-sig") as stream:
        payload = json.load(stream)
    if not isinstance(payload, dict):
        raise ValueError("agent telemetry snapshot must be an object")
    if payload.get("version") in (1, 2):
        return _parse_telemetry(payload)
    if payload.get("schema_version") == 1:
        return _parse_telemetry(payload)
    raise ValueError("unsupported agent telemetry snapshot version")


def load_v1_trial_result(path: Path) -> V1TrialResult:
    """Load one explicit V1 result without participating in agent discovery."""

    explicit_path = Path(path)
    with explicit_path.open("r", encoding="utf-8-sig") as stream:
        payload = json.load(stream)
    if (not isinstance(payload, dict) or
            payload.get("schema_version") != 1 or
            "version" in payload):
        raise ValueError("unsupported V1 trial result version")
    result_markers = (
        "transport_passed", "passed", "stop_reason", "lifecycle_outcome"
    )
    if not any(marker in payload for marker in result_markers):
        raise ValueError("the selected V1 JSON is not a trial result")
    passed_values = []
    for field in ("transport_passed", "passed"):
        if field in payload:
            passed_values.append(_require_bool(payload[field], field))
    if len(passed_values) == 2 and passed_values[0] != passed_values[1]:
        raise ValueError("V1 trial pass fields disagree")
    passed = passed_values[0] if passed_values else None
    return V1TrialResult(
        path=explicit_path,
        transport_passed=passed,
        telemetry=_parse_telemetry(payload),
    )


def format_v1_trial_summary(result: V1TrialResult) -> str:
    """Return a compact lifecycle summary without importing the Tk UI."""

    items = ["V1 trial"]
    if result.transport_passed is not None:
        items.append(
            "transport passed" if result.transport_passed
            else "transport failed"
        )
    telemetry = result.telemetry
    if telemetry.stop_reason is not None:
        items.append(f"stop {telemetry.stop_reason}")
    if (telemetry.graceful_stop_actions is not None or
            telemetry.cancel_actions is not None):
        graceful = (
            "—" if telemetry.graceful_stop_actions is None
            else str(telemetry.graceful_stop_actions)
        )
        cancelled = (
            "—" if telemetry.cancel_actions is None
            else str(telemetry.cancel_actions)
        )
        items.append(f"actions G/C {graceful}/{cancelled}")
    if telemetry.media_duration_ms is not None:
        items.append(f"media {telemetry.media_duration_ms} ms")
    if telemetry.final_attempt_archived is not None:
        archived = "yes" if telemetry.final_attempt_archived else "no"
        items.append(f"archived {archived}")
    if telemetry.resources_released is not None:
        released = "yes" if telemetry.resources_released else "no"
        items.append(f"resources released {released}")
    if telemetry.lifecycle_outcome is not None:
        items.append(f"outcome {telemetry.lifecycle_outcome}")
    return " · ".join(items)


def compare_v1_trial_results(
    paths: Sequence[str | Path],
) -> V1TrialComparison:
    """Load and compare a non-empty, explicit sequence of V1 result paths."""

    if isinstance(paths, (str, bytes, Path)) or not isinstance(paths, Sequence):
        raise TypeError("paths must be an explicit sequence of result paths")
    if not paths:
        raise ValueError("at least one explicit V1 result path is required")
    explicit_paths = tuple(Path(path) for path in paths)
    normalized = tuple(path.resolve(strict=False) for path in explicit_paths)
    if len(set(normalized)) != len(normalized):
        raise ValueError("duplicate V1 result paths are not allowed")
    results = tuple(load_v1_trial_result(path) for path in explicit_paths)

    passed_count = sum(result.transport_passed is True for result in results)
    failed_count = sum(result.transport_passed is False for result in results)
    unknown_count = len(results) - passed_count - failed_count
    graceful_values = tuple(
        result.telemetry.graceful_stop_actions
        for result in results
        if result.telemetry.graceful_stop_actions is not None
    )
    cancel_values = tuple(
        result.telemetry.cancel_actions
        for result in results
        if result.telemetry.cancel_actions is not None
    )
    durations = tuple(
        result.telemetry.media_duration_ms
        for result in results
        if result.telemetry.media_duration_ms is not None
    )
    stop_reasons = tuple(sorted({
        result.telemetry.stop_reason
        for result in results
        if result.telemetry.stop_reason is not None
    }))
    lifecycle_outcomes = tuple(sorted({
        result.telemetry.lifecycle_outcome
        for result in results
        if result.telemetry.lifecycle_outcome is not None
    }))
    archived_values = tuple(
        result.telemetry.final_attempt_archived for result in results
    )
    released_values = tuple(
        result.telemetry.resources_released for result in results
    )
    return V1TrialComparison(
        results=results,
        passed_count=passed_count,
        failed_count=failed_count,
        unknown_count=unknown_count,
        graceful_stop_actions=(
            sum(graceful_values)
            if len(graceful_values) == len(results) else None
        ),
        cancel_actions=(
            sum(cancel_values)
            if len(cancel_values) == len(results) else None
        ),
        minimum_media_duration_ms=min(durations) if durations else None,
        maximum_media_duration_ms=max(durations) if durations else None,
        stop_reasons=stop_reasons,
        lifecycle_outcomes=lifecycle_outcomes,
        all_final_attempts_archived=(
            False if any(value is False for value in archived_values)
            else (True if all(value is True for value in archived_values)
                  else None)
        ),
        all_resources_released=(
            False if any(value is False for value in released_values)
            else (True if all(value is True for value in released_values)
                  else None)
        ),
    )


def format_v1_trial_comparison(comparison: V1TrialComparison) -> str:
    """Format aggregate differences without importing or starting Tk."""

    items = [f"V1 trials {len(comparison.results)}"]
    items.append(
        "transport P/F/? "
        f"{comparison.passed_count}/{comparison.failed_count}/"
        f"{comparison.unknown_count}"
    )
    if (comparison.graceful_stop_actions is not None or
            comparison.cancel_actions is not None):
        graceful = (
            "—" if comparison.graceful_stop_actions is None
            else str(comparison.graceful_stop_actions)
        )
        cancelled = (
            "—" if comparison.cancel_actions is None
            else str(comparison.cancel_actions)
        )
        items.append(f"actions G/C {graceful}/{cancelled}")
    if comparison.minimum_media_duration_ms is not None:
        duration = str(comparison.minimum_media_duration_ms)
        if (comparison.maximum_media_duration_ms is not None and
                comparison.maximum_media_duration_ms !=
                comparison.minimum_media_duration_ms):
            duration += f"–{comparison.maximum_media_duration_ms}"
        items.append(f"media {duration} ms")
    if comparison.stop_reasons:
        items.append("stops " + ",".join(comparison.stop_reasons))
    if comparison.lifecycle_outcomes:
        items.append("outcomes " + ",".join(comparison.lifecycle_outcomes))
    if comparison.all_final_attempts_archived is not None:
        archived = "yes" if comparison.all_final_attempts_archived else "no"
        items.append(f"all archived {archived}")
    if comparison.all_resources_released is not None:
        released = "yes" if comparison.all_resources_released else "no"
        items.append(f"all resources released {released}")
    return " · ".join(items)


def process_is_running(
    process_id: int, expected_executable: str = "ldac_agent.exe"
) -> bool:
    if process_id <= 0 or not hasattr(ctypes, "WinDLL"):
        return False
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.argtypes = (
        wintypes.DWORD,
        wintypes.BOOL,
        wintypes.DWORD,
    )
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel32.CloseHandle.restype = wintypes.BOOL
    kernel32.QueryFullProcessImageNameW.argtypes = (
        wintypes.HANDLE,
        wintypes.DWORD,
        wintypes.LPWSTR,
        ctypes.POINTER(wintypes.DWORD),
    )
    kernel32.QueryFullProcessImageNameW.restype = wintypes.BOOL
    process_query_limited_information = 0x1000
    handle = kernel32.OpenProcess(
        process_query_limited_information, False, process_id
    )
    if not handle:
        return False
    capacity = wintypes.DWORD(32768)
    buffer = ctypes.create_unicode_buffer(capacity.value)
    queried = kernel32.QueryFullProcessImageNameW(
        handle, 0, buffer, ctypes.byref(capacity)
    )
    kernel32.CloseHandle(handle)
    return (
        bool(queried)
        and Path(buffer.value).name.casefold() == expected_executable.casefold()
    )
