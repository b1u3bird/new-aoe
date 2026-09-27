#!/bin/bash
# 同一张地图的 4 个旋转：16 倍速、有画面。
#
#   bash scripts/run_one_map.sh            # map.njust
#   bash scripts/run_one_map.sh map2.njust
#
# 【为什么分两批、每批 2 个】4 个 OpenGL 窗口同时跑时，被遮挡的那些会停止
# 重绘、整块变黑 —— 实测单实例正常、2 个并行正常、4 个并行全黑。
# 每批 2 个是这台机器上的稳定值；两批依次跑完，仍然是同一张图的 4 个旋转。
# 想一口气开 4 个的话，得让游戏走软件渲染（改 main.cpp 的 OpenGL 属性）
# 才绕得开 GPU 上下文争用。
#
# 日志分流（唯一保留的额外东西）：4 个实例的 cwd 相同，而两份日志都是
# 相对 cwd 的路径（UsrAI.cpp 的 fopen("ai_debug.log")、Logger.cpp 的
# "./GameLog.log"），不分开会交织进同一份文件；cwd 又不能逐个换，因为游戏
# 要从 cwd 读 config.json / res.rcc。不需要就直接删掉下面两行 AOE_*_LOG。

cd "$(dirname "$0")/.." || exit 1

INPUT="${1:-map.njust}"
MAP="$(cd "$(dirname "$INPUT")" && pwd)/$(basename "$INPUT")"
TAG="$(basename "$MAP" .njust)"

launch() {
    local R="$1"
    local D="$(pwd)/.ai-eval/one-map/${TAG}-r$R"
    mkdir -p "$D"
    AOE_AI_LOG="$D/ai_debug.log" \
    AOE_GAME_LOG="$D/GameLog.log" \
        ./debug/newAOE.exe --freq 16 --map "$MAP" --rotate "$R" &
}

for PAIR in "0 90" "180 270"; do
    echo ">>> 启动旋转: $PAIR"
    for R in $PAIR; do
        launch "$R"
    done
    wait
done

echo ">>> 4 个旋转都跑完了"
