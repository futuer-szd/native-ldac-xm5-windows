"""Strict, read-only consumer for AVRCP ObserveOnly snapshot JSON."""

from __future__ import annotations

import json
from dataclasses import dataclass
from enum import Enum
from typing import Any


SCHEMA_VERSION = 1
MAX_EVENT_COUNT = 256
MAX_JSON_BYTES = 4096
MAX_UINT64 = (1 << 64) - 1


class AvrcpSupport(str, Enum):
    UNKNOWN = "unknown"
    UNSUPPORTED = "unsupported"
    SUPPORTED = "supported"


class Xm5VolumeEvent(str, Enum):
    COMMAND_RESPONSE = "command_response"
    REMOTE_NOTIFICATION = "remote_notification"


class VolumeGateMode(str, Enum):
    OBSERVE_ONLY = "observe_only"
    SYNCHRONIZE = "synchronize"


@dataclass(frozen=True)
class AvrcpObserveSnapshot:
    schema_version: int
    replayed_event_count: int
    last_sequence: int
    generations_started: int
    generations_ended: int
    current_generation: int
    generation_current: bool
    support: AvrcpSupport
    capability_event_count: int
    windows_volume_observed: bool
    windows_volume_percent: int
    windows_volume_muted: bool
    windows_callback_count: int
    xm5_volume_observed: bool
    xm5_absolute_volume: int
    xm5_event: Xm5VolumeEvent
    xm5_command_response_count: int
    xm5_remote_notification_count: int
    owner_lease: int
    lease_acquire_count: int
    lease_revoke_count: int
    requested_mode: VolumeGateMode
    mode_change_count: int
    enforced_replay_mode: VolumeGateMode
    emitted_action_count: int


_FIELD_ORDER = tuple(AvrcpObserveSnapshot.__dataclass_fields__)
_FIELD_SET = frozenset(_FIELD_ORDER)


def _reject_constant(value: str) -> None:
    raise ValueError(f"non-finite JSON number is forbidden: {value}")


def _strict_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON field: {key}")
        result[key] = value
    return result


def _uint(payload: dict[str, Any], field: str,
          maximum: int = MAX_UINT64) -> int:
    value = payload[field]
    if type(value) is not int or value < 0 or value > maximum:
        raise ValueError(f"{field} must be an unsigned integer <= {maximum}")
    return value


def _boolean(payload: dict[str, Any], field: str) -> bool:
    value = payload[field]
    if type(value) is not bool:
        raise ValueError(f"{field} must be a boolean")
    return value


def _enum(payload: dict[str, Any], field: str,
          enum_type: type[Enum]) -> Enum:
    value = payload[field]
    if type(value) is not str:
        raise ValueError(f"{field} must be a string")
    try:
        return enum_type(value)
    except ValueError as error:
        raise ValueError(f"invalid {field}: {value}") from error


def _decode_json(document: str | bytes | bytearray) -> dict[str, Any]:
    if isinstance(document, str):
        try:
            encoded_size = len(document.encode("utf-8"))
        except UnicodeEncodeError as error:
            raise ValueError("snapshot JSON is not valid UTF-8") from error
        text: str | bytes = document
    elif isinstance(document, (bytes, bytearray)):
        encoded_size = len(document)
        try:
            text = bytes(document).decode("utf-8")
        except UnicodeDecodeError as error:
            raise ValueError("snapshot JSON is not valid UTF-8") from error
    else:
        raise TypeError("snapshot JSON must be str, bytes, or bytearray")
    if encoded_size == 0 or encoded_size > MAX_JSON_BYTES:
        raise ValueError("snapshot JSON size is outside the fixed contract")
    payload = json.loads(
        text,
        object_pairs_hook=_strict_object,
        parse_constant=_reject_constant,
    )
    if type(payload) is not dict:
        raise ValueError("snapshot JSON root must be an object")
    fields = frozenset(payload)
    if fields != _FIELD_SET:
        missing = sorted(_FIELD_SET - fields)
        extra = sorted(fields - _FIELD_SET)
        raise ValueError(
            f"snapshot fields do not match schema; missing={missing}, "
            f"extra={extra}"
        )
    return payload


def parse_avrcp_observe_snapshot_json(
    document: str | bytes | bytearray,
) -> AvrcpObserveSnapshot:
    """Parse the exact schema-v1 ObserveOnly contract without side effects."""

    payload = _decode_json(document)
    schema_version = _uint(payload, "schema_version", (1 << 32) - 1)
    if schema_version != SCHEMA_VERSION:
        raise ValueError("unsupported AVRCP ObserveOnly snapshot schema")

    replayed_event_count = _uint(
        payload, "replayed_event_count", MAX_EVENT_COUNT
    )
    last_sequence = _uint(payload, "last_sequence")
    generations_started = _uint(
        payload, "generations_started", MAX_EVENT_COUNT
    )
    generations_ended = _uint(
        payload, "generations_ended", MAX_EVENT_COUNT
    )
    current_generation = _uint(payload, "current_generation")
    generation_current = _boolean(payload, "generation_current")
    support = _enum(payload, "support", AvrcpSupport)
    capability_event_count = _uint(
        payload, "capability_event_count", MAX_EVENT_COUNT
    )
    windows_volume_observed = _boolean(
        payload, "windows_volume_observed"
    )
    windows_volume_percent = _uint(
        payload, "windows_volume_percent", 100
    )
    windows_volume_muted = _boolean(payload, "windows_volume_muted")
    windows_callback_count = _uint(
        payload, "windows_callback_count", MAX_EVENT_COUNT
    )
    xm5_volume_observed = _boolean(payload, "xm5_volume_observed")
    xm5_absolute_volume = _uint(payload, "xm5_absolute_volume", 127)
    xm5_event = _enum(payload, "xm5_event", Xm5VolumeEvent)
    xm5_command_response_count = _uint(
        payload, "xm5_command_response_count", MAX_EVENT_COUNT
    )
    xm5_remote_notification_count = _uint(
        payload, "xm5_remote_notification_count", MAX_EVENT_COUNT
    )
    owner_lease = _uint(payload, "owner_lease")
    lease_acquire_count = _uint(
        payload, "lease_acquire_count", MAX_EVENT_COUNT
    )
    lease_revoke_count = _uint(
        payload, "lease_revoke_count", MAX_EVENT_COUNT
    )
    requested_mode = _enum(payload, "requested_mode", VolumeGateMode)
    mode_change_count = _uint(
        payload, "mode_change_count", MAX_EVENT_COUNT
    )
    enforced_replay_mode = _enum(
        payload, "enforced_replay_mode", VolumeGateMode
    )
    emitted_action_count = _uint(
        payload, "emitted_action_count", MAX_EVENT_COUNT
    )

    counts = (
        generations_started,
        generations_ended,
        capability_event_count,
        windows_callback_count,
        xm5_command_response_count,
        xm5_remote_notification_count,
        lease_acquire_count,
        lease_revoke_count,
        mode_change_count,
    )
    if any(value > replayed_event_count for value in counts):
        raise ValueError("event category count exceeds replayed_event_count")
    if sum(counts) != replayed_event_count:
        raise ValueError("event category counts do not match replayed total")
    if last_sequence != replayed_event_count:
        raise ValueError("last_sequence does not match replayed total")
    if generations_ended > generations_started:
        raise ValueError("ended generation count exceeds started count")
    if replayed_event_count != 0 and generations_started == 0:
        raise ValueError("non-empty snapshot has no generation start")
    if ((generations_started == 0) != (current_generation == 0)):
        raise ValueError("current_generation does not match generation history")
    if generation_current and current_generation == 0:
        raise ValueError("current generation cannot be zero")
    if generation_current and generations_started <= generations_ended:
        raise ValueError("current generation has already ended")
    if (not generation_current and generations_started != 0 and
            generations_ended == 0):
        raise ValueError("inactive generation history has no end event")
    if support is not AvrcpSupport.UNKNOWN and capability_event_count == 0:
        raise ValueError("support value lacks a capability event")
    if windows_volume_observed and windows_callback_count == 0:
        raise ValueError("Windows volume value lacks a callback event")
    if (xm5_volume_observed and xm5_command_response_count == 0 and
            xm5_remote_notification_count == 0):
        raise ValueError("XM5 volume value lacks an XM5 event")
    if lease_revoke_count > lease_acquire_count:
        raise ValueError("lease revocations exceed acquisitions")
    if owner_lease != 0 and lease_acquire_count <= lease_revoke_count:
        raise ValueError("active owner lease has no unmatched acquisition")
    if (requested_mode is VolumeGateMode.SYNCHRONIZE and
            mode_change_count == 0):
        raise ValueError("synchronize request lacks a mode event")
    if not generation_current and owner_lease != 0:
        raise ValueError("inactive generation retains an owner lease")
    if (not generation_current and
            requested_mode is not VolumeGateMode.OBSERVE_ONLY):
        raise ValueError("inactive generation retains synchronize mode")
    if enforced_replay_mode is not VolumeGateMode.OBSERVE_ONLY:
        raise ValueError("consumer accepts ObserveOnly replay mode only")
    if emitted_action_count != 0:
        raise ValueError("consumer rejects snapshots with emitted actions")

    return AvrcpObserveSnapshot(
        schema_version=schema_version,
        replayed_event_count=replayed_event_count,
        last_sequence=last_sequence,
        generations_started=generations_started,
        generations_ended=generations_ended,
        current_generation=current_generation,
        generation_current=generation_current,
        support=support,
        capability_event_count=capability_event_count,
        windows_volume_observed=windows_volume_observed,
        windows_volume_percent=windows_volume_percent,
        windows_volume_muted=windows_volume_muted,
        windows_callback_count=windows_callback_count,
        xm5_volume_observed=xm5_volume_observed,
        xm5_absolute_volume=xm5_absolute_volume,
        xm5_event=xm5_event,
        xm5_command_response_count=xm5_command_response_count,
        xm5_remote_notification_count=xm5_remote_notification_count,
        owner_lease=owner_lease,
        lease_acquire_count=lease_acquire_count,
        lease_revoke_count=lease_revoke_count,
        requested_mode=requested_mode,
        mode_change_count=mode_change_count,
        enforced_replay_mode=enforced_replay_mode,
        emitted_action_count=emitted_action_count,
    )
