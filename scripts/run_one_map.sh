#!/bin/bash
# 同一张地图的 4 个旋转：16 倍速、有画面、并行。
#
#   bash scripts/run_one_map.sh            # map.njust
#   bash scripts/run_one_map.sh map2.njust
#
# 【--softgl 是必须的】多个实例并行时，几个 OpenGL 窗口争抢同一个 GPU 上下文，
# 被遮挡的那些会停止重绘、整块变黑 —— 实测单实例正常，只要并行就黑
# （2 个和 4 个都一样）。这个参数让游戏改走软件渲染，各画各的，代价是渲染
# 改由 CPU 承担、会慢一些。
# 它由 main.cpp 手工扫描 argv 生效（Qt 要求渲染后端必须在 QApplication 构造
# 之前定下，用不了 QCommandLineParser）；在 GlobalVariate.cpp 里注册它只是为了
# 不被 parser.process() 判成未知选项而直接退出。
#
# 日志分流：4 个实例 cwd 相同，而两份日志都是相对 cwd 的路径
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
    AOE_AI_LOG="$D/ai_debug.log" \
    AOE_GAME_LOG="$D/GameLog.log" \
        ./debug/newAOE.exe --softgl --freq 16 --map "$MAP" --rotate "$R" &
done

wait
echo ">>> 4 个旋转都跑完了"
