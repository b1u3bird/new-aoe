#!/bin/bash
# 同一张地图的 4 个旋转：16 倍速、有画面、并行。
#
#   bash scripts/run_one_map.sh            # map.njust
#   bash scripts/run_one_map.sh map2.njust
#
# 【地图路径必须保持相对路径 —— 这是这个脚本踩过的坑】
# 游戏是 Windows 程序，而 bash 的 pwd 给出的是 /d/Code/new-aoe 这种 MSYS 格式。
# 早期版本在这里把 --map 拼成了绝对路径，游戏拿到后报
#     Map.cpp | loadGenerateMapText | fixed map file not found: "/mnt/d/..."
# 然后退化成空地图 —— 表现就是【窗口正常弹出、画面全黑】，很难往路径上想。
# 所以这里一律用调用者给进来的相对路径，绝不拼绝对路径。
# 同理，日志路径也用相对 cwd 的写法（下面 D 变量）。
#
# 用法示例：
#   bash scripts/run_one_map.sh map2.njust     # 在项目根执行，map2.njust 就在根目录

cd "$(dirname "$0")/.." || exit 1

INPUT="${1:-map.njust}"
TAG="$(basename "$INPUT" .njust)"

if [ ! -f "$INPUT" ]; then
    echo "找不到地图: $INPUT（要相对于项目根，例如 map2.njust）"
    exit 1
fi

for R in 0 90 180 270; do
    # 相对 cwd 的相对路径：Windows 程序认得，不需要 cygpath 转换。
    D=".ai-eval/one-map/${TAG}-r$R"
    mkdir -p "$D"
    AOE_AI_LOG="$D/ai_debug.log" \
    AOE_GAME_LOG="$D/GameLog.log" \
        ./debug/newAOE.exe --freq 16 --map "$INPUT" --rotate "$R" &
done

wait
echo ">>> 4 个旋转都跑完了"
