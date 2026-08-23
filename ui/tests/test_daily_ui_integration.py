from __future__ import annotations

from pathlib import Path
import unittest


UI_ROOT = Path(__file__).resolve().parents[1]
PROJECT_ROOT = UI_ROOT.parent


class DailyUiIntegrationTests(unittest.TestCase):
    def test_daily_adapter_remains_read_only(self) -> None:
        source = (UI_ROOT / "daily_state.py").read_text(encoding="utf-8")
        for forbidden in (
            "SetEvent",
            "update_config",
            "subprocess",
            "WinDLL",
            "pnputil",
            "SetDefaultEndpoint",
            "DeviceIoControl",
        ):
            self.assertNotIn(forbidden, source)

    def test_ui_prioritizes_daily_host_and_disables_controls(self) -> None:
        source = (UI_ROOT / "ldac_control.py").read_text(encoding="utf-8")
        for required in (
            '"v1_presence_agent.exe"',
            "self.daily_mode = True",
            'text="后台常驻运行", state="disabled"',
            'text="后台独立运行", state="disabled"',
            "if self.daily_mode or self.agent_mode:",
            "derive_daily_presentation",
        ):
            self.assertIn(required, source)

    def test_product_bundle_contains_daily_ui_adapter(self) -> None:
        builder = (
            PROJECT_ROOT / "tools" / "build-v1-daily-host-candidate.ps1"
        ).read_text(encoding="utf-8")
        for required in (
            "run-v1-daily-ui.ps1",
            "ldac_control.py",
            "agent_config.py",
            "daily_state.py",
            "probe_protocol.py",
        ):
            self.assertIn(required, builder)

    def test_quality_ipc_is_bounded_and_not_a_general_command_channel(self) -> None:
        source = (UI_ROOT / "daily_config_ipc.py").read_text(encoding="utf-8")
        for required in (
            "PIPE_NAME_PATTERN",
            "QUALITY_CODES",
            "REQUEST.size",
            "CallNamedPipeW",
        ):
            self.assertIn(required, source)
        for forbidden in ("subprocess", "shell=True", "pnputil", "DeviceIoControl"):
            self.assertNotIn(forbidden, source)


if __name__ == "__main__":
    unittest.main()
