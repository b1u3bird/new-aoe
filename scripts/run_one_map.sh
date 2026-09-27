#!/bin/bash
# 并行跑【一张地图】的若干旋转，每局独占自己的日志文件。
#
# 【为什么用环境变量而不是各自的工作目录】游戏要从 cwd 读 config.json / res.rcc
# 等资源，把 cwd 换成各自的目录会让它在初始化阶段直接 segfault（实测崩在
# initPlayers）。而两个日志文件又都是相对 cwd 的路径：
#     UsrAI.cpp : fopen("ai_debug.log", "a")
#     Logger.cpp: setFileName("./GameLog.log")
# 多个实例共用同一个 cwd 会把它们交织进同一份文件、事后没法读。
#
# 所以两边各加了一个环境变量开关（AOE_AI_LOG / AOE_GAME_LOG），cwd 保持项目根
# 不动，只让每个实例把日志写到自己的目录里。
#
# 用法:
#   bash scripts/run_one_map.sh                    # 默认 map.njust，跑 4 个旋转
#   bash scripts/run_one_map.sh map2.njust         # 指定地图
#   bash scripts/run_one_map.sh map2.njust 0 180   # 只跑指定旋转
#
# 产物（每局一套，互不干扰）:
#   .ai-eval/one-map/map2-r0/ai_debug.log    AI 调试日志（[SIEGE]/[PRIEST]/[FARMESC]...）
#   .ai-eval/one-map/map2-r0/GameLog.log     游戏自己的日志
#   .ai-eval/one-map/map2-r0/result.jsonl    该局结果，最后一行是胜负
#   .ai-eval/one-map/map2-r0/stdout.log      标准输出与错误
#
# 例：看某局的祭司为什么没转成
#   grep -E "\[SIEGE\]|\[CONVBLOCK\]|\[CONV\]" .ai-eval/one-map/map2-r0/ai_debug.log | tail -40

set -u
cd "$(dirname "$0")/.." || exit 1

MAP="${1:-map.njust}"
if [ $# -gt 0 ]; then shift; fi
ROTATIONS=("$@")
if [ ${#ROTATIONS[@]} -eq 0 ]; then
    ROTATIONS=(0 90 180 270)
fi

EXE="$(pwd)/debug/newAOE.exe"
MAPABS="$(cd "$(dirname "$MAP")" && pwd)/$(basename "$MAP")"
OUT=".ai-eval/one-map"
OUTABS="$(pwd)/$OUT"
TAG="$(basename "$MAP" .njust)"

if [ ! -f "$EXE" ]; then
    echo "找不到 exe: $EXE（先编译: mingw32-make -f Makefile.Debug）"
    exit 1
fi
if [ ! -f "$MAPABS" ]; then
    echo "找不到地图: $MAPABS"
    exit 1
fi

echo "地图 : $MAPABS"
echo "旋转 : ${ROTATIONS[*]}"
echo "产物 : $OUT/${TAG}-r<旋转>/"
echo

PIDS=()
for R in "${ROTATIONS[@]}"; do
    D="$OUTABS/${TAG}-r$R"
    mkdir -p "$D"
    rm -f "$D/result.jsonl" "$D/ai_debug.log" "$D/GameLog.log" "$D/stdout.log"
    # cwd 保持项目根（游戏要读 config.json / res.rcc），
    # 只把两份日志和结果文件指到各自的目录。
    AOE_AI_LOG="$D/ai_debug.log" \
    AOE_GAME_LOG="$D/GameLog.log" \
    timeout 600 "$EXE" --exam --offscreen --freq MAX \
        --map "$MAPABS" --rotate "$R" \
        --ResultLogFile "$D/result.jsonl" \
        > "$D/stdout.log" 2>&1 &
    PIDS+=($!)
done

echo "已启动 ${#PIDS[@]} 个实例: ${PIDS[*]}"
echo "运行中……（可 tail -f $OUT/${TAG}-r0/ai_debug.log 实时看）"
wait

echo
echo "=== 结果 ==="
for R in "${ROTATIONS[@]}"; do
    F="$OUT/${TAG}-r$R/result.jsonl"
    if [ -s "$F" ]; then
        python -c "
import json
line = open(r'$F', encoding='utf-8', errors='replace').read().strip().split('\n')[-1]
d = json.loads(line)
print('r%-4s %-5s frame=%-6s score=%-4s food=%-4s wood=%-4s stone=%-4s'
      % ('$R', 'WIN' if d.get('win') else 'LOSE', d.get('frame'),
         d.get('score'), d.get('food'), d.get('wood'), d.get('stone')))
" 2>/dev/null || echo "r$R   结果解析失败"
    else
        echo "r$R   无结果（崩溃 / 被 timeout 切断）"
    fi
done
echo
echo "每局日志: $OUT/${TAG}-r<旋转>/ai_debug.log"
