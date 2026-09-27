#!/bin/bash
# 同一张地图的 4 个旋转：并行、16 倍速、有画面。
#
#   bash scripts/run_one_map.sh            # map.njust
#   bash scripts/run_one_map.sh map2.njust
#
# 会同时弹 4 个窗口（rotation 0 / 90 / 180 / 270）。
#
# 唯一保留的东西是日志分流：4 个实例的 cwd 相同，而两份日志都是相对 cwd 的
# 路径（UsrAI.cpp 的 fopen("ai_debug.log")、Logger.cpp 的 "./GameLog.log"），
# 不分开就会交织进同一份文件。cwd 又不能逐个换 —— 游戏要从 cwd 读
# config.json / res.rcc，换了直接起不来。所以靠两个环境变量把日志指到
# .ai-eval/one-map/<图>-r<旋转>/ 下。不需要就直接删掉那两行 AOE_*_LOG。

cd "$(dirname "$0")/.." || exit 1

INPUT="${1:-map.njust}"
MAP="$(cd "$(dirname "$INPUT")" && pwd)/$(basename "$INPUT")"

for R in 0 90 180 270; do
    D="$(pwd)/.ai-eval/one-map/$(basename "$MAP" .njust)-r$R"
    mkdir -p "$D"
    AOE_AI_LOG="$D/ai_debug.log" \
    AOE_GAME_LOG="$D/GameLog.log" \
        ./debug/newAOE.exe --freq 16 --map "$MAP" --rotate "$R" &
done

wait
