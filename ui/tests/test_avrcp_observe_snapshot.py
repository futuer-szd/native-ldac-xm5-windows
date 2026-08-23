from __future__ import annotations

import copy
import json
import sys
import unittest
from pathlib import Path


UI_ROOT = Path(__file__).resolve().parents[1]
if str(UI_ROOT) not in sys.path:
    sys.path.insert(0, str(UI_ROOT))

from avrcp_observe_snapshot import (  # noqa: E402
    AvrcpSupport,
    MAX_JSON_BYTES,
    MAX_UINT64,
    VolumeGateMode,
    Xm5VolumeEvent,
    parse_avrcp_observe_snapshot_json,
)


def valid_payload() -> dict[str, object]:
    return {
        "schema_version": 1,
        "replayed_event_count": 7,
        "last_sequence": 7,
        "generations_started": 1,
        "generations_ended": 0,
        "current_generation": 9,
        "generation_current": True,
        "support": "supported",
        "capability_event_count": 1,
        "windows_volume_observed": True,
        "windows_volume_percent": 55,
        "windows_volume_muted": False,
        "windows_callback_count": 1,
        "xm5_volume_observed": True,
        "xm5_absolute_volume": 71,
        "xm5_event": "remote_notification",
        "xm5_command_response_count": 1,
        "xm5_remote_notification_count": 1,
        "owner_lease": 42,
        "lease_acquire_count": 1,
        "lease_revoke_count": 0,
        "requested_mode": "synchronize",
        "mode_change_count": 1,
        "enforced_replay_mode": "observe_only",
        "emitted_action_count": 0,
    }


def encode(payload: object) -> str:
    return json.dumps(payload, separators=(",", ":"), sort_keys=False)


class AvrcpObserveSnapshotTests(unittest.TestCase):
    def test_accepts_exact_producer_schema(self) -> None:
        snapshot = parse_avrcp_observe_snapshot_json(
            encode(valid_payload())
        )
        self.assertEqual(snapshot.schema_version, 1)
        self.assertEqual(snapshot.current_generation, 9)
        self.assertTrue(snapshot.generation_current)
        self.assertIs(snapshot.support, AvrcpSupport.SUPPORTED)
        self.assertEqual(snapshot.windows_volume_percent, 55)
        self.assertIs(snapshot.xm5_event,
                      Xm5VolumeEvent.REMOTE_NOTIFICATION)
        self.assertEqual(snapshot.owner_lease, 42)
        self.assertIs(snapshot.requested_mode,
                      VolumeGateMode.SYNCHRONIZE)
        self.assertIs(snapshot.enforced_replay_mode,
                      VolumeGateMode.OBSERVE_ONLY)
        self.assertEqual(snapshot.emitted_action_count, 0)

    def test_accepts_bytes_bytearray_and_reordered_fields(self) -> None:
        payload = valid_payload()
        reversed_payload = dict(reversed(tuple(payload.items())))
        document = encode(reversed_payload).encode("utf-8")
        expected = parse_avrcp_observe_snapshot_json(document)
        self.assertEqual(
            parse_avrcp_observe_snapshot_json(bytearray(document)),
            expected,
        )
        self.assertEqual(
            parse_avrcp_observe_snapshot_json("  " + document.decode() + "\n"),
            expected,
        )

    def test_rejects_unknown_schema_missing_and_extra_fields(self) -> None:
        payload = valid_payload()
        payload["schema_version"] = 2
        with self.assertRaises(ValueError):
            parse_avrcp_observe_snapshot_json(encode(payload))

        for field in tuple(valid_payload()):
            missing = valid_payload()
            del missing[field]
            with self.subTest(missing=field), self.assertRaises(ValueError):
                parse_avrcp_observe_snapshot_json(encode(missing))

        extra = valid_payload()
        extra["future_field"] = 1
        with self.assertRaises(ValueError):
            parse_avrcp_observe_snapshot_json(encode(extra))

    def test_rejects_duplicate_malformed_and_non_object_json(self) -> None:
        document = encode(valid_payload())
        duplicate = document[:-1] + ',"schema_version":1}'
        with self.assertRaises(ValueError):
            parse_avrcp_observe_snapshot_json(duplicate)
        for invalid in ("{} trailing", "null", "[]", "{", "\ufeff{}"):
            with self.subTest(document=invalid), self.assertRaises(ValueError):
                parse_avrcp_observe_snapshot_json(invalid)
        with self.assertRaises(ValueError):
            parse_avrcp_observe_snapshot_json(b"\xff")
        with self.assertRaises(TypeError):
            parse_avrcp_observe_snapshot_json(123)  # type: ignore[arg-type]

    def test_rejects_invalid_enums(self) -> None:
        enum_cases = {
            "support": ("future", 1),
            "xm5_event": ("changed", False),
            "requested_mode": ("write", []),
            "enforced_replay_mode": ("synchronize", {}),
        }
        for field, invalid_values in enum_cases.items():
            for value in invalid_values:
                payload = valid_payload()
                payload[field] = value
                with (self.subTest(field=field, value=value),
                      self.assertRaises(ValueError)):
                    parse_avrcp_observe_snapshot_json(encode(payload))

    def test_rejects_integer_boolean_and_range_confusion(self) -> None:
        cases = (
            ("schema_version", True),
            ("replayed_event_count", -1),
            ("last_sequence", 1.0),
            ("current_generation", MAX_UINT64 + 1),
            ("generation_current", 1),
            ("windows_volume_percent", 101),
            ("windows_volume_muted", 0),
            ("xm5_absolute_volume", 128),
            ("emitted_action_count", -1),
        )
        for field, value in cases:
            payload = valid_payload()
            payload[field] = value
            with (self.subTest(field=field, value=value),
                  self.assertRaises(ValueError)):
                parse_avrcp_observe_snapshot_json(encode(payload))

        nan_document = encode(valid_payload()).replace(
            '"last_sequence":7', '"last_sequence":NaN'
        )
        with self.assertRaises(ValueError):
            parse_avrcp_observe_snapshot_json(nan_document)

    def test_rejects_count_and_generation_inconsistency(self) -> None:
        cases: list[tuple[str, object]] = [
            ("last_sequence", 6),
            ("generations_started", 0),
            ("generations_ended", 2),
            ("capability_event_count", 0),
            ("windows_callback_count", 0),
            ("xm5_command_response_count", 0),
            ("lease_revoke_count", 2),
            ("mode_change_count", 0),
        ]
        for field, value in cases:
            payload = valid_payload()
            payload[field] = value
            with (self.subTest(field=field), self.assertRaises(ValueError)):
                parse_avrcp_observe_snapshot_json(encode(payload))

        inactive = valid_payload()
        inactive.update({
            "generation_current": False,
            "owner_lease": 0,
            "requested_mode": "observe_only",
        })
        with self.assertRaises(ValueError):
            parse_avrcp_observe_snapshot_json(encode(inactive))

    def test_rejects_writable_or_action_emitting_snapshot(self) -> None:
        payload = valid_payload()
        payload["enforced_replay_mode"] = "synchronize"
        with self.assertRaises(ValueError):
            parse_avrcp_observe_snapshot_json(encode(payload))
        payload = valid_payload()
        payload["emitted_action_count"] = 1
        with self.assertRaises(ValueError):
            parse_avrcp_observe_snapshot_json(encode(payload))

    def test_accepts_uint64_extremes_and_empty_snapshot(self) -> None:
        payload = valid_payload()
        payload["current_generation"] = MAX_UINT64
        payload["owner_lease"] = MAX_UINT64
        snapshot = parse_avrcp_observe_snapshot_json(encode(payload))
        self.assertEqual(snapshot.current_generation, MAX_UINT64)
        self.assertEqual(snapshot.owner_lease, MAX_UINT64)

        empty = valid_payload()
        for field in tuple(empty):
            if field.endswith("_count"):
                empty[field] = 0
        empty.update({
            "replayed_event_count": 0,
            "last_sequence": 0,
            "generations_started": 0,
            "generations_ended": 0,
            "current_generation": 0,
            "generation_current": False,
            "support": "unknown",
            "windows_volume_observed": False,
            "windows_volume_percent": 0,
            "windows_volume_muted": True,
            "xm5_volume_observed": False,
            "xm5_absolute_volume": 0,
            "xm5_event": "command_response",
            "owner_lease": 0,
            "requested_mode": "observe_only",
            "enforced_replay_mode": "observe_only",
            "emitted_action_count": 0,
        })
        snapshot = parse_avrcp_observe_snapshot_json(encode(empty))
        self.assertEqual(snapshot.replayed_event_count, 0)
        self.assertFalse(snapshot.generation_current)

    def test_rejects_document_above_fixed_size(self) -> None:
        document = encode(valid_payload())
        exact = document + (" " * (MAX_JSON_BYTES - len(document)))
        snapshot = parse_avrcp_observe_snapshot_json(exact)
        self.assertEqual(snapshot.replayed_event_count, 7)
        oversized = document + (" " * (MAX_JSON_BYTES - len(document) + 1))
        with self.assertRaises(ValueError):
            parse_avrcp_observe_snapshot_json(oversized)

    def test_parser_does_not_mutate_input_mapping(self) -> None:
        payload = valid_payload()
        original = copy.deepcopy(payload)
        parse_avrcp_observe_snapshot_json(encode(payload))
        self.assertEqual(payload, original)


if __name__ == "__main__":
    unittest.main()
