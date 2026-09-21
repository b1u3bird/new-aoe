#!/usr/bin/env python3
"""对比各地图的早期状态，定位"某张地图开局更难"的具体表现。

info.resources 只含「已探索」的资源，所以早期 food 收入间接反映
视野内有没有可采集的食物点（浆果 / 瞪羚）。stoneRes 同理反映可见石矿数。

用法：python scripts/compare_maps.py <summary.json> <matrix.log>
"""

from __future__ import annotations

import io
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FRAME_RE = re.compile(r"f=(\d+)")


def parse_segments(log_path: Path) -> list[dict]:
    segments: list[dict] = []
    cur: dict | None = None
    for raw in log_path.open(encoding="utf-8", errors="replace"):
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


def field(line: str, key: str) -> str:
    m = re.search(key + r"=(\d+)", line)
    return m.group(1) if m else "-"


def ai_line_at(seg: dict, frame: int) -> str:
    prefix = "[AI] f=%d " % frame
    for line in seg["lines"]:
        if line.startswith(prefix):
            return line
    return ""


def main() -> int:
    summary_path = Path(sys.argv[1])
    log_path = Path(sys.argv[2])
    results = json.loads(summary_path.read_text(encoding="utf-8"))["results"]
    segments = parse_segments(log_path)

    print("段数 %d / summary %d\n" % (len(segments), len(results)))
    header = "%-22s %-7s %-7s %-7s %-7s %-8s %-8s %-8s %s" % (
        "map/rot", "f2k食", "f4k食", "f6k食", "f8k食", "2k农民", "2k石", "12k农场",
        "首个非0 enemyA")
    print(header)
    print("-" * len(header))

    for seg, res in zip(segments, results):
        name = Path(res["map_file"]).stem
        label = "%s r%s" % (name, res["rotation"])

        first_nonzero = "-"
        for line in seg["lines"]:
            if not line.startswith("[AI] "):
                continue
            val = field(line, "enemyA")
            if val not in ("0", "-"):
                first_nonzero = "%s:%s" % (line.split()[1], val)
                break

        print("%-22s %-7s %-7s %-7s %-7s %-8s %-8s %-8s %s" % (
            label,
            field(ai_line_at(seg, 2000), "food"),
            field(ai_line_at(seg, 4000), "food"),
            field(ai_line_at(seg, 6000), "food"),
            field(ai_line_at(seg, 8000), "food"),
            field(ai_line_at(seg, 2000), "farmers"),
            field(ai_line_at(seg, 2000), "stoneRes"),
            field(ai_line_at(seg, 12000), "farm"),
            first_nonzero))
    return 0


if __name__ == "__main__":
    sys.exit(main())
