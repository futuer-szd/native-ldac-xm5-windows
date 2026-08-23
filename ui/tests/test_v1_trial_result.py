from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from agent_config import (
    compare_v1_trial_results,
    format_v1_trial_comparison,
    format_v1_trial_summary,
    load_state,
    load_v1_trial_result,
)


class V1TrialResultTests(unittest.TestCase):
    def _write(self, directory: str, payload: object) -> Path:
        path = Path(directory) / "result.json"
        path.write_text(json.dumps(payload), encoding="utf-8")
        return path

    def test_missing_explicit_result_is_reported(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "missing-result.json"
            with self.assertRaises(FileNotFoundError):
                load_v1_trial_result(path)

    def test_invalid_or_non_result_json_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "result.json"
            path.write_text("{partial", encoding="utf-8")
            with self.assertRaises(json.JSONDecodeError):
                load_v1_trial_result(path)

            for payload in (
                {"version": 2, "state": "stopped"},
                {"schema_version": 2, "transport_passed": True},
                {"schema_version": 1, "state": "stopped"},
                {
                    "schema_version": 1,
                    "transport_passed": True,
                    "passed": False,
                },
                {
                    "schema_version": 1,
                    "transport_passed": True,
                    "stop_reason": "unknown",
                },
            ):
                path.write_text(json.dumps(payload), encoding="utf-8")
                with self.subTest(payload=payload), self.assertRaises(
                    ValueError
                ):
                    load_v1_trial_result(path)

    def test_normal_render_stop_summary(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = self._write(directory, {
                "schema_version": 1,
                "transport_passed": True,
                "stop_reason": "render-stop",
                "transport_graceful_stop_actions": 1,
                "transport_cancel_actions": 0,
                "media_duration_ms": 10000,
                "final_attempt_archived": True,
                "resources_released": True,
                "lifecycle_outcome": "graceful-stop",
            })
            result = load_v1_trial_result(path)
            self.assertEqual(result.path, path)
            self.assertEqual(
                format_v1_trial_summary(result),
                "V1 trial · transport passed · stop render-stop · "
                "actions G/C 1/0 · media 10000 ms · archived yes · "
                "resources released yes · outcome graceful-stop",
            )

    def test_acl_disconnect_summary(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = self._write(directory, {
                "schema_version": 1,
                "transport_passed": True,
                "stop_reason": "acl-disconnect",
                "transport_graceful_stop_actions": 0,
                "transport_cancel_actions": 1,
                "actual_duration_ms": 4500,
                "final_attempt_archived": True,
                "resources_released": True,
                "lifecycle_outcome": "cancelled",
            })
            summary = format_v1_trial_summary(load_v1_trial_result(path))
            self.assertIn("stop acl-disconnect", summary)
            self.assertIn("actions G/C 0/1", summary)
            self.assertIn("media 4500 ms", summary)
            self.assertIn("outcome cancelled", summary)

    def test_fault_summary_and_abi_separation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = self._write(directory, {
                "schema_version": 1,
                "transport_passed": False,
                "stop_reason": "fault",
                "resources_released": False,
                "lifecycle_outcome": "faulted",
            })
            result = load_v1_trial_result(path)
            summary = format_v1_trial_summary(result)
            self.assertIn("transport failed", summary)
            self.assertIn("stop fault", summary)
            self.assertIn("resources released no", summary)
            self.assertIn("outcome faulted", summary)
            with self.assertRaises(ValueError):
                load_state(path)

    def test_explicit_multi_result_comparison(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = []
            payloads = (
                {
                    "schema_version": 1,
                    "transport_passed": True,
                    "stop_reason": "render-stop",
                    "transport_graceful_stop_actions": 1,
                    "transport_cancel_actions": 0,
                    "media_duration_ms": 10000,
                    "final_attempt_archived": True,
                    "resources_released": True,
                    "lifecycle_outcome": "graceful-stop",
                },
                {
                    "schema_version": 1,
                    "transport_passed": True,
                    "stop_reason": "acl-disconnect",
                    "transport_graceful_stop_actions": 0,
                    "transport_cancel_actions": 1,
                    "media_duration_ms": 4500,
                    "final_attempt_archived": True,
                    "resources_released": True,
                    "lifecycle_outcome": "cancelled",
                },
                {
                    "schema_version": 1,
                    "transport_passed": False,
                    "stop_reason": "fault",
                    "transport_graceful_stop_actions": 0,
                    "transport_cancel_actions": 1,
                    "media_duration_ms": 250,
                    "final_attempt_archived": True,
                    "resources_released": False,
                    "lifecycle_outcome": "faulted",
                },
            )
            for index, payload in enumerate(payloads):
                path = root / f"explicit-{index}.json"
                path.write_text(json.dumps(payload), encoding="utf-8")
                paths.append(path)
            # This invalid sibling proves the API does not scan the directory.
            (root / "not-selected.json").write_text(
                "{invalid", encoding="utf-8"
            )

            comparison = compare_v1_trial_results(paths)
            self.assertEqual(
                tuple(result.path for result in comparison.results),
                tuple(paths),
            )
            self.assertEqual(comparison.passed_count, 2)
            self.assertEqual(comparison.failed_count, 1)
            self.assertEqual(comparison.unknown_count, 0)
            self.assertEqual(comparison.graceful_stop_actions, 1)
            self.assertEqual(comparison.cancel_actions, 2)
            self.assertEqual(comparison.minimum_media_duration_ms, 250)
            self.assertEqual(comparison.maximum_media_duration_ms, 10000)
            self.assertEqual(
                comparison.stop_reasons,
                ("acl-disconnect", "fault", "render-stop"),
            )
            self.assertFalse(comparison.all_resources_released)
            self.assertTrue(comparison.all_final_attempts_archived)
            summary = format_v1_trial_comparison(comparison)
            self.assertIn("V1 trials 3", summary)
            self.assertIn("transport P/F/? 2/1/0", summary)
            self.assertIn("media 250–10000 ms", summary)
            self.assertIn("all resources released no", summary)

    def test_comparison_requires_unique_explicit_paths(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = self._write(directory, {
                "schema_version": 1,
                "transport_passed": True,
            })
            with self.assertRaises(ValueError):
                compare_v1_trial_results([])
            with self.assertRaises(TypeError):
                compare_v1_trial_results(path)
            with self.assertRaises(TypeError):
                compare_v1_trial_results(str(path))
            with self.assertRaises(ValueError):
                compare_v1_trial_results([path, path])

    def test_one_invalid_explicit_result_rejects_whole_comparison(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            valid = root / "valid.json"
            invalid = root / "legacy.json"
            valid.write_text(json.dumps({
                "schema_version": 1,
                "transport_passed": True,
            }), encoding="utf-8")
            invalid.write_text(json.dumps({
                "version": 2,
                "state": "stopped",
            }), encoding="utf-8")
            with self.assertRaises(ValueError):
                compare_v1_trial_results([valid, invalid])

    def test_comparison_does_not_hide_known_resource_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            known_failure = root / "known-failure.json"
            unknown = root / "unknown.json"
            known_failure.write_text(json.dumps({
                "schema_version": 1,
                "transport_passed": False,
                "resources_released": False,
            }), encoding="utf-8")
            unknown.write_text(json.dumps({
                "schema_version": 1,
                "transport_passed": True,
            }), encoding="utf-8")
            comparison = compare_v1_trial_results([
                known_failure, unknown
            ])
            self.assertFalse(comparison.all_resources_released)
            self.assertIsNone(comparison.graceful_stop_actions)
            self.assertIsNone(comparison.cancel_actions)


if __name__ == "__main__":
    unittest.main()
