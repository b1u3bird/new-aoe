#!/usr/bin/env python3
"""按局统计「升到铜器时代」的帧号，用于对比各地图的前期节奏。

波次时间：第一波 6000 / 第二波 13500 / 第三波 21000（enemyai.cpp）。
若某地图普遍在第二波之后才升到铜器，就会被第二波按时打一波空档。

用法：python scripts/civ_timing.py <summary.json> <log>
"""

from __future__ import annotations

import io
import json
import re
import sys
from pathlib import Path

FRAME_RE = re.compile(r"f=(\d+)")
CIV_RE = re.compile(r"civ=(\d+)")


def main() -> int:
    results = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))["results"]
    log_path = Path(sys.argv[2])

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

    header = "%-22s %-12s %-12s %-14s %s" % (
        "地图/旋转", "升铜器帧", "第二波13500时civ", "第三波21000时civ", "阵亡帧")
    print(header)
    print("-" * len(header))

    for seg, res in zip(segments, results):
        name = Path(res["map_file"]).stem
        label = "%s r%s" % (name, res["rotation"])

        bronze_frame = "-"
        civ_at_sat = civ_at_tat = "-"
        for line in seg["lines"]:
            if not line.startswith("[AI] "):
                continue
            frame = int(FRAME_RE.search(line).group(1))
            civ = int(CIV_RE.search(line).group(1))
            if bronze_frame == "-" and civ >= 3:
                bronze_frame = str(frame)
            if frame >= 13500 and civ_at_sat == "-":
                civ_at_sat = str(civ)
            if frame >= 21000 and civ_at_tat == "-":
                civ_at_tat = str(civ)

        death = "-"
        for line in seg["lines"]:
            if "PRIEST_LOST" in line:
                death = FRAME_RE.search(line).group(1)
                break

        print("%-22s %-12s %-12s %-14s %s" % (
            label, bronze_frame, civ_at_sat, civ_at_tat, death))
    return 0


if __name__ == "__main__":
    sys.exit(main())
