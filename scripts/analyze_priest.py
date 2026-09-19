#!/usr/bin/env python3
"""按局解析 ai_debug.log，汇总祭司阵亡帧与临终状态。

ai_debug.log 会在多次运行间累加，因此用「帧号回退」作为新一局的起点，
再与 run_ai_trials.py 产出的 summary.json 按顺序对应（两者都是运行顺序）。
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FRAME_RE = re.compile(r"f=(\d+)")


def parse_segments(log_path: Path) -> list[dict]:
    segments: list[dict] = []
    cur: dict | None = None
    with log_path.open(encoding="utf-8", errors="replace") as fh:
        for raw in fh:
            m = FRAME_RE.search(raw)
            if not m:
                continue
            frame = int(m.group(1))
            if cur is None or frame < cur["last"]:
                cur = {"last": frame, "lines": []}
                segments.append(cur)
            cur["last"] = frame
            cur["lines"].append(raw.rstrip())
    return segments


def field(line: str | None, name: str) -> str:
    if not line:
        return "-"
    m = re.search(name + r"=(\d+)", line)
    return m.group(1) if m else "-"


def main() -> int:
    summary_path = (Path(sys.argv[1]) if len(sys.argv) > 1
                    else ROOT / ".ai-eval" / "matrix-observ" / "summary.json")
    log_path = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "ai_debug.log"

    segments = parse_segments(log_path)
    results = json.loads(summary_path.read_text(encoding="utf-8"))["results"]

    print(f"解析出 {len(segments)} 局，summary 有 {len(results)} 局\n")
    header = ("%-22s %-7s %-7s %-6s %-7s %-7s %-7s %s" %
              ("map/rot", "终帧", "祭司阵亡", "阵亡帧", "临终血", "minDis", "target", "备注"))
    print(header)
    print("-" * len(header))

    for i, seg in enumerate(segments):
        res = results[i] if i < len(results) else {}
        name = Path(res.get("map_file", "?")).stem
        label = f"{name} r{res.get('rotation', '?')}"

        death_frame = None
        last_priest = None
        for ln in seg["lines"]:
            if "[PRIEST]" in ln:
                last_priest = ln
            if "PRIEST_LOST" in ln or "CENTER_LOST" in ln:
                death_frame = FRAME_RE.search(ln).group(1)

        if res.get("timed_out"):
            note = "timeout"
        elif res.get("exit_code") not in (0, None):
            note = f"exit={res.get('exit_code')}"
        else:
            note = ""

        print("%-22s %-7s %-7s %-6s %-7s %-7s %-7s %s" % (
            label, seg["last"], "是" if death_frame else "否", death_frame or "-",
            field(last_priest, "hp"), field(last_priest, "minDis"),
            field(last_priest, "target"), note))

    return 0


if __name__ == "__main__":
    sys.exit(main())
