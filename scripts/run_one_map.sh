#!/bin/bash
# 同一张地图的 4 个旋转：16 倍速、有画面。
#
#   bash scripts/run_one_map.sh            # map.njust
#   bash scripts/run_one_map.sh map2.njust
#
# 【为什么要错开启动，而不是一次全拉起来】4 个实例同时启动时，几个窗口会拿到
# 同一个位置、完全叠在一起；被遮挡的那些停止重绘，看起来就是黑的。
# 手动一个一个开就没这个问题 —— 因为窗口是错开的。
# 所以这里每拉起一个就等几秒，让前一个窗口完成创建与首次绘制再拉下一个，
# 窗口位置自然就错开了。等全部起来之后它们仍然是并行跑的。
#
# 日志分流：4 个实例的 cwd 相同，而两份日志都是相对 cwd 的路径
# （UsrAI.cpp 的 fopen("ai_debug.log")、Logger.cpp 的 "./GameLog.log"），
# 不分开会交织进同一份文件；cwd 又不能逐个换 —— 游戏要从 cwd 读
# config.json / res.rcc。不需要就直接删掉下面两行 AOE_*_LOG。

cd "$(dirname "$0")/.." || exit 1

INPUT="${1:-map.njust}"
MAP="$(cd "$(dirname "$INPUT")" && pwd)/$(basename "$INPUT")"
TAG="$(basename "$MAP" .njust)"

for R in 0 90 180 270; do
    D="$(pwd)/.ai-eval/one-map/${TAG}-r$R"
    mkdir -p "$D"
    echo ">>> 启动 rotation=$R"
    AOE_AI_LOG="$D/ai_debug.log" \
    AOE_GAME_LOG="$D/GameLog.log" \
        ./debug/newAOE.exe --freq 16 --map "$MAP" --rotate "$R" &
    sleep 3
done

wait
echo ">>> 4 个旋转都跑完了"
