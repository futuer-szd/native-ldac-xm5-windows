from __future__ import annotations

import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


UI_ROOT = Path(__file__).resolve().parents[1]
if str(UI_ROOT) not in sys.path:
    sys.path.insert(0, str(UI_ROOT))

from daily_state import (  # noqa: E402
    daily_state_path,
    daily_transport_result_path,
    derive_daily_presentation,
    load_daily_state,
    load_daily_transport_result,
)


def state_payload() -> dict[str, object]:
    return {
        "schema_version": 1,
        "mode": "daily",
        "daily_mode": True,
        "state": "present",
        "host_process_id": 4242,
        "physical_presence": "present",
        "render_demand": "running",
        "acl_generation": 3,
        "connected_events": 3,
        "disconnected_events": 2,
        "child_processes_started": 3,
        "transport_worker_sequence": 3,
        "engine_exit_events": 2,
        "engine_start_failures": 0,
        "engine_ready_timeouts": 0,
        "engine_stop_failures": 0,
        "engine_unexpected_exits": 0,
        "media_started_events": 3,
        "media_stopped_events": 0,
        "media_failed_events": 0,
        "avrcp_volume_sync_enabled": True,
        "avrcp_observer_active": True,
        "avrcp_observer_poll_failures": 0,
        "avrcp_pc_playback": "playing",
        "hfp_capture_monitor_ready": True,
        "hfp_capture_matched": False,
        "hfp_capture_active": False,
        "hfp_render_bridge_ready": False,
        "hfp_suspended": False,
        "endpoint_presence_failures": 0,
        "render_query_failures": 0,
        "config_pipe": "NativeLdac.V1.Config.ctest-1",
        "requested_quality": "HQ",
        "applied_quality": "HQ",
        "requested_config_revision": 0,
        "applied_config_revision": 0,
        "config_rejected_count": 0,
        "config_last_error": 0,
    }


def transport_payload() -> dict[str, object]:
    return {
        "schema_version": 1,
        "disposition": "cancelled",
        "backend_error": 0,
        "sample_rate_hz": 44100,
        "bits_per_sample": 16,
        "channel_mode": 1,
        "media_packets_written": 100,
        "media_bytes_written": 125000,
        "actual_duration_ms": 1000,
        "encoder_quality": "SQ",
        "nominal_ldac_bitrate_kbps": 606,
    }


class DailyStateTests(unittest.TestCase):
    def _write(self, root: Path, name: str, payload: object) -> Path:
        path = root / name
        path.write_text(json.dumps(payload), encoding="utf-8")
        return path

    def test_loads_current_daily_state_and_ignores_extensions(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            payload = state_payload()
            payload["future_field"] = {"safe": True}
            path = self._write(Path(directory), "state.json", payload)
            state = load_daily_state(path)
        self.assertEqual(state.host_process_id, 4242)
        self.assertEqual(state.acl_generation, 3)
        self.assertTrue(state.avrcp_observer_active)

    def test_rejects_wrong_schema_missing_pid_and_type_confusion(self) -> None:
        cases = []
        wrong_mode = state_payload()
        wrong_mode["mode"] = "observer"
        cases.append(wrong_mode)
        missing_pid = state_payload()
        del missing_pid["host_process_id"]
        cases.append(missing_pid)
        boolean_generation = state_payload()
        boolean_generation["acl_generation"] = True
        cases.append(boolean_generation)
        invalid_playback = state_payload()
        invalid_playback["avrcp_pc_playback"] = "buffering"
        cases.append(invalid_playback)
        invalid_pipe = state_payload()
        invalid_pipe["config_pipe"] = "NativeLdac.V1.Config.bad\\pipe"
        cases.append(invalid_pipe)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for index, payload in enumerate(cases):
                path = self._write(root, f"bad-{index}.json", payload)
                with self.assertRaises(ValueError):
                    load_daily_state(path)

    def test_running_presentation_exposes_daily_health(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state = load_daily_state(
                self._write(root, "state.json", state_payload())
            )
            transport = load_daily_transport_result(
                self._write(root, "result.json", transport_payload())
            )
        presentation = derive_daily_presentation(state, transport)
        self.assertEqual(presentation.status, "Native LDAC 正在播放")
        self.assertEqual(presentation.severity, "ok")
        self.assertEqual(presentation.volume, "统一音量已就绪")
        self.assertEqual(presentation.last_bitrate, "1000 kbps")
        self.assertIn("44.1 kHz", presentation.last_format)
        self.assertIn("stereo", presentation.last_format)
        self.assertEqual(presentation.quality, "SQ")

    def test_paused_and_degraded_states_are_distinct(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paused = state_payload()
            paused["render_demand"] = "idle"
            paused["avrcp_pc_playback"] = "paused"
            paused_state = load_daily_state(
                self._write(root, "paused.json", paused)
            )
            failed = state_payload()
            failed["media_failed_events"] = 1
            failed_state = load_daily_state(
                self._write(root, "failed.json", failed)
            )
        self.assertEqual(
            derive_daily_presentation(paused_state).status,
            "已暂停，等待继续播放",
        )
        self.assertEqual(
            derive_daily_presentation(failed_state).severity, "error"
        )

    def test_volume_failsafe_and_hfp_are_visible(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            payload = state_payload()
            payload["avrcp_observer_active"] = False
            payload["hfp_capture_matched"] = True
            payload["hfp_capture_active"] = True
            path = self._write(Path(directory), "state.json", payload)
            presentation = derive_daily_presentation(load_daily_state(path))
        self.assertEqual(presentation.volume, "Windows 增益保护中")
        self.assertEqual(presentation.hfp, "XM5 麦克风正在使用")

    def test_default_and_explicit_paths_are_bounded(self) -> None:
        with mock.patch.dict(os.environ, {}, clear=True):
            state = daily_state_path(local_app_data="C:/Users/Test/AppData/Local")
            result = daily_transport_result_path(
                local_app_data="C:/Users/Test/AppData/Local"
            )
        self.assertEqual(state.name, "daily-state.json")
        self.assertEqual(result.name, "latest-session.json")
        with mock.patch.dict(
            os.environ,
            {
                "NATIVE_LDAC_V1_STATE_PATH": "D:/explicit/state.json",
                "NATIVE_LDAC_V1_RESULT_PATH": "D:/explicit/result.json",
            },
            clear=True,
        ):
            self.assertEqual(
                daily_state_path(), Path("D:/explicit/state.json")
            )
            self.assertEqual(
                daily_transport_result_path(), Path("D:/explicit/result.json")
            )


if __name__ == "__main__":
    unittest.main()
