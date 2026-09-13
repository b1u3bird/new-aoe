#!/usr/bin/env python3
"""一键测试四个地图（map/map1/map2/map3）及四种旋转（0/90/180/270）。

直接复用 scripts/run_ai_trials.py，仅把四个地图作为默认参数固化下来，
避免每次手动拼地图文件名。其余参数（--trials/--timeout/--exe/--output-dir）
原样透传给 run_ai_trials.py。

用法：
    python scripts/test_all.py                 # 默认：4 地图 x 4 旋转，各 1 次
    python scripts/test_all.py --trials 3      # 每个组合重复 3 次
    python scripts/test_all.py --timeout 300   # 单局超时 300 秒
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAPS = ["map.njust", "map1.njust", "map2.njust", "map3.njust"]
RUNNER = ROOT / "scripts" / "run_ai_trials.py"


def main() -> int:
    if not RUNNER.is_file():
        raise SystemExit(f"runner not found: {RUNNER}")

    maps = [str(ROOT / name) for name in MAPS]
    command = [
        sys.executable,
        str(RUNNER),
        *maps,
        *sys.argv[1:],  # 透传额外参数
    ]
    return subprocess.call(command, cwd=str(ROOT))


if __name__ == "__main__":
    sys.exit(main())
