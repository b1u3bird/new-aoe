#!/usr/bin/env python3
"""按局统计祭司的阵亡、移动与撤退目标抖动。

用法：python scripts/analyze_run.py <log 起点字节> [日志路径]
偏移之后的日志会被切成若干局（帧号回退为界）逐局统计。
"""

from __future__ import annotations

import io
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FRAME_RE = re.compile(r"f=(\d+)")


def main() -> int:
    offset = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    log_path = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "ai_debug.log"

    segments: list[dict] = []
    cur: dict | None = None
    with log_path.open("rb") as fh:
        fh.seek(offset)
        for raw in io.TextIOWrapper(fh, encoding="utf-8", errors="replace"):
            m = FRAME_RE.search(raw)
            if not m:
                continue
            frame = int(m.group(1))
            # 帧号小幅回退只是日志写入交错，不算新局；只有大幅回退（帧号重启）
            # 才是新一局。阈值取 500，远大于单帧内的交错幅度。
            if cur is None or frame < cur["last"] - 500:
                cur = {"last": frame, "lines": []}
                segments.append(cur)
            cur["last"] = frame
            cur["lines"].append(raw.rstrip())

    header = "%-5s %-8s %-8s %-8s %-9s %-9s %s" % (
        "局", "终帧", "阵亡帧", "阵亡血", "mvTgt种类", "mvTgt切换", "祭司移动格数(累计曼哈顿)")
    print(header)
    print("-" * len(header))

    for i, seg in enumerate(segments):
        death_frame, death_hp = "-", "-"
        for idx, ln in enumerate(seg["lines"]):
            if "PRIEST_LOST" in ln:
                death_frame = FRAME_RE.search(ln).group(1)
                for j in range(idx - 1, max(0, idx - 200), -1):
                    m = re.search(r"\[PRIEST\] f=\d+ hp=(\d+)/", seg["lines"][j])
                    if m:
                        death_hp = m.group(1)
                        break
                break

        targets: list[tuple[str, str]] = []
        positions: list[tuple[int, int]] = []
        for ln in seg["lines"]:
            if ln.startswith("[THREAT]"):
                m = re.search(r"mvTgt=\((-?\d+),(-?\d+)\)", ln)
                if m:
                    targets.append((m.group(1), m.group(2)))
            elif ln.startswith("[PRIEST]"):
                m = re.search(r"pos=\((\d+),(\d+)\)", ln)
                if m:
                    positions.append((int(m.group(1)), int(m.group(2))))

        distinct = len(set(targets))
        switches = sum(1 for a, b in zip(targets, targets[1:]) if a != b)
        travel = sum(
            abs(b[0] - a[0]) + abs(b[1] - a[1])
            for a, b in zip(positions, positions[1:])
        )
        print("%-5d %-8d %-8s %-8s %-9d %-9d %d" % (
            i + 1, seg["last"], death_frame, death_hp, distinct, switches, travel))
    return 0


if __name__ == "__main__":
    sys.exit(main())
