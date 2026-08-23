from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from agent_config import AgentTelemetry
from ldac_control import format_agent_telemetry, load_agent_state_for_ui


class AgentTelemetryDisplayTests(unittest.TestCase):
    def test_missing_telemetry_has_no_ui_summary(self) -> None:
        self.assertEqual(format_agent_telemetry(AgentTelemetry()), "")

    def test_compact_full_summary(self) -> None:
        text = format_agent_telemetry(AgentTelemetry(
            transport_policy="stability",
            transport_policy_version=8,
            acl_generation=4,
            render_demand="running",
            open_attempts=3,
            maximum_open_attempts=4,
            retryable_failures=2,
            retries_scheduled=2,
            retry_reasons=("OpenSignaling Win32 71 / zero exchange",),
            pcm_prepare_attempts=5,
            pcm_epoch_restarts=4,
            consumer_lease_acquire_count=5,
            consumer_lease_release_count=5,
            consumer_lease_acquired=True,
            consumer_lease_released=True,
            maximum_output_peak_ceiling=0.25,
            maximum_unlimited_post_gain_peak=0.31,
            maximum_post_gain_peak=0.25,
            limited_output_samples=37,
            stop_reason="render-stop",
            graceful_stop_actions=3,
            cancel_actions=2,
            media_duration_ms=10000,
            final_attempt_archived=True,
            resources_released=True,
            lifecycle_outcome="graceful-stop",
        ))
        for expected in (
            "stability P8",
            "ACL 4",
            "Render running",
            "OPEN 3/4",
            "Retry 2",
            "Scheduled 2",
            "PCM prepare 5",
            "epoch +4",
            "Lease 5/5 已释放",
            "Limiter 0.25",
            "Peak 0.31→0.25",
            "Limited 37",
            "Stop render-stop",
            "Stop G/C 3/2",
            "Media 10000 ms",
            "Archive yes",
            "Resources released",
            "Lifecycle graceful-stop",
        ):
            with self.subTest(expected=expected):
                self.assertIn(expected, text)

    def test_partial_telemetry_only_displays_published_values(self) -> None:
        text = format_agent_telemetry(AgentTelemetry(
            acl_generation=2,
            consumer_lease_acquired=True,
            consumer_lease_released=False,
            maximum_post_gain_peak=0.125,
        ))
        self.assertEqual(
            text, "ACL 2 · Lease 已获取 持有中 · Peak 0.125"
        )

    def test_negative_lifecycle_flags_are_visible(self) -> None:
        text = format_agent_telemetry(AgentTelemetry(
            final_attempt_archived=False,
            resources_released=False,
            lifecycle_outcome="faulted",
        ))
        self.assertEqual(
            text, "Archive no · Resources held · Lifecycle faulted"
        )

    def test_invalid_state_snapshot_is_ignored(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "state.json"
            path.write_text("{partial", encoding="utf-8")
            self.assertIsNone(load_agent_state_for_ui(path))
            path.write_text(json.dumps({"version": 99}), encoding="utf-8")
            self.assertIsNone(load_agent_state_for_ui(path))


if __name__ == "__main__":
    unittest.main()
