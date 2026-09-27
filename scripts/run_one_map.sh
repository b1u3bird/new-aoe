#!/bin/bash
# 跑【一张地图】的若干旋转，每局独占自己的日志文件。
#
# 两种模式:
#   默认      无头 + --freq MAX + --exam，多个旋转并行  —— 批量评测用（最快）
#   --show    有画面 + --freq 16，逐个跑，【不带 --exam】—— 普通对局模式，
#             看 AI 到底在干什么用。胜负由游戏自己的对话框告知，不产
#             result.jsonl（它的写入挂在考试分支里，见下面 SHOW 分支的注释）；
#             ai_debug.log 照常写，不受影响。
#
# 【--show 为什么要串行】有画面时每个实例各开一个窗口，并行会同时弹出好几个、
# 抢焦点、也看不清；而且 16 倍速下画面本身有开销，并行只会互相拖慢。
#
# 【为什么日志要走环境变量、而不是让每局用自己的 cwd】游戏要从 cwd 读
# config.json / res.rcc 等资源，cwd 换成各自的目录会直接起不来（实测 segfault
# 在 initPlayers）；而两份日志又都是相对 cwd 的路径
# （UsrAI.cpp 的 fopen("ai_debug.log")、Logger.cpp 的 setFileName("./GameLog.log")），
# 多实例共用 cwd 就会把它们交织进同一份文件。所以给两份日志各加了环境变量开关
# （AOE_AI_LOG / AOE_GAME_LOG），cwd 保持项目根不动。不设环境变量时行为与改动前一致。
#
# 用法:
#   bash scripts/run_one_map.sh                          # map.njust 的 4 个旋转，并行无头
#   bash scripts/run_one_map.sh --show                   # 同上，但有画面 + 16 倍速，逐个看
#   bash scripts/run_one_map.sh --show map2.njust 0      # 只看 map2 的 rotation 0
#   bash scripts/run_one_map.sh map2.njust 0 180         # map2 的 r0 / r180，并行无头
#
# 产物（每局一套，互不干扰）:
#   .ai-eval/one-map/<图>-r<旋转>/ai_debug.log    AI 日志（[SIEGE]/[CONV]/[FARMESC]...）
#   .ai-eval/one-map/<图>-r<旋转>/GameLog.log     游戏日志
#   .ai-eval/one-map/<图>-r<旋转>/result.jsonl    逐帧结果记录
#   .ai-eval/one-map/<图>-r<旋转>/stdout.log      标准输出与错误

set -u
cd "$(dirname "$0")/.." || exit 1

SHOW=0
if [ "${1:-}" = "--show" ]; then
    SHOW=1
    shift
fi

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

if [ $SHOW -eq 1 ]; then
    echo "模式 : 有画面 + 16 倍速（逐个跑）"
else
    echo "模式 : 无头 + MAX 速度（并行）"
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
    # 只把两份日志与结果文件指到各自的目录。
    if [ $SHOW -eq 1 ]; then
        echo ">>> 开跑 rotation=$R（关掉窗口或等它跑完，会接着跑下一个）"
        # 【不带 --exam】用普通对局模式：胜负由游戏自己的对话框告知，
        # 不走考试那套预处理。代价是没有 result.jsonl ——
        # 它的写入挂在考试分支里（Core.cpp:38 的 if(IsExamining)
        # PreProcessDuringExam，LogOut 就在那个函数里）。
        # ai_debug.log 不受影响：AiDebugLog 是静态函数、没挂任何模式开关。
        AOE_AI_LOG="$D/ai_debug.log" \
        AOE_GAME_LOG="$D/GameLog.log" \
        timeout 900 "$EXE" --freq 16 \
            --map "$MAPABS" --rotate "$R" \
            > "$D/stdout.log" 2>&1
    else
        AOE_AI_LOG="$D/ai_debug.log" \
        AOE_GAME_LOG="$D/GameLog.log" \
        timeout 600 "$EXE" --exam --offscreen --freq MAX \
            --map "$MAPABS" --rotate "$R" \
            --ResultLogFile "$D/result.jsonl" \
            > "$D/stdout.log" 2>&1 &
        PIDS+=($!)
    fi
done

if [ $SHOW -eq 0 ]; then
    echo "已启动 ${#PIDS[@]} 个实例: ${PIDS[*]}"
    echo "运行中……（可 tail -f $OUT/${TAG}-r0/ai_debug.log 实时看）"
    wait
fi

echo
if [ $SHOW -eq 1 ]; then
    echo "=== 完成（普通对局模式，胜负见游戏对话框；日志如下）==="
    for R in "${ROTATIONS[@]}"; do
        echo "  $OUT/${TAG}-r$R/ai_debug.log"
    done
    exit 0
fi

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
