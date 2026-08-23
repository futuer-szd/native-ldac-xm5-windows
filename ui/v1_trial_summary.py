"""Read-only CLI for explicitly selected V1 trial result files."""

from __future__ import annotations

import argparse
from dataclasses import asdict
import json
from pathlib import Path
import sys
from typing import Sequence

from agent_config import (
    V1TrialComparison,
    compare_v1_trial_results,
    format_v1_trial_comparison,
    format_v1_trial_summary,
)


def _explicit_result_paths(values: Sequence[str]) -> tuple[Path, ...]:
    paths = []
    for value in values:
        if any(character in value for character in "*?[]"):
            raise ValueError("wildcards are not allowed; provide result.json paths")
        path = Path(value)
        if path.name.lower() != "result.json":
            raise ValueError("every explicit path must name result.json")
        paths.append(path)
    return tuple(paths)


def _telemetry_json(telemetry: object) -> dict[str, object]:
    values = asdict(telemetry)
    return {
        key: value
        for key, value in values.items()
        if value is not None and value != ()
    }


def _comparison_json(comparison: V1TrialComparison) -> dict[str, object]:
    return {
        "schema_version": 1,
        "result_count": len(comparison.results),
        "passed_count": comparison.passed_count,
        "failed_count": comparison.failed_count,
        "unknown_count": comparison.unknown_count,
        "graceful_stop_actions": comparison.graceful_stop_actions,
        "cancel_actions": comparison.cancel_actions,
        "minimum_media_duration_ms": comparison.minimum_media_duration_ms,
        "maximum_media_duration_ms": comparison.maximum_media_duration_ms,
        "stop_reasons": comparison.stop_reasons,
        "lifecycle_outcomes": comparison.lifecycle_outcomes,
        "all_final_attempts_archived": (
            comparison.all_final_attempts_archived
        ),
        "all_resources_released": comparison.all_resources_released,
        "summary": format_v1_trial_comparison(comparison),
        "results": [
            {
                "path": str(result.path),
                "transport_passed": result.transport_passed,
                "summary": format_v1_trial_summary(result),
                "telemetry": _telemetry_json(result.telemetry),
            }
            for result in comparison.results
        ],
    }


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Summarize explicitly selected V1 result.json files without "
            "directory or system discovery."
        )
    )
    parser.add_argument(
        "paths",
        nargs="+",
        metavar="RESULT_JSON",
        help="explicit path to one result.json; repeat for comparison",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="emit stable JSON instead of text",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parser().parse_args(argv)
    try:
        paths = _explicit_result_paths(arguments.paths)
        comparison = compare_v1_trial_results(paths)
    except (OSError, TypeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    if arguments.json:
        print(json.dumps(
            _comparison_json(comparison),
            ensure_ascii=False,
            indent=2,
            sort_keys=True,
        ))
        return 0
    if len(comparison.results) == 1:
        print(format_v1_trial_summary(comparison.results[0]))
        return 0
    print(format_v1_trial_comparison(comparison))
    for index, result in enumerate(comparison.results, start=1):
        print(f"[{index}] {result.path}")
        print(f"    {format_v1_trial_summary(result)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
