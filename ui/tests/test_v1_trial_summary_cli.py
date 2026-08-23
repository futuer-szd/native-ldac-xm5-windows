from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from v1_trial_summary import main


class V1TrialSummaryCliTests(unittest.TestCase):
    def _result(
        self, root: Path, name: str, payload: dict[str, object]
    ) -> Path:
        directory = root / name
        directory.mkdir()
        path = directory / "result.json"
        path.write_text(json.dumps(payload), encoding="utf-8")
        return path

    def _run(self, arguments: list[str]) -> tuple[int, str, str]:
        stdout = io.StringIO()
        stderr = io.StringIO()
        with redirect_stdout(stdout), redirect_stderr(stderr):
            code = main(arguments)
        return code, stdout.getvalue(), stderr.getvalue()

    def test_single_file_text_output(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = self._result(Path(directory), "normal", {
                "schema_version": 1,
                "transport_passed": True,
                "stop_reason": "render-stop",
                "resources_released": True,
                "lifecycle_outcome": "graceful-stop",
            })
            code, stdout, stderr = self._run([str(path)])
            self.assertEqual(code, 0)
            self.assertEqual(stderr, "")
            self.assertEqual(
                stdout,
                "V1 trial · transport passed · stop render-stop · "
                "resources released yes · outcome graceful-stop\n",
            )

    def test_multiple_files_stable_text_and_json(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            normal = self._result(root, "normal", {
                "schema_version": 1,
                "transport_passed": True,
                "stop_reason": "render-stop",
                "media_duration_ms": 10000,
            })
            fault = self._result(root, "fault", {
                "schema_version": 1,
                "transport_passed": False,
                "stop_reason": "fault",
                "media_duration_ms": 500,
            })
            code, stdout, stderr = self._run([str(normal), str(fault)])
            self.assertEqual(code, 0)
            self.assertEqual(stderr, "")
            lines = stdout.splitlines()
            self.assertIn("transport P/F/? 1/1/0", lines[0])
            self.assertIn("media 500–10000 ms", lines[0])
            self.assertEqual(lines[1], f"[1] {normal}")
            self.assertEqual(lines[3], f"[2] {fault}")

            code, stdout, stderr = self._run([
                "--json", str(normal), str(fault)
            ])
            self.assertEqual(code, 0)
            self.assertEqual(stderr, "")
            payload = json.loads(stdout)
            self.assertEqual(payload["schema_version"], 1)
            self.assertEqual(payload["result_count"], 2)
            self.assertEqual(payload["passed_count"], 1)
            self.assertEqual(payload["failed_count"], 1)
            self.assertEqual(
                [item["path"] for item in payload["results"]],
                [str(normal), str(fault)],
            )

    def test_invalid_legacy_duplicate_and_wildcard_exit_one(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            valid = self._result(root, "valid", {
                "schema_version": 1,
                "transport_passed": True,
            })
            legacy = self._result(root, "legacy", {
                "version": 2,
                "state": "stopped",
            })
            invalid = self._result(root, "invalid", {
                "schema_version": 1,
                "transport_passed": "yes",
            })
            cases = (
                [str(legacy)],
                [str(invalid)],
                [str(valid), str(valid)],
                [str(root / "*" / "result.json")],
                [str(root)],
            )
            for arguments in cases:
                with self.subTest(arguments=arguments):
                    code, stdout, stderr = self._run(arguments)
                    self.assertEqual(code, 1)
                    self.assertEqual(stdout, "")
                    self.assertTrue(stderr.startswith("error: "))

    def test_missing_required_path_uses_argparse_exit_two(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()
        with redirect_stdout(stdout), redirect_stderr(stderr):
            with self.assertRaises(SystemExit) as context:
                main([])
        self.assertEqual(context.exception.code, 2)
        self.assertIn("RESULT_JSON", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
