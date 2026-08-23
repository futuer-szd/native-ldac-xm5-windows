from __future__ import annotations

from pathlib import Path
import json
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from agent_config import (
    AgentConfig,
    AgentTelemetry,
    load_config,
    load_state,
    load_telemetry_snapshot,
    save_config,
    update_config,
)


class AgentConfigTests(unittest.TestCase):
    def test_missing_config_uses_default(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            self.assertEqual(load_config(path), AgentConfig())

    def test_atomic_save_and_update(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "NativeLdac" / "config.json"
            save_config(path, AgentConfig(False, "sq", 4))
            self.assertEqual(load_config(path), AgentConfig(False, "sq", 4))
            updated = update_config(path, enabled=True, quality="auto")
            self.assertEqual(updated, AgentConfig(True, "auto", 5))
            self.assertEqual(load_config(path), updated)
            self.assertEqual(list(path.parent.glob("config.json.tmp.*")), [])

    def test_channel_mode_v2_and_v1_migration(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            path.write_text(
                '{"version":1,"revision":3,"enabled":true,'
                '"quality":"hq"}',
                encoding="utf-8",
            )
            self.assertEqual(load_config(path).channel_mode, "stereo")
            updated = update_config(path, channel_mode="mono")
            self.assertEqual(updated.channel_mode, "mono")
            self.assertEqual(load_config(path), updated)
            self.assertEqual(
                json.loads(path.read_text(encoding="utf-8"))["version"], 3
            )

    def test_format_v3_and_legacy_defaults(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            path.write_text(
                '{"version":2,"revision":3,"enabled":true,'
                '"quality":"hq","channel_mode":"stereo"}',
                encoding="utf-8",
            )
            self.assertEqual(load_config(path).sample_rate, 48000)
            self.assertEqual(load_config(path).bits_per_sample, 16)
            updated = update_config(
                path, sample_rate=96000, bits_per_sample=24
            )
            self.assertEqual(updated.sample_rate, 96000)
            self.assertEqual(updated.bits_per_sample, 24)
            self.assertEqual(load_config(path), updated)

    def test_invalid_config_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            path.write_text(
                '{"version":1,"revision":0,"enabled":true,'
                '"quality":"invalid"}',
                encoding="utf-8",
            )
            with self.assertRaises(ValueError):
                load_config(path)
            repaired = update_config(path, enabled=False, quality="mq")
            self.assertEqual(repaired, AgentConfig(False, "mq", 1))
            self.assertEqual(load_config(path), repaired)

    def test_incomplete_v3_config_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            path.write_text(
                '{"version":3,"revision":0,"enabled":true,'
                '"quality":"hq","channel_mode":"stereo"}',
                encoding="utf-8",
            )
            with self.assertRaises(ValueError):
                load_config(path)

    def test_state_v2(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "state.json"
            path.write_text(
                '{"version":2,"state":"waiting_device","quality":"hq",'
                '"agent_pid":123,"probe_pid":0,"generation":7,'
                '"config_enabled":true,"config_revision":9}',
                encoding="utf-8",
            )
            state = load_state(path)
            self.assertEqual(state.state, "waiting_device")
            self.assertEqual(state.config_revision, 9)
            self.assertEqual(state.telemetry, AgentTelemetry())

    def test_state_optional_transport_telemetry(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "state.json"
            path.write_text(
                json.dumps({
                    "version": 2,
                    "state": "probe_running",
                    "quality": "hq",
                    "agent_pid": 123,
                    "probe_pid": 456,
                    "generation": 9,
                    "config_enabled": True,
                    "config_revision": 10,
                    "transport_policy": "stability",
                    "transport_policy_version": 8,
                    "acl_generation": 4,
                    "render_demand": "running",
                    "transport_open_attempts_for_generation": 3,
                    "maximum_transport_open_attempts": 4,
                    "transport_retryable_failures": 2,
                    "transport_retries_scheduled": 2,
                    "transport_retry_reasons": [
                        "OpenSignaling Win32 71 / zero exchange",
                        "OpenSignaling Win32 71 / zero exchange",
                    ],
                    "pcm_prepare_attempts": 5,
                    "pcm_epoch_restarts": 4,
                    "consumer_lease_acquire_count": 5,
                    "consumer_lease_release_count": 4,
                    "consumer_lease_acquired": True,
                    "consumer_lease_released": False,
                    "maximum_output_peak_ceiling": 0.25,
                    "maximum_unlimited_post_gain_peak": 0.31,
                    "maximum_post_gain_peak": 0.25,
                    "limited_output_samples": 37,
                    "stop_reason": "render-stop",
                    "transport_graceful_stop_actions": 3,
                    "transport_cancel_actions": 2,
                    "media_duration_ms": 10000,
                    "final_attempt_archived": True,
                    "resources_released": True,
                    "lifecycle_outcome": "graceful-stop",
                }),
                encoding="utf-8",
            )
            telemetry = load_state(path).telemetry
            self.assertEqual(telemetry.transport_policy, "stability")
            self.assertEqual(telemetry.transport_policy_version, 8)
            self.assertEqual(telemetry.acl_generation, 4)
            self.assertEqual(telemetry.render_demand, "running")
            self.assertEqual(telemetry.open_attempts, 3)
            self.assertEqual(telemetry.maximum_open_attempts, 4)
            self.assertEqual(telemetry.retryable_failures, 2)
            self.assertEqual(telemetry.retries_scheduled, 2)
            self.assertEqual(len(telemetry.retry_reasons), 2)
            self.assertEqual(telemetry.pcm_prepare_attempts, 5)
            self.assertEqual(telemetry.pcm_epoch_restarts, 4)
            self.assertEqual(telemetry.consumer_lease_acquire_count, 5)
            self.assertEqual(telemetry.consumer_lease_release_count, 4)
            self.assertTrue(telemetry.consumer_lease_acquired)
            self.assertFalse(telemetry.consumer_lease_released)
            self.assertEqual(telemetry.maximum_output_peak_ceiling, 0.25)
            self.assertEqual(
                telemetry.maximum_unlimited_post_gain_peak, 0.31
            )
            self.assertEqual(telemetry.maximum_post_gain_peak, 0.25)
            self.assertEqual(telemetry.limited_output_samples, 37)
            self.assertEqual(telemetry.stop_reason, "render-stop")
            self.assertEqual(telemetry.graceful_stop_actions, 3)
            self.assertEqual(telemetry.cancel_actions, 2)
            self.assertEqual(telemetry.media_duration_ms, 10000)
            self.assertTrue(telemetry.final_attempt_archived)
            self.assertTrue(telemetry.resources_released)
            self.assertEqual(telemetry.lifecycle_outcome, "graceful-stop")

    def test_nested_telemetry_and_flat_override(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "state.json"
            path.write_text(
                json.dumps({
                    "version": 1,
                    "state": "waiting_device",
                    "quality": "hq",
                    "agent_pid": 1,
                    "probe_pid": 0,
                    "generation": 2,
                    "telemetry": {
                        "acl_generation": 6,
                        "render_demand": "idle",
                        "open_attempts": 2,
                        "retry_reasons": "temporary refusal",
                    },
                    "acl_generation": 7,
                }),
                encoding="utf-8",
            )
            telemetry = load_state(path).telemetry
            self.assertEqual(telemetry.acl_generation, 7)
            self.assertEqual(telemetry.render_demand, "idle")
            self.assertEqual(telemetry.open_attempts, 2)
            self.assertEqual(
                telemetry.retry_reasons, ("temporary refusal",)
            )

    def test_invalid_optional_telemetry_is_rejected(self) -> None:
        base = {
            "version": 2,
            "state": "probe_running",
            "quality": "hq",
            "agent_pid": 1,
            "probe_pid": 2,
            "generation": 3,
        }
        invalid_fields = (
            ("acl_generation", True),
            ("render_demand", 1),
            ("transport_retry_reasons", ["valid", 71]),
            ("consumer_lease_released", 1),
            ("maximum_output_peak_ceiling", float("inf")),
            ("limited_output_samples", -1),
            ("stop_reason", "player-closed"),
            ("transport_cancel_actions", True),
            ("media_duration_ms", -1),
            ("final_attempt_archived", 1),
            ("resources_released", "yes"),
            ("lifecycle_outcome", "success"),
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "state.json"
            for field, value in invalid_fields:
                payload = dict(base)
                payload[field] = value
                path.write_text(json.dumps(payload), encoding="utf-8")
                with self.subTest(field=field), self.assertRaises(ValueError):
                    load_state(path)

    def test_all_stop_reason_enums_are_accepted(self) -> None:
        base = {
            "version": 2,
            "state": "stopped",
            "quality": "hq",
            "agent_pid": 1,
            "probe_pid": 0,
            "generation": 3,
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "state.json"
            for reason in ("render-stop", "acl-disconnect", "fault"):
                payload = dict(base)
                payload["stop_reason"] = reason
                path.write_text(json.dumps(payload), encoding="utf-8")
                with self.subTest(reason=reason):
                    self.assertEqual(load_state(path).telemetry.stop_reason,
                                     reason)

    def test_v1_observer_telemetry_schema_is_separate_from_agent_state(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "state.json"
            path.write_text(json.dumps({
                "schema_version": 1,
                "mode": "transport-pcm-burst-exercise",
                "state": "stopped",
                "physical_presence": "absent",
                "render_demand": "idle",
                "acl_generation": 3,
                "transport_open_actions": 2,
                "maximum_transport_open_attempts": 4,
                "transport_retryable_failures": 1,
                "transport_retries_scheduled": 1,
                "stop_reason": "acl-disconnect",
                "transport_graceful_stop_actions": 0,
                "transport_cancel_actions": 1,
                "actual_duration_ms": 10001,
                "final_attempt_archived": True,
                "resources_released": True,
                "lifecycle_outcome": "cancelled",
            }), encoding="utf-8")
            telemetry = load_telemetry_snapshot(path)
            self.assertEqual(telemetry.acl_generation, 3)
            self.assertEqual(telemetry.render_demand, "idle")
            self.assertEqual(telemetry.open_attempts, 2)
            self.assertEqual(telemetry.maximum_open_attempts, 4)
            self.assertEqual(telemetry.retryable_failures, 1)
            self.assertEqual(telemetry.stop_reason, "acl-disconnect")
            self.assertEqual(telemetry.graceful_stop_actions, 0)
            self.assertEqual(telemetry.cancel_actions, 1)
            self.assertEqual(telemetry.media_duration_ms, 10001)
            self.assertTrue(telemetry.final_attempt_archived)
            self.assertTrue(telemetry.resources_released)
            self.assertEqual(telemetry.lifecycle_outcome, "cancelled")
            with self.assertRaises(ValueError):
                load_state(path)


if __name__ == "__main__":
    unittest.main()
