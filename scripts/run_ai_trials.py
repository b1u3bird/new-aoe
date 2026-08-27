#!/usr/bin/env python3
"""Run repeatable newAOE AI trials and summarize JSONL results."""

from __future__ import annotations

import argparse
import json
import statistics
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable


DEFAULT_ROTATIONS = (0, 90, 180, 270)


@dataclass
class TrialResult:
    commit: str
    map_file: str
    rotation: int
    trial: int
    exit_code: int | None
    timed_out: bool
    elapsed_seconds: float
    win: bool | None
    score: int | None
    frame: int | None
    wood: int | None
    food: int | None
    stone: int | None
    gold: int | None
    result_file: str
    message: str | None = None
    error: str | None = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "maps",
        nargs="+",
        type=Path,
        help="Fixed .njust map files to test.",
    )
    parser.add_argument(
        "--exe",
        type=Path,
        default=Path("debug/newAOE.exe"),
        help="Path to newAOE executable (default: debug/newAOE.exe).",
    )
    parser.add_argument(
        "--rotations",
        nargs="+",
        type=int,
        default=list(DEFAULT_ROTATIONS),
        choices=DEFAULT_ROTATIONS,
        help="Map rotations to test.",
    )
    parser.add_argument(
        "--trials",
        type=int,
        default=1,
        help="Number of repetitions per map and rotation (default: 1).",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=180.0,
        help="Per-process timeout in seconds (default: 180).",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path(".ai-eval/latest"),
        help="Directory for raw logs and summary files.",
    )
    return parser.parse_args()


def read_commit() -> str:
    try:
        completed = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return "unknown"
    return completed.stdout.strip() or "unknown"


def read_last_json_line(path: Path) -> dict[str, Any] | None:
    if not path.exists():
        return None

    last_record: dict[str, Any] | None = None
    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            line = line.strip()
            if not line:
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(record, dict):
                if last_record is None or record.get("msg") is not None:
                    last_record = record
                elif last_record.get("msg") is None:
                    last_record = record
    return last_record


def run_trial(
    executable: Path,
    map_file: Path,
    rotation: int,
    trial: int,
    timeout_seconds: float,
    output_dir: Path,
    commit: str,
) -> TrialResult:
    stem = f"{map_file.stem}-r{rotation}-t{trial}"
    result_path = output_dir / f"{stem}.jsonl"
    stdout_path = output_dir / f"{stem}.stdout.log"
    stderr_path = output_dir / f"{stem}.stderr.log"
    result_path.unlink(missing_ok=True)

    command = [
        str(executable.resolve()),
        "--exam",
        "--offscreen",
        "--freq",
        "MAX",
        "--map",
        str(map_file.resolve()),
        "--rotate",
        str(rotation),
        "--ResultLogFile",
        str(result_path.resolve()),
    ]

    started = time.monotonic()
    exit_code: int | None = None
    timed_out = False
    error: str | None = None
    try:
        with stdout_path.open("w", encoding="utf-8") as stdout_stream, stderr_path.open(
            "w", encoding="utf-8"
        ) as stderr_stream:
            completed = subprocess.run(
                command,
                stdout=stdout_stream,
                stderr=stderr_stream,
                timeout=timeout_seconds,
                check=False,
            )
            exit_code = completed.returncode
    except subprocess.TimeoutExpired:
        timed_out = True
        error = f"process exceeded {timeout_seconds:g}s timeout"
    except OSError as exc:
        error = str(exc)

    elapsed = time.monotonic() - started
    record = read_last_json_line(result_path)
    if record is None and error is None:
        error = "result log contains no valid JSON object"

    return TrialResult(
        commit=commit,
        map_file=str(map_file),
        rotation=rotation,
        trial=trial,
        exit_code=exit_code,
        timed_out=timed_out,
        elapsed_seconds=round(elapsed, 3),
        win=record.get("win") if record else None,
        score=record.get("score") if record else None,
        frame=record.get("frame") if record else None,
        wood=record.get("wood") if record else None,
        food=record.get("food") if record else None,
        stone=record.get("stone") if record else None,
        gold=record.get("gold") if record else None,
        result_file=str(result_path),
        message=record.get("msg") if record else None,
        error=error,
    )


def numeric_values(results: Iterable[TrialResult], field: str) -> list[int]:
    values: list[int] = []
    for result in results:
        value = getattr(result, field)
        if isinstance(value, int) and not isinstance(value, bool):
            values.append(value)
    return values


def summarize(results: list[TrialResult]) -> dict[str, Any]:
    valid = [result for result in results if result.win is not None]
    wins = [result for result in valid if result.win]
    scores = numeric_values(wins, "score")
    frames = numeric_values(wins, "frame")

    return {
        "runs": len(results),
        "valid_runs": len(valid),
        "wins": len(wins),
        "win_rate": len(wins) / len(results) if results else 0.0,
        "win_score_average": statistics.fmean(scores) if scores else None,
        "win_score_worst": min(scores) if scores else None,
        "win_frame_average": statistics.fmean(frames) if frames else None,
        "win_frame_worst": max(frames) if frames else None,
        "timeouts": sum(result.timed_out for result in results),
        "errors": sum(result.error is not None for result in results),
    }


def main() -> int:
    args = parse_args()
    if args.trials < 1:
        raise SystemExit("--trials must be at least 1")
    if args.timeout <= 0:
        raise SystemExit("--timeout must be positive")
    if not args.exe.is_file():
        raise SystemExit(f"executable not found: {args.exe}")
    for map_file in args.maps:
        if not map_file.is_file():
            raise SystemExit(f"map not found: {map_file}")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    commit = read_commit()
    results: list[TrialResult] = []

    total = len(args.maps) * len(args.rotations) * args.trials
    run_number = 0
    for map_file in args.maps:
        for rotation in args.rotations:
            for trial in range(1, args.trials + 1):
                run_number += 1
                print(
                    f"[{run_number}/{total}] {map_file} rotate={rotation} trial={trial}",
                    flush=True,
                )
                result = run_trial(
                    args.exe,
                    map_file,
                    rotation,
                    trial,
                    args.timeout,
                    args.output_dir,
                    commit,
                )
                results.append(result)
                print(
                    f"  win={result.win} score={result.score} frame={result.frame} "
                    f"reason={result.message} exit={result.exit_code} "
                    f"timeout={result.timed_out}",
                    flush=True,
                )

    summary = summarize(results)
    report = {
        "commit": commit,
        "comparison_order": [
            "win_rate_desc",
            "win_score_average_desc",
            "win_score_worst_desc",
            "win_frame_average_asc",
            "win_frame_worst_asc",
        ],
        "summary": summary,
        "results": [asdict(result) for result in results],
    }
    report_path = args.output_dir / "summary.json"
    report_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    print(f"report: {report_path}")

    return 0 if summary["valid_runs"] == summary["runs"] else 1


if __name__ == "__main__":
    sys.exit(main())
