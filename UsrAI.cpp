#include "UsrAI.h"
#include <set>
#include <iostream>
#include <unordered_map>
#include <list>
#include <cstdlib>

using namespace std;
tagGame tagUsrGame;
ins UsrIns;
/*##########DO NOT MODIFY THE CODE ABOVE##########*/
tagInfo info;
#include <algorithm>
#include <cstdlib>
#include <map>
#include <utility>
#include <vector>

// 军队自卫指令的最小发送间隔，单位为游戏帧。
static const int USR_FIELD_SELF_DEFENSE_ORDER_INTERVAL = 12;
// 军队协防友军时允许响应的最大曼哈顿距离，单位为地图格。
static const int USR_FIELD_ASSIST_RADIUS = 12;
// 普通军队主动发现敌军的最大欧氏距离，单位为地图格。
static const int USR_FIELD_ARMY_AGGRO_RADIUS = 7;
// 农民遭遇敌人时触发主动处理的最大欧氏距离，单位为地图格。
static const int USR_FIELD_FARMER_AGGRO_RADIUS = 7;
// 箭塔重新选择攻击目标的最小间隔，单位为游戏帧。
static const int USR_TOWER_ORDER_INTERVAL = 20;
// 经济采集指令的最小发送间隔，单位为游戏帧。
static const int USR_ECONOMY_ORDER_INTERVAL = 80;
// 建造指令的最小发送间隔，单位为游戏帧。
static const int USR_BUILD_ORDER_INTERVAL = 100;
// 建筑研发或升级动作之间的最小发送间隔，单位为游戏帧。
;
// 同一生产通道没有收到异步结果时，允许恢复的超时帧数。
static const int USR_PRODUCTION_ORDER_TIMEOUT = 300;
// 研发订单回执的兜底超时。正常情况下 Core 在 1 帧内写入 info.ins_ret
// （Core.cpp:1466 的 insertInsRet 对成功与失败都会写），该超时只是防止
// 回执意外丢失时通道被永久占住；触发时的重试间隔也由它决定。
static const int USR_TECH_ORDER_TIMEOUT = 60;
// 研发下单时保留的食物余量：科技不应把食物吃光而挤掉造兵与造农民。
static const int USR_TECH_FOOD_RESERVE = 60;
// 调试面板状态输出间隔，避免逐帧刷屏。
;
// 祭司转换或移动指令的最小发送间隔，单位为游戏帧。
static const int USR_PRIEST_ORDER_INTERVAL = 20;
// 判定祭司「卡住」的帧数：目标未变、且祭司自上次下发移动指令后一直没挪窝，
// 超过这么多帧才重发一次（让它重新寻路）。
//
// 为什么不能定期无条件重发：HumanMove 会经由 Core_List::addRelation 先调用
// suspendRelation（Core_List.cpp:450-469），后者把单位的移动路径 setPath 清空
// 并 initAction 重置。所以定期重发同一坐标等于每次刚起步就把路径清掉 ——
// 实测祭司位置从 f=1700 到 f=2800 一直是 (83,21)，一步都迈不出去。
// 取值要显著大于一次寻路所需时间，避免把正常起步误判成卡住。
static const int USR_PRIEST_STUCK_FRAMES = 150;
// 转换结束后的强制撤离窗口（帧）。祭司转换期间会停在原地，而敌方 AI 会把
// 「WorkObjectSN 指向我」的祭司记进反击锁（enemyai.cpp:1070-1078 判定，
// :1142-1148 的锁只在目标死亡时解除、且判据排在 FindThreatToArmy 之前）——
// 也就是说一旦祭司转换过某个单位，那个单位会追祭司追到死，我方士兵打它也不换目标。
// 所以转换一结束就立刻撤离，趁反击锁的目标（被转换者）刚消失、敌方尚未重新
// 锁定祭司的窗口离开该区域。
static const int USR_PRIEST_POST_CONVERSION_RETREAT_FRAMES = 600;
// 祭司探路的下发间隔，与侦察兵一致（DispatchScouts 里 scoutOrderInterval = 60）。
// 节流窗口内什么都不做：不做到达判定、不重选、不下发。若每帧都做这些，
// 目标一变就会重发，而 HumanMove 每次都会清空移动路径，祭司永远走不出去。
static const int USR_PRIEST_EXPLORE_ORDER_INTERVAL = 60;
// 祭司危险判定半径，单位为地图格。
static const int USR_PRIEST_DANGER_RADIUS = 9;
// 祭司治疗目标的最大距离（格，欧氏）。
// 原实现不限距离：只要图上有伤兵，祭司就会横穿全图去治，这是它长期离开基地的
// 主要原因（实测某局它跑到离市镇中心 30 格处待机，援军与箭塔都够不到）。
static const int USR_PRIEST_HEAL_RADIUS = 12;
// 第三波时间点（enemyai.cpp 的 TAT = 21000）。在此之前，视野内没有敌人时
// 祭司留在基地：它只有 100 血、近战与远程防御都是 0、且不能自愈，
// 待在基地的箭塔覆盖圈里最安全。
static const int USR_PRIEST_HOLD_HOME_UNTIL_FRAME = 21000;
// 总攻阶段起始帧：此后祭司进入「只保存实力」状态 —— 不再转换、不再治疗，
// 视野内没有敌人时留在基地。
// 祭司只有 100 血、近战与远程防御都是 0、不能自愈且不可补充（兵种列表里没有
// 第二个），总攻期间让它待在箭塔覆盖圈里，比跟部队出去换血更稳。
static const int USR_PRIEST_PASSIVE_FRAME = 30000;
// 挨打时触发撤退的敌人搜索半径（格，欧氏）。
// 比各兵种射程更远：实测真正打伤祭司的战车弓箭手恰好停在判定边缘
// （tE2=82 对阈值 81），而锁定祭司的近战兵从 10~20 格外走过来。
static const int USR_PRIEST_HURT_THREAT_RADIUS = 14;
// 祭司探路的前沿点距市镇中心的最大半径（格）。
// 前沿探索是「就近优先」，正常情况下不会跳到远处；这个上限兜住的是
// 「近处已探明、最近的前沿点已落到基地之外」的情形。
//
// 原为 25：实测它会让祭司在基地附近反复打转却找不到食物 —— 食物只靠浆果丛
// 与瞪羚，而瞪羚群经常落在 25 格之外，此时祭司既看不见它、又不会往远处走，
// 只能停在基地待命（观感上就是「卡住」，日志里 mvF 停在某一帧后再无新指令）。
// 放宽到 40 让它够得到常见的兽群距离；再远就不去了，避免为探路承担过大风险。
static const int USR_PRIEST_EXPLORE_RADIUS = 40;
// 祭司开局探路是否已结束。
static bool priestExploreDone = false;
// 祭司距市镇中心多近算「已经在基地范围」，单位：格。
// 在此范围内不再「退向中心」（那是原地不动），改为绕到中心相对威胁的另一侧。
static const int USR_PRIEST_HOME_RADIUS = 12;
// 第一个侦察骑兵的生产时间点。它执行「视野内出现敌人即撤回市镇中心」的
// 保命策略，不承担基地侦测（角色判定见 DispatchScouts）。
static const int USR_SCOUT_FIRST_FRAME = 33000;
// 专职侦测敌方基地的侦察骑兵的生产时间点。
// 在 USR_SCOUT_FIRST_FRAME 之后产出的侦察兵执行「视野内出现敌人即撤回市镇中心」
// 的保命策略，不承担基地侦测；这一帧之后再补 1 个专门负责侦测敌方基地的。
static const int USR_SCOUT_RECON_FRAME = 36000;
// 农民自卫的启动帧。此前的农民遇袭一律靠撤离，不还手 —— 早期被零星骚扰
// 牵着走会白白损失采集力；三波骚扰过后（enemyai.cpp:45 的 TAT = 21000 已过）
// 再让农民挨打时就地反击。
static const int USR_FARMER_SELF_DEFENSE_FRAME = 30000;
// 箭塔建筑候选点相对中心的目标距离，单位为地图格。
static const int USR_ARROWTOWER_BUILD_RADIUS = 18;
// 箭塔目标数量。TOWER_RINGS 提供 3 圈 × 4 方向 = 12 个候选位，
// 建造循环从近圈（6 格）起逐圈填。
// 取 4：刚好填满近圈的 4 个方向，覆盖半径约 13 格。
// 注意：建造与采石共用 ArrowTowerStillWanted()（见 CalculateFarmerTargets）——
// 建满 4 座后它返回 false，于是同时停建箭塔、并停止采石把农民让给食物与木头。
// 只改建造而不改采石的话，塔数永远停在目标值以下，农民会一直采无用的石头。
static const int USR_ARROWTOWER_TARGET = 4;
// 箭塔停止建造的帧号：第三波骚扰（enemyai.cpp:45 的 TAT=21000）之后不再建造，
// 石头与采集力让给兵力与科技。
// 注意 enemyai.cpp 的 FAT/SAT/TAT 是那个文件内部的宏，UsrAI.cpp 里看不到，
// 所以这里是独立取值 —— 两处若要调整需要同步。
static const int USR_ARROWTOWER_STOP_FRAME = 21000;
// 建筑候选点距离地图边界的最小安全边距，单位为地图格。
static const int USR_ARROWTOWER_BUILD_MIN_MARGIN = 3;
// 表示尚未发生过相关事件的哨兵帧值。
static const int USR_INVALID_FRAME = -1000000000;
// 建筑研发「已结束」判定的防抖窗口：观察到 Project 离开研发项后连续空闲这么多帧，
// 才认定研发真的完成。研发被中断（suspendRelation）时 Project 会短暂回到 0，
// 窗口太短会把中断误判成完成 —— 实测就是因此让 wheelTechReady 提前变真，
// 之后 AI 一直发战车弓兵订单而 Core 一直以 ACTION_INVALID_BUILDACT_LOCK 拒绝，
// 整局产不出一个战车弓兵。
static const int USR_MARKET_IDLE_FRAMES = 300;
// 市场研发指令的确认窗口：发出后这么多帧内市场 Project 仍未进入研发，
// 就判定这条研发不可用（被 Core 以 ACTION_INVALID_BUILDACT_LOCK 拒绝）。
// 不能用 ins_ret 回执来判定 —— 它只保留最近 100 条（tagGame::update），
// 而一次研发要跑上千帧，指令的回执早被后续指令挤掉。依赖回执会让状态机
// 永久卡在「等回执」上：实测表现为车轮研发结束后市场全程空闲，
// 木材链与农田链再也没被下发过一次。
static const int USR_MARKET_ORDER_TIMEOUT = 120;
// 市场研发失败后的冷却帧数。失败往往只是暂时的（资源不足、或研发链这一级
// 时代未到），所以只冷却一段时间再重试，绝不永久跳过 —— 永久标记会让车轮与
// 木材加工在食物攒够之后依然永远发不出去：实测早期食物只有 20~85 时被 Core
// 拒掉，等食物涨到 155，市场已经把它们记成「不可研发」，整局再没试过。
static const int USR_MARKET_RETRY_COOLDOWN = 600;
// 人口硬上限（Development.h:134 的 humanNum_Top）。
// info.Human_MaxNum 导出的是 min(房屋数 × HOUSE_HUMAN_NUM, 该值)，
// 所以只有房屋补够之后它才可能等于 50。
static const int USR_HUMAN_NUM_CAP = 50;

// 我方军队 SN 到当前锁定敌方目标 SN 的映射。
static map<int, int> currentTarget;
// 我方军队 SN 到波次反击目标 SN 的映射。
static map<int, int> waveRetaliationTarget;
// 我方军队 SN 到首次发现波次威胁时刻的映射。
static map<int, int> waveThreatFirstSeenFrame;
// 我方军队 SN 到上次自卫指令帧的映射。
static map<int, int> fieldSelfDefenseLastOrderFrame;
// 我方箭塔 SN 到上次索敌指令帧的映射。
static map<int, int> towerLastOrderFrame;
// 我方主动进攻单位 SN 到其返回位置的映射，坐标单位为世界坐标。
static map<int, pair<double, double>> harassHome;
// 农民 SN 到上次采集或撤离指令帧的映射。
static map<int, int> farmerLastOrderFrame;
// 当前处于敌袭撤离状态的农民 SN 到最近一次发现威胁的帧。
static map<int, int> farmerThreatLastFrame;
// 农民确认脱离威胁后的起始帧，用于安全滞后。
static map<int, int> farmerSafeSinceFrame;
// 第三波结束后侦察骑兵的移动节流和巡逻点状态。
static map<int, int> scoutLastOrderFrame;
static map<int, int> scoutWaypointIndex;
static map<int, pair<int, int>> scoutTargetBlock;
static map<int, pair<int, int>> scoutLastBlock;
static map<int, int> scoutStuckCount;
static map<int, int> scoutEmergencyOrderId;
static map<int, pair<int, int>> scoutEmergencyTarget;
static map<int, int> scoutDangerLastFrame;
static map<pair<int, int>, int> scoutFrontierVisitFrame;
// 侦察骑兵分工：true = 专责侦测敌方基地（遇敌不撤，压向前沿）；
// false = 谨慎型（勾引敌人后撤回市镇中心，把追兵引进基地火力圈）。
// 角色在首次看到该侦察兵时按当时帧号确定，之后不再改变。
static map<int, bool> scoutIsRecon;
// 谨慎型侦察兵最近一次主动攻击过的敌人 SN。
// 用途：攻击关系只需建立一次（敌方反击锁只有目标死亡才解除），
// 记录后避免每帧重复下达攻击指令而挤掉撤回基地的移动指令。
static map<int, int> scoutLuredTarget;
// 第三波结束后是否已经进入侦察任务，以及是否发现敌方基地。
static bool enemyBaseDiscovered = false;
// 敌方基地信息（供进攻与箭塔朝向复用，需在建造逻辑之前定义）。
static int enemyBaseSN = -1;
static int enemyBaseLastSeenFrame = USR_INVALID_FRAME;
static int enemyBaseBlockDR = -1;
static int enemyBaseBlockUR = -1;
// 上次提交经济采集指令的游戏帧。
static int lastEconomyOrderFrame = USR_INVALID_FRAME;
// 上次提交建造指令的游戏帧。
static int lastBuildOrderFrame = USR_INVALID_FRAME;
// 上次为未完成建筑恢复建造的游戏帧。
static int lastConstructionRecoveryFrame = USR_INVALID_FRAME;

// 单个资源目标允许关联的农民数量上限，避免动态资源附近互相卡位。
static const int USR_RESOURCE_HARD_CAP_FOOD = 4;
static const int USR_RESOURCE_HARD_CAP_OTHER = 3;
// 资源目标采集指令失败后的基础冷却和最大冷却。
static const int USR_RESOURCE_FAIL_COOLDOWN = 240;
static const int USR_RESOURCE_FAIL_COOLDOWN_MAX = 1200;
// Core 状态快照刷新前，保留成功订单预占的宽限帧数。
static const int USR_RESOURCE_PENDING_GRACE = 120;
// 即使 Core 未返回结果，预占也必须在绝对期限后释放。
static const int USR_RESOURCE_PENDING_MAX_LIFETIME = 600;

struct ResourceAttemptState
{
    int cooldownUntilFrame;
    int failureCount;
    int lastAttemptFrame;

    ResourceAttemptState()
        : cooldownUntilFrame(USR_INVALID_FRAME), failureCount(0),
          lastAttemptFrame(USR_INVALID_FRAME)
    {
    }
};

struct PendingGatherOrder
{
    int orderId;
    int targetSN;
    int submitFrame;
    int result;
    int resultFrame;
    // 关系建立（WorkObjectSN !=
    // -1）的帧，用于区分「稳定采集」与「卡住后被取消」。
    int relationFrame;

    PendingGatherOrder()
        : orderId(-1), targetSN(-1), submitFrame(USR_INVALID_FRAME),
          result(USR_INVALID_FRAME), resultFrame(USR_INVALID_FRAME),
          relationFrame(USR_INVALID_FRAME) {}
};

// 资源 SN 到最近一次采集尝试状态的映射。
static map<int, ResourceAttemptState> resourceAttemptState;
// 农民 SN 到尚未完全反映在 Core 快照中的采集订单预占。
static map<int, PendingGatherOrder> pendingGatherOrders;
// 用于检测同一进程中新对局导致的帧号回退。
static int farmerResourceStateFrame = USR_INVALID_FRAME;

static int FindDirectThreatToFarmerSN(const tagFarmer &farmer);
static int FindNearbyEnemyForFarmer(const tagFarmer &farmer);
static bool IsKnownLandBlock(int blockDR, int blockUR);
static bool IsExplorationFrontierBlock(int blockDR, int blockUR);
static bool FindBestScoutFrontier(const tagArmy &scout, int &targetDR, int &targetUR);
;
static bool IsAliveFarmerSN(int farmerSN);
static bool IsFarmerRelationEstablished(int farmerSN, int targetSN);
// 上次提交建筑研发、升级或生产动作的游戏帧。
static int lastBuildingActionFrame = USR_INVALID_FRAME;
// 上次提交单位生产动作的游戏帧。
static int lastProductionActionFrame = USR_INVALID_FRAME;
// 上次提交祭司移动或转换指令的游戏帧。
static int lastPriestOrderFrame = USR_INVALID_FRAME;
// 主力开始总攻后的祭司跟随门控状态。
static bool offensiveAttackStarted = false;
static int offensiveAttackStartFrame = USR_INVALID_FRAME;
// 祭司最近一次确认安全的起始游戏帧。
static int priestSafeSinceFrame = USR_INVALID_FRAME;
// 当前祭司移动指令的异步指令 ID，-1 表示没有等待中的指令。
static int priestMoveOrderId = -1;
// 最近一次祭司移动指令的回执码，以及回执帧。
// 用途：HumanMove 只是把指令入队（AI::AddToIns 同步返回 id），是否真的建立
// 移动关系要看 Core 的返回码——Core_List::addRelation 在「旧关系存在且
// respondConduct == false」时返回 ACTION_INVALID_ISNTFREE（Core_List.cpp:332），
// 此时祭司原地不动，而 AI 侧原本完全看不到。
static int priestMoveLastRet = -999;
static int priestMoveLastRetFrame = USR_INVALID_FRAME;
// 攻击祭司的敌人 SN → 被派去转移其仇恨的诱饵单位 SN（每个威胁各配一个诱饵）。
static map<int, int> priestDecoyByThreat;
// 当前祭司撤退或防守目标的地图格坐标。
static pair<int, int> priestEmergencyTarget = make_pair(-1, -1);
// 当前祭司撤退或防守目标最后一次更新的游戏帧。
static int priestEmergencyTargetFrame = USR_INVALID_FRAME;
// 上一次向祭司下发移动指令时它所在的格。用于「卡住」判定：目标没变、
// 但祭司自那以后一直没挪窝，才说明路径失效、需要重发一次。
static int priestMoveFromDR = -1;
static int priestMoveFromUR = -1;
// 祭司当前的探索目标格（first < 0 表示没有目标）。
// 与侦察骑兵的 scoutTargetBlock 同理，目标必须持久保存，只在「已到达」
// （2 格内）或「卡住」时才作废重选 —— 若每帧都用 FindBestScoutFrontier
// 重新选点，目标会一直在变，祭司永远走不到任何一个前沿点。
static pair<int, int> priestFrontierTarget = make_pair(-1, -1);
// 祭司连续走不到前沿点的次数（诊断用）。到达一个点就清零。
// 注意：它不再触发「结束探路」—— 祭司只在找到瞪羚时才停。
static int priestFrontierStuckCount = 0;
// 上一帧祭司是否正在转换。用于检测「转换刚刚结束」这一瞬间。
static bool priestWasConverting = false;
// 转换结束后的撤离窗口截止帧。窗口内优先回市镇中心，不再发起新转换。
static int priestRetreatUntilFrame = USR_INVALID_FRAME;
// 最近一次检测到祭司危险状态的游戏帧。
static int priestDangerLastFrame = USR_INVALID_FRAME;
// 最近一次触发祭司危险状态的敌方单位 SN。
static int priestDangerTargetSN = -1;
// 最近一次农民自毁指令的订单 ID、目标农民 SN、以及发起时该农民的血量。
// 用途：下单只代表「指令入队」，回执才代表 Core 接受了；靠它把
// [SACRIFICE-RET] 与 [SACRIFICE] 对上。
static int sacrificeOrderId = -1;
static int sacrificeFarmerSN = -1;
static int sacrificeHpBefore = -1;
// 下一个建筑候选位置在候选数组中的索引。
static int buildCandidateIndex = 0;
// 兵营建造指令的异步指令 ID，-1 表示没有等待中的指令。
static int armyCampOrderId = -1;
// 当前普通建造指令的异步指令 ID，-1 表示没有等待中的指令。
static int buildOrderId = -1;
static int buildOrderType = -1;
static int buildFarmerSN = -1;
static int stableOrderFrame = USR_INVALID_FRAME;
// 最近一次非生产建筑动作的异步指令 ID。
static int technologyOrderId = -1;
// 最近一次非生产建筑动作提交时的游戏帧。
static int technologyOrderFrame = USR_INVALID_FRAME;
// 最近一次非生产建筑动作的动作枚举，用于异步成功后推进科技里程碑。
static int technologyPendingAction = -1;
// 研发完成的兵种科技数量，全部完成后解锁兵力生产上限并开始进攻。
static int researchedTechCount = 0;
// 各兵种科技的研发订单 ID（-1 表示无 pending）。
static int clubmanUpgradeOrderId = -1;
static int broadswordUpgradeOrderId = -1;
// 研发订单 ID → 下单帧。Core::logActionResult 只把「成功」指令写入
// info.ins_ret， 被拒绝的指令不会留下任何结果，因此不能仅凭 orderId != -1
// 判断在途， 必须配合下单帧做超时判定，否则该研发通道会永久卡死。
static map<int, int> techOrderFrame;
// 最近一次结算的研发指令结果码与帧号（-1 表示超时判定为被 Core 拒绝）。
static int lastTechRet = -1;
static int lastTechFrame = USR_INVALID_FRAME;
// 仓库三项科技的串行推进游标与在途订单。
// 同帧并发下单会被 Core 拒绝（见 ResearchTechQueue 注释）。
static int stockTechCursor = 0;
static int stockTechOrderId = -1;
// 解锁兵力上限与总攻所需研发完成的兵种科技数量。
// 只统计用 ResearchTech 追踪的科技（当前仅「升级为阔剑」——阔剑兵全面
// 优于斧头兵且更便宜，故跳过「升级为战斧」）。
static const int TOTAL_REQUIRED_TECH = 1;
// 谷仓「研发:建造箭塔」是否已完成（完成后才允许建造箭塔）。
// 判断方式：观察到谷仓 Project == BUILDING_GRANARY_ARROWTOWER（研发中），
// 之后 Project 回到空闲即视为研发完成。
static bool arrowTowerTechnologyReady = false;
static bool arrowTowerTechSeenRunning = false;
// 首次尝试发起箭塔研发的帧，用于长时间未启动时兜底放行。
static int arrowTowerTechStartFrame = USR_INVALID_FRAME;
// 后勤研究（兵营单位人口占用 1 → 0.5）是否已完成。
// 判定同箭塔研发：观察到兵营 Project 进入该研发，之后离开即视为完成。
static bool logisticsReady = false;
static bool logisticsSeenRunning = false;
// 车轮升级（解锁战车与战车弓兵）是否已完成。判定方式同后勤：观察到市场
// Project 进入该研发、之后连续空闲 USR_MARKET_IDLE_FRAMES 帧视为完成。
// 用途：TryProduceChariotArcher 在科技未就绪时直接放弃，避免每帧向 Core
// 发一条注定被 ACTION_INVALID_BUILDACT_LOCK 拒绝的指令。
// 车轮是单级研发链（Development.cpp:665-671 只有 setHead、没有 push_back），
// 所以一个布尔量就足以表示「整条链走完」—— 木材 / 农田那两条两级链不行，
// 见 ManageMarketResearch 的说明。
static bool wheelTechReady = false;
static bool wheelSeenRunning = false;
static int wheelIdleSinceFrame = USR_INVALID_FRAME;
// 市场研发的下发状态（见 ManageMarketResearch）：
// marketCooldownUntil[i] 是 kMarketResearch[i] 的下次可尝试帧。失败只冷却、
// 不永久跳过 —— 原因见 USR_MARKET_RETRY_COOLDOWN 的说明。
static int marketCooldownUntil[3] = {0, 0, 0};
static int marketOrderId = -1;
static int marketOrderSlot = -1;
static int marketOrderFrame = USR_INVALID_FRAME;
// 最近一次战车弓兵生产指令的订单 ID 与回执码（0=成功；12=LOCK 表示前置未满足）。
static int chariotArcherOrderId = -1;
static int chariotArcherRet = -999;
// 最近一次兵营建造指令的返回结果。
static int armyCampResult = ACTION_SUCCESS;
// 最近一次棍棒兵生产指令的返回结果。
static int clubmanResult = ACTION_SUCCESS;
// 兵营建造指令返回结果对应的游戏帧。
static int armyCampResultFrame = USR_INVALID_FRAME;
// 棍棒兵生产指令返回结果对应的游戏帧。
static int clubmanResultFrame = USR_INVALID_FRAME;

// 调试日志：写入 ai_debug.log，用于离线分析 AI 决策与游戏状态。
static void AiDebugLog(const char *msg) {
  FILE *f = fopen("ai_debug.log", "a");
  if (f) {
    fprintf(f, "%s\n", msg);
    fclose(f);
  }
}

// 接口：计算两个格子坐标的曼哈顿距离。
// 用途：适合做射程、警戒范围、追击半径等粗略判断。
static int BlockDis(int x1, int y1, int x2, int y2)
{
    return abs(x1 - x2) + abs(y1 - y2);
}

// 接口：计算两个格子坐标的欧氏距离平方。
// 用途：适合比较“谁更近”，避免 sqrt 带来的额外开销。
static int BlockDis2(int x1, int y1, int x2, int y2)
{
    int dx = x1 - x2;
    int dy = y1 - y2;
    return dx * dx + dy * dy;
}

// 接口：返回箭塔攻击范围，单位是地图格。
// 用途：玩家箭塔自动索敌时判断敌人是否进入射程。
static int ArrowTowerAttackRangeBlocks()
{
    return static_cast<int>(double(DIS_ARROWTOWER));
}

// 接口：判断 vector<int> 中是否存在指定 SN。
// 用途：兵力编组、避免重复选择同一个单位。
static bool ContainsInt(const vector<int> &values, int value)
{
    return find(values.begin(), values.end(), value) != values.end();
}

// 接口：向 vector<int> 中去重加入 SN。
// 用途：维护目标列表、波次单位列表、已使用单位列表。
// 接口：按 SN 查找我方军队对象。
// 用途：目标锁清理、兵力调度、自动反击前确认单位仍然存在。
static const tagArmy *FindMyArmyBySN(int sn)
{
    for (const tagArmy &army : info.armies)
    {
        if (army.SN == sn)
            return &army;
    }
    return nullptr;
}

// 接口：判断敌方任意目标是否仍然存在。
// 用途：目标锁校验，目标死亡后允许重新选目标。
static bool EnemyTargetAlive(int sn)
{
    if (sn == -1)
        return false;

    for (const tagArmy &army : info.enemy_armies)
    {
        if (army.SN == sn)
            return true;
    }

    for (const tagFarmer &farmer : info.enemy_farmers)
    {
        if (farmer.SN == sn)
            return true;
    }

    for (const tagBuilding &building : info.enemy_buildings)
    {
        if (building.SN == sn)
            return true;
    }

    return false;
}

// 接口：读取某个我方军队单位当前锁定的敌方目标。
// 用途：避免每帧重复下令；如果目标已不存在，会自动清锁。
static int GetLockedArmyTarget(int armySN)
{
    auto it = currentTarget.find(armySN);
    if (it == currentTarget.end())
        return -1;

    if (EnemyTargetAlive(it->second))
        return it->second;

    currentTarget.erase(it);
    waveRetaliationTarget.erase(armySN);
    waveThreatFirstSeenFrame.erase(armySN);
    return -1;
}

// 接口：清除某个我方军队单位的目标锁。
// 用途：目标死亡、撤退、重新分配任务时调用。
static void ClearArmyTargetLock(int armySN)
{
    currentTarget.erase(armySN);
    waveRetaliationTarget.erase(armySN);
    waveThreatFirstSeenFrame.erase(armySN);
}

// 接口：清理已死亡或消失的我方单位目标锁。
// 用途：建议每帧 AI 开头调用，防止静态 map 残留无效 SN。
static void CleanDeadOwnerTargetLocks()
{
    for (auto it = currentTarget.begin(); it != currentTarget.end();)
    {
        if (FindMyArmyBySN(it->first) == nullptr)
        {
            int armySN = it->first;
            it = currentTarget.erase(it);
            waveRetaliationTarget.erase(armySN);
            waveThreatFirstSeenFrame.erase(armySN);
        }
        else
        {
            ++it;
        }
    }
}

// 接口：按敌方 SN 查询敌方单位格子坐标。
// 用途：箭塔判断当前目标是否还在射程内。
static bool FindEnemyUnitBlockPosition(int sn, int &blockDR, int &blockUR)
{
    for (const tagArmy &army : info.enemy_armies)
    {
        if (army.SN == sn)
        {
            blockDR = army.BlockDR;
            blockUR = army.BlockUR;
            return true;
        }
    }

    for (const tagFarmer &farmer : info.enemy_farmers)
    {
        if (farmer.SN == sn)
        {
            blockDR = farmer.BlockDR;
            blockUR = farmer.BlockUR;
            return true;
        }
    }

    return false;
}

// 接口：寻找正在直接攻击指定我方军队单位的最近敌人。
// 用途：实现“受到攻击自动反击”。
static int FindDirectThreatToArmySN(int myArmySN)
{
    const tagArmy *myArmy = FindMyArmyBySN(myArmySN);
    if (!myArmy)
        return -1;

    int bestSN = -1;
    int bestDis = 1000000000;

    for (const tagArmy &enemyArmy : info.enemy_armies)
    {
        if (enemyArmy.WorkObjectSN != myArmySN)
            continue;

        int d = BlockDis2(myArmy->BlockDR, myArmy->BlockUR,
                          enemyArmy.BlockDR, enemyArmy.BlockUR);
        if (d < bestDis)
        {
            bestDis = d;
            bestSN = enemyArmy.SN;
        }
    }

    for (const tagFarmer &enemyFarmer : info.enemy_farmers)
    {
        if (enemyFarmer.WorkObjectSN != myArmySN)
            continue;

        int d = BlockDis2(myArmy->BlockDR, myArmy->BlockUR,
                          enemyFarmer.BlockDR, enemyFarmer.BlockUR);
        if (d < bestDis)
        {
            bestDis = d;
            bestSN = enemyFarmer.SN;
        }
    }

    for (const tagBuilding &enemyBuilding : info.enemy_buildings)
    {
        if (enemyBuilding.Type != BUILDING_ARROWTOWER)
            continue;
        if (enemyBuilding.Project != myArmySN)
            continue;
        if (BlockDis(myArmy->BlockDR, myArmy->BlockUR,
                     enemyBuilding.BlockDR, enemyBuilding.BlockUR) > USR_FIELD_ASSIST_RADIUS)
        {
            continue;
        }

        int d = BlockDis2(myArmy->BlockDR, myArmy->BlockUR,
                          enemyBuilding.BlockDR, enemyBuilding.BlockUR);
        if (d < bestDis)
        {
            bestDis = d;
            bestSN = enemyBuilding.SN;
        }
    }

    return bestSN;
}

// 接口：寻找进入我方军队警戒范围内的最近敌方农民。
// 用途：让空闲兵力主动驱赶或击杀靠近的敌方农民。
static int FindNearbyEnemyFarmerToAttack(const tagArmy &myArmy)
{
    if (myArmy.Sort == AT_SHIP)
        return -1;

    int bestSN = -1;
    int bestDis2 = USR_FIELD_FARMER_AGGRO_RADIUS * USR_FIELD_FARMER_AGGRO_RADIUS;

    for (const tagFarmer &enemyFarmer : info.enemy_farmers)
    {
        if (enemyFarmer.Blood <= 0)
            continue;

        int dis2 = BlockDis2(myArmy.BlockDR, myArmy.BlockUR,
                             enemyFarmer.BlockDR, enemyFarmer.BlockUR);
        if (dis2 < bestDis2)
        {
            bestDis2 = dis2;
            bestSN = enemyFarmer.SN;
        }
    }

    return bestSN;
}

// 动物在状态快照中以资源形式出现；只把存活且靠近军队的狮子作为自卫目标。
static int FindNearbyEnemyLionToAttack(const tagArmy &myArmy)
{
    if (myArmy.Sort == AT_SHIP)
        return -1;

    int bestSN = -1;
    int bestDis2 = USR_FIELD_ARMY_AGGRO_RADIUS * USR_FIELD_ARMY_AGGRO_RADIUS;

    for (const tagResource &resource : info.resources)
    {
        if (resource.Type != RESOURCE_LION || resource.Blood <= 0)
            continue;

        const int dis2 = BlockDis2(myArmy.BlockDR, myArmy.BlockUR,
                                   resource.BlockDR, resource.BlockUR);
        if (dis2 < bestDis2)
        {
            bestDis2 = dis2;
            bestSN = resource.SN;
        }
    }

    return bestSN;
}

// 接口：寻找附近友军正在承受的威胁。
// 用途：实现小范围自动支援，附近队友被打时协助反击。
static int FindAssistThreatNearArmy(const tagArmy &myArmy)
{
    int bestSN = -1;
    int bestDis = 1000000000;

    for (const tagArmy &ally : info.armies)
    {
        if (ally.SN == myArmy.SN)
            continue;

        int allyDist = BlockDis(myArmy.BlockDR, myArmy.BlockUR,
                                ally.BlockDR, ally.BlockUR);
        if (allyDist > USR_FIELD_ASSIST_RADIUS)
            continue;

        for (const tagArmy &enemyArmy : info.enemy_armies)
        {
            if (enemyArmy.WorkObjectSN != ally.SN)
                continue;

            int d = BlockDis2(myArmy.BlockDR, myArmy.BlockUR,
                              enemyArmy.BlockDR, enemyArmy.BlockUR);
            if (d < bestDis)
            {
                bestDis = d;
                bestSN = enemyArmy.SN;
            }
        }

        for (const tagFarmer &enemyFarmer : info.enemy_farmers)
        {
            if (enemyFarmer.WorkObjectSN != ally.SN)
                continue;

            int d = BlockDis2(myArmy.BlockDR, myArmy.BlockUR,
                              enemyFarmer.BlockDR, enemyFarmer.BlockUR);
            if (d < bestDis)
            {
                bestDis = d;
                bestSN = enemyFarmer.SN;
            }
        }

        for (const tagBuilding &enemyBuilding : info.enemy_buildings)
        {
            if (enemyBuilding.Type != BUILDING_ARROWTOWER)
                continue;
            if (enemyBuilding.Project != ally.SN)
                continue;
            if (BlockDis(myArmy.BlockDR, myArmy.BlockUR,
                         enemyBuilding.BlockDR, enemyBuilding.BlockUR) > USR_FIELD_ASSIST_RADIUS)
            {
                continue;
            }

            int d = BlockDis2(myArmy.BlockDR, myArmy.BlockUR,
                              enemyBuilding.BlockDR, enemyBuilding.BlockUR);
            if (d < bestDis)
            {
                bestDis = d;
                bestSN = enemyBuilding.SN;
            }
        }
    }

    return bestSN;
}

// 接口：选择当前主动进攻或骚扰的中心格。
// 用途：给 SelectWaveUnitsBySort 提供“离哪里最近”的排序中心。
static pair<int, int> GetHarassCenterBlock()
{
    for (const tagBuilding &building : info.enemy_buildings)
    {
        if (building.Type == BUILDING_ARROWTOWER)
        {
            return make_pair(building.BlockDR, building.BlockUR);
        }
    }

    if (!info.enemy_farmers.empty())
    {
        return make_pair(info.enemy_farmers[0].BlockDR,
                         info.enemy_farmers[0].BlockUR);
    }

    if (!info.enemy_buildings.empty())
    {
        return make_pair(info.enemy_buildings[0].BlockDR,
                         info.enemy_buildings[0].BlockUR);
    }

    return make_pair(64, 64);
}

// 接口：按兵种选择离进攻中心最近的一批我方军队。
// 用途：主动进攻时自动调度距离敌人最近的兵力。
// 接口：按类型查找已完成且空闲的我方建筑。
// 用途：统一调度时代、科技和生产命令，避免覆盖正在执行的项目。
static const tagBuilding *FindReadyBuildingByType(int type)
{
    for (const tagBuilding &building : info.buildings)
    {
        // 普通建筑的 Project 直接映射 Building::getActNum()；空闲值为 ACT_NULL(0)，
        // 而不是 -1。箭塔是例外，其 Project 保存攻击目标，未由此函数用于生产。
        if (building.Type == type && building.Percent >= 100 &&
            building.Project == ACT_NULL)
            return &building;
    }
    return nullptr;
}

static const tagBuilding *FindBuildingByType(
    int type,
    bool requireCompleted)
{
    for (const tagBuilding &building : info.buildings)
    {
        if (building.Type == type && building.Blood > 0 &&
            (!requireCompleted || building.Percent >= 100))
        {
            return &building;
        }
    }
    return nullptr;
}

// 接口：判断某类建筑是否已经存在（包括在建建筑）。
// 用途：避免冷却重试期间重复创建同类地基。
static bool HasBuilding(int type)
{
    for (const tagBuilding &building : info.buildings)
    {
        if (building.Type == type && building.Blood > 0)
            return true;
    }
    return false;
}

static bool HasIncompleteBuilding(int type)
{
    for (const tagBuilding &building : info.buildings)
    {
        if (building.Type == type && building.Blood > 0 && building.Percent < 100)
            return true;
    }
    return false;
}

// 接口：统计某类建筑的数量（包括在建建筑）。
// 用途：限制同种建筑数量，例如场上农田不超过三块。
static int CountBuilding(int type) {
  int count = 0;
  for (const tagBuilding &building : info.buildings) {
    if (building.Type == type && building.Blood > 0)
      count++;
  }
  return count;
}

// 箭塔是否还需要建造：未过停建帧，且数量未达目标。
// 建造点与采石权重必须共用这一个判断 —— 只改建造而不改采石的话，第三波之后
// 农民会一直采石（塔数永远停在目标值以下），把采集力浪费在没有用途的石头上。
static bool ArrowTowerStillWanted()
{
  return g_frame < USR_ARROWTOWER_STOP_FRAME &&
         CountBuilding(BUILDING_ARROWTOWER) < USR_ARROWTOWER_TARGET;
}

static int CountArmyBySort(int sort)
{
    int count = 0;
    for (const tagArmy &army : info.armies)
    {
        if (army.Sort == sort && army.Blood > 0)
            count++;
    }
    return count;
}

// 统计敌方某兵种存活数量，用于按敌方阵容调整我方兵种生产。
static int CountEnemyBySort(int sort)
{
    int count = 0;
    for (const tagArmy &enemy : info.enemy_armies)
    {
        if (enemy.Sort == sort && enemy.Blood > 0)
            count++;
    }
    return count;
}

static const tagBuilding *FindCenter()
{
    for (const tagBuilding &building : info.buildings)
    {
        if (building.Type == BUILDING_CENTER && building.Blood > 0)
            return &building;
    }
    return nullptr;
}

static const tagArmy *FindPriest()
{
    for (const tagArmy &army : info.armies)
    {
        if (army.Sort == AT_PRIEST && army.Blood > 0)
            return &army;
    }
    return nullptr;
}

static int FindThreatToPriestSN(int priestSN)
{
    const tagArmy *priest = FindPriest();
    if (priest == nullptr)
        return -1;

    int bestSN = -1;
    int bestDis2 = 1000000000;
    for (const tagArmy &enemy : info.enemy_armies)
    {
        if (enemy.Blood <= 0)
            continue;
        int dis2 = BlockDis2(priest->BlockDR, priest->BlockUR,
                             enemy.BlockDR, enemy.BlockUR);
        // 派兵主动保护祭司的触发半径，取 USR_PRIEST_HURT_THREAT_RADIUS（14 格）。
        // 原先是写死的 6 格，比实际交战距离还小——祭司转换距离 12 格、敌方远程
        // 射程 7~10 格、骑兵速度是祭司的两倍，等敌人进到 6 格时军队已经来不及
        // 赶到（实测 priestThreatSN 绝大多数采样都是 -1，全军根本不会来救）。
        // 代价：祭司附近有敌人时全军会被拉过去，基地正面防守变薄；但祭司是唯一
        // 的获胜路径且不可补充，这个代价应当付。
        if (dis2 < USR_PRIEST_HURT_THREAT_RADIUS * USR_PRIEST_HURT_THREAT_RADIUS &&
            dis2 < bestDis2)
        {
            bestDis2 = dis2;
            bestSN = enemy.SN;
        }
    }
    return bestSN;
}

static const tagBuilding *FindEnemySiege(const tagArmy &priest)
{
    for (const tagBuilding &building : info.enemy_buildings)
    {
        // 只找攻城武器厂（胜利条件：转换它），修复距离计算的 BlockUR 笔误。
        if (building.Blood <= 0 || building.Type != BUILDING_SIEGE)
            continue;
        const int dis2 = BlockDis2(building.BlockDR, building.BlockUR, priest.BlockDR, priest.BlockUR);
        if (dis2 >= 12 * 12)
            continue;
        // 攻城武器厂附近 5 格内有敌人则视为不安全，等军队清理后再转换。
        bool safe = true;
        for (const tagArmy &enemy : info.enemy_armies)
        {
            if (enemy.Blood <= 0)
                continue;
            const int enemyDis2 = BlockDis2(building.BlockDR, building.BlockUR,
                                            enemy.BlockDR, enemy.BlockUR);
            if (enemyDis2 < 2 * 2)
            {
                safe = false;
                break;
            }
        }
        if (!safe)
            continue;
        return &building;
    }
    return nullptr;
}

static int ArmyTargetPriority(const tagArmy &enemy)
{
    const tagArmy *priest = FindPriest();
    if (priest && enemy.WorkObjectSN == priest->SN)
        return 0;
    if (enemy.Sort == AT_STONE_THROWER)
        return 1;
    if (enemy.Sort == AT_CAVALRY || enemy.Sort == AT_CHARIOT ||
        enemy.Sort == AT_CHARIOT_ARCHER || enemy.Sort == AT_COMPOSITE_BOWMAN)
    {
        return 2;
    }
    return 3;
}

// 接口：选择军队视野内的高优先级敌军。
// 用途：优先保护祭司并集火投石车，而不是只攻击最近的单位。
static int FindEnemyArmyInVision(const tagArmy &army)
{
    const int radius = army.Sort == AT_SCOUT ? 9 : USR_FIELD_ARMY_AGGRO_RADIUS;
    int bestSN = -1;
    int bestPriority = 1000000000;
    int bestDis2 = 1000000000;
    for (const tagArmy &enemy : info.enemy_armies)
    {
        int dis2 = BlockDis2(army.BlockDR, army.BlockUR, enemy.BlockDR, enemy.BlockUR);
        if (dis2 > radius * radius)
            continue;
        int priority = ArmyTargetPriority(enemy);
        if (priority < bestPriority || (priority == bestPriority && dis2 < bestDis2))
        {
            bestPriority = priority;
            bestDis2 = dis2;
            bestSN = enemy.SN;
        }
    }
    return bestSN;
}

static int ResourceBucket(int resourceType)
{
  // 食物来源只保留浆果丛与瞪羚；大象会反击农民、击杀损失过大，不纳入采集。
  if (resourceType == RESOURCE_BUSH || resourceType == RESOURCE_GAZELLE)
    return 0;
  if (resourceType == RESOURCE_TREE)
    return 1;
  if (resourceType == RESOURCE_STONE)
    return 2;
  if (resourceType == RESOURCE_GOLD)
    return 3;
  return -1;
}

static bool IsGatherableResource(const tagResource &resource)
{
    return resource.Cnt > 0 || resource.Blood > 0;
}

// 视野内是否已经出现瞪羚（食物来源之一，见 ResourceBucket）。
// 用途：祭司探路的终止条件 —— 探路就是为了给农民找到食物，找到了就收工。
static bool HasVisibleGazelle()
{
  for (const tagResource &resource : info.resources) {
    if (resource.Type == RESOURCE_GAZELLE && resource.Blood > 0)
      return true;
  }
  return false;
}

// 结束祭司探路并记录原因。
// 几个退出条件（找到瞪羚 / 没有可用前沿 / 前沿都超出半径 / 连续卡住）
// 在观感上都是「祭司不动了」，只有这行日志能把它们区分开。
static void FinishPriestExplore(const char *reason)
{
  if (priestExploreDone)
    return;
  priestExploreDone = true;
  char buf[192];
  snprintf(buf, sizeof(buf), "[PRIEST-EXPLORE] f=%d end reason=%s", g_frame,
           reason);
  AiDebugLog(buf);
}

static bool IsGatherableFarm(const tagBuilding &building) {
  return building.Type == BUILDING_FARM && building.Percent >= 100 &&
         building.Blood > 0 && building.Cnt > 0;
}

static bool HasGatherableResourceType(int bucket)
{
    for (const tagResource &resource : info.resources)
    {
        if (ResourceBucket(resource.Type) == bucket &&
            IsGatherableResource(resource))
            return true;
    }
    // 农场是食物来源，仅对食物 bucket 生效。
    if (bucket == 0) {
      for (const tagBuilding &building : info.buildings) {
        if (IsGatherableFarm(building))
          return true;
      }
    }
    return false;
}

static bool IsFarmerBuilding(const tagFarmer &farmer)
{
    if (farmer.WorkObjectSN < 0)
        return false;
    for (const tagBuilding &building : info.buildings)
    {
        if (building.SN == farmer.WorkObjectSN && building.Blood > 0 &&
            building.Percent < 100)
            return true;
    }
    return false;
}

static void CalculateFarmerTargets(int targets[4], int current[4])
{
    for (int bucket = 0; bucket < 4; bucket++)
    {
        targets[bucket] = 0;
        current[bucket] = 0;
    }

    int farmerCount = 0;
    for (const tagFarmer &farmer : info.farmers)
    {
        if (farmer.FarmerSort != FARMERTYPE_FARMER || farmer.Blood <= 0)
            continue;
        farmerCount++;

        for (const tagResource &resource : info.resources)
        {
            // WorkObjectSN 比 ResourceSort 更可靠：后者可能表示农民手里携带的资源。
            if (farmer.WorkObjectSN == resource.SN &&
                IsGatherableResource(resource))
            {
                const int bucket = ResourceBucket(resource.Type);
                if (bucket >= 0)
                    current[bucket]++;
                break;
            }
        }
        // 农场属于食物来源，SN 全局唯一，与资源目标不会重复计数。
        for (const tagBuilding &building : info.buildings) {
          if (farmer.WorkObjectSN == building.SN &&
              IsGatherableFarm(building)) {
            current[0]++;
            break;
          }
        }
    }

    // Core 快照尚未建立关系时，用 pending 预占补齐配额统计；
    // 已建立关系的 pending 由真实 WorkObjectSN 统计覆盖，跳过避免双重计数。
    for (map<int, PendingGatherOrder>::const_iterator it =
             pendingGatherOrders.begin();
         it != pendingGatherOrders.end(); ++it)
    {
      if (!IsAliveFarmerSN(it->first) ||
          IsFarmerRelationEstablished(it->first, it->second.targetSN))
        continue;
      for (const tagResource &resource : info.resources) {
        if (resource.SN != it->second.targetSN ||
            !IsGatherableResource(resource))
          continue;
        const int bucket = ResourceBucket(resource.Type);
        if (bucket >= 0)
          current[bucket]++;
        break;
      }
        for (const tagBuilding &building : info.buildings) {
          if (building.SN == it->second.targetSN &&
              IsGatherableFarm(building)) {
            current[0]++;
            break;
          }
        }
    }

    if (farmerCount == 0)
        return;

    // 前期优先保障食物，避免生产和侦察计划因食物短缺停滞。
    // 木材给基础权重持续采集，黄金在青铜时代后也持续采集，避免「缺了才采」的波动。
    int weight[4] = {20, 7, 0, 0};
    if (info.Meat < 600)
        weight[0] += 5;
    else if (info.civilizationStage != CIVILIZATION_TOOLAGE)
      weight[0] += 3;

    // 靶场建成说明已进入产兵阶段（弓兵与战车弓兵都要靶场）。
    const bool producingUnits = HasBuilding(BUILDING_RANGE);
    // 该阶段的木头上限：战车弓兵 70 木/个、目标 25~30 个，光这一项就要
    // 1750~2100 木，500 的库存撑不住几轮补兵 —— 会变成「采满 → 造兵花光 →
    // 再采」的震荡，兵员补充断断续续。
    const int woodStockCap = producingUnits ? 1000 : 500;

    // 铜器时代之前、马厩建成后停止伐木：升铜器时代要攒 800 食物，而这时
    // 前置建筑（谷仓 / 兵营 / 市场 / 马厩 / 靶场）的木头需求已经满足，
    // 把农民全部让给食物，缩短升时代的时间。
    // 用 FindBuildingByType(.., true)（内含 Percent >= 100 判定）而不是
    // HasBuilding —— 后者只要 Blood > 0 就算数，会把正在盖的马厩当成建好。
    const bool stopWoodForBronze =
        info.civilizationStage == CIVILIZATION_TOOLAGE &&
        FindBuildingByType(BUILDING_STABLE, true) != nullptr;

    if (stopWoodForBronze || info.Wood >= woodStockCap) {
      // 停止伐木，把农民让给食物/黄金/石头。
      weight[1] = 0;
    } else {
      if (info.Wood < 300)
        weight[1] += 8;
      // 建房阶段（市场/马厩/靶场/兵营未齐）木头是硬约束：
      // 这批建筑共需约 575 木，木头不足会导致升时代条件无法凑齐，
      // 因此额外加权，让伐木优先级高于此阶段的食物积累。
      if (!HasBuilding(BUILDING_MARKET) || !HasBuilding(BUILDING_STABLE) ||
          !HasBuilding(BUILDING_RANGE) || !HasBuilding(BUILDING_ARMYCAMP))
        weight[1] += 8;
      // 产兵阶段木头是主消耗：战车弓兵 40 食 + 70 木，木头需求高于食物
      // （目标 25~30 个 = 1750~2100 木）。基础权重 7 远低于食物的 20+，
      // 不抬高就会让木头成为产兵瓶颈，战车弓兵一直凑不齐。
      if (producingUnits)
        weight[1] += 12;
    }

    // 人口接近上限且仍需补房屋时临时加木权：房屋每间 30 木、+4 人口，
    // 木头不足会导致人口卡死、军队无法扩充（实测人口会停在 28~32）。
    // 只在真正需要时抬高，平时不干扰食物采集。
    if (info.Human_Num + 2 >= info.Human_MaxNum &&
        !HasIncompleteBuilding(BUILDING_HOME))
      weight[1] += 10;

    // 核心建筑齐备后全力食物：这一阶段食物是持续消耗（产兵/升时代），
    // 木头已不再是大头，把农民从伐木转回食物。
    if (HasBuilding(BUILDING_MARKET) && HasBuilding(BUILDING_STABLE) &&
        HasBuilding(BUILDING_RANGE))
      weight[0] += 5;

    // 【暂停】黄金采集：当前兵种配比（战车弓兵 / 弓兵 / 战车）与所研发的科技
    // （车轮、木材加工、复合弓）都不消耗黄金，采金没有去处，只会抢走食物与
    // 木材的采集力。将来恢复金系兵种或金系科技时再打开。
    // if (info.civilizationStage != CIVILIZATION_TOOLAGE && info.Gold < 500)
    //   weight[3] += 6;

    // 石头：造箭塔需要石头（每个 150），仅在箭塔还要建时高权重采石；
    // 箭塔建满、或过了第三波的停建帧之后石头无其他用途，
    // 停止采集把农民让给食物/木头/黄金。
    if (ArrowTowerStillWanted())
      weight[2] += 12;

    // const bool nearPopulationCap = info.Human_Num + 1 >= info.Human_MaxNum;
    // if (nearPopulationCap || HasIncompleteBuilding(BUILDING_HOME))
    //     weight[1] += 2;

    int totalWeight = 0;
    for (int bucket = 0; bucket < 4; bucket++)
    {
        if (weight[bucket] > 0 && HasGatherableResourceType(bucket))
            totalWeight += weight[bucket];
        else
            weight[bucket] = 0;
    }
    if (totalWeight == 0)
        return;

    int allocated = 0;
    for (int bucket = 0; bucket < 4; bucket++)
    {
        targets[bucket] = farmerCount * weight[bucket] / totalWeight;
        allocated += targets[bucket];
    }

    // 最大余数不足以保留时，优先补给最高权重资源。
    while (allocated < farmerCount)
    {
        int bestBucket = -1;
        for (int bucket = 0; bucket < 4; bucket++)
        {
            if (weight[bucket] <= 0 || !HasGatherableResourceType(bucket))
                continue;
            if (bestBucket < 0 || weight[bucket] > weight[bestBucket])
                bestBucket = bucket;
        }
        if (bestBucket < 0)
            break;
        targets[bestBucket]++;
        allocated++;
    }
}

static bool IsFarmerClusterCrowded(int blockDR, int blockUR, int excludeSN)
{
    int nearbyFarmers = 0;
    for (const tagFarmer &other : info.farmers)
    {
        if (other.SN == excludeSN || other.Blood <= 0 ||
            other.FarmerSort != FARMERTYPE_FARMER)
            continue;

        // 将目标点周围相邻的局部区域限制为最多四名农民，减少互相卡位。
        if (abs(other.BlockDR - blockDR) <= 1 &&
            abs(other.BlockUR - blockUR) <= 1)
        {
            ++nearbyFarmers;
            if (nearbyFarmers >= 3)
                return true;
        }
    }
    return false;
}

static bool IsAliveFarmerSN(int farmerSN)
{
    for (const tagFarmer &farmer : info.farmers)
    {
        if (farmer.SN == farmerSN)
            return farmer.Blood > 0;
    }
    return false;
}

// 判断农民是否已建立到 targetSN 的采集关系；已建立时真实 WorkObjectSN
// 统计已覆盖， pending 预占不应重复计入，避免 worker 统计虚高导致派工不足。
static bool IsFarmerRelationEstablished(int farmerSN, int targetSN) {
  for (const tagFarmer &farmer : info.farmers) {
    if (farmer.SN == farmerSN)
      return farmer.WorkObjectSN == targetSN;
  }
  return false;
}

static bool IsVisibleResourceSN(int targetSN) {
  for (const tagResource &resource : info.resources) {
    if (resource.SN == targetSN)
      return true;
  }
  for (const tagBuilding &building : info.buildings) {
    if (building.Type == BUILDING_FARM && building.SN == targetSN)
      return true;
  }
  return false;
}

static void RecordResourceAttemptFailure(int resourceSN)
{
    if (resourceSN < 0)
        return;

    ResourceAttemptState &state = resourceAttemptState[resourceSN];
    if (state.failureCount < 4)
        state.failureCount++;

    int cooldown = USR_RESOURCE_FAIL_COOLDOWN;
    for (int i = 1; i < state.failureCount; i++)
    {
        if (cooldown >= USR_RESOURCE_FAIL_COOLDOWN_MAX / 2)
        {
            cooldown = USR_RESOURCE_FAIL_COOLDOWN_MAX;
            break;
        }
        cooldown *= 2;
    }
    if (cooldown > USR_RESOURCE_FAIL_COOLDOWN_MAX)
        cooldown = USR_RESOURCE_FAIL_COOLDOWN_MAX;

    state.lastAttemptFrame = g_frame;
    state.cooldownUntilFrame = g_frame + cooldown;
}

static void CleanupFarmerResourceState()
{
    for (map<int, PendingGatherOrder>::iterator it = pendingGatherOrders.begin();
         it != pendingGatherOrders.end();)
    {
        const int farmerSN = it->first;
        PendingGatherOrder &pending = it->second;
        if (!IsAliveFarmerSN(farmerSN))
        {
            it = pendingGatherOrders.erase(it);
        }
        else if (!IsVisibleResourceSN(pending.targetSN))
        {
            // 目标消失通常表示已被耗尽或删除，不应把正常完成误记为失败。
            it = pendingGatherOrders.erase(it);
        }
        else
            ++it;
    }

    for (map<int, ResourceAttemptState>::iterator it = resourceAttemptState.begin();
         it != resourceAttemptState.end();)
    {
        if (!IsVisibleResourceSN(it->first) &&
            g_frame >= it->second.cooldownUntilFrame)
            it = resourceAttemptState.erase(it);
        else
            ++it;
    }
}

static void ProcessPendingGatherOrders()
{
    if (farmerResourceStateFrame != USR_INVALID_FRAME &&
        g_frame < farmerResourceStateFrame)
    {
        resourceAttemptState.clear();
        pendingGatherOrders.clear();
        currentTarget.clear();
        waveRetaliationTarget.clear();
        waveThreatFirstSeenFrame.clear();
        farmerLastOrderFrame.clear();
        farmerThreatLastFrame.clear();
        farmerSafeSinceFrame.clear();
        fieldSelfDefenseLastOrderFrame.clear();
        lastEconomyOrderFrame = USR_INVALID_FRAME;
        lastBuildOrderFrame = USR_INVALID_FRAME;
        lastConstructionRecoveryFrame = USR_INVALID_FRAME;
        lastBuildingActionFrame = USR_INVALID_FRAME;
        lastProductionActionFrame = USR_INVALID_FRAME;
        buildOrderId = -1;
        buildOrderType = -1;
        buildFarmerSN = -1;
        armyCampOrderId = -1;
        stableOrderFrame = USR_INVALID_FRAME;
        technologyOrderId = -1;
        technologyOrderFrame = USR_INVALID_FRAME;
        technologyPendingAction = -1;
        researchedTechCount = 0;
        clubmanUpgradeOrderId = -1;
        broadswordUpgradeOrderId = -1;
        techOrderFrame.clear();
        lastTechRet = -1;
        lastTechFrame = USR_INVALID_FRAME;
        stockTechCursor = 0;
        stockTechOrderId = -1;
        wheelTechReady = false;
        wheelSeenRunning = false;
        wheelIdleSinceFrame = USR_INVALID_FRAME;
        marketCooldownUntil[0] = 0;
        marketCooldownUntil[1] = 0;
        marketCooldownUntil[2] = 0;
        marketOrderId = -1;
        marketOrderSlot = -1;
        marketOrderFrame = USR_INVALID_FRAME;
        chariotArcherOrderId = -1;
        chariotArcherRet = -999;
        priestEmergencyTarget = make_pair(-1, -1);
        priestEmergencyTargetFrame = USR_INVALID_FRAME;
        priestFrontierTarget = make_pair(-1, -1);
        priestFrontierStuckCount = 0;
        priestWasConverting = false;
        priestRetreatUntilFrame = USR_INVALID_FRAME;
        priestMoveFromDR = -1;
        priestMoveFromUR = -1;
        priestExploreDone = false;
        priestMoveLastRet = -999;
        priestMoveLastRetFrame = USR_INVALID_FRAME;
        scoutIsRecon.clear();
        scoutLuredTarget.clear();
        sacrificeOrderId = -1;
        sacrificeFarmerSN = -1;
        sacrificeHpBefore = -1;
        priestDecoyByThreat.clear();
    }
    farmerResourceStateFrame = g_frame;

    CleanupFarmerResourceState();

    for (map<int, PendingGatherOrder>::iterator it = pendingGatherOrders.begin();
         it != pendingGatherOrders.end();)
    {
        const int farmerSN = it->first;
        PendingGatherOrder &pending = it->second;
        const tagFarmer *farmerState = NULL;
        for (const tagFarmer &farmer : info.farmers)
        {
            if (farmer.SN == farmerSN)
            {
                farmerState = &farmer;
                break;
            }
        }

        // ins_ret 会跨帧保留，因此结果只在第一次观察到时记录。
        if (pending.result == USR_INVALID_FRAME)
        {
            map<int, int>::const_iterator result =
                info.ins_ret.find(pending.orderId);
            if (result != info.ins_ret.end())
            {
                pending.result = result->second;
                pending.resultFrame = g_frame;
                if (pending.result != ACTION_SUCCESS)
                {
                    RecordResourceAttemptFailure(pending.targetSN);
                    it = pendingGatherOrders.erase(it);
                    continue;
                }
            }
        }

        // 关系建立后不立即释放预占：农民可能被障碍卡住，关系随后被 Core 取消。
        // 只要还有工作对象就保留 pending，避免卡住后反复派工同一资源。
        if (farmerState != NULL && farmerState->WorkObjectSN != -1) {
          if (pending.relationFrame == USR_INVALID_FRAME)
            pending.relationFrame = g_frame;
          ++it;
          continue;
        }

        // 关系建立后又回到空闲，说明目标不可达或关系被取消，记录失败并退避。
        if (pending.relationFrame != USR_INVALID_FRAME && farmerState != NULL &&
            farmerState->NowState == HUMAN_STATE_IDLE) {
          RecordResourceAttemptFailure(pending.targetSN);
          it = pendingGatherOrders.erase(it);
          continue;
        }

        if (pending.result == ACTION_SUCCESS &&
            g_frame - pending.resultFrame >= USR_RESOURCE_PENDING_GRACE &&
            farmerState != NULL &&
            farmerState->NowState == HUMAN_STATE_IDLE)
        {
            // 成功返回后若农民已恢复空闲且关系仍未建立，说明目标不可用。
            RecordResourceAttemptFailure(pending.targetSN);
            it = pendingGatherOrders.erase(it);
            continue;
        }

        if (pending.result == USR_INVALID_FRAME &&
            g_frame - pending.submitFrame >= USR_RESOURCE_PENDING_GRACE &&
            farmerState != NULL && farmerState->NowState == HUMAN_STATE_IDLE)
        {
            RecordResourceAttemptFailure(pending.targetSN);
            it = pendingGatherOrders.erase(it);
            continue;
        }

        if (g_frame - pending.submitFrame >=
            USR_RESOURCE_PENDING_MAX_LIFETIME)
        {
            // 非空闲状态通常表示订单被建造、自卫等逻辑覆盖，只释放预占。
            it = pendingGatherOrders.erase(it);
            continue;
        }
        ++it;
    }
}

static void CancelPendingGatherOrder(int farmerSN)
{
    pendingGatherOrders.erase(farmerSN);
}

static int ResourceHardCapacity(int bucket)
{
    return bucket == 0 ? USR_RESOURCE_HARD_CAP_FOOD
                       : USR_RESOURCE_HARD_CAP_OTHER;
}

// 返回资源点到最近交付建筑（市镇中心 + 特殊建筑）的距离平方。
// 特殊建筑：仓库（木头/石头/黄金/猎物食物）或谷仓（农场食物）。
static int FindNearestReturnBuildingDistance(int specialBuilding,
                                             int blockDR, int blockUR)
{
    int bestDis2 = 1000000000;
    for (const tagBuilding &building : info.buildings)
    {
        if (building.Blood <= 0 || building.Percent < 100)
            continue;
        if (building.Type != BUILDING_CENTER &&
            building.Type != specialBuilding)
            continue;
        const int dis2 = BlockDis2(blockDR, blockUR,
                                   building.BlockDR, building.BlockUR);
        if (dis2 < bestDis2)
            bestDis2 = dis2;
    }
    return bestDis2;
}

static int FindBestResourceSN(const tagFarmer &farmer, int desiredBucket,
                              const int current[4])
{
    int bestSN = -1;
    int bestScore = 1000000000;
    map<int, int> resourceWorkers;

    for (const tagFarmer &other : info.farmers)
    {
        if (other.Blood <= 0 || other.WorkObjectSN < 0)
            continue;
        resourceWorkers[other.WorkObjectSN]++;
    }

    for (map<int, PendingGatherOrder>::const_iterator it =
             pendingGatherOrders.begin();
         it != pendingGatherOrders.end(); ++it)
    {
      if (it->first != farmer.SN && IsAliveFarmerSN(it->first) &&
          !IsFarmerRelationEstablished(it->first, it->second.targetSN))
        resourceWorkers[it->second.targetSN]++;
    }

    for (const tagResource &resource : info.resources)
    {
        const int bucket = ResourceBucket(resource.Type);
        if (bucket != desiredBucket || !IsGatherableResource(resource))
            continue;

        map<int, ResourceAttemptState>::const_iterator attemptIt =
            resourceAttemptState.find(resource.SN);
        if (attemptIt != resourceAttemptState.end() &&
            g_frame < attemptIt->second.cooldownUntilFrame)
            continue;

        const int workers = resourceWorkers[resource.SN];
        if (workers >= ResourceHardCapacity(bucket))
            continue;

        if (IsFarmerClusterCrowded(resource.BlockDR, resource.BlockUR,
                                   farmer.SN))
            continue;

        const int distance = BlockDis2(farmer.BlockDR, farmer.BlockUR,
                                       resource.BlockDR, resource.BlockUR);
        const int returnDistance = FindNearestReturnBuildingDistance(
            BUILDING_STOCK, resource.BlockDR, resource.BlockUR);
        const int softCapacity = bucket == 0 ? 4 : 3;
        const int crowdPenalty = workers >= softCapacity
                                     ? (workers - softCapacity + 1) * 80
                                     : workers * 12;
        const int remainingPenalty = resource.Cnt > 0 && resource.Cnt < 100
                                         ? 100
                                         : 0;
        // 瞪羚要追上才能击杀，而它会逃跑（Animal.cpp:217 的 isMonitorObject），
        // 比站着不动的浆果丛费时，因此保留一点轻微的代价。
        // 只给 10：它**不会反击**（AnimalAtk 里瞪羚是 0、阵营 FRIENDLY_FRI，
        // 见 MainWidget.cpp:2454-2456），而且农民比它快 50%
        // （HUMAN_SPEED 2.236 vs ANIMAL_SPEED 1.491，config.json:36/38），
        // 追杀并无风险。原先的 60 相当于把羚羊在打分里推后 60 格，
        // 结果只要附近有浆果丛就永远轮不到它。
        const int huntPenalty = resource.Type == RESOURCE_GAZELLE ? 10 : 0;
        const int score = distance + returnDistance + crowdPenalty +
                          remainingPenalty + huntPenalty -
                          (current[bucket] > 0 ? 0 : 2);
        if (score < bestScore)
        {
            bestScore = score;
            bestSN = resource.SN;
        }
    }

    // 农场是食物来源，与野果/瞪羚/大象同 bucket 竞争，按同一打分公式择优。
    if (desiredBucket == 0) {
      for (const tagBuilding &building : info.buildings) {
        if (!IsGatherableFarm(building))
          continue;

        map<int, ResourceAttemptState>::const_iterator attemptIt =
            resourceAttemptState.find(building.SN);
        if (attemptIt != resourceAttemptState.end() &&
            g_frame < attemptIt->second.cooldownUntilFrame)
          continue;

        // 每块农田同一时刻只派一个农民：Core 每帧重置地主，其他农民无法采集。
        const int workers = resourceWorkers[building.SN];
        if (workers >= 1)
          continue;

        if (IsFarmerClusterCrowded(building.BlockDR, building.BlockUR,
                                   farmer.SN))
          continue;

        const int distance = BlockDis2(farmer.BlockDR, farmer.BlockUR,
                                       building.BlockDR, building.BlockUR);
        const int returnDistance = FindNearestReturnBuildingDistance(
            BUILDING_GRANARY, building.BlockDR, building.BlockUR);
        const int softCapacity = 4;
        const int crowdPenalty = workers >= softCapacity
                                     ? (workers - softCapacity + 1) * 80
                                     : workers * 12;
        const int remainingPenalty = building.Cnt < 100 ? 100 : 0;
        const int score = distance + returnDistance + crowdPenalty +
                          remainingPenalty - (current[0] > 0 ? 0 : 2);
        if (score < bestScore) {
          bestScore = score;
          bestSN = building.SN;
        }
      }
    }
    return bestSN;
}

static int FindBuilderFarmerSN()
{
    int bestSN = -1;
    int bestDis2 = 1000000000;
    const tagBuilding *center = FindBuildingByType(BUILDING_CENTER, true);

    // 优先使用空闲农民，避免打断正在采集或建造的工作。
    for (const tagFarmer &farmer : info.farmers)
    {
        if (farmer.FarmerSort != FARMERTYPE_FARMER ||
            farmer.Blood <= 0 || farmer.NowState != HUMAN_STATE_IDLE)
            continue;

        int dis2 = center ? BlockDis2(farmer.BlockDR, farmer.BlockUR,
                                      center->BlockDR, center->BlockUR)
                          : 0;
        if (dis2 < bestDis2)
        {
            bestDis2 = dis2;
            bestSN = farmer.SN;
        }
    }
    if (bestSN != -1)
        return bestSN;

    // 人口达到上限时不能等待所有农民自然空闲，否则无法补房。
    // 但建造中的农民不能被新的 HumanBuild 或采集命令抢占。
    bestDis2 = 1000000000;
    for (const tagFarmer &farmer : info.farmers)
    {
        if (farmer.FarmerSort != FARMERTYPE_FARMER ||
            farmer.Blood <= 0 || farmer.NowState != HUMAN_STATE_WORKING)
            continue;

        bool building = false;
        for (const tagBuilding &candidate : info.buildings)
        {
            if (candidate.SN == farmer.WorkObjectSN && candidate.Blood > 0 &&
                candidate.Percent < 100)
            {
                building = true;
                break;
            }
        }
        if (building)
            continue;

        int dis2 = center ? BlockDis2(farmer.BlockDR, farmer.BlockUR,
                                      center->BlockDR, center->BlockUR)
                          : 0;
        if (dis2 < bestDis2)
        {
            bestDis2 = dis2;
            bestSN = farmer.SN;
        }
    }
    return bestSN;
}

static bool IsBuildCandidateUsable(int blockDR, int blockUR, int buildingType)
{
    if (!info.theMap)
        return false;

    // Core 会按建筑类型检查实际占地；这里使用对应的保守尺寸提前筛除候选点。
    const int buildSize = (buildingType == BUILDING_HOME ||
                           buildingType == BUILDING_ARROWTOWER)
                              ? 2
                              : 3;
    if (blockDR < USR_ARROWTOWER_BUILD_MIN_MARGIN ||
        blockUR < USR_ARROWTOWER_BUILD_MIN_MARGIN ||
        blockDR + buildSize > MAP_L - USR_ARROWTOWER_BUILD_MIN_MARGIN ||
        blockUR + buildSize > MAP_U - USR_ARROWTOWER_BUILD_MIN_MARGIN)
        return false;

    const int baseHeight = (*info.theMap)[blockDR][blockUR].height;
    for (int dr = blockDR; dr < blockDR + buildSize; ++dr)
    {
        for (int ur = blockUR; ur < blockUR + buildSize; ++ur)
        {
            if ((*info.theMap)[dr][ur].type == MAPPATTERN_OCEAN ||
                (*info.theMap)[dr][ur].type == MAPPATTERN_UNKNOWN ||
                (*info.theMap)[dr][ur].height != baseHeight)
                return false;
        }
    }

    // tagInfo 不提供内核占用栅格，因此根据可见对象的实际位置做保守排除。
    const int clearance = 1;
    const auto overlaps = [blockDR, blockUR, buildSize, clearance](
                              int objectDR, int objectUR, int objectSize)
    {
        return blockDR - clearance < objectDR + objectSize &&
               blockDR + buildSize + clearance > objectDR &&
               blockUR - clearance < objectUR + objectSize &&
               blockUR + buildSize + clearance > objectUR;
    };

    for (const tagBuilding &building : info.buildings)
    {
        const int objectSize = (building.Type == BUILDING_HOME ||
                                building.Type == BUILDING_ARROWTOWER)
                                   ? 2
                                   : 3;
        if (overlaps(building.BlockDR, building.BlockUR, objectSize))
            return false;
    }
    for (const tagResource &resource : info.resources)
    {
        if (overlaps(resource.BlockDR, resource.BlockUR, 1))
            return false;
    }
    for (const tagFarmer &farmer : info.farmers)
    {
        if (farmer.Blood > 0 &&
            overlaps(farmer.BlockDR, farmer.BlockUR, 1))
            return false;
    }
    for (const tagArmy &army : info.armies)
    {
        if (army.Blood > 0 && overlaps(army.BlockDR, army.BlockUR, 1))
            return false;
    }
    return true;
}

// 返回「我方基地前线」的归一化方向（各轴 -1/0/1），用于让箭塔面向敌人来路。
// 主依据：市镇中心－房屋连线的延长线上、远离房屋的一端。
//   房屋由地图固定布置在基地后方（四张地图实测其与敌方方向的夹角均 > 90°），
//   方向稳定且开局即可用，不依赖侦察；房屋被拆光时回退到敌方位置。
static pair<int, int> GetEnemyDirection()
{
    const tagBuilding *center = FindCenter();
    if (!center)
        return make_pair(0, 0);

    int targetDR = -1;
    int targetUR = -1;

    // 主依据：远离房屋的一端（房屋 → 中心的延长方向 = 前线）。
    for (const tagBuilding &building : info.buildings)
    {
        if (building.Type == BUILDING_HOME && building.Blood > 0)
        {
            targetDR = 2 * center->BlockDR - building.BlockDR;
            targetUR = 2 * center->BlockUR - building.BlockUR;
            break;
        }
    }
    // 回退：房屋全被摧毁时改用敌方位置。
    if (targetDR == -1)
    {
        for (const tagBuilding &building : info.enemy_buildings)
        {
            if (building.Blood > 0)
            {
                targetDR = building.BlockDR;
                targetUR = building.BlockUR;
                break;
            }
        }
    }
    if (targetDR == -1 && enemyBaseBlockDR >= 0 && enemyBaseBlockUR >= 0)
    {
        targetDR = enemyBaseBlockDR;
        targetUR = enemyBaseBlockUR;
    }
    if (targetDR == -1)
        return make_pair(0, 0);

    const int dx = targetDR - center->BlockDR;
    const int dy = targetUR - center->BlockUR;
    return make_pair(dx > 0 ? 1 : (dx < 0 ? -1 : 0),
                     dy > 0 ? 1 : (dy < 0 ? -1 : 0));
}

static pair<int, int> GetBuildCandidate(int buildingType)
{
    const tagBuilding *center = FindCenter();
    if (!center)
        return make_pair(-1, -1);

    // 箭塔：围绕市镇中心呈四方向（90° 间隔）环形布置，形成防守圈。
    // 敌方波次以追击祭司/最近的农民为目标，来向不固定，因此均匀覆盖全向。
    if (buildingType == BUILDING_ARROWTOWER)
    {
        // [圈层][方向][xy]：近圈 6 格、主圈 9 格、远圈 12 格。
        //
        // 近圈放最前且压到 6 格：箭塔射程只有 7 格（DIS_ARROWTOWER），
        // 建在 9 格处距中心 9 > 7，中心本身反而打不到，中心外半径 2 格内
        // 成了盲区 —— 而祭司平时就待在中心旁，正好落在盲区里挨打而无人掩护。
        // 6 格 < 7 格，前 4 座塔（循环从 ring 0 起、每个 ring 先试完四个方向，
        // 所以最早 4 座都在同一圈）就能把中心和祭司的待机位置罩住。
        // 箭塔**分散**建在基地外围：三圈 × 四方向，近圈 6 格起。
        // 分散的意义是让祭司能在塔与塔之间「风筝」敌人（见
        // GetPriestEmergencyPoint）：跑到离敌人最远的那座塔，
        // 一路上始终有某座塔的火力掩护，而敌人得跟着跑。
        // 先前试过把塔聚成一簇，那样只在簇附近有掩护，祭司一旦离簇就失去保护。
        static const int TOWER_RINGS[3][4][2] = {
            {{0, -6}, {6, 0}, {0, 6}, {-6, 0}},
            {{0, -9}, {9, 0}, {0, 9}, {-9, 0}},
            {{0, -12}, {12, 0}, {0, 12}, {-12, 0}}};
        // 按已有箭塔数量错开起始方向，保证新塔依次填满不同方向/圈层。
        const int startDir = CountBuilding(BUILDING_ARROWTOWER) % 4;
        for (int ring = 0; ring < 3; ring++)
        {
            for (int k = 0; k < 4; k++)
            {
                const int v = (startDir + k) % 4;
                const int dr = center->BlockDR + TOWER_RINGS[ring][v][0];
                const int ur = center->BlockUR + TOWER_RINGS[ring][v][1];
                if (IsBuildCandidateUsable(dr, ur, buildingType))
                    return make_pair(dr, ur);
            }
        }
        // 环形位置全部不可用 → 落到通用扫描。
    }

    static const int OFFSETS[][2] = {
        {6, 0}, {0, 6}, {-6, 0}, {0, -6},
        {7, 4}, {4, 7}, {-7, 4}, {-4, 7},
        {7, -4}, {4, -7}, {-7, -4}, {-4, -7},
        {10, 0}, {0, 10}, {-10, 0}, {0, -10},
        {13, 7}, {7, 13}, {-13, 7}, {-7, 13}
    };
    const int count = static_cast<int>(sizeof(OFFSETS) / sizeof(OFFSETS[0]));
    const int radiusAddition = 0;

    for (int step = 0; step < count; step++)
    {
        const int index = (buildCandidateIndex + step) % count;
        int dr = center->BlockDR + OFFSETS[index][0];
        int ur = center->BlockUR + OFFSETS[index][1];
        if (radiusAddition != 0)
        {
            dr += OFFSETS[index][0] > 0 ? radiusAddition :
                  (OFFSETS[index][0] < 0 ? -radiusAddition : 0);
            ur += OFFSETS[index][1] > 0 ? radiusAddition :
                  (OFFSETS[index][1] < 0 ? -radiusAddition : 0);
        }
        if (IsBuildCandidateUsable(dr, ur, buildingType))
        {
            buildCandidateIndex = (index + 1) % count;
            return make_pair(dr, ur);
        }
    }

    // 固定偏移全部不可用时，按距离市镇中心由近到远扫描，避免候选点集中在同一组障碍物上。
    const int maxRadius = max(MAP_L, MAP_U);
    for (int radius = 1; radius <= maxRadius; ++radius)
    {
        for (int drOffset = -radius; drOffset <= radius; ++drOffset)
        {
            for (int urOffset = -radius; urOffset <= radius; ++urOffset)
            {
                if (max(abs(drOffset), abs(urOffset)) != radius)
                    continue;
                const int dr = center->BlockDR + drOffset;
                const int ur = center->BlockUR + urOffset;
                if (IsBuildCandidateUsable(dr, ur, buildingType))
                    return make_pair(dr, ur);
            }
        }
    }
    return make_pair(-1, -1);
}

// 在指定锚点附近扫描一个可建造位置，用于在资源群旁建仓库/谷仓。
static pair<int, int> GetBuildCandidateNear(int anchorDR, int anchorUR,
                                            int buildingType)
{
    static const int OFFSETS[][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {2, 0}, {-2, 0}, {0, 2}, {0, -2},
        {2, 2}, {-2, 2}, {2, -2}, {-2, -2},
        {3, 0}, {-3, 0}, {0, 3}, {0, -3},
        {4, 0}, {-4, 0}, {0, 4}, {0, -4}
    };
    const int count = static_cast<int>(sizeof(OFFSETS) / sizeof(OFFSETS[0]));
    for (int i = 0; i < count; i++)
    {
        const int dr = anchorDR + OFFSETS[i][0];
        const int ur = anchorUR + OFFSETS[i][1];
        if (IsBuildCandidateUsable(dr, ur, buildingType))
            return make_pair(dr, ur);
    }
    return make_pair(-1, -1);
}

static bool TryAssignIdleFarmer(UsrAI *ai)
{
    // 订单结果和超时状态必须每次调度调用都回收，不能被经济下单节流延迟。
    ProcessPendingGatherOrders();
    // 每农民已有 farmerLastOrderFrame 节流，不再用全局节流，
    // 避免多农民同时空闲（建筑完工/资源耗尽）时派工过慢。

    int target[4] = {0, 0, 0, 0};
    int assigned[4] = {0, 0, 0, 0};
    // 每次派工都按 Core 快照和 pending 预占重算，避免失败或抢占后配额滞后。
    CalculateFarmerTargets(target, assigned);

    for (const tagFarmer &farmer : info.farmers)
    {
        if (farmer.FarmerSort != FARMERTYPE_FARMER || farmer.Blood <= 0 ||
            farmer.NowState != HUMAN_STATE_IDLE)
            continue;
        if (IsFarmerBuilding(farmer) ||
            (farmer.SN == buildFarmerSN && buildOrderId != -1))
            continue;
        if (pendingGatherOrders.find(farmer.SN) != pendingGatherOrders.end())
            continue;

        // 被威胁的农民必须先经过安全滞后，避免敌人刚离开就反复撤退/采集。
        map<int, int>::const_iterator threatIt = farmerThreatLastFrame.find(farmer.SN);
        if (threatIt != farmerThreatLastFrame.end())
        {
            map<int, int>::const_iterator safeIt = farmerSafeSinceFrame.find(farmer.SN);
            if (safeIt == farmerSafeSinceFrame.end() ||
                g_frame - safeIt->second < 120)
                continue;
        }

        map<int, int>::const_iterator orderIt = farmerLastOrderFrame.find(farmer.SN);
        if (orderIt != farmerLastOrderFrame.end() &&
            g_frame - orderIt->second < USR_ECONOMY_ORDER_INTERVAL)
            continue;

        int desiredBucket = 0;
        int largestDeficit = 0;
        for (int bucket = 0; bucket < 4; bucket++)
        {
            const int deficit = target[bucket] - assigned[bucket];
            if (deficit > largestDeficit)
            {
                largestDeficit = deficit;
                desiredBucket = bucket;
            }
        }

        // 没有配额缺口时，空闲农民才补到食物，避免无意义地改派农民。
        int targetSN = FindBestResourceSN(farmer, desiredBucket, assigned);
        if (targetSN < 0)
        {
            for (int bucket = 0; bucket < 4 && targetSN < 0; bucket++)
            {
                if (bucket == desiredBucket || target[bucket] <= assigned[bucket])
                    continue;
                targetSN = FindBestResourceSN(farmer, bucket, assigned);
                if (targetSN >= 0)
                    desiredBucket = bucket;
            }
        }
        if (targetSN < 0)
            continue;

        const int orderId = ai->HumanAction(farmer.SN, targetSN);
        PendingGatherOrder pending;
        pending.orderId = orderId;
        pending.targetSN = targetSN;
        pending.submitFrame = g_frame;
        pendingGatherOrders[farmer.SN] = pending;
        farmerLastOrderFrame[farmer.SN] = g_frame;
        lastEconomyOrderFrame = g_frame;
        assigned[desiredBucket]++;
        farmerThreatLastFrame.erase(farmer.SN);
        farmerSafeSinceFrame.erase(farmer.SN);
        return true;
    }
    return false;
}

static bool TryResumeIncompleteBuilding(UsrAI *ai)
{
    const int recoveryInterval = 80;
    if (g_frame - lastConstructionRecoveryFrame < recoveryInterval)
        return false;

    const tagBuilding *target = nullptr;
    for (const tagBuilding &building : info.buildings)
    {
        if (building.Blood <= 0 || building.Percent >= 100)
            continue;

        bool hasBuilder = false;
        for (const tagFarmer &farmer : info.farmers)
        {
            if (farmer.Blood > 0 && farmer.FarmerSort == FARMERTYPE_FARMER &&
                farmer.WorkObjectSN == building.SN)
            {
                hasBuilder = true;
                break;
            }
        }
        if (!hasBuilder && (!target || building.Percent > target->Percent))
            target = &building;
    }
    if (!target)
        return false;

    int bestFarmerSN = -1;
    // 预留状态惩罚后仍允许在没有空闲农民时选择普通采集者。
    int bestDis2 = 2000000000;
    for (const tagFarmer &farmer : info.farmers)
    {
        if (farmer.Blood <= 0 || farmer.FarmerSort != FARMERTYPE_FARMER ||
            farmer.NowState == HUMAN_STATE_ATTACKING || IsFarmerBuilding(farmer))
            continue;
        if (FindDirectThreatToFarmerSN(farmer) != -1 ||
            FindNearbyEnemyForFarmer(farmer) != -1)
            continue;

        const int dis2 = BlockDis2(farmer.BlockDR, farmer.BlockUR,
                                   target->BlockDR, target->BlockUR);
        // 优先空闲农民；没有空闲农民时才中断最近农民的普通采集。
        const int statePenalty = farmer.NowState == HUMAN_STATE_IDLE ? 0 : 1000000;
        if (statePenalty + dis2 < bestDis2)
        {
            bestDis2 = statePenalty + dis2;
            bestFarmerSN = farmer.SN;
        }
    }
    if (bestFarmerSN == -1)
        return false;

    CancelPendingGatherOrder(bestFarmerSN);
    ai->HumanAction(bestFarmerSN, target->SN);
    farmerLastOrderFrame[bestFarmerSN] = g_frame;
    lastConstructionRecoveryFrame = g_frame;
    buildFarmerSN = bestFarmerSN;
    return true;
}

// 每个建筑类型的最近一次建造尝试失败码，供 2000 帧调试日志输出。
// 1=等待上一条指令结果 2=冷却中 3=找不到建造农民 4=找不到合法位置
// 100+ret=上次建造返回码
static int buildFailCodes[16] = {0};

static bool TryBuild(UsrAI *ai, int buildingType)
{
  const int typeIdx =
      (buildingType >= 0 && buildingType < 16) ? buildingType : 15;
  buildFailCodes[typeIdx] = 0;
  if (buildOrderId != -1) {
    map<int, int>::const_iterator result = info.ins_ret.find(buildOrderId);
    if (result == info.ins_ret.end()) {
      buildFailCodes[typeIdx] = 1; // 等待上一个建造指令结果
      return false;
    }
    if (result->second == ACTION_INVALID_POSITION_NOT_FIT ||
        result->second == ACTION_INVALID_HUMANBUILD_OVERLAP ||
        result->second == ACTION_INVALID_HUMANBUILD_DIFFERENTHIGH ||
        result->second == ACTION_INVALID_HUMANBUILD_OVERBORDER ||
        result->second == ACTION_INVALID_HUMANBUILD_UNEXPLORE)
      buildCandidateIndex++;
    buildFailCodes[typeIdx] = 100 + result->second; // 上一次建造的返回码
    {
      char buf[256];
      snprintf(buf, sizeof(buf), "[RESULT] f=%d type=%d orderId=%d ret=%d",
               g_frame, buildOrderType, buildOrderId, result->second);
      AiDebugLog(buf);
    }
    buildOrderId = -1;
    buildOrderType = -1;
    buildFarmerSN = -1;
    return false;
  }
  if (g_frame - lastBuildOrderFrame < USR_BUILD_ORDER_INTERVAL) {
    buildFailCodes[typeIdx] = 2; // 建造冷却中
    return false;
  }
    int farmerSN = FindBuilderFarmerSN();
    if (farmerSN == -1) {
      buildFailCodes[typeIdx] = 3; // 找不到空闲/可中断的建造农民
      return false;
    }
    pair<int, int> position = GetBuildCandidate(buildingType);
    if (position.first == -1) {
      buildFailCodes[typeIdx] = 4; // 找不到合法建造位置
      return false;
    }
    CancelPendingGatherOrder(farmerSN);
    buildOrderId = ai->HumanBuild(farmerSN, buildingType, position.first, position.second);
    buildOrderType = buildingType;
    buildFarmerSN = farmerSN;
    if (buildingType == BUILDING_ARMYCAMP)
        armyCampOrderId = buildOrderId;
    lastBuildOrderFrame = g_frame;
    {
      char buf[256];
      snprintf(buf, sizeof(buf),
               "[ORDER] f=%d type=%d pos=(%d,%d) farmer=%d orderId=%d", g_frame,
               buildingType, position.first, position.second, farmerSN,
               buildOrderId);
      AiDebugLog(buf);
    }
    return true;
}

// 在离交付建筑较远的资源群旁建仓库/谷仓，缩短交付往返距离。
static void TryBuildReturnDepot(UsrAI *ai)
{
    if (buildOrderId != -1 ||
        g_frame - lastBuildOrderFrame < USR_BUILD_ORDER_INTERVAL)
        return;

    // 找交付距离最远的资源（木头/石头/黄金/猎物 → 仓库；农场 → 谷仓）。
    int worstBuildingType = -1;
    int worstReturnDis2 = 0;
    int anchorDR = 0, anchorUR = 0;
    for (const tagResource &resource : info.resources)
    {
        if (!IsGatherableResource(resource))
            continue;
        const int dis2 = FindNearestReturnBuildingDistance(
            BUILDING_STOCK, resource.BlockDR, resource.BlockUR);
        if (dis2 > worstReturnDis2)
        {
            worstReturnDis2 = dis2;
            worstBuildingType = BUILDING_STOCK;
            anchorDR = resource.BlockDR;
            anchorUR = resource.BlockUR;
        }
    }
    for (const tagBuilding &building : info.buildings)
    {
        if (!IsGatherableFarm(building))
            continue;
        const int dis2 = FindNearestReturnBuildingDistance(
            BUILDING_GRANARY, building.BlockDR, building.BlockUR);
        if (dis2 > worstReturnDis2)
        {
            worstReturnDis2 = dis2;
            worstBuildingType = BUILDING_GRANARY;
            anchorDR = building.BlockDR;
            anchorUR = building.BlockUR;
        }
    }

    // 交付距离平方超过 400（约 20 格）才值得建仓。
    if (worstBuildingType == -1 || worstReturnDis2 <= 400)
        return;
    if (info.Wood < 120)
        return;
    if (HasBuilding(worstBuildingType))
        return;

    pair<int, int> pos = GetBuildCandidateNear(anchorDR, anchorUR,
                                               worstBuildingType);
    if (pos.first == -1)
        return;
    const int farmerSN = FindBuilderFarmerSN();
    if (farmerSN == -1)
        return;
    CancelPendingGatherOrder(farmerSN);
    buildOrderId = ai->HumanBuild(farmerSN, worstBuildingType,
                                  pos.first, pos.second);
    buildOrderType = worstBuildingType;
    buildFarmerSN = farmerSN;
    lastBuildOrderFrame = g_frame;
}

// 检查单个科技研发订单结果。
// 返回值：本次是否收到 ACTION_SUCCESS（供串行游标判断能否推进到下一项）。
//
// 关键：Core::logActionResult 在 ret != ACTION_SUCCESS 时直接 return
// （Core.cpp:933），失败指令不会在 info.ins_ret 留下任何记录。若只判断
// 「orderId != -1 即在途」，一次被拒绝的下单就会让该研发通道永久阻塞。
// 因此这里配合 techOrderFrame
// 做超时判定：超时未见到结果即视为被拒绝并放行重试。
static bool CheckTechOrder(int &orderId) {
  if (orderId == -1)
    return false;

  map<int, int>::const_iterator result = info.ins_ret.find(orderId);
  if (result != info.ins_ret.end()) {
    const bool success = result->second == ACTION_SUCCESS;
    if (success)
      researchedTechCount++;
    lastTechRet = result->second;
    lastTechFrame = g_frame;
    techOrderFrame.erase(orderId);
    orderId = -1;
    return success;
  }

  map<int, int>::const_iterator ordered = techOrderFrame.find(orderId);
  if (ordered != techOrderFrame.end() &&
      g_frame - ordered->second < USR_TECH_ORDER_TIMEOUT)
    return false; // 仍在途，继续等待

  // 超时仍未收到结果 → 判定为被 Core 拒绝（前置/资源/占用不满足）。
  lastTechRet = -1;
  lastTechFrame = g_frame;
  techOrderFrame.erase(orderId);
  orderId = -1;
  return false;
}

// 研发科技：处理旧结果 + 空闲时下单（记录订单 ID 以便追踪研发完成）。
static void ResearchTech(UsrAI *ai, int &orderId, int buildingType, int action)
{
    CheckTechOrder(orderId);
    if (orderId != -1)
        return;
    const tagBuilding *building = FindReadyBuildingByType(buildingType);
    if (!building)
        return;
    orderId = ai->BuildingAction(building->SN, action);
    if (orderId != -1)
      techOrderFrame[orderId] = g_frame;
}

// 串行研发：同一建筑上的多项科技必须逐项下单。
//
// Core_List::addRelation(object1, evenType, actNum) 要求
// !relate_AllObject[object1].isExist（Core_List.cpp:416），否则返回
// ACTION_INVALID_ISNTFREE（Core_List.cpp:447）。而建筑快照的 Project 在
// 帧内不刷新，所以同帧连发 N 条时 FindReadyBuildingByType 对每次调用都
// 报告「空闲」，实际只有第一条能建立关系，其余全部被拒绝且结果被丢弃——
// 表现为「仓库有建筑、资源也够，却始终点不上科技」。
// cursor 仅在拿到 ACTION_SUCCESS 后前进；被拒绝时停在原项等待重试。
//
// techFoodCost / techGoldCost 为各项科技的花费（均可为 NULL 表示不做该项门控）。
// 传入时在资源不足的情况下不下单：Core 会以 ACTION_INVALID_RESOURCE(20)
// 拒绝这类指令，虽然能靠超时重试，但与其每 60 帧发一条注定失败的指令，
// 不如等资源到位再点。食物额外留 USR_TECH_FOOD_RESERVE，避免科技把食物
// 吃光而挤掉造兵与造农民；黄金不设余量，按实际花费严格门控。
//
// 同一个 action 可以在 actions 里出现多次：Core 的科技链每个 action 下挂
// 若干个节点（如仓库的近战攻击是「工具时代 L1 → 铜器时代 L2」），
// 再发一次同一 action 就会推进到链上的下一级，这是「进一步研发」的实现方式。
static void ResearchTechQueue(UsrAI *ai, int buildingType, const int *actions,
                              const int *techFoodCost, const int *techGoldCost,
                              int actionCount, int &cursor, int &orderId) {
  if (orderId != -1) {
    if (CheckTechOrder(orderId))
      ++cursor;
    // 无论成功还是被判失败，本帧都不再下单，下一帧再推进，
    // 避免在同一帧内重新占用建筑关系表。
    return;
  }

  if (cursor >= actionCount)
    return;

  if (techFoodCost != NULL &&
      info.Meat < techFoodCost[cursor] + USR_TECH_FOOD_RESERVE)
    return;
  if (techGoldCost != NULL && info.Gold < techGoldCost[cursor])
    return;

  const tagBuilding *building = FindReadyBuildingByType(buildingType);
  if (!building)
    return;

  orderId = ai->BuildingAction(building->SN, actions[cursor]);
  if (orderId != -1)
    techOrderFrame[orderId] = g_frame;
}

// 对指定类型的建筑下发一条研发 / 升级指令。返回值只表示「找到了空闲建筑」，
// 不代表 Core 接受了这条指令 —— BuildingAction 只是把指令入队，接受与否要等
// 下一帧从建筑 Project 上观察（Core 对前置不满足的订单会静默拒绝）。
static bool TryBuildingAction(UsrAI *ai,
                              int buildingType, int action)
{
    const tagBuilding *building = FindReadyBuildingByType(buildingType);
    if (!building)
        return false;
    ai->BuildingAction(building->SN, action);
    return true;
}

// 追踪车轮升级是否完成（供 TryProduceChariotArcher 门控）：观察到市场 Project
// 进入车轮研发 → 标记运行中；之后连续空闲 USR_MARKET_IDLE_FRAMES 帧 → 判定完成。
// 防抖是必需的：研发被中断时 Project 会短暂回到 0，误判会让 AI 一直发战车弓兵
// 订单而 Core 一直以 LOCK 拒绝（实测整局产不出一个战车弓兵）。
static void TrackWheelResearch(const tagBuilding *market)
{
  if (wheelTechReady || market == nullptr)
    return;
  if (market->Project == BUILDING_MARKET_WHEEL_UPGRADE)
  {
    wheelSeenRunning = true;
    wheelIdleSinceFrame = USR_INVALID_FRAME;
    return;
  }
  if (!wheelSeenRunning)
    return;
  if (wheelIdleSinceFrame == USR_INVALID_FRAME)
  {
    wheelIdleSinceFrame = g_frame;
    return;
  }
  if (g_frame - wheelIdleSinceFrame >= USR_MARKET_IDLE_FRAMES)
    wheelTechReady = true;
}

// 市场研发的候选，数组顺序即优先级。
// 伐木科技排在最前：进入青铜时代后它还有第二级（工艺）要研发，而木材是
// 战车弓兵的主消耗（40 食 + 70 木，目标 25~30 个 = 1750~2100 木），
// 伐木效率直接决定产兵速度。工具时代车轮被「时代未到」拒绝时，
// 排第二的它本来就会顶上来，所以这个顺序对工具时代没有影响。
static const int kMarketResearch[] = {
    BUILDING_MARKET_WOOD_UPGRADE,  // 木材加工 → 工艺（伐木科技，优先级最高）
    BUILDING_MARKET_WHEEL_UPGRADE, // 车轮（单级）：解锁战车 / 战车弓兵
    BUILDING_MARKET_FARM_UPGRADE,  // 驯养动物 → 犁
};
static const int kMarketResearchCount =
    (int)(sizeof(kMarketResearch) / sizeof(kMarketResearch[0]));

// 下发市场研发：每帧最多一条，按优先级挑第一项「当前可研发」的。
//
// 【为什么每帧只能发一条】Core::deduplicateInstructions（Core.cpp:1221-1237）在
// 每帧处理指令之前按 SN 去重，且是覆盖语义（uniqueInstructions[cur.SN] = cur，
// 保留【最后】入队的一条）。三条研发用的是同一个市场的 SN，同一帧发三条就只有
// 最后一条能活 —— 上一版写成三个独立 if，排在最后的农田升级永久盖掉了车轮升级，
// 实测整局市场 Project 只出现过 6 和 8、10 一次都没有，战车弓兵一个都产不出来。
//
// 【为什么用回执而不是完成标记】木材加工与农田升级都是两级链（木材加工→工艺、
// 驯养动物→犁，Development.cpp:659-690），两级共用同一个 action 编号，AI 从
// Project 上只能看到「某一级完成了」，分不清是第一级还是第二级，给它们上完成
// 标记会把第二级永久截断 —— 犁就是这么丢掉的。哪一项当前可研发只有 Core 知道：
// Core_List.cpp:427 用建筑行动的 isShowAble 过滤，不可研发（时代未到 / 链已走完）
// 返回 ACTION_INVALID_BUILDACT_LOCK。AI 拿不到 isShowAble（Player* 不暴露给 AI），
// 但能从回执等价地得到，于是按优先级试、把 LOCK 的项跳过；跨时代时清空标记重试，
// 这样被「时代未到」挡住的项（如铜器时代的犁）会重新获得机会。
static void ManageMarketResearch(UsrAI *ai, const tagBuilding *market)
{
  if (market == nullptr)
    return;
  if (market->Project != ACT_NULL)
  {
    // 研发正在进行 —— 刚下发的那条被 Core 接受了，清掉待确认状态。
    marketOrderId = -1;
    marketOrderSlot = -1;
    return;
  }
  // 市场空闲：上一条若还没让市场忙起来，等够确认窗口就判定这次没成
  // （时代未到 / 资源不足 / 链已走完）。只让它冷却一段时间，不永久跳过。
  // 判据是「市场有没有真的进入研发」而不是回执 —— 理由见 USR_MARKET_ORDER_TIMEOUT。
  if (marketOrderId != -1)
  {
    if (g_frame - marketOrderFrame < USR_MARKET_ORDER_TIMEOUT)
      return;
    if (marketOrderSlot >= 0 && marketOrderSlot < 3)
      marketCooldownUntil[marketOrderSlot] = g_frame + USR_MARKET_RETRY_COOLDOWN;
    marketOrderId = -1;
    marketOrderSlot = -1;
  }
  for (int i = 0; i < kMarketResearchCount; ++i)
  {
    if (g_frame < marketCooldownUntil[i])
      continue;
    // 农田链（驯养动物 → 犁）推迟到铜器时代之后再研发：
    //   · 工具时代的市场时间让给木材加工（伐木 +50%），那才是经济瓶颈；
    //   · 驯养动物要 200 食物，会挤占升时代所需的 800 食物。
    // 另外两级都让农场储量 +75，农场太少回不了本（沿用原先的 4 块门槛）。
    // 这里必须显式判时代：不能指望 Core 拒绝 —— 驯养动物的注册时代是
    // CIVILIZATION_TOOLAGE（Development.cpp:661），工具时代它本来就被允许。
    if (kMarketResearch[i] == BUILDING_MARKET_FARM_UPGRADE &&
        (info.civilizationStage == CIVILIZATION_TOOLAGE ||
         CountBuilding(BUILDING_FARM) < 4))
      continue;
    marketOrderId = ai->BuildingAction(market->SN, kMarketResearch[i]);
    marketOrderSlot = i;
    marketOrderFrame = g_frame;
    return;
  }
}

static bool TryProduceFarmer(UsrAI *ai,
                             bool nearPopulationCap)
{
  if (nearPopulationCap || info.Meat < 50)
    return false;

  return TryBuildingAction(ai, BUILDING_CENTER, BUILDING_CENTER_CREATEFARMER);
}

static bool TryProduceSoldier(UsrAI *ai,
                              bool nearPopulationCap)
{
    if (nearPopulationCap || info.Meat < 50)
        return false;
    return TryBuildingAction(ai, BUILDING_ARMYCAMP,
                             BUILDING_ARMYCAMP_CREATE_CLUBMAN);
}

// 训练普通弓兵：造价 40 食物 + 20 木材，属性（血35/攻3/射程5）。
// 配额由 bowmanTarget 控制（当前 5 个）。原先这里会优先转产复合弓兵，
// 于是 CountArmyBySort(AT_BOWMAN) 永远为 0、目标永远达不到、会一直产下去；
// 现在固定在普通弓兵上，配额才有效。
static bool TryProduceBowman(UsrAI *ai, bool nearPopulationCap)
{
  if (nearPopulationCap || info.Meat < 40 || info.Wood < 20)
    return false;

  return TryBuildingAction(ai, BUILDING_RANGE, BUILDING_RANGE_CREATE_BOWMAN);
}
// 训练战车弓兵：需要「车轮升级」科技（BUILDING_MARKET_WHEEL_UPGRADE，与战车同前置），
// 造价 40 食物 + 70 木材。属性（血70/攻4/射程7/速度4.07）：射程与敌方远程对等，
// 机动性最好，是本版的主力远程兵种。
// 车轮科技未完成时 Core 会以 ACTION_INVALID_BUILDACT_LOCK 拒绝，这里先用
// wheelTechReady 挡一道，避免每帧发一条注定失败的指令。
static bool TryProduceChariotArcher(UsrAI *ai, bool nearPopulationCap)
{
  // 诊断：节流记录被挡在哪一道。战车弓兵产不出来时，需要区分是人口上限、
  // 科技未就绪、资源不足，还是靶场正忙（研复合弓/造弓兵会占住靶场）。
  static int lastDiagFrame = 0;
  const char *blockReason = NULL;

  if (nearPopulationCap)
    blockReason = "popCap";
  else if (!wheelTechReady)
    blockReason = "noWheel";
  else if (info.Meat < 40 || info.Wood < 70)
    blockReason = "resource";

  if (blockReason != NULL) {
    if (g_frame - lastDiagFrame >= 1000) {
      lastDiagFrame = g_frame;
      char buf[256];
      snprintf(buf, sizeof(buf),
               "[CARCHER] f=%d blocked=%s pop=%.1f/%d food=%d wood=%d "
               "wheel=%d rangeReady=%d",
               g_frame, blockReason, info.Human_Num,
               (int)info.Human_MaxNum, (int)info.Meat, info.Wood,
               (int)wheelTechReady,
               (int)(FindReadyBuildingByType(BUILDING_RANGE) != nullptr));
      AiDebugLog(buf);
    }
    return false;
  }

  // 记录上一张订单的回执：Core 对前置不满足（如车轮科技未完成）的订单会返回
  // ACTION_INVALID_BUILDACT_LOCK(12)。TryBuildingAction 只看「有没有空闲靶场」、
  // 不看回执，所以这类失败是完全静默的 —— 这一行把它暴露出来。
  if (chariotArcherOrderId != -1) {
    map<int, int>::const_iterator result = info.ins_ret.find(chariotArcherOrderId);
    if (result != info.ins_ret.end()) {
      chariotArcherRet = result->second;
      chariotArcherOrderId = -1;
    }
  }

  const tagBuilding *range = FindReadyBuildingByType(BUILDING_RANGE);
  if (range == nullptr) {
    if (g_frame - lastDiagFrame >= 1000) {
      lastDiagFrame = g_frame;
      char buf[256];
      snprintf(buf, sizeof(buf),
               "[CARCHER] f=%d blocked=rangeBusy pop=%.1f/%d food=%d wood=%d "
               "wheel=%d",
               g_frame, info.Human_Num, (int)info.Human_MaxNum, (int)info.Meat,
               info.Wood, (int)wheelTechReady);
      AiDebugLog(buf);
    }
    return false;
  }

  chariotArcherOrderId = ai->BuildingAction(range->SN,
                                            BUILDING_RANGE_CREATE_CHARIOT_ARCHER);
  return true;
}
static bool TryProduceScout(UsrAI *ai, bool nearPopulationCap)
{
  if (nearPopulationCap || info.Meat < 100)
    return false;

  return TryBuildingAction(ai, BUILDING_STABLE, BUILDING_STABLE_CREATE_SCOUT);
}
// 训练阔剑兵：需要「升级为阔剑」科技，造价 35 食物 + 15 黄金。
// 属性（血70/攻9/近防1）全面优于棍棒兵，且比斧头兵更强更便宜。
static bool TryProduceBroadsword(UsrAI *ai, bool nearPopulationCap) {
  if (nearPopulationCap || info.Meat < 35 || info.Gold < 15)
    return false;

  return TryBuildingAction(ai, BUILDING_ARMYCAMP,
                           BUILDING_ARMYCAMP_CREATE_BROADSWORD);
}
// 训练战车：需要「车轮升级」科技（BUILDING_MARKET_WHEEL_UPGRADE），
// 造价 40 食物 + 60 木材（不消耗黄金）。速度 4.07，机动性强。
static bool TryProduceChariot(UsrAI *ai, bool nearPopulationCap) {
  if (nearPopulationCap || info.Meat < 40 || info.Wood < 60)
    return false;

  return TryBuildingAction(ai, BUILDING_STABLE, BUILDING_STABLE_CREATE_CHARIOT);
}
// 训练方阵兵：需要学院（BUILDING_COLLAGE），造价 60 食物 + 40 黄金。
static bool TryProduceHoplite(UsrAI *ai, bool nearPopulationCap)
{
  if (nearPopulationCap || info.Meat < 60 || info.Gold < 40)
    return false;

  return TryBuildingAction(ai, BUILDING_COLLAGE,
                           BUILDING_COLLAGE_CREATE_HOPLITE);
}
static bool HasProductionCapacity(int buildingType, int orderId,
                                  bool nearPopulationCap)
{
    return !nearPopulationCap && orderId == -1 &&
           FindReadyBuildingByType(buildingType) != nullptr;
}

static bool TryProduceFarmer(UsrAI *ai, bool nearPopulationCap);
static bool TryProduceSoldier(UsrAI *ai, bool nearPopulationCap);
static bool TryProduceBowman(UsrAI *ai, bool nearPopulationCap);
static bool TryProduceScout(UsrAI *ai, bool nearPopulationCap);
static bool TryProduceHoplite(UsrAI *ai, bool nearPopulationCap);
static bool TryProduceBroadsword(UsrAI *ai, bool nearPopulationCap);
static bool TryProduceChariot(UsrAI *ai, bool nearPopulationCap);
static bool TryProduceChariotArcher(UsrAI *ai, bool nearPopulationCap);

// 生产函数指针类型：与 TryProduceFarmer/Soldier/Bowman/Scout 签名一致。
typedef bool (*ProduceFunc)(UsrAI *, bool);

// 接口：当前数量低于目标时调用对应生产函数补员。
// 用途：每个人种设目标，未达标就生产；资源与人口上限由生产函数内部把关。
static void ProduceIfBelowTarget(UsrAI *ai, bool nearPopulationCap,
                                 int currentCount, int target,
                                 ProduceFunc produce) {
  if (currentCount < target)
    produce(ai, nearPopulationCap);
}

static void ManageWeightedProduction(UsrAI *ai, bool nearPopulationCap) {
  const int farmerCount = static_cast<int>(info.farmers.size());
  const int clubmanCount = CountArmyBySort(AT_CLUBMAN);
  const int bowmanCount = CountArmyBySort(AT_BOWMAN);
  const int scoutCount = CountArmyBySort(AT_SCOUT);
  const int hopliteCount = CountArmyBySort(AT_HOPLITE);
  const int broadswordCount = CountArmyBySort(AT_BROADSWORDSMAN);
  const int chariotCount = CountArmyBySort(AT_CHARIOT);
  const int chariotArcherCount = CountArmyBySort(AT_CHARIOT_ARCHER);

  // 每个人种的目标数量；后续可按敌方兵力或时代动态调整。
  // 工具时代农民目标 14 个（原为 18）。
  // 每个农民 50 食物，18 个 = 900 食物，正好把升铜器所需的 800 食物吃掉，
  // 导致升时代最早也要 f≈13000 才启动、耗时 1500 帧（TIME_BUILDING_CENTER_UPGRADE
  // = 60 秒），完成时已撞上第二波（SAT = 13500）——而那时食物清零、且按
  // 「升时代前只生产农民」的策略 army=0，完全无力防守。
  // 降到 14 可省下约 200 食物，让升时代提前启动，争取在第二波之前完成。
  int farmerTarget = 14;
  // 进入青铜时代后的农民目标 = 12（原为 5）。
  // 5 个农民撑不起后面的开销：车轮升级要 100 木 + 150 食、25 个战车弓兵要
  // 1750 木 + 1000 食，实测木材/食物长期在 13~200 徘徊，两项几乎从不同时满足，
  // 导致车轮科技整局研不出来、战车弓兵一个都产不出来。
  // 这只是「不再补产」——已有农民不会被主动裁掉，只会随战损与
  // SacrificeExcessFarmers（后勤完成 + 人口顶到 50 后才启动）逐步减少。
  // 用 != TOOLAGE 而非 == BRONZEAGE，与文件内其它「已升时代」判定保持一致，
  // 且万一达到铁器时代也不会把目标弹回去。
  if (info.civilizationStage != CIVILIZATION_TOOLAGE)
    farmerTarget = 12;

  // 兵种配比：以战车弓兵为远程主力，普通弓兵少量，不造近战步兵。
  int armyTarget = 0;            // 棍棒兵/斧头兵：不造
  int broadswordTarget = 0;      // 阔剑兵：不造
  int bowmanTarget = 0;          // 普通弓兵
  int scoutTarget = 0;           // 侦察骑兵
  int hopliteTarget = 0;         // 方阵兵：不造
  int chariotTarget = 0;         // 战车（近战冲锋）
  int chariotArcherTarget = 0;   // 战车弓兵（远程主力）
  if (info.civilizationStage == CIVILIZATION_TOOLAGE) {
    // 升级时代之前只生产农民：士兵会消耗食物并拖慢升时代与经济发展。
    armyTarget = 0;
    bowmanTarget = 0;
    scoutTarget = 0;
  } else {
    bowmanTarget = 5;        // 普通弓兵 5 个（40 食 + 20 木，便宜、前期即可产）
    chariotArcherTarget = 25; // 战车弓兵 25 个（40 食 + 70 木，血70/攻4/射程7/速度4.07）
    // 侦察骑兵：USR_SCOUT_FIRST_FRAME 后生产 1 个保命型（遇敌即撤回基地，
    // 不承担侦测）；USR_SCOUT_RECON_FRAME 后再补 1 个，由它专职侦测敌方基地
    // （见 DispatchScouts 的角色判定）。
    scoutTarget = (g_frame >= USR_SCOUT_RECON_FRAME)
                      ? 2
                      : ((g_frame >= USR_SCOUT_FIRST_FRAME) ? 1 : 0);
    // 战车（40 食 + 60 木）保留 2 个：速度 4.07，可作为前排挡一下远程。
    chariotTarget = 2;
  }
  // 敌方阔剑兵近战克制（攻9近防1），远程单位对其远防 0 很有效。
  // 增补的是战车弓兵而不是普通弓兵，避免破坏上面固定的弓兵配额。
  if (CountEnemyBySort(AT_BROADSWORDSMAN) > 0) {
    armyTarget = 0;
    chariotArcherTarget += 6;
  }
  // 关键科技研发完成后扩军（仍受人口上限约束，优先高价值兵种）。
  // 注意：这里只上调远程配额，不再改回近战步兵——否则会把上面
  // 「不造棍棒兵/方阵兵」的设定覆盖掉。
  if (researchedTechCount >= TOTAL_REQUIRED_TECH) {
    chariotArcherTarget = 30;
    bowmanTarget = 5;
  }
  // 「升级为阔剑」科技尚未完成时，阔剑兵无法训练，目标转给棍棒兵兜底，
  // 避免兵力押注在未解锁的兵种上导致总兵力下降。
  if (broadswordTarget > 0 && researchedTechCount < TOTAL_REQUIRED_TECH) {
    armyTarget += broadswordTarget;
    broadswordTarget = 0;
  }
  // 学院未建成时方阵兵无法训练（需要学院），目标转给斧头兵兜底，
  // 避免兵力押注在尚未解锁的兵种上导致总兵力下降。
  if (hopliteTarget > 0 && !HasBuilding(BUILDING_COLLAGE)) {
    armyTarget += hopliteTarget;
    hopliteTarget = 0;
  }
  // 【取消】原先在车轮升级完成前（g_frame < 24000）把战车配额转给斧头兵兜底。
  // 这条转移是 armyTarget 唯一的赋值来源，会让上面「不造棍棒兵/斧头兵」的
  // 设定在 24000 帧前失效：青铜时代后 chariotTarget = 2 转过来，armyTarget
  // 就变成 2，TryProduceSoldier 随即造出斧头兵（实测 [AI] 里 army 比
  // bow+carcher+chariot 多 1~2 个，就是它）。现在战车配额宁可空转，
  // 也不生产任何近战步兵。
  // if (chariotTarget > 0 && g_frame < 24000) {
  //   armyTarget += chariotTarget;
  //   chariotTarget = 0;
  // }
  // 后勤完成后兵营单位人口占用减半（1 → 0.5），斧头兵可养数量翻倍。
  if (logisticsReady)
    armyTarget *= 2;
  ProduceIfBelowTarget(ai, nearPopulationCap, farmerCount, farmerTarget,
                       TryProduceFarmer);
  // 人口有限，优先生产高价值兵种：方阵兵（血120/攻17）先占人口，
  // 其次是阔剑兵、弓兵与侦察，最后才是弱小的棍棒兵。
  ProduceIfBelowTarget(ai, nearPopulationCap, hopliteCount, hopliteTarget,
                       TryProduceHoplite);
  ProduceIfBelowTarget(ai, nearPopulationCap, broadswordCount, broadswordTarget,
                       TryProduceBroadsword);
  ProduceIfBelowTarget(ai, nearPopulationCap, chariotCount, chariotTarget,
                       TryProduceChariot);
  ProduceIfBelowTarget(ai, nearPopulationCap, bowmanCount, bowmanTarget,
                       TryProduceBowman);
  ProduceIfBelowTarget(ai, nearPopulationCap, chariotArcherCount,
                       chariotArcherTarget, TryProduceChariotArcher);
  ProduceIfBelowTarget(ai, nearPopulationCap, scoutCount, scoutTarget,
                       TryProduceScout);
  ProduceIfBelowTarget(ai, nearPopulationCap, clubmanCount, armyTarget,
                       TryProduceSoldier);
}

// 检测受损建筑，为每个尚无修复者的建筑派一个空闲农民（Core 按修复比例扣资源）。
static bool TryRepairDamagedBuilding(UsrAI *ai) {
  static int lastRepairFrame = USR_INVALID_FRAME;
  const int repairInterval = 200;
  if (lastRepairFrame != USR_INVALID_FRAME &&
      g_frame - lastRepairFrame < repairInterval)
    return false;

  bool repairedAny = false;
  set<int> usedFarmers;  // 本轮已分配的农民，避免多个建筑抢同一个农民

  for (const tagBuilding &building : info.buildings) {
    if (building.Blood <= 0 || building.MaxBlood <= 0 || building.Percent < 100)
      continue;
    const int missing = building.MaxBlood - building.Blood;
    // 受损不足 20% 不修，避免浪费农民采集时间。
    if (missing * 5 < building.MaxBlood)
      continue;

    // 该建筑已有农民在修复（含正在赶路的）→ 跳过，避免多人挤同一建筑。
    bool hasRepairer = false;
    for (const tagFarmer &farmer : info.farmers) {
      if (farmer.Blood > 0 && farmer.WorkObjectSN == building.SN) {
        hasRepairer = true;
        break;
      }
    }
    if (hasRepairer)
      continue;

    // 找最近的空闲农民（排除本轮已分配者）。
    int bestFarmerSN = -1;
    int bestDis2 = 2000000000;
    for (const tagFarmer &farmer : info.farmers) {
      if (farmer.Blood <= 0 || farmer.FarmerSort != FARMERTYPE_FARMER ||
          farmer.NowState != HUMAN_STATE_IDLE)
        continue;
      if (usedFarmers.find(farmer.SN) != usedFarmers.end())
        continue;
      const int dis2 = BlockDis2(farmer.BlockDR, farmer.BlockUR,
                                 building.BlockDR, building.BlockUR);
      if (dis2 < bestDis2) {
        bestDis2 = dis2;
        bestFarmerSN = farmer.SN;
      }
    }
    if (bestFarmerSN == -1)
      continue;  // 没有空闲农民，处理下一个建筑

    usedFarmers.insert(bestFarmerSN);
    CancelPendingGatherOrder(bestFarmerSN);
    ai->HumanAction(bestFarmerSN, building.SN);
    farmerLastOrderFrame[bestFarmerSN] = g_frame;
    repairedAny = true;
    {
      char buf[256];
      snprintf(buf, sizeof(buf),
               "[REPAIR] f=%d type=%d sn=%d blood=%d/%d farmer=%d", g_frame,
               building.Type, building.SN, building.Blood, building.MaxBlood,
               bestFarmerSN);
      AiDebugLog(buf);
    }
  }

  if (repairedAny)
    lastRepairFrame = g_frame;
  return repairedAny;
}

// 接口：推进最短经济、时代与军队生产链。
// 用途：所有决策均基于可见状态；失败后冷却重试，不依赖作弊资源。
static void ManageEconomyAndProduction(UsrAI *ai)
{
    if (buildOrderId != -1)
    {
        map<int, int>::const_iterator result = info.ins_ret.find(buildOrderId);
        if (result != info.ins_ret.end() ||
            g_frame - lastBuildOrderFrame >= 300)
        {
            if (result != info.ins_ret.end() &&
                result->second == ACTION_INVALID_POSITION_NOT_FIT)
                buildCandidateIndex++;
            {
              char buf[256];
              snprintf(buf, sizeof(buf),
                       "[RESET] f=%d type=%d orderId=%d ret=%d", g_frame,
                       buildOrderType, buildOrderId,
                       result != info.ins_ret.end() ? result->second : -999);
              AiDebugLog(buf);
            }
            buildOrderId = -1;
            buildOrderType = -1;
            buildFarmerSN = -1;
        }
    }

    // 建筑可能因农民被攻击、碰撞或其他关系覆盖而中断；优先恢复已有工地，
    // 防止未完成建筑长期占位却无人继续建造。
    TryResumeIncompleteBuilding(ai);

    // 自动修复受损建筑。
    TryRepairDamagedBuilding(ai);

    // 箭塔科技：必须先完成谷仓的「研发:建造箭塔」才能建造箭塔，
    // 否则 Core 会以 ACTION_INVALID_HUMANBUILD_LOCK 拒绝，
    // 而 AI 侧已更新建造冷却，白白阻塞其他建筑 100 帧。
    // 判定：观察到谷仓 Project 进入该研发 → 标记运行中；之后 Project 离开即视为完成。
    if (!arrowTowerTechnologyReady)
    {
        const tagBuilding *granary = FindBuildingByType(BUILDING_GRANARY, true);
        if (granary != nullptr)
        {
            if (granary->Project == BUILDING_GRANARY_ARROWTOWER)
            {
                arrowTowerTechSeenRunning = true;  // 研发进行中
            }
            else if (arrowTowerTechSeenRunning)
            {
                arrowTowerTechnologyReady = true;  // 研发结束 → 已解锁
            }
            else
            {
                // 尚未启动：尝试发起研发。
                TryBuildingAction(ai, BUILDING_GRANARY,
                                  BUILDING_GRANARY_ARROWTOWER);
                if (arrowTowerTechStartFrame == USR_INVALID_FRAME)
                    arrowTowerTechStartFrame = g_frame;
                else if (g_frame - arrowTowerTechStartFrame >
                         USR_PRODUCTION_ORDER_TIMEOUT)
                    // 长时间无法启动（如已解锁或前置不足）→ 兜底放行，
                    // 交由 Core 自行校验，避免永久不建箭塔。
                    arrowTowerTechnologyReady = true;
            }
        }
    }

    // 人口接近上限时补房屋；上限 12 间（config.h 的「建造房屋(上限:12)」）。
    // 没有数量上限会导致人口封顶（50 = 12房 + 1中心 ×
    // 4）后仍持续建房、白扔木头。
    const bool nearPopulationCap = info.Human_Num + 1.9 >= info.Human_MaxNum;
    if (nearPopulationCap && info.Wood >= 30 &&
        CountBuilding(BUILDING_HOME) < 12 &&
        !HasIncompleteBuilding(BUILDING_HOME)) {
      TryBuild(ai, BUILDING_HOME);
    }

    // 建造顺序必须满足 Core 的前置链（否则 LOCK 会占用建造通道死循环）：
    // 谷仓 → 兵营 → 市场(需谷仓) → 马厩/靶场(需兵营) → 农场(需市场) →
    // 箭塔(需谷仓研发)
    if (!HasBuilding(BUILDING_MARKET)) {
      if (info.Wood >= 150)
        TryBuild(ai, BUILDING_MARKET);
    }
    // 箭塔防守：围绕市镇中心四方向环形布置（TOWER_RINGS，近圈 6 格起），
    // 须等箭塔科技研发完成后才建造，且第三波骚扰之后不再建造
    // （USR_ARROWTOWER_STOP_FRAME，与采石权重共用 ArrowTowerStillWanted）。
    if (arrowTowerTechnologyReady && ArrowTowerStillWanted() &&
        info.Stone >= 150) {
      TryBuild(ai, BUILDING_ARROWTOWER);
    }
    // 兵营是靶场与马厩的建造前置（马厩 Development.cpp:697、靶场 :727 ——
    // 两处都用 addPreCondition 挂上 developLab[BUILDING_ARMYCAMP].buildCon，
    // 而 isShowable 要求前置的 acttimes > 0，即兵营已建成）。所以兵营必须排在
    // 它们前面，挪到后面会让靶场 / 马厩一直拿到 ACTION_INVALID_HUMANBUILD_LOCK。
    // 建筑链：谷仓 → 兵营 → 市场(需谷仓) → 马厩/靶场(需兵营) → 农场(需市场) →
    // 箭塔(需谷仓研发)
    if (!HasBuilding(BUILDING_ARMYCAMP)) {
      if (info.Wood >= 125)
        TryBuild(ai, BUILDING_ARMYCAMP);
    }
    if (!HasBuilding(BUILDING_RANGE)) {
      if (info.Wood >= 150)
        TryBuild(ai, BUILDING_RANGE);
    }
    if (!HasBuilding(BUILDING_STABLE)) {
      if (info.Wood >= 150)
        TryBuild(ai, BUILDING_STABLE);
    }
    // 【不再修建】学院：唯一用途是训练方阵兵，但要吃掉 180 木和一座建筑的
    // 建造时间。停建后 hopliteTarget 会被 ManageWeightedProduction 里的
    // 「学院未建成时方阵兵无法训练」分支自动转成斧头兵，兵种配额不会空转。
    // if (info.civilizationStage != CIVILIZATION_TOOLAGE &&
    //     HasBuilding(BUILDING_STABLE) && !HasBuilding(BUILDING_COLLAGE)) {
    //   if (info.Wood >= 180)
    //     TryBuild(ai, BUILDING_COLLAGE);
    // }
    // 农场只在马厩/靶场建成后建：否则木头会被农场（75木）持续消耗，
    // 永远攒不够马厩/靶场（各150木），导致时代无法升级。
    if (HasBuilding(BUILDING_MARKET)) {
      // 农场数量上限：
      //   第三波攻击（f>=21000）之前 = 村民数的二分之一（原为三分之一）；
      //   第三波之后 = 农民数 - 8（保留 8 个农民负责伐木/采石/采金，
      //   其余专注食物，此时农场食物已是主要来源）。
      //
      // 前期由 /3 改为 /2：14 个农民时原来只允许 4 座农场，食物产能被农场数量
      // 卡死（浆果/瞪羚耗尽后更是如此），实测食物长期停在 40~125，凑不齐车轮
      // 升级所需的 150。改成 /2 后同样 14 个农民可建 7 座。
      // 代价：多 3 座农场 × 75 木 = 225 木材，而此时木材已有 370 的余量。
      const int villagerCount = static_cast<int>(info.farmers.size());
      const int farmLimit =
          (g_frame >= 21000) ? max(0, villagerCount - 8) : villagerCount / 2;
      // 升时代前置（市场 + 靶场 + 马厩 ≥ 2，见 Development::hasAgeUpgradeBuildings）
      // 未满足前，为缺的那座建筑预留 150 木材。
      // 否则农场（75 木/座）会把木材吃光，前置建筑永远建不出来、升时代被无限推迟：
      // 实测 7 座农场让木材在 f=8000 归零（8）、马厩推迟到 f>14000，而升时代本身
      // 还要 1500 帧，祭司在 f≈14700 就阵亡了 —— 整局没进青铜时代、没有军队。
      // 这正是上面那句注释本来想表达的意思，只是原先只检查了市场。
      const int ageUpBuildings = (int)HasBuilding(BUILDING_MARKET) +
                                 (int)HasBuilding(BUILDING_RANGE) +
                                 (int)HasBuilding(BUILDING_STABLE);
      const int woodReserve = (ageUpBuildings < 2) ? 150 : 0;
      if (info.Wood >= 75 + woodReserve &&
          CountBuilding(BUILDING_FARM) < farmLimit) {
        TryBuild(ai, BUILDING_FARM);
      }
    }

    // 在离交付建筑较远的资源群旁建仓库/谷仓，缩短交付往返。
    TryBuildReturnDepot(ai);
    if (info.civilizationStage == CIVILIZATION_TOOLAGE && info.Meat >= 800 &&
        (HasBuilding(BUILDING_MARKET) + HasBuilding(BUILDING_RANGE) +
         HasBuilding(BUILDING_STABLE)) >= 2) {
      TryBuildingAction(ai, BUILDING_CENTER, BUILDING_CENTER_UPGRADE);
    }
    // 箭塔研发已在建造序列之前处理（见上方 arrowTowerTechnologyReady 逻辑）。
    //
    // ===== 市场研发：同一帧只下发一条，按 车轮升级 → 木材加工 → 农田升级 放行 =====
    //
    // 【为什么必须互斥】Core::deduplicateInstructions（Core.cpp:1221-1237）在每帧
    // 处理指令之前按 SN 去重，且是覆盖语义：
    //     uniqueInstructions[cur.SN] = cur;   // 同一 SN 只保留【最后】入队的一条
    // 下面三条研发用的都是同一个市场的 SN，所以同一帧里排最后的那个才生效。
    // 原先写成三个独立 if 时，排在最后的农田升级把车轮升级和木材加工一起挤掉 ——
    // 实测整局日志中市场 Project 只出现过 6(WOOD) 和 8(FARM)，10(WHEEL) 一次都没有，
    // wheelTechReady 永远是假，[CARCHER] 全程 wheel=0，一个战车弓兵都产不出来。
    // 作为对照：仓库（单一队列）的 13/14/15 三个科技、兵营的 18/20 都研发成功了。
    //
    // 【优先级的去处】三条链的下发策略 —— 每帧只发一条、按优先级挑当前可研发的、
    // 靠回执跳过不可研发的项 —— 集中在 ManageMarketResearch，理由也写在那里。
    //
    // 【暂停】金矿开采：成本 120 食物 + 100 木，与车轮升级抢同一批资源，
    // 而当前兵种配比（战车弓兵 40食+70木 / 弓兵 40食+20木 / 战车 40食+60木）
    // 完全不用黄金，采金收益归零。先关掉把资源让给车轮升级；
    // 将来若恢复金系兵种（方阵兵/阔剑兵/骑兵）或金系科技，再把这段打开。
    // if (HasBuilding(BUILDING_MARKET) && g_frame >= 21000)
    //   TryBuildingAction(ai, BUILDING_MARKET, BUILDING_MARKET_GOLD_UPGRADE);
    //
    // 车轮升级（战车科技）：解锁战车/战车弓箭手（战车 40 食 + 60 木、无黄金），
    // 并让村民移速 +30%，采集与搬运同时受益。成本 100 木 + 150 食物。
    // 优先级最高 —— 它直接决定战车 / 战车弓兵能否出场。
    //
    // 木材加工（工具时代，75 木 + 120 食物）→ 工艺（青铜时代，链上第二个节点）。
    // 工具时代车轮因时代不足会被 Core 以 ACTION_INVALID_BUILDACT_LOCK 拒绝
    // （Core_List.cpp:427-428 在设置建筑关系之前就 return，不占用市场），
    // 此时按优先级自然落到木材加工，与原先的行为一致。
    //
    // 农田升级链（驯养动物@工具时代 → 犁@铜器时代）：两级都让农场储量 +75。
    // 铜器时代之后由 ManageMarketResearch 自动推进到犁。
    if (HasBuilding(BUILDING_MARKET)) {
      const tagBuilding *market = FindBuildingByType(BUILDING_MARKET, true);
      if (market != nullptr) {
        TrackWheelResearch(market);       // 车轮完成状态（供战车弓兵门控）
        ManageMarketResearch(ai, market); // 市场研发下发（每帧最多一条）
      }
    }
    // 【取消】升级为复合弓（青铜时代，180 食 + 100 木）：这是靶场
    // （BUILDING_RANGE）的科技，不是市场的。它解锁复合弓兵，而当前远程主力是
    // 战车弓兵，普通弓兵只保留 5 个且 TryProduceBowman 固定产普通弓兵
    // （不会转产复合弓兵）—— 解锁的兵种根本不在生产队列里，研发它只是白占
    // 靶场一个研发周期，还把食物从升时代与产兵上挤走。
    // if (HasBuilding(BUILDING_RANGE) &&
    //     info.civilizationStage != CIVILIZATION_TOOLAGE)
    //   TryBuildingAction(ai, BUILDING_RANGE,
    //                     BUILDING_RANGE_UPGRADE_COMPOSITE_BOW);
    // 【取消】棍棒兵升级（→斧头兵）：兵营单位的配额 armyTarget 已经是 0
    // （见 ManageWeightedProduction 的兵种配比），一个棍棒兵都不造，
    // 升级出来的斧头兵自然也没人用，研发它只是白耗食物。
    // if (info.civilizationStage != CIVILIZATION_TOOLAGE) {
    //   ResearchTech(ai, clubmanUpgradeOrderId, BUILDING_ARMYCAMP,
    //                BUILDING_ARMYCAMP_UPGRADE_CLUBMAN);
    // }
    // 农田升级链：驯养动物（工具时代，200 食 + 50 木）→ 犁（铜器时代，250 食 +
    // 75 木）， 两级都让农场储量上限 +75（Development.cpp:310-318，合计
    // +150）。 整条链推迟到铜器时代之后再开始：
    //   · 工具时代的市场时间让给木材加工（伐木 +50%），那才是经济瓶颈；
    //   · 驯养动物的 200 食物会挤占升时代所需的 800 食物。
    // 4 块以上农场才划算（农场太少回不了本）。
    // 下发点在上方的市场研发互斥链里 —— 这里不能再发一条，否则会和车轮升级
    // 争用同一个市场 SN，重新触发「排最后的覆盖排前面的」那个 bug。
    // 【跳过】升级为阔剑：改以「斧头兵」（棍棒兵升级，血50/攻5、仅耗食物）
    // 作为步兵主力，避免阔剑兵的黄金消耗（15 金/个 + 研发 50 金）。
    // if (info.civilizationStage != CIVILIZATION_TOOLAGE) {
    //   ResearchTech(ai, broadswordUpgradeOrderId, BUILDING_ARMYCAMP,
    //                BUILDING_ARMYCAMP_UPGRADE_BROADSWORD);
    // }
    // 【取消】后勤研究（兵营单位人口占用 1 → 0.5）：只对兵营单位生效，
    // 而兵营单位的配额 armyTarget 是 0；它还是唯一消耗黄金的科技，偏偏 AI 的
    // 采金早已停用（CalculateFarmerTargets 里 weight[3] 归零），研发它等于
    // 白扔食物与黄金。logisticsReady 因此恒为 false，兵力目标翻倍那处不会再触发。
    // if (!logisticsReady) {
    //   const tagBuilding *camp = FindBuildingByType(BUILDING_ARMYCAMP, true);
    //   if (camp != nullptr) {
    //     if (camp->Project == BUILDING_ARMYCAMP_RESEARCH_LOGISTICS)
    //       logisticsSeenRunning = true; // 研发进行中
    //     else if (logisticsSeenRunning)
    //       logisticsReady = true; // 研发结束 → 人口减半已生效
    //     else if (info.civilizationStage != CIVILIZATION_TOOLAGE)
    //       TryBuildingAction(ai, BUILDING_ARMYCAMP,
    //                         BUILDING_ARMYCAMP_RESEARCH_LOGISTICS);
    //   }
    // }
    // 仓库科技，三项逐项串行下单。考察点的链每个 action 下挂两个节点
    // （Development.cpp:535-559）：工具时代 L1 → 铜器时代 L2；再发一次同一
    // action 即推进到链上下一级，所以 USETOOL 在数组里出现两次，第二遍就是
    // 「进一步研发」（再 +2）。
    //
    //   1. 近战攻击 +2        100 食
    //   2. 弓兵护甲 +2        100 食
    //   3. 近战攻击再 +2      200 食 + 120 金   ← 进一步研发
    //
    // 【取消】步兵护甲（DEFENSE_INFANTRY）的两项已移除：当前兵种配比里没有
    // 任何近战步兵（棍棒兵 / 阔剑兵 / 方阵兵的生产目标都是 0），研发它只会
    // 白占食物与研发周期。近战攻击（USETOOL）保留 —— 它同时作用于战车。
    //
    // 门控不用固定帧号，改用「已过升时代 + 资源够点下一项」：
    // 实测食物峰值出现在 f≈12000（600+，此时仍在攒升时代的 800 食物）
    // 与后期兵力目标饱和之后（700+）；原先写死的 g_frame >= 21000 恰好
    // 落在升时代扣掉 800 食物之后的最低谷（30~100），第一项就要 100 食物，
    // 于是整局都点不上（日志中 Core 持续返回 20 = ACTION_INVALID_RESOURCE）。
    // 要求进入青铜时代，是为了不挤占升时代所需的 800 食物。
    if (info.civilizationStage != CIVILIZATION_TOOLAGE &&
        HasBuilding(BUILDING_STOCK)) {
      static const int kStockTechs[3] = {
          BUILDING_STOCK_UPGRADE_USETOOL,
          BUILDING_STOCK_UPGRADE_DEFENSE_ARCHER,
          BUILDING_STOCK_UPGRADE_USETOOL};
      static const int kStockTechFood[3] = {
          BUILDING_STOCK_UPGRADE_CLOSER_ATTACK_FOOD,
          BUILDING_STOCK_UPGRADE_DEFENSE_ARCHER_FOOD,
          BUILDING_STOCK_UPGRADE_CLOSER_ATTACK_2_FOOD};
      static const int kStockTechGold[3] = {
          0, 0, BUILDING_STOCK_UPGRADE_CLOSER_ATTACK_2_GOLD};
      ResearchTechQueue(ai, BUILDING_STOCK, kStockTechs, kStockTechFood,
                        kStockTechGold, 3, stockTechCursor, stockTechOrderId);
    }
    ManageWeightedProduction(ai, nearPopulationCap);
    TryAssignIdleFarmer(ai);
}


static bool IsPriestPointUsable(int blockDR, int blockUR)
{
    if (blockDR < 1 || blockUR < 1 ||
        blockDR >= MAP_L - 1 || blockUR >= MAP_U - 1)
        return false;
    if (info.theMap && (*info.theMap)[blockDR][blockUR].type == MAPPATTERN_OCEAN)
        return false;

    for (const tagBuilding &building : info.buildings)
    {
        if (building.Blood > 0 && building.BlockDR == blockDR &&
            building.BlockUR == blockUR)
            return false;
    }
    return true;
}

// 祭司撤退点选择依赖该合法性检查，提前声明以保持函数定义顺序清晰。
static bool IsPriestPointUsable(int blockDR, int blockUR);

static pair<double, double> GetPriestEmergencyPoint(
    const tagArmy &priest,
    const tagArmy &threat)
{
    // 【第一版公式，943f7f7】朝市镇中心走（取祭司与中心连线的 2/3 处），
    // 再朝「远离威胁」的方向偏移 3 格。
    //
    // 为什么回到这一版：它是历史上唯一的「连续公式」—— 不需要"选哪座塔"、
    // 也不需要"朝哪个方向"的离散判断。后续所有重写（候选表 12/10/8/6、
    // 退中心、绕中心 18/15/12、跨塔打分、热力图）都在"选一个固定点"，
    // 因而反复出现「算出的点等于祭司自身格 → 移动指令变成走回原地」
    // 或「打分随祭司移动翻转 argmax → 目标在两座塔之间抖动」。
    // 连续公式天然避开这两类问题。
    //
    // 注意：原版这一行的偏移方向写反了 —— dx = priest.DR - threat.DR，
    // 原代码取 dx>0 ? -3 : 3，那是朝威胁一侧走，与它自己的注释
    // 「keep a small offset away from the threat」相反；这很可能就是
    // 该提交说明里「有点Bug」的所指。这里按注释本意把方向改回。
    int dx = priest.BlockDR - threat.BlockDR;
    int dy = priest.BlockUR - threat.BlockUR;
    if (dx == 0 && dy == 0)
        dx = -1;  // 与威胁完全重合时给一个确定方向，避免两轴偏移都为 0

    int blockDR = priest.BlockDR;
    int blockUR = priest.BlockUR;
    const tagBuilding *center = FindCenter();
    if (center != nullptr) {
        // 朝中心方向走 2/3 路程（落在祭司与中心之间、靠中心一侧）
        blockDR = (priest.BlockDR + center->BlockDR * 2) / 3;
        blockUR = (priest.BlockUR + center->BlockUR * 2) / 3;
    }
    blockDR += dx > 0 ? 3 : (dx < 0 ? -3 : 0);  // 远离威胁
    blockUR += dy > 0 ? 3 : (dy < 0 ? -3 : 0);
    blockDR = max(1, min(MAP_L - 2, blockDR));
    blockUR = max(1, min(MAP_U - 2, blockUR));
    return make_pair((blockDR + 0.5) * double(BLOCKSIDELENGTH),
                     (blockUR + 0.5) * double(BLOCKSIDELENGTH));
}

// 返回正在攻击祭司的敌人 SN（敌方单位关系目标指向祭司）。
static int FindEnemyTargetingPriestSN(int priestSN) {
  for (const tagArmy &enemy : info.enemy_armies) {
    if (enemy.Blood > 0 && enemy.WorkObjectSN == priestSN)
      return enemy.SN;
  }
  return -1;
}

// 判断指定单位（士兵或农民）是否存活。
static bool IsUnitAlive(int sn) {
  for (const tagArmy &army : info.armies)
    if (army.SN == sn && army.Blood > 0)
      return true;
  for (const tagFarmer &farmer : info.farmers)
    if (farmer.SN == sn && farmer.Blood > 0)
      return true;
  return false;
}

// 返回指定单位当前的行动目标 SN（-1 表示无）。
static int GetUnitWorkTargetSN(int sn) {
  for (const tagArmy &army : info.armies)
    if (army.SN == sn)
      return army.WorkObjectSN;
  for (const tagFarmer &farmer : info.farmers)
    if (farmer.SN == sn)
      return farmer.WorkObjectSN;
  return -1;
}

// 敌人攻击祭司时，为每个威胁敌人各派一个单位（优先士兵，其次农民）转移仇恨。
static void AssignPriestDecoy(UsrAI *ai) {
  const tagArmy *priest = FindPriest();
  if (priest == nullptr) {
    priestDecoyByThreat.clear();
    return;
  }

  // 收集当前正在攻击祭司的敌人。
  set<int> threats;
  for (const tagArmy &enemy : info.enemy_armies) {
    if (enemy.Blood > 0 && enemy.WorkObjectSN == priest->SN)
      threats.insert(enemy.SN);
  }

  // 清理已失效的记录：威胁消失或诱饵阵亡。
  for (map<int, int>::iterator it = priestDecoyByThreat.begin();
       it != priestDecoyByThreat.end();) {
    if (threats.find(it->first) == threats.end() || !IsUnitAlive(it->second))
      it = priestDecoyByThreat.erase(it);
    else
      ++it;
  }

  // 已派出的诱饵继续攻击各自对应的敌人。
  set<int> usedDecoys;
  for (map<int, int>::iterator it = priestDecoyByThreat.begin();
       it != priestDecoyByThreat.end(); ++it) {
    usedDecoys.insert(it->second);
    if (GetUnitWorkTargetSN(it->second) != it->first)
      ai->HumanAction(it->second, it->first);
  }

  // 为每个尚无诱饵的威胁敌人分配一个新诱饵。
  for (set<int>::const_iterator tit = threats.begin(); tit != threats.end();
       ++tit) {
    if (priestDecoyByThreat.find(*tit) != priestDecoyByThreat.end())
      continue;

    int bestSN = -1;
    int bestDis2 = 2000000000;
    // 优先士兵（排除祭司与侦察兵），避免重复使用已有诱饵。
    for (const tagArmy &army : info.armies) {
      if (army.Blood <= 0 || army.Sort == AT_PRIEST || army.Sort == AT_SCOUT)
        continue;
      if (usedDecoys.find(army.SN) != usedDecoys.end())
        continue;
      const int dis2 = BlockDis2(army.BlockDR, army.BlockUR, priest->BlockDR,
                                 priest->BlockUR);
      if (dis2 < bestDis2) {
        bestDis2 = dis2;
        bestSN = army.SN;
      }
    }
    // 其次农民。
    if (bestSN == -1) {
      for (const tagFarmer &farmer : info.farmers) {
        if (farmer.Blood <= 0 || farmer.FarmerSort != FARMERTYPE_FARMER)
          continue;
        if (usedDecoys.find(farmer.SN) != usedDecoys.end())
          continue;
        const int dis2 = BlockDis2(farmer.BlockDR, farmer.BlockUR,
                                   priest->BlockDR, priest->BlockUR);
        if (dis2 < bestDis2) {
          bestDis2 = dis2;
          bestSN = farmer.SN;
        }
      }
    }
    if (bestSN == -1)
      break; // 没有可用单位，剩余威胁暂不处理

    usedDecoys.insert(bestSN);
    priestDecoyByThreat[*tit] = bestSN;
    CancelPendingGatherOrder(bestSN);
    ai->HumanAction(bestSN, *tit);
    {
      char buf[256];
      snprintf(buf, sizeof(buf), "[DECOY] f=%d decoy=%d threat=%d priest=%d",
               g_frame, bestSN, *tit, priest->SN);
      AiDebugLog(buf);
    }
  }
}

// 前向声明：治疗按兵种优先级选择目标，需要用到该函数（定义在下方）。
static int PriestConversionPriority(int armySort);

// 祭司在安全状态下治疗受伤友军：士兵按兵种优先级优先，同级再按受伤程度；
// 农民优先级最低，仅在无士兵受伤时才治疗。忽略自身与侦察兵。
static bool TryPriestHeal(UsrAI *ai, const tagArmy &priest) {
  // ① 正在治疗的友军未满血 → 保持该治疗关系，不切换目标。
  // 否则会因「别人伤更重」而在伤员之间反复横跳，谁都治不满。
  if (priest.WorkObjectSN != -1) {
    const int curSN = priest.WorkObjectSN;
    for (const tagArmy &ally : info.armies) {
      if (ally.SN == curSN && ally.Blood > 0 && ally.MaxBlood > 0) {
        if (ally.Blood < ally.MaxBlood)
          return true; // 未满血：继续治疗当前目标
        break;         // 已满血：换下一个伤员
      }
    }
    for (const tagFarmer &farmer : info.farmers) {
      if (farmer.SN == curSN && farmer.Blood > 0 && farmer.MaxBlood > 0) {
        if (farmer.Blood < farmer.MaxBlood)
          return true;
        break;
      }
    }
  }

  // ② 没有在治疗中（或目标已满血）→ 按兵种优先级挑选新目标。
  int bestSN = -1;
  int bestPriority = -1;
  double bestRatio = 0.95; // 只治疗血量低于 95% 的单位
  for (const tagArmy &ally : info.armies) {
    if (ally.SN == priest.SN || ally.Blood <= 0 || ally.MaxBlood <= 0)
      continue;
    // 太远的伤兵不治：Core 的治疗是「走过去建立关系」，跑一趟会把祭司拽离
    // 基地（实测它因此跑到离中心 30 格处待机，援军与箭塔都够不到）。
    if (BlockDis2(priest.BlockDR, priest.BlockUR, ally.BlockDR, ally.BlockUR) >
        USR_PRIEST_HEAL_RADIUS * USR_PRIEST_HEAL_RADIUS)
      continue;
    const double ratio = double(ally.Blood) / double(ally.MaxBlood);
    if (ratio >= 0.95)
      continue;
    const int priority = PriestConversionPriority(ally.Sort);
    // 兵种优先级更高者优先；同一优先级时受伤更重者优先。
    if (priority > bestPriority ||
        (priority == bestPriority && ratio < bestRatio)) {
      bestPriority = priority;
      bestRatio = ratio;
      bestSN = ally.SN;
    }
  }
  // 农民视为最低优先级：仅在没有受伤士兵时才治疗农民。
  if (bestSN == -1) {
    for (const tagFarmer &farmer : info.farmers) {
      if (farmer.Blood <= 0 || farmer.MaxBlood <= 0)
        continue;
      // 同士兵：太远的不治，避免祭司为了一个远处农民横穿全图。
      if (BlockDis2(priest.BlockDR, priest.BlockUR, farmer.BlockDR,
                    farmer.BlockUR) >
          USR_PRIEST_HEAL_RADIUS * USR_PRIEST_HEAL_RADIUS)
        continue;
      const double ratio = double(farmer.Blood) / double(farmer.MaxBlood);
      if (ratio < bestRatio) {
        bestRatio = ratio;
        bestSN = farmer.SN;
      }
    }
  }
  if (bestSN == -1)
    return false;
  if (priest.WorkObjectSN == bestSN)
    return true; // 已在治疗该目标
  ai->HumanAction(priest.SN, bestSN);
  {
    char buf[256];
    snprintf(buf, sizeof(buf),
             "[HEAL] f=%d priest=%d target=%d ratio=%.2f priority=%d", g_frame,
             priest.SN, bestSN, bestRatio, bestPriority);
    AiDebugLog(buf);
  }
  return true;
}

// 接口：祭司安全与最终转换状态机。
// 用途：危险时立即撤退；第三波后才在安全窗口推进并转换敌方工厂。
// 返回祭司转换目标的显式兵种优先级；枚举值本身不代表战斗阶级。
static int PriestConversionPriority(int armySort)
{
    switch (armySort)
    {
    case AT_STONE_THROWER:
        return 110;
    case AT_COMPOSITE_BOWMAN:
        return 100;
    case AT_CAVALRY:
        return 95;
    case AT_CHARIOT_ARCHER:
        return 90;
    case AT_BROADSWORDSMAN:
        return 85;
    case AT_HOPLITE:
        return 84;
    case AT_CHARIOT:
        return 80;
    case AT_SWORDSMAN:
        return 75;
    case AT_IMPROVED:
        return 70;
    case AT_BOWMAN:
        return 60;
    case AT_SLINGER:
        return 55;
    case AT_SCOUT:
        return 50;
    case AT_CLUBMAN:
        return 40;
    default:
        return -1;
    }
}

// 在祭司安全时选择当前可见的最高阶普通敌军；同阶优先选择距离较近者。
static const tagArmy *FindPriestConversionTarget(const tagArmy &priest)
{
    const tagArmy *best = nullptr;
    int bestPriority = -1;
    int bestDis2 = 1000000000;

    for (const tagArmy &enemy : info.enemy_armies)
    {
        if (enemy.Blood <= 0 || enemy.Sort == AT_PRIEST)
            continue;

        const int priority = PriestConversionPriority(enemy.Sort);
        if (priority < 0)
            continue;

        const int dis2 = BlockDis2(priest.BlockDR, priest.BlockUR,
                                   enemy.BlockDR, enemy.BlockUR);
        if (priority > bestPriority ||
            (priority == bestPriority && dis2 < bestDis2))
        {
            best = &enemy;
            bestPriority = priority;
            bestDis2 = dis2;
        }
    }
    return best;
}

// 返回敌方兵种的攻击射程（单位：地图格）；近战兵种返回 2。
// 用途：判断攻击祭司的敌人是否已进入其有效射程。
// 数值取自 config.json 的 DIS_* 配置；多级兵种取较大值以保证安全裕度。
static int EnemyAttackRange(int armySort)
{
    switch (armySort)
    {
    case AT_STONE_THROWER:
    case AT_SHIP:
        return 10;  // DIS_STONE_THROWER / DIS_SHIP
    case AT_CHARIOT_ARCHER:
    case AT_COMPOSITE_BOWMAN:
    case AT_IMPROVED:  // 改良弓兵：1 级 6 格、2 级 7 格，取 7
        return 7;
    case AT_BOWMAN:
        return 5;
    case AT_SLINGER:
        return 4;   // DIS_SLINGER
    default:
        return 2;   // 近战（DIS 为 0，留 2 格贴身裕度）
    }
}

// 是否需要向祭司重发移动指令。
//
// 只在两种情况下为真：目标点变了，或者目标没变但祭司确实卡住了。
// 不能定期无条件重发 —— HumanMove 经由 Core_List::addRelation 会先调用
// suspendRelation（Core_List.cpp:450-469）清空移动路径并重置行动，
// 定期重发等于每次刚起步就把路径清掉（详见 USR_PRIEST_STUCK_FRAMES 的说明）。
static bool ShouldReissuePriestMove(const tagArmy *priest, int tx, int ty)
{
  if (priest == nullptr)
    return false;
  if (priestEmergencyTarget.first != tx || priestEmergencyTarget.second != ty)
    return g_frame - priestEmergencyTargetFrame >= USR_PRIEST_ORDER_INTERVAL;
  // 目标没变：只有祭司自上次下发后一直没挪窝，才判定路径失效需要重发
  if (g_frame - priestEmergencyTargetFrame < USR_PRIEST_STUCK_FRAMES)
    return false;
  return priest->BlockDR == priestMoveFromDR &&
         priest->BlockUR == priestMoveFromUR;
}

static void ManagePriest(UsrAI *ai)
{
    const tagArmy *priest = FindPriest();
    if (priest == nullptr)
        return;  // 祭司死亡后不再执行祭司逻辑，避免解引用空指针

    if (priestMoveOrderId != -1)
    {
        map<int, int>::const_iterator result = info.ins_ret.find(priestMoveOrderId);
        if (result != info.ins_ret.end() ||
            g_frame - priestEmergencyTargetFrame >=
                USR_PRODUCTION_ORDER_TIMEOUT)
        {
            // 记录回执码：0 = 移动关系已建立；非 0（尤其 14 = ISNTFREE）
            // 说明指令被 Core 拒绝，祭司不会移动。
            priestMoveLastRet =
                result != info.ins_ret.end() ? result->second : -1;
            priestMoveLastRetFrame = g_frame;
            priestMoveOrderId = -1;
        }
    }

    // WorkObjectSN 是 Core 当前关系的目标。转换关系从接近阶段开始就不能
    // 被新的移动指令中止，不能只依赖 NowState 已经变成攻击状态。
    bool conversionTargetAlive = false;
    if (priest->WorkObjectSN != -1)
    {
        for (const tagArmy &enemy : info.enemy_armies)
        {
            if (enemy.SN == priest->WorkObjectSN && enemy.Blood > 0)
            {
                conversionTargetAlive = true;
                break;
            }
        }
        if (!conversionTargetAlive)
        {
            for (const tagBuilding &building : info.enemy_buildings)
            {
                if (building.SN == priest->WorkObjectSN && building.Blood > 0)
                {
                    conversionTargetAlive = true;
                    break;
                }
            }
        }
    }
    // 【诊断】记录祭司周围威胁与拦截原因（节流 100 帧），用于定位"被打不逃"。
    {
        static int lastPriestLogFrame = 0;
        if (g_frame - lastPriestLogFrame >= 100)
        {
            lastPriestLogFrame = g_frame;
            int near10 = 0;
            int targeting = 0;
            int minDis = 9999;
            for (const tagArmy &e : info.enemy_armies)
            {
                if (e.Blood <= 0)
                    continue;
                const int d = BlockDis(priest->BlockDR, priest->BlockUR,
                                       e.BlockDR, e.BlockUR);
                if (d <= 10)
                    near10++;
                if (e.WorkObjectSN == priest->SN)
                    targeting++;
                if (d < minDis)
                    minDis = d;
            }
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "[PRIEST] f=%d hp=%d/%d pos=(%d,%d) near10=%d target=%d "
                     "minDis=%d convAlive=%d",
                     g_frame, priest->Blood, priest->MaxBlood, priest->BlockDR,
                     priest->BlockUR, near10, targeting, minDis,
                     (int)conversionTargetAlive);
            AiDebugLog(buf);
        }
    }

    // 总攻阶段：祭司停止治疗、无敌人时留在基地（转换保留，见下方）。
    const bool priestPassive = g_frame >= USR_PRIEST_PASSIVE_FRAME;

    // 撤退：只对「正在攻击祭司」的敌人反应（WorkObjectSN 指向祭司），
    // 且该敌人已进入自身射程（含 2 格提前量）时才撤；避免被路过或攻击
    // 其他目标的敌人惊动。
    // 【第一版触发，943f7f7】取「最近的敌人」，满足其一即撤：
    //   ① 它正在直接攻击祭司（WorkObjectSN 指向祭司）—— 不论距离
    //   ② 它距祭司 ≤ USR_PRIEST_DANGER_RADIUS(9) 格 —— 不论是否在攻击祭司
    //
    // 历史沿革：这一版半径是 9 格；后来被逐步收紧
    //   （736e538 改成 2 格 → f0d2df6 改成 5 格 → 04cf80c 改成「按兵种射程+2」）。
    // 收到"按兵种射程"后阈值只有 tRngE2=16~81，而实测杀死祭司的敌人停在
    // tE2=97~145（约 10~12 格）—— 永远差一点，hasThreat 触发率掉到 0~1%，
    // 撤退（连同退中心、换塔风筝、派援军）几乎从不发生。故回退到第一版。
    const tagArmy *nearestEnemy = nullptr;
    int nearestDis2 = 1000000000;
    int targetingCount = 0;  // 已把祭司设为攻击目标的敌人数（仅供诊断输出）
    for (const tagArmy &enemy : info.enemy_armies)
    {
        if (enemy.Blood <= 0)
            continue;
        if (enemy.WorkObjectSN == priest->SN) ++targetingCount;
        const int dis2 = BlockDis2(priest->BlockDR, priest->BlockUR,
                                   enemy.BlockDR, enemy.BlockUR);
        if (dis2 < nearestDis2) {
            nearestDis2 = dis2;
            nearestEnemy = &enemy;
        }
    }

    const tagArmy *closeThreat = nullptr;
    int closestDis2 = 1000000000;
    if (nearestEnemy != nullptr &&
        (nearestEnemy->WorkObjectSN == priest->SN ||
         nearestDis2 <=
             USR_PRIEST_DANGER_RADIUS * USR_PRIEST_DANGER_RADIUS)) {
        closeThreat = nearestEnemy;
        closestDis2 = nearestDis2;
    }

    // 【诊断】威胁判定明细，用于定位「祭司被打却不撤」。
    // 取「正在攻击祭司」的敌人中最近的一个，打印其兵种、撤退判定用的射程、
    // 以及实际距离的两种度量：mvManh 是日志 [PRIEST] minDis 用的曼哈顿距离，
    // mvE2 是撤退判定用的欧氏距离平方。两者混用是已知隐患，这里一并暴露。
    // hasThreat/threat 是最终是否找到威胁、以及已进射程的攻击者数量。
    {
        static int lastThreatLogFrame = 0;
        if (g_frame - lastThreatLogFrame >= 100)
        {
            lastThreatLogFrame = g_frame;
            int tSort = -1, tRng = -1, tManh = -1, tE2 = -1;
            for (const tagArmy &enemy : info.enemy_armies)
            {
                if (enemy.Blood <= 0 || enemy.WorkObjectSN != priest->SN)
                    continue;
                tSort = enemy.Sort;
                tRng = EnemyAttackRange(enemy.Sort) + 2;
                tManh = BlockDis(priest->BlockDR, priest->BlockUR,
                                 enemy.BlockDR, enemy.BlockUR);
                tE2 = BlockDis2(priest->BlockDR, priest->BlockUR,
                                enemy.BlockDR, enemy.BlockUR);
                break;
            }
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "[THREAT] f=%d hp=%d hasThreat=%d lock=%d "
                     "tSort=%d tRng=%d tRngE2=%d tManh=%d tE2=%d conv=%d "
                     "mvF=%d mvTgt=(%d,%d) mvRet=%d mvRetF=%d wo=%d ns=%d",
                     g_frame, priest->Blood, closeThreat != nullptr,
                     targetingCount, tSort, tRng,
                     tRng * tRng, tManh, tE2, (int)conversionTargetAlive,
                     priestEmergencyTargetFrame, priestEmergencyTarget.first,
                     priestEmergencyTarget.second, priestMoveLastRet,
                     priestMoveLastRetFrame, priest->WorkObjectSN,
                     (int)priest->NowState);
            AiDebugLog(buf);
        }
    }

    // 转换进行中保持关系，不追加移动或改派目标，避免打断既有转换。
    // （曾有「遭到围攻或血量过低时放弃转换先保命」的中断判据，已按要求移除：
    //   它会让祭司在被打断后反复重来，转换永远完不成。）
    // 先记录转换状态。必须放在下面那个 return 之前 —— 否则「转换中」这一状态
    // 永远不会被记下，也就检测不到「转换刚结束」的瞬间。
    const bool justFinishedConversion =
        priestWasConverting && !conversionTargetAlive;
    priestWasConverting = conversionTargetAlive;
    if (justFinishedConversion)
        priestRetreatUntilFrame =
            g_frame + USR_PRIEST_POST_CONVERSION_RETREAT_FRAMES;

    if (conversionTargetAlive)
        return;

    // 转换结束后的撤离窗口：优先回市镇中心（箭塔覆盖圈内），期间不发起新转换。
    // 敌方把转换中的祭司记成了反击锁目标，且那个锁只在目标死亡时解除
    // （详见 USR_PRIEST_POST_CONVERSION_RETREAT_FRAMES 的说明），
    // 所以必须趁这个窗口拉开距离，否则会被一路追到底。
    if (g_frame < priestRetreatUntilFrame)
    {
        const tagBuilding *home = FindCenter();
        if (home == nullptr)
        {
            priestRetreatUntilFrame = USR_INVALID_FRAME;  // 找不到中心，放弃撤离
        }
        else if (BlockDis(priest->BlockDR, priest->BlockUR, home->BlockDR,
                          home->BlockUR) <= 2)
        {
            priestRetreatUntilFrame = USR_INVALID_FRAME;  // 已到家，恢复正常行为
        }
        else if (ShouldReissuePriestMove(priest, home->BlockDR, home->BlockUR))
        {
            priestMoveOrderId = ai->HumanMove(
                priest->SN, (home->BlockDR + 0.5) * double(BLOCKSIDELENGTH),
                (home->BlockUR + 0.5) * double(BLOCKSIDELENGTH));
            priestEmergencyTarget = make_pair(home->BlockDR, home->BlockUR);
            priestEmergencyTargetFrame = g_frame;
            priestMoveFromDR = priest->BlockDR;
            priestMoveFromUR = priest->BlockUR;
            lastPriestOrderFrame = g_frame;
        }
        if (g_frame < priestRetreatUntilFrame)
            return;  // 撤离期间不做转换 / 治疗
    }

    // 【转换优先于撤退】先尝试转换，能转换就转换；不能转换时才撤退。
    //
    // 原先转换分支排在撤退之后，而撤退分支直接 return —— 恢复触发 ③ 之后，
    // 「敌人锁定祭司且在 30 格内」很容易成立，转换路径因此几乎被完全堵死
    // （实测 convAlive=1 只占祭司采样的 1~5%）。
    // 转换敌方攻城武器厂是唯一的获胜条件，所以把转换提到撤退之前。
    // 取舍：转换中若被围攻不会中断（见上方注释），祭司可能死在转换里 ——
    // 这是"能不能赢"与"能不能活"的取舍，按"优先转换"的要求选前者。
    {
        bool anyEnemyVisible = false;
        for (const tagArmy &enemy : info.enemy_armies) {
            if (enemy.Blood > 0) {
                anyEnemyVisible = true;
                break;
            }
        }
        if (anyEnemyVisible) {
            const tagArmy *armyTarget = FindPriestConversionTarget(*priest);
            const tagBuilding *buildingTarget = nullptr;
            if (!armyTarget && g_frame >= 30000)
                buildingTarget = FindEnemySiege(*priest);
            const int targetSN = armyTarget
                                     ? armyTarget->SN
                                     : (buildingTarget ? buildingTarget->SN : -1);
            // 同一目标已有关系时不重复下达 HumanAction；重复指令会中止原关系。
            if (targetSN != -1 && priest->ConvertCooldown <= 0 &&
                priestMoveOrderId == -1 && priest->WorkObjectSN != targetSN) {
                priestMoveOrderId = ai->HumanAction(priest->SN, targetSN);
                priestEmergencyTargetFrame = g_frame;
                lastPriestOrderFrame = g_frame;
                return;
            }
        }
    }

    if (closeThreat)
    {
        priestSafeSinceFrame = USR_INVALID_FRAME;
        priestDangerLastFrame = g_frame;
        priestDangerTargetSN = closeThreat->SN;

        const pair<double, double> retreatPoint =
            GetPriestEmergencyPoint(*priest, *closeThreat);
        const pair<int, int> target = make_pair(
            static_cast<int>(retreatPoint.first / double(BLOCKSIDELENGTH)),
            static_cast<int>(retreatPoint.second / double(BLOCKSIDELENGTH)));
        // 撤退点变化时发指令；点位没变但祭司卡住（被挡、寻路失败）也会重发，
        // 判据统一在 ShouldReissuePriestMove 里 —— 那里同时保证不会因为
        // 「定期重发」而反复清空移动路径。
        //
        // 这里刻意不再要求「上一张订单已结算」（原条件含 priestMoveOrderId
        // == -1）：实测该订单可能长时间不在 info.ins_ret 结算，导致一次撤退
        // 指令下达后 637 帧都发不出新指令，而祭司正在持续掉血。
        if (ShouldReissuePriestMove(priest, target.first, target.second)) {
          priestMoveOrderId = ai->HumanMove(priest->SN, retreatPoint.first,
                                            retreatPoint.second);
          priestEmergencyTarget = target;
          priestEmergencyTargetFrame = g_frame;
          priestMoveFromDR = priest->BlockDR;
          priestMoveFromUR = priest->BlockUR;
          lastPriestOrderFrame = g_frame;
        }
        return;
    }

    // 无威胁时清除撤退点记录：否则下次遇到同方向的威胁时，算出的撤退点
    // 与残留记录相同，会因「点位未变化」而不再下达移动指令。
    priestEmergencyTarget = make_pair(-1, -1);

    // 安全后不再追加移动，避免移动指令反复中止转换关系。
    priestDangerTargetSN = -1;
    if (priestSafeSinceFrame == USR_INVALID_FRAME)
        priestSafeSinceFrame = g_frame;

    // 只在视野内没有敌人时才治疗（有敌人时祭司专注转换，保持战斗准备）。
    bool enemyVisible = false;
    for (const tagArmy &enemy : info.enemy_armies)
    {
        if (enemy.Blood > 0)
        {
            enemyVisible = true;
            break;
        }
    }
    if (!enemyVisible)
    {
        // 开局探路：借用侦察骑兵的前沿探索（FindBestScoutFrontier）。
        //
        // 原先用「绕市镇中心半径 10 格的 8 个固定环点」，那个方案的两个问题：
        //   ① 覆盖只有半径 10 格，对 100×100 的地图几乎没有意义；
        //   ② 完全不理会地形与已探索区域，可能反复走已探明的地方、
        //      也可能被海洋/建筑挡住而原地打转。
        // 侦察骑兵的探索是按「已探明陆地 + 邻接未探明」找前沿点，
        // 并按「距自己越近越好、最近访问过的点扣分」排序 —— 这正是"探路"该有的样子。
        // 复用同一套前沿判定（IsScoutFrontierUsable 已排除建筑、资源与紧贴的敌人）。
        //
        // 放在「无敌人」分支里：有敌人时仍走威胁/转换逻辑，不会为了探路挨打。
        // 探路：一直走到找到瞪羚为止 —— 探路的目的就是给农民找到食物来源
        // （食物只靠浆果丛与瞪羚，见 ResourceBucket），找到了就收工，
        // 不再受固定帧数限制。USR_PRIEST_EXPLORE_RADIUS（不离开基地 40 格）
        // 与「前沿点走不到就换下一个」仍是兜底，
        // 避免它无限往外跑或在够不到的点上原地空耗。
        if (!priestExploreDone && HasVisibleGazelle())
            FinishPriestExplore("gazelle");  // 找到食物来源，收工

        if (!priestExploreDone)
        {
            // 有目标时只做「到达 / 卡住」判定，绝不在行进途中重发。
            // HumanMove 经由 Core_List::addRelation 会先 suspendRelation
            // （Core_List.cpp:450-469）清空移动路径，定期重发等于每隔一会儿就把
            // 祭司拽回起点 —— 实测表现为每 2.4 秒重发同一坐标、原地不动。
            if (priestFrontierTarget.first >= 0)
            {
                const int ttx = priestFrontierTarget.first;
                const int tty = priestFrontierTarget.second;
                if (BlockDis(priest->BlockDR, priest->BlockUR, ttx, tty) <= 2)
                {
                    priestFrontierTarget = make_pair(-1, -1);  // 到达 → 换一个
                    priestFrontierStuckCount = 0;  // 确实在推进，清空计数
                }
                else if (g_frame - priestEmergencyTargetFrame >=
                             USR_PRIEST_STUCK_FRAMES &&
                         priest->BlockDR == priestMoveFromDR &&
                         priest->BlockUR == priestMoveFromUR)
                {
                    // 给足整段 USR_PRIEST_STUCK_FRAMES 都没挪窝 → 判定走不到
                    // （隔着海洋 / 被建筑围住），放弃该点换下一个。用这么长的
                    // 窗口是为了不把「正常行进」误判成卡住 —— 误判就会重发，
                    // 重发就会清空路径。
                    priestFrontierTarget = make_pair(-1, -1);
                    // 走不到就换下一个点继续试，但**不结束探路** ——
                    // 祭司只在找到瞪羚时才停（见 FinishPriestExplore 的调用点）。
                    ++priestFrontierStuckCount;
                }
                return;
            }

            // 没有目标时才选点下发，并按 USR_PRIEST_EXPLORE_ORDER_INTERVAL 节流
            if (g_frame - priestEmergencyTargetFrame <
                USR_PRIEST_EXPLORE_ORDER_INTERVAL)
                return;

            int tx = -1;
            int ty = -1;
            if (!FindBestScoutFrontier(*priest, tx, ty))
            {
                // 暂时没有可用前沿（已探明陆地不再邻接未探明）。不结束探路：
                // 前沿集合会随已探明区域变化而更新，隔一会儿再看。
                return;
            }
            tx = max(1, min(MAP_L - 2, tx));
            ty = max(1, min(MAP_U - 2, ty));
            // 不离开基地太远：最近的前沿点都已超出探索半径，说明近处已探明
            // 得差不多，此时不再为探路把祭司送出箭塔覆盖圈。
            const tagBuilding *home = FindCenter();
            if (home != nullptr &&
                BlockDis(home->BlockDR, home->BlockUR, tx, ty) >
                    USR_PRIEST_EXPLORE_RADIUS)
            {
                // 最近的前沿点都超出了探索半径。不结束探路，只是这一轮不去 ——
                // 祭司只在找到瞪羚时才停；这里既不移动也不清状态，等下一轮。
                return;
            }

            priestFrontierTarget = make_pair(tx, ty);
            priestMoveOrderId = ai->HumanMove(
                priest->SN, (tx + 0.5) * double(BLOCKSIDELENGTH),
                (ty + 0.5) * double(BLOCKSIDELENGTH));
            priestEmergencyTarget = priestFrontierTarget;
            priestEmergencyTargetFrame = g_frame;
            priestMoveFromDR = priest->BlockDR;
            priestMoveFromUR = priest->BlockUR;
            // 与侦察兵一致：记下该前沿点的访问帧，让 FindBestScoutFrontier
            // 的 recentPenalty（2400 帧内去过的点扣 1800 分）避开刚走过的点。
            // 不记这一笔，「到达 → 作废 → 重选」会又选回同一个点。
            scoutFrontierVisitFrame[make_pair(tx, ty)] = g_frame;
            return;  // 探路期间不做别的事（治疗/留基地）
        }

        // 第三波之前没有敌人时，祭司留在基地待机。
        // 它只有 100 血、近战与远程防御都是 0、且不能自愈，待在基地的箭塔
        // 覆盖圈里最安全；原先它会在无威胁时自由治疗，被远处伤兵一路带离基地
        // （实测跑到离市镇中心 30 格处待机，援军半径 14 与箭塔 6/9/12 格圈
        //  都够不到）。
        if (priestPassive || g_frame < USR_PRIEST_HOLD_HOME_UNTIL_FRAME)
        {
            const tagBuilding *home = FindCenter();
            if (home != nullptr)
            {
                const int homeDis2 = BlockDis2(priest->BlockDR, priest->BlockUR,
                                               home->BlockDR, home->BlockUR);
                if (homeDis2 >
                    USR_PRIEST_HOME_RADIUS * USR_PRIEST_HOME_RADIUS)
                {
                    // 节流重发：每 USR_PRIEST_ORDER_INTERVAL 帧最多一次，
                    // 否则每帧重发会不断 suspendRelation + 重新寻路，反而走不动。
                    if (ShouldReissuePriestMove(priest, home->BlockDR,
                                                home->BlockUR))
                    {
                        priestMoveOrderId = ai->HumanMove(
                            priest->SN,
                            (home->BlockDR + 0.5) * double(BLOCKSIDELENGTH),
                            (home->BlockUR + 0.5) * double(BLOCKSIDELENGTH));
                        priestEmergencyTarget =
                            make_pair(home->BlockDR, home->BlockUR);
                        priestEmergencyTargetFrame = g_frame;
                        priestMoveFromDR = priest->BlockDR;
                        priestMoveFromUR = priest->BlockUR;
                        lastPriestOrderFrame = g_frame;
                    }
                    return;
                }
            }
        }
        // 总攻阶段治疗也停用：祭司只保存实力，不做任何会被敌人牵制的行动。
        if (priestPassive)
            return;
        TryPriestHeal(ai, *priest);
        return;
    }

    // 转换全程保留，不因总攻阶段停用。
    // 转换敌方攻城武器厂是唯一的获胜条件，关掉它就等于关掉获胜路径——
    // 而且原先挂在上面的 `g_frame >= 30000` 与 priestPassive 的门槛相同，
    // 两条规则互相抵消，FindEnemySiege 那行从来没有被执行过。
    // 代价：FindPriestConversionTarget 没有距离限制，祭司可能为此跑到离基地
    // 30~50 格处（实测被 3 个敌人锁定后既撤不回来也打不过）。这是已知取舍。

    // 到这里说明：无转换目标（或转换在冷却 / 有在途指令）且视野内有敌人。
    // 转换分支已提前到撤退之前处理，这里不再重复。
}

static bool IsScoutFrontierUsable(int blockDR, int blockUR)
{
    if (!IsExplorationFrontierBlock(blockDR, blockUR) ||
        blockDR < 2 || blockUR < 2 ||
        blockDR >= MAP_L - 2 || blockUR >= MAP_U - 2)
        return false;

    for (const tagBuilding &building : info.buildings)
    {
        const int size = (building.Type == BUILDING_HOME ||
                          building.Type == BUILDING_ARROWTOWER)
                             ? 2
                             : 3;
        if (abs(blockDR - building.BlockDR) <= size + 1 &&
            abs(blockUR - building.BlockUR) <= size + 1)
            return false;
    }
    for (const tagResource &resource : info.resources)
    {
        if (resource.Blood > 0 &&
            abs(blockDR - resource.BlockDR) <= 1 &&
            abs(blockUR - resource.BlockUR) <= 1)
            return false;
    }
    // 前沿点若紧贴敌方单位则排除，避免侦察骑兵把落点直接选在敌人身上。
    // 这里刻意只排除 2 格内的敌人：更远的敌意交给 DispatchScouts 里的
    // FindScoutThreatSN + GetScoutEmergencyPoint 紧急撤离处理，前沿过滤
    // 不该重复承担安全职责。
    //
    // 原先是排除 6 格（dis2 <= 36）。敌方基地周围必然有驻军，于是那片区域的
    // 前沿点永久不可用，侦察骑兵结构上到不了敌方基地 —— info.enemy_buildings
    // 因此全程为空，祭司永远没有可转换的攻城武器厂，获胜路径直接断掉。
    for (const tagArmy &enemy : info.enemy_armies)
    {
        if (enemy.Blood > 0 &&
            BlockDis2(blockDR, blockUR, enemy.BlockDR, enemy.BlockUR) <= 4)
            return false;
    }
    return true;
}

static bool FindBestScoutFrontier(const tagArmy &scout, int &targetDR,
                                  int &targetUR)
{
    int bestScore = -2000000000;
    bool found = false;
    for (int dr = 2; dr < MAP_L - 2; ++dr)
    {
        for (int ur = 2; ur < MAP_U - 2; ++ur)
        {
            if (!IsScoutFrontierUsable(dr, ur))
                continue;

            const pair<int, int> block = {dr, ur};
            const int recentPenalty = scoutFrontierVisitFrame.count(block) &&
                                              g_frame - scoutFrontierVisitFrame[block] < 2400
                                          ? 1800
                                          : 0;
            const int distance = BlockDis2(scout.BlockDR, scout.BlockUR,
                                           dr, ur);
            int occupiedPenalty = 0;
            for (const auto &other : info.armies)
            {
                if (other.SN != scout.SN && other.Sort == AT_SCOUT &&
                    other.Blood > 0 && other.BlockDR == dr &&
                    other.BlockUR == ur)
                    occupiedPenalty += 2500;
            }
            const int score = -distance * 10 - recentPenalty - occupiedPenalty;
            if (!found || score > bestScore)
            {
                bestScore = score;
                targetDR = dr;
                targetUR = ur;
                found = true;
            }
        }
    }
    return found;
}
static int FindScoutThreatSN(const tagArmy &scout)
{
    int threatSN = -1;
    int bestDis2 = USR_PRIEST_DANGER_RADIUS * USR_PRIEST_DANGER_RADIUS;
    for (const tagArmy &enemy : info.enemy_armies)
    {
        if (enemy.Blood <= 0)
            continue;
        const int dis2 = BlockDis2(scout.BlockDR, scout.BlockUR,
                                   enemy.BlockDR, enemy.BlockUR);
        if (dis2 < bestDis2)
        {
            bestDis2 = dis2;
            threatSN = enemy.SN;
        }
    }
    for (const tagFarmer &enemy : info.enemy_farmers)
    {
        if (enemy.Blood <= 0)
            continue;
        const int dis2 = BlockDis2(scout.BlockDR, scout.BlockUR,
                                   enemy.BlockDR, enemy.BlockUR);
        if (dis2 < bestDis2)
        {
            bestDis2 = dis2;
            threatSN = enemy.SN;
        }
    }
    return threatSN;
}

static bool FindScoutThreatBlock(int threatSN, int &blockDR, int &blockUR)
{
    if (FindEnemyUnitBlockPosition(threatSN, blockDR, blockUR))
        return true;
    for (const tagResource &resource : info.resources)
    {
        if (resource.SN == threatSN && resource.Blood > 0)
        {
            blockDR = resource.BlockDR;
            blockUR = resource.BlockUR;
            return true;
        }
    }
    return false;
}

static pair<double, double> GetScoutEmergencyPoint(const tagArmy &scout,
                                                    int threatDR, int threatUR)
{
    const int dx = scout.BlockDR - threatDR;
    const int dy = scout.BlockUR - threatUR;
    const int stepDR = dx == 0 ? 0 : (dx > 0 ? 1 : -1);
    const int stepUR = dy == 0 ? 0 : (dy > 0 ? 1 : -1);
    // 与 GetPriestEmergencyPoint 同一处缺陷：二维候选表只取单一轴的分量，
    // |dx| < |dy| 时前四个候选偏移量为 0，撤离点算成原地。改为两轴共用距离表。
    const bool useDR = abs(dx) >= abs(dy);
    static const int kDistances[] = {12, 10, 8, 6};
    const int distanceCount = sizeof(kDistances) / sizeof(kDistances[0]);
    for (int i = 0; i < distanceCount; ++i) {
      int blockDR = scout.BlockDR;
      int blockUR = scout.BlockUR;
      if (useDR)
        blockDR += stepDR * kDistances[i];
      else
        blockUR += stepUR * kDistances[i];
      blockDR = max(1, min(MAP_L - 2, blockDR));
      blockUR = max(1, min(MAP_U - 2, blockUR));
      if (IsPriestPointUsable(blockDR, blockUR))
        return make_pair((blockDR + 0.5) * double(BLOCKSIDELENGTH),
                         (blockUR + 0.5) * double(BLOCKSIDELENGTH));
    }
    return make_pair((scout.BlockDR + 0.5) * double(BLOCKSIDELENGTH),
                     (scout.BlockUR + 0.5) * double(BLOCKSIDELENGTH));
}
static void DispatchScouts(UsrAI *ai)
{
    const int scoutOrderInterval = 60;
    const int scoutEmergencyOrderInterval = 20;
    const int scoutSafeRadius = 6;
    const int scoutWaypointCount = 8;
    const int scoutMargin = 10;
    // 巡逻点避开左下角（敌方基地方向），只探索我方基地周边与地图中部，避免过早遭遇敌人。
    static const int scoutWaypoints[][2] = {
        {MAP_L - scoutMargin - 1, scoutMargin},             // 右上角
        {MAP_L / 2, scoutMargin},                           // 上边中点
        {MAP_L / 2, MAP_U / 3},                             // 中上
        {MAP_L - scoutMargin - 1, MAP_U / 2},               // 右边中点
        {MAP_L / 2, MAP_U / 2},                             // 地图中心
        {MAP_L - scoutMargin - 1, MAP_U - scoutMargin - 1}, // 右下角
        {MAP_L / 2, MAP_U - scoutMargin - 1},               // 下边中点
        {MAP_L * 3 / 4, MAP_U / 3}};                        // 右上偏中

    set<int> liveScouts;
    for (const tagArmy &army : info.armies)
    {
        if (army.Blood > 0 && army.Sort == AT_SCOUT)
            liveScouts.insert(army.SN);
    }

    for (map<int, int>::iterator it = scoutLastOrderFrame.begin();
         it != scoutLastOrderFrame.end();)
    {
        if (liveScouts.find(it->first) == liveScouts.end())
        {
            scoutWaypointIndex.erase(it->first);
            scoutTargetBlock.erase(it->first);
            scoutStuckCount.erase(it->first);
            scoutEmergencyOrderId.erase(it->first);
            scoutEmergencyTarget.erase(it->first);
            scoutDangerLastFrame.erase(it->first);
            scoutIsRecon.erase(it->first);
            scoutLuredTarget.erase(it->first);
            it = scoutLastOrderFrame.erase(it);
        }
        else
            ++it;
    }

    for (const tagArmy &scout : info.armies)
    {
        if (scout.Blood <= 0 || scout.Sort != AT_SCOUT)
            continue;

        // 首次看到该侦察兵时确定角色：在 USR_SCOUT_RECON_FRAME 之后生产
        // （即首次被看到时帧号已过该点）的那个负责侦测敌方基地。
        map<int, bool>::iterator roleIt = scoutIsRecon.find(scout.SN);
        if (roleIt == scoutIsRecon.end())
            roleIt = scoutIsRecon
                         .insert(make_pair(scout.SN,
                                           g_frame >= USR_SCOUT_RECON_FRAME))
                         .first;
        const bool isRecon = roleIt->second;

        // 【谨慎型专用】勾引 + 撤回基地。
        // 用途：先在视野内挑一个最近的敌人打一下，建立「我在打它」的关系，
        // 敌方 AI 的 FindThreatToArmy 会据此把它锁定为反击目标
        // （waveRetaliationTarget），而该锁定按 enemyai.cpp:1142-1148 的规则
        // 「只有目标死亡才解锁」——所以之后撤回基地，敌人仍会一路追来，
        // 被基地的箭塔与部队消灭。
        //
        // 注意：必须先建立攻击关系再撤。只跑不打的话敌人不会追，因为
        // FindThreatToArmy 看的是 enemyArmy.WorkObjectSN == 自己，与距离无关；
        // 单纯移动不会产生这个关系。
        // 攻击关系只需建立一次：敌人一旦锁定，后续移动指令覆盖掉攻击关系
        // 也不影响已经产生的锁定。
        // 专职侦测的那个（isRecon）不执行本条，否则它会在敌方基地外围被驻军
        // 反复逼退，永远推进不到基地（见下方对紧急撤离的同样处理）。
        if (!isRecon) {
            const tagBuilding *home = FindCenter();

            // 挑视野内最近的敌人作为勾引对象。
            int lureSN = -1;
            int lureDis2 = 1000000000;
            for (const tagArmy &enemy : info.enemy_armies) {
                if (enemy.Blood <= 0)
                    continue;
                const int dis2 = BlockDis2(scout.BlockDR, scout.BlockUR,
                                           enemy.BlockDR, enemy.BlockUR);
                if (dis2 < lureDis2) {
                    lureDis2 = dis2;
                    lureSN = enemy.SN;
                }
            }

            if (lureSN != -1 && home != nullptr) {
                // 还没对这个敌人建立过攻击关系 → 先打它，本帧不移动，
                // 让攻击关系完整存在一帧，确保敌方 AI 能观察到。
                if (scoutLuredTarget[scout.SN] != lureSN) {
                    ai->HumanAction(scout.SN, lureSN);
                    scoutLuredTarget[scout.SN] = lureSN;
                    continue;
                }

                // 关系已建立 → 撤回市镇中心，把追兵引进火力圈。
                const pair<int, int> homeBlock =
                    make_pair(home->BlockDR, home->BlockUR);
                const map<int, pair<int, int>>::const_iterator targetIt =
                    scoutEmergencyTarget.find(scout.SN);
                if (targetIt == scoutEmergencyTarget.end() ||
                    targetIt->second != homeBlock ||
                    g_frame - scoutDangerLastFrame[scout.SN] >=
                        scoutEmergencyOrderInterval) {
                    scoutEmergencyOrderId[scout.SN] = ai->HumanMove(
                        scout.SN, (home->BlockDR + 0.5) * double(BLOCKSIDELENGTH),
                        (home->BlockUR + 0.5) * double(BLOCKSIDELENGTH));
                    scoutEmergencyTarget[scout.SN] = homeBlock;
                    scoutDangerLastFrame[scout.SN] = g_frame;
                }
                continue;
            }
        }

        // 紧急撤离只对谨慎型生效。专职侦测的那个一旦在敌基地外围（9 格内有驻军）
        // 就被逼退，同样推进不到基地——它的任务是探明基地，代价可以接受。
        const int threatSN = isRecon ? -1 : FindScoutThreatSN(scout);
        int threatDR = -1;
        int threatUR = -1;
        if (threatSN != -1 &&
            FindEnemyUnitBlockPosition(threatSN, threatDR, threatUR))
        {
            const pair<double, double> retreatPoint =
                GetScoutEmergencyPoint(scout, threatDR, threatUR);
            const pair<int, int> retreatTarget = make_pair(
                static_cast<int>(retreatPoint.first / double(BLOCKSIDELENGTH)),
                static_cast<int>(retreatPoint.second / double(BLOCKSIDELENGTH)));
            const map<int, pair<int, int>>::const_iterator targetIt =
                scoutEmergencyTarget.find(scout.SN);
            if (targetIt == scoutEmergencyTarget.end() ||
                targetIt->second != retreatTarget ||
                g_frame - scoutDangerLastFrame[scout.SN] >=
                    scoutEmergencyOrderInterval)
            {
                scoutEmergencyOrderId[scout.SN] = ai->HumanMove(
                    scout.SN, retreatPoint.first, retreatPoint.second);
                scoutEmergencyTarget[scout.SN] = retreatTarget;
                scoutDangerLastFrame[scout.SN] = g_frame;
            }
            continue;
        }

        const map<int, pair<int, int>>::const_iterator emergencyIt =
            scoutEmergencyTarget.find(scout.SN);
        if (emergencyIt != scoutEmergencyTarget.end())
        {
            if (BlockDis2(scout.BlockDR, scout.BlockUR,
                          emergencyIt->second.first,
                          emergencyIt->second.second) <=
                scoutSafeRadius * scoutSafeRadius)
            {
                scoutEmergencyTarget.erase(emergencyIt);
                scoutEmergencyOrderId.erase(scout.SN);
            }
            else
            {
                continue;
            }
        }
        if (g_frame < 8000)
            continue;
        map<int, int>::const_iterator lastIt =
            scoutLastOrderFrame.find(scout.SN);
        if (lastIt != scoutLastOrderFrame.end() &&
            g_frame - lastIt->second < scoutOrderInterval)
            continue;

        int &stuckCount = scoutStuckCount[scout.SN];
        map<int, pair<int, int>>::const_iterator targetIt =
            scoutTargetBlock.find(scout.SN);
        if (targetIt != scoutTargetBlock.end())
        {
            if (BlockDis2(scout.BlockDR, scout.BlockUR,
                          targetIt->second.first, targetIt->second.second) <= 9)
            {
                scoutTargetBlock.erase(targetIt);
                stuckCount = 0;
            }
            else
            {
                map<int, pair<int, int>>::const_iterator oldBlockIt =
                    scoutLastBlock.find(scout.SN);
                if (oldBlockIt != scoutLastBlock.end() &&
                    BlockDis2(scout.BlockDR, scout.BlockUR,
                              oldBlockIt->second.first,
                              oldBlockIt->second.second) <= 1)
                {
                    ++stuckCount;
                    scoutTargetBlock.erase(targetIt);
                }
            }
        }

        int targetDR = -1;
        int targetUR = -1;
        if (FindBestScoutFrontier(scout, targetDR, targetUR))
        {
            scoutTargetBlock[scout.SN] = make_pair(targetDR, targetUR);
        }
        else
        {
            // 暂时没有可见前沿时才使用保底巡逻，避免固定路线主导探索。
            int &waypoint = scoutWaypointIndex[scout.SN];
            if (waypoint < 0 || waypoint >= scoutWaypointCount)
                waypoint = scout.SN % scoutWaypointCount;
            targetDR = scoutWaypoints[waypoint][0];
            targetUR = scoutWaypoints[waypoint][1];
            if (BlockDis2(scout.BlockDR, scout.BlockUR, targetDR, targetUR) <= 9)
                waypoint = (waypoint + 1) % scoutWaypointCount;
            targetDR = scoutWaypoints[waypoint][0];
            targetUR = scoutWaypoints[waypoint][1];
        }

        targetDR = max(2, min(MAP_L - 3, targetDR));
        targetUR = max(2, min(MAP_U - 3, targetUR));
        ai->HumanMove(scout.SN,
                      (targetDR + 0.5) * double(BLOCKSIDELENGTH),
                      (targetUR + 0.5) * double(BLOCKSIDELENGTH));
        scoutLastBlock[scout.SN] = make_pair(scout.BlockDR, scout.BlockUR);
        scoutLastOrderFrame[scout.SN] = g_frame;
        scoutFrontierVisitFrame[make_pair(targetDR, targetUR)] = g_frame;
    }
}

static int FindDirectThreatToFarmerSN(const tagFarmer &farmer)
{
    int bestSN = -1;
    int bestDis2 = 1000000000;

    for (const tagArmy &enemyArmy : info.enemy_armies)
    {
        if (enemyArmy.WorkObjectSN != farmer.SN)
            continue;
        const int dis2 = BlockDis2(farmer.BlockDR, farmer.BlockUR,
                                   enemyArmy.BlockDR, enemyArmy.BlockUR);
        if (dis2 < bestDis2)
        {
            bestDis2 = dis2;
            bestSN = enemyArmy.SN;
        }
    }
    for (const tagFarmer &enemyFarmer : info.enemy_farmers)
    {
        if (enemyFarmer.WorkObjectSN != farmer.SN)
            continue;
        const int dis2 = BlockDis2(farmer.BlockDR, farmer.BlockUR,
                                   enemyFarmer.BlockDR, enemyFarmer.BlockUR);
        if (dis2 < bestDis2)
        {
            bestDis2 = dis2;
            bestSN = enemyFarmer.SN;
        }
    }
    return bestSN;
}

static int FindNearbyEnemyForFarmer(const tagFarmer &farmer)
{
    const int aggroRadius = 3;
    int bestSN = -1;
    int bestDis2 = aggroRadius * aggroRadius;

    for (const tagArmy &enemyArmy : info.enemy_armies)
    {
        if (enemyArmy.Blood <= 0)
            continue;
        const int dis2 = BlockDis2(farmer.BlockDR, farmer.BlockUR,
                                   enemyArmy.BlockDR, enemyArmy.BlockUR);
        if (dis2 < bestDis2)
        {
            bestDis2 = dis2;
            bestSN = enemyArmy.SN;
        }
    }
    for (const tagFarmer &enemyFarmer : info.enemy_farmers)
    {
        if (enemyFarmer.Blood <= 0)
            continue;
        const int dis2 = BlockDis2(farmer.BlockDR, farmer.BlockUR,
                                   enemyFarmer.BlockDR, enemyFarmer.BlockUR);
        if (dis2 < bestDis2)
        {
            bestDis2 = dis2;
            bestSN = enemyFarmer.SN;
        }
    }
    for (const tagResource &resource : info.resources)
    {
        if (resource.Type != RESOURCE_LION || resource.Blood <= 0)
            continue;
        const int dis2 = BlockDis2(farmer.BlockDR, farmer.BlockUR,
                                   resource.BlockDR, resource.BlockUR);
        if (dis2 < bestDis2)
        {
            bestDis2 = dis2;
            bestSN = resource.SN;
        }
    }
    return bestSN;
}

static void AssignFarmerSelfDefense(UsrAI *ai)
{
    map<int, int> targetWorkers;
    for (const tagFarmer &farmer : info.farmers)
    {
        if (farmer.Blood <= 0 || farmer.FarmerSort != FARMERTYPE_FARMER)
            continue;

        int targetSN = FindDirectThreatToFarmerSN(farmer);
        if (targetSN == -1)
            targetSN = FindNearbyEnemyForFarmer(farmer);
        if (targetSN == -1)
            continue;

        // 农民只进行近距离自卫；同一目标最多安排四名农民，避免围堵。
        if (targetWorkers[targetSN] >= 4)
            continue;
        targetWorkers[targetSN]++;
        currentTarget[farmer.SN] = targetSN;

        if (farmer.WorkObjectSN != targetSN &&
            g_frame - fieldSelfDefenseLastOrderFrame[farmer.SN] >=
                USR_FIELD_SELF_DEFENSE_ORDER_INTERVAL)
        {
            CancelPendingGatherOrder(farmer.SN);
            ai->HumanAction(farmer.SN, targetSN);
            fieldSelfDefenseLastOrderFrame[farmer.SN] = g_frame;
        }
    }
}

static bool IsKnownLandBlock(int blockDR, int blockUR)
{
    if (!info.theMap || blockDR < 0 || blockUR < 0 ||
        blockDR >= MAP_L || blockUR >= MAP_U)
        return false;
    const int type = (*info.theMap)[blockDR][blockUR].type;
    return type == MAPPATTERN_GRASS || type == MAPPATTERN_SHOAL;
}

static bool IsUnknownBlock(int blockDR, int blockUR)
{
    if (!info.theMap || blockDR < 0 || blockUR < 0 ||
        blockDR >= MAP_L || blockUR >= MAP_U)
        return false;
    return (*info.theMap)[blockDR][blockUR].type == MAPPATTERN_UNKNOWN;
}

static bool IsExplorationFrontierBlock(int blockDR, int blockUR)
{
    if (!IsKnownLandBlock(blockDR, blockUR))
        return false;

    const int offsets[][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (const auto &offset : offsets)
    {
        if (IsUnknownBlock(blockDR + offset[0], blockUR + offset[1]))
            return true;
    }
    return false;
}

static int offensiveLastOrderFrame = USR_INVALID_FRAME;

static void UpdateEnemyBaseDiscovery()
{
    // 侦察骑兵探路后即可发现敌方基地；进攻时机由科技完成度控制。

    for (const tagBuilding &building : info.enemy_buildings)
    {
        // 任意存活敌方建筑都代表敌方基地已被侦察到，不要求必须是市镇中心。
        if (building.Blood > 0)
        {
            enemyBaseDiscovered = true;
            enemyBaseSN = building.SN;
            enemyBaseBlockDR = building.BlockDR;
            enemyBaseBlockUR = building.BlockUR;
            enemyBaseLastSeenFrame = g_frame;
            return;
        }
    }
}

// 进攻目标
// SN：优先敌方市镇中心，基地摧毁后转为攻城武器厂附近的防守兵（护送祭司）。
static int FindOffensiveTargetSN() {
  // 1. 敌方市镇中心（推平基地）。
  for (const tagBuilding &building : info.enemy_buildings) {
    if (building.Blood > 0 && building.Type == BUILDING_CENTER)
      return building.SN;
  }
  // 2. 敌方基地（enemyBaseSN，若仍存活且不是攻城武器厂）。
  for (const tagBuilding &building : info.enemy_buildings) {
    if (building.Blood > 0 && building.SN == enemyBaseSN &&
        building.Type != BUILDING_SIEGE)
      return building.SN;
  }
  // 3. 基地摧毁后，清理攻城武器厂附近的防守兵，护送祭司转换。
  int siegeDR = -1, siegeUR = -1;
  for (const tagBuilding &building : info.enemy_buildings) {
    if (building.Blood > 0 && building.Type == BUILDING_SIEGE) {
      siegeDR = building.BlockDR;
      siegeUR = building.BlockUR;
      break;
    }
  }
  if (siegeDR != -1) {
    int bestSN = -1;
    int bestDis2 = 1000000000;
    for (const tagArmy &enemy : info.enemy_armies) {
      if (enemy.Blood <= 0)
        continue;
      const int dis2 =
          BlockDis2(siegeDR, siegeUR, enemy.BlockDR, enemy.BlockUR);
      if (dis2 < 8 * 8 && dis2 < bestDis2) {
        bestDis2 = dis2;
        bestSN = enemy.SN;
      }
    }
    if (bestSN != -1)
      return bestSN;
  }
  // 4. 其余敌方建筑（跳过攻城武器厂，留给祭司）。
  for (const tagBuilding &building : info.enemy_buildings) {
    if (building.Blood > 0 && building.Type != BUILDING_SIEGE)
      return building.SN;
  }
  return -1;
}

static bool IsOffensiveArmy(const tagArmy &army)
{
    // 侦察骑兵继续承担视野任务，不加入主力攻坚编队；祭司由独立逻辑管理。
    return army.Blood > 0 && army.Sort != AT_PRIEST &&
           army.Sort != AT_SCOUT;
}

static void ManageOffensiveArmy(UsrAI *ai)
{
  // 总攻时机：至少等到 38000 帧（约 25 分钟）之后，给经济、科技与兵力
  // 充分的积累时间；此前只防守，避免以弱击强被敌方大量守军歼灭。
  if (g_frame < 38000)
    return;

  // 升时代前不进攻；升时代后需等关键兵种科技研发完成。
  if (info.civilizationStage == CIVILIZATION_TOOLAGE ||
      researchedTechCount < TOTAL_REQUIRED_TECH)
    return;

  // 兵力不足时不进攻，避免送死。
  const int offensiveArmyCount =
      CountArmyBySort(AT_CLUBMAN) + CountArmyBySort(AT_BOWMAN) +
      CountArmyBySort(AT_BROADSWORDSMAN) + CountArmyBySort(AT_COMPOSITE_BOWMAN) +
      CountArmyBySort(AT_HOPLITE);
  if (offensiveArmyCount < 8)
    return;

  UpdateEnemyBaseDiscovery();
  if (!enemyBaseDiscovered)
    return;

  const int targetSN = FindOffensiveTargetSN();
  const int orderInterval = 60;
  if (g_frame - offensiveLastOrderFrame < orderInterval)
    return;
  offensiveLastOrderFrame = g_frame;

  bool issuedAttack = false;
  for (const tagArmy &army : info.armies) {
    if (!IsOffensiveArmy(army))
      continue;

    if (targetSN != -1) {
      if (GetLockedArmyTarget(army.SN) == targetSN)
        continue;
      ClearArmyTargetLock(army.SN);
      currentTarget[army.SN] = targetSN;
      ai->HumanAction(army.SN, targetSN);
      issuedAttack = true;
    } else if (enemyBaseBlockDR >= 0 && enemyBaseBlockUR >= 0) {
      // 基地暂时离开视野时，先向最后已知位置推进，等待重新发现。
      ClearArmyTargetLock(army.SN);
      ai->HumanMove(army.SN, (enemyBaseBlockDR + 0.5) * double(BLOCKSIDELENGTH),
                    (enemyBaseBlockUR + 0.5) * double(BLOCKSIDELENGTH));
    }
  }
    if (issuedAttack && !offensiveAttackStarted)
    {
        offensiveAttackStarted = true;
        offensiveAttackStartFrame = g_frame;
    }
}

static void AssignFieldSelfDefense(UsrAI *ai)
{
    const tagArmy *priest = FindPriest();
    const int priestThreatSN = priest ? FindThreatToPriestSN(priest->SN) : -1;

    for (const tagArmy &army : info.armies)
    {
        if (army.Sort == AT_PRIEST || army.Sort == AT_SCOUT)
            continue;

        int targetSN = priestThreatSN;
        if (targetSN == -1)
            targetSN = GetLockedArmyTarget(army.SN);

        if (targetSN == -1)
            targetSN = FindDirectThreatToArmySN(army.SN);

        if (targetSN == -1)
            targetSN = FindEnemyArmyInVision(army);

        if (targetSN == -1)
            targetSN = FindNearbyEnemyLionToAttack(army);

        if (targetSN == -1)
            targetSN = FindNearbyEnemyFarmerToAttack(army);

        if (targetSN == -1)
            targetSN = FindAssistThreatNearArmy(army);

        if (targetSN == -1)
            continue;
        currentTarget[army.SN] = targetSN;

        if (army.WorkObjectSN != targetSN &&
            g_frame - fieldSelfDefenseLastOrderFrame[army.SN] >=
                USR_FIELD_SELF_DEFENSE_ORDER_INTERVAL)
        {
            ai->HumanAction(army.SN, targetSN);
            fieldSelfDefenseLastOrderFrame[army.SN] = g_frame;
        }
    }
}

// 接口：寻找箭塔射程内最近的敌方单位。
// 用途：玩家箭塔自动攻击。
static int FindArrowTowerTarget(const tagBuilding &tower)
{
    int targetSN = -1;
    int bestDis = 1000000000;
    const int towerRange = ArrowTowerAttackRangeBlocks();

    for (const tagArmy &enemyArmy : info.enemy_armies)
    {
        int d = BlockDis(tower.BlockDR, tower.BlockUR,
                         enemyArmy.BlockDR, enemyArmy.BlockUR);
        if (d <= towerRange && d < bestDis)
        {
            bestDis = d;
            targetSN = enemyArmy.SN;
        }
    }

    for (const tagFarmer &enemyFarmer : info.enemy_farmers)
    {
        int d = BlockDis(tower.BlockDR, tower.BlockUR,
                         enemyFarmer.BlockDR, enemyFarmer.BlockUR);
        if (d <= towerRange && d < bestDis)
        {
            bestDis = d;
            targetSN = enemyFarmer.SN;
        }
    }

    return targetSN;
}

// 接口：为所有我方箭塔执行自动索敌。
// 用途：当前目标离开射程或不存在时，自动攻击射程内最近敌军/敌农民。
static void AssignArrowTowerTargets(UsrAI *ai)
{
    for (const tagBuilding &building : info.buildings)
    {
        if (building.Type != BUILDING_ARROWTOWER)
            continue;

        int targetDR = 0;
        int targetUR = 0;
        const bool currentTargetInRange =
            building.Project != ACT_NULL &&
            FindEnemyUnitBlockPosition(building.Project, targetDR, targetUR) &&
            BlockDis(building.BlockDR, building.BlockUR, targetDR, targetUR) <= ArrowTowerAttackRangeBlocks();

        if (currentTargetInRange)
            continue;
        if (g_frame - towerLastOrderFrame[building.SN] < USR_TOWER_ORDER_INTERVAL)
            continue;

        int targetSN = FindArrowTowerTarget(building);
        if (targetSN != -1)
        {
            ai->HumanAction(building.SN, targetSN);
            towerLastOrderFrame[building.SN] = g_frame;
        }
    }
}

// 资源富余时派多余农民攻击敌人送死，为军队腾出人口。
// 说明：Core 禁止友军互攻（含自伤，见 Core.cpp 的 addRelation 分支），
// 因此只能通过攻击敌方单位/建筑实现「送死」。
static void SacrificeExcessFarmers(UsrAI *ai) {
  // 结算上一张自毁指令的回执（下单后的下一帧打印），用于确认农民真的死了。
  // 原先的 [SACRIFICE] 是在 HumanAction 之后立刻写的，只表示「指令已入队」，
  // 无法区分「已生效」与「被 Core 拒绝」——实测 4600 帧内触发 5 次而
  // farmers 始终是 18，两种情况都解释得通，必须看回执才能定性。
  // 回执码：0 = 成功；16 = ACTION_INVALID_NULLWORKER；25 = ACTION_INVALID_SN。
  if (sacrificeOrderId != -1) {
    map<int, int>::const_iterator result = info.ins_ret.find(sacrificeOrderId);
    if (result != info.ins_ret.end()) {
      char buf[256];
      snprintf(buf, sizeof(buf),
               "[SACRIFICE-RET] f=%d farmer=%d hpBefore=%d ret=%d retF=%d",
               g_frame, sacrificeFarmerSN, sacrificeHpBefore, result->second,
               g_frame);
      AiDebugLog(buf);
      sacrificeOrderId = -1;
    }
  }

  // 节流：每 300 帧最多送一个，避免农民集体冲出基地。
  static int lastSacrificeFrame = USR_INVALID_FRAME;
  if (lastSacrificeFrame != USR_INVALID_FRAME &&
      g_frame - lastSacrificeFrame < 300)
    return;

  // 仅后期触发：已升时代 + 核心建筑齐备（经济已成型，少几个农民不影响）
  // + 人口已满（军队无法继续生产，需要腾出人口）。
  // 注：不用「资源存量」判断——AI 的资源始终在被消耗，存量永远不高。
  if (info.civilizationStage == CIVILIZATION_TOOLAGE)
    return;
  if (!HasBuilding(BUILDING_STABLE) || !HasBuilding(BUILDING_RANGE))
    return;
  // 后勤未完成前不送死农民：该科技把兵营单位的人口占用从 1 降到 0.5，
  // 相当于凭空多出一倍的兵营容量。没点它就先腾人口，等于用农民的采集力
  // 去换本来靠科技免费就能拿到的容量。
  // logisticsReady 由 ManageEconomyAndProduction 当帧更新，本函数在其之后
  // 调用，所以读到的是当前状态。
  if (!logisticsReady)
    return;
  // 只在人口顶到硬上限 50 之后才腾人口。
  // info.Human_MaxNum = min(房屋数 × 4, 50)，不足 50 说明房屋还没补够，
  // 此时该去补房屋而不是送死农民——否则会在 42/44 这类低上限时就开杀，
  // 白白损失采集力。
  if (info.Human_MaxNum < USR_HUMAN_NUM_CAP)
    return;
  // 未满员时不腾人口（+2 是「再放一个单位都放不下」的余量）。
  if (info.Human_Num + 2 < info.Human_MaxNum)
    return;

  int farmerCount = 0;
  for (const tagFarmer &farmer : info.farmers) {
    if (farmer.Blood > 0 && farmer.FarmerSort == FARMERTYPE_FARMER)
      farmerCount++;
  }
  if (farmerCount <= 5) // 保留 5 个农民维持采集
    return;

  const tagBuilding *center = FindBuildingByType(BUILDING_CENTER, true);
  if (center == nullptr)
    return;

  // 选离基地最近的农民去送死（最不干扰远端采集）。
  int farmerSN = -1;
  int farmerHp = -1;
  int bestDis2 = 1000000000;
  for (const tagFarmer &farmer : info.farmers) {
    if (farmer.Blood <= 0 || farmer.FarmerSort != FARMERTYPE_FARMER)
      continue;
    const int dis2 = BlockDis2(farmer.BlockDR, farmer.BlockUR, center->BlockDR,
                               center->BlockUR);
    if (dis2 < bestDis2) {
      bestDis2 = dis2;
      farmerSN = farmer.SN;
      farmerHp = farmer.Blood;
    }
  }
  if (farmerSN == -1)
    return;

  // 直接自毁：HumanAction(farmerSN, farmerSN) 命中 Core 的自毁分支
  // （handleFarmerAction 的 `if (self == obj)`，Core.cpp:993-1001），
  // 由 deleteSelf 扣掉全部血量并走统一的销毁流程。
  // 旧实现要先去 30 格内找一个敌方单位当目标，敌人不在基地附近时就
  // 完全不会触发；自毁则与敌人位置无关，人口满时必定能腾出人口。
  CancelPendingGatherOrder(farmerSN);
  sacrificeOrderId = ai->HumanAction(farmerSN, farmerSN);
  sacrificeFarmerSN = farmerSN;
  sacrificeHpBefore = farmerHp;
  lastSacrificeFrame = g_frame;
  {
    char buf[256];
    snprintf(buf, sizeof(buf), "[SACRIFICE] f=%d farmer=%d selfDelete=1",
             g_frame, farmerSN);
    AiDebugLog(buf);
  }
}

void UsrAI::processData()
{
    info = getInfo();
    // 祭司/市镇中心丢失时立即记录（用于定位判负原因）。
    {
        static bool hadPriest = false;
        static bool hadCenter = false;
        bool hasPriest = false;
        bool hasCenter = false;
        for (const tagArmy &a : info.armies)
            if (a.Sort == AT_PRIEST && a.Blood > 0)
                hasPriest = true;
        for (const tagBuilding &b : info.buildings)
            if (b.Type == BUILDING_CENTER && b.Blood > 0)
                hasCenter = true;
        char buf[256];
        if (hadPriest && !hasPriest)
        {
            snprintf(buf, sizeof(buf), "[LOST] f=%d PRIEST_LOST", g_frame);
            AiDebugLog(buf);
        }
        if (hadCenter && !hasCenter)
        {
            snprintf(buf, sizeof(buf), "[LOST] f=%d CENTER_LOST", g_frame);
            AiDebugLog(buf);
        }
        hadPriest = hasPriest;
        hadCenter = hasCenter;
    }
    // 定期写入调试日志，便于离线分析 AI 状态（每 2000 帧一次）。
    static int lastAiDebugFrame = 0;
    if (g_frame - lastAiDebugFrame >= 2000) {
      lastAiDebugFrame = g_frame;
      char buf[512];
      int stoneRes = 0;
      for (const tagResource &r : info.resources)
        if (ResourceBucket(r.Type) == 2 && IsGatherableResource(r))
          stoneRes++;
      snprintf(
          buf, sizeof(buf),
          "[AI] f=%d food=%d wood=%d stone=%d gold=%d farmers=%d scout=%d "
          "army=%d bow=%d carcher=%d chariot=%d caret=%d wheel=%d "
          "civ=%d tech=%d tower=%d farm=%d market=%d stable=%d range=%d "
          "camp=%d "
          "stoneRes=%d enemyB=%d enemyA=%d baseKnown=%d",
          g_frame, (int)info.Meat, (int)info.Wood, info.Stone, info.Gold,
          (int)info.farmers.size(), CountArmyBySort(AT_SCOUT),
          CountArmyBySort(AT_CLUBMAN) + CountArmyBySort(AT_BOWMAN) +
              CountArmyBySort(AT_BROADSWORDSMAN) +
              CountArmyBySort(AT_COMPOSITE_BOWMAN) + CountArmyBySort(AT_HOPLITE),
          // 兵种配比验证用：原先 army= 不含战车/战车弓兵，看不出新配比是否生效。
          CountArmyBySort(AT_BOWMAN), CountArmyBySort(AT_CHARIOT_ARCHER),
          CountArmyBySort(AT_CHARIOT),
          // caret：战车弓兵生产指令的最近回执码（0=成功；12=前置未满足/LOCK）。
          // wheel：车轮科技完成标志。两者一起看就能区分「科技没研出来」与「资源不够」。
          chariotArcherRet, (int)wheelTechReady,
          info.civilizationStage, researchedTechCount,
          CountBuilding(BUILDING_ARROWTOWER), CountBuilding(BUILDING_FARM),
          (int)HasBuilding(BUILDING_MARKET), (int)HasBuilding(BUILDING_STABLE),
          (int)HasBuilding(BUILDING_RANGE), (int)HasBuilding(BUILDING_ARMYCAMP),
          stoneRes, (int)info.enemy_buildings.size(),
          (int)info.enemy_armies.size(), (int)enemyBaseDiscovered);
      AiDebugLog(buf);
      int incomplete = 0;
      for (const tagBuilding &b : info.buildings)
        if (b.Blood > 0 && b.Percent < 100)
          incomplete++;
      int priestAlive = 0;
      int centerAlive = 0;
      for (const tagArmy &a : info.armies)
        if (a.Sort == AT_PRIEST && a.Blood > 0)
          priestAlive++;
      for (const tagBuilding &b : info.buildings)
        if (b.Type == BUILDING_CENTER && b.Blood > 0)
          centerAlive++;
      snprintf(
          buf, sizeof(buf),
          "[BUILD] f=%d pop=%.1f/%d incomplete=%d priest=%d center=%d | "
          "granary=%d stable=%d market=%d farm=%d camp=%d range=%d tower=%d",
          g_frame, (double)info.Human_Num, info.Human_MaxNum, incomplete,
          priestAlive, centerAlive, buildFailCodes[BUILDING_GRANARY],
          buildFailCodes[BUILDING_STABLE], buildFailCodes[BUILDING_MARKET],
          buildFailCodes[BUILDING_FARM], buildFailCodes[BUILDING_ARMYCAMP],
          buildFailCodes[BUILDING_RANGE], buildFailCodes[BUILDING_ARROWTOWER]);
      AiDebugLog(buf);
      // 诊断：定位「有建筑、有资源却点不了科技」。
      // 逐类打印第一座该建筑的血量/完成度/当前项目，以及是否存在
      // FindReadyBuildingByType 认可的空闲实例（h=存在 pct=完成度
      // proj=当前项目 free=就绪）。再附上仓库串行研发的游标与最近结果码：
      // ret=-1 表示该次下单被 Core 拒绝（超时未回结果）。
      {
        static const int kResearchTypes[4] = {BUILDING_STOCK, BUILDING_MARKET,
                                              BUILDING_GRANARY,
                                              BUILDING_ARMYCAMP};
        char detail[512];
        int used = 0;
        for (int t = 0; t < 4; t++) {
          const tagBuilding *first = NULL;
          bool free = false;
          for (const tagBuilding &b : info.buildings) {
            if (b.Type != kResearchTypes[t] || b.Blood <= 0)
              continue;
            if (first == NULL)
              first = &b;
            if (b.Percent >= 100 && b.Project == ACT_NULL) {
              free = true;
              break;
            }
          }
          if (used < (int)sizeof(detail) - 64) {
            used += snprintf(
                detail + used, sizeof(detail) - used,
                " %d[h=%d pct=%d proj=%d free=%d]", kResearchTypes[t],
                first != NULL ? 1 : 0, first != NULL ? (int)first->Percent : -1,
                first != NULL ? (int)first->Project : -9999, free ? 1 : 0);
          }
        }
        // 与 ManageEconomyAndProduction 里的 kStockTechFood 保持同一顺序。
        // 步兵护甲的两项已从研发队列移除，这里同步去掉（否则下标会错位）。
        static const int kStockFood[3] = {
            BUILDING_STOCK_UPGRADE_CLOSER_ATTACK_FOOD,
            BUILDING_STOCK_UPGRADE_DEFENSE_ARCHER_FOOD,
            BUILDING_STOCK_UPGRADE_CLOSER_ATTACK_2_FOOD};
        static const int kStockGold[3] = {
            0, 0, BUILDING_STOCK_UPGRADE_CLOSER_ATTACK_2_GOLD};
        snprintf(buf, sizeof(buf),
                 "[TECH] f=%d food=%d gold=%d need=%d/%d%s stockCur=%d "
                 "stockPend=%d ret=%d retF=%d",
                 g_frame, (int)info.Meat, info.Gold,
                 stockTechCursor < 3 ? kStockFood[stockTechCursor] : 0,
                 stockTechCursor < 3 ? kStockGold[stockTechCursor] : 0, detail,
                 stockTechCursor, stockTechOrderId, lastTechRet, lastTechFrame);
        AiDebugLog(buf);
      }
    }
    // 在任何策略读取静态订单状态前处理新对局帧号回退和采集订单结果。
    ProcessPendingGatherOrders();
    CleanDeadOwnerTargetLocks();
    ManagePriest(this);
    // 临时禁用祭司仇恨转移：验证仅靠箭塔与祭司转化能否撑过三波。
    // AssignPriestDecoy(this);
    ManageEconomyAndProduction(this);
    AssignFieldSelfDefense(this);
    // 【暂时注释】总攻：观察纯防守下的表现
    // ManageOffensiveArmy(this);
    // AssignFarmerSelfDefense(this);
    SacrificeExcessFarmers(this);
    // 农民自卫：30000 帧后启动（对应「第三波骚扰之后」，enemyai.cpp:45 的
    // TAT = 21000 已过）。此前农民遇袭一律靠撤离、不还手，避免早期被零星
    // 骚扰牵着走、把采集力从食物/木头上拽开；进入后期农民挨打时则就地反击。
    if (g_frame >= USR_FARMER_SELF_DEFENSE_FRAME)
        AssignFarmerSelfDefense(this);
    DispatchScouts(this);
    AssignArrowTowerTargets(this);
}
