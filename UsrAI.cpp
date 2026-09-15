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
// 调试面板状态输出间隔，避免逐帧刷屏。
;
// 祭司转换或移动指令的最小发送间隔，单位为游戏帧。
static const int USR_PRIEST_ORDER_INTERVAL = 20;
// 祭司危险判定半径，单位为地图格。
static const int USR_PRIEST_DANGER_RADIUS = 9;
// 箭塔建筑候选点相对中心的目标距离，单位为地图格。
static const int USR_ARROWTOWER_BUILD_RADIUS = 18;
// 建筑候选点距离地图边界的最小安全边距，单位为地图格。
static const int USR_ARROWTOWER_BUILD_MIN_MARGIN = 3;
// 表示尚未发生过相关事件的哨兵帧值。
static const int USR_INVALID_FRAME = -1000000000;

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
// 攻击祭司的敌人 SN → 被派去转移其仇恨的诱饵单位 SN（每个威胁各配一个诱饵）。
static map<int, int> priestDecoyByThreat;
// 当前祭司撤退或防守目标的地图格坐标。
static pair<int, int> priestEmergencyTarget = make_pair(-1, -1);
// 当前祭司撤退或防守目标最后一次更新的游戏帧。
static int priestEmergencyTargetFrame = USR_INVALID_FRAME;
// 最近一次检测到祭司危险状态的游戏帧。
static int priestDangerLastFrame = USR_INVALID_FRAME;
// 最近一次触发祭司危险状态的敌方单位 SN。
static int priestDangerTargetSN = -1;
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
// 解锁兵力上限所需研发完成的兵种科技数量（棍棒兵升级、阔剑兵）。
static const int TOTAL_REQUIRED_TECH = 2;
// 谷仓「研发:建造箭塔」是否已完成（完成后才允许建造箭塔）。
// 判断方式：观察到谷仓 Project == BUILDING_GRANARY_ARROWTOWER（研发中），
// 之后 Project 回到空闲即视为研发完成。
static bool arrowTowerTechnologyReady = false;
static bool arrowTowerTechSeenRunning = false;
// 首次尝试发起箭塔研发的帧，用于长时间未启动时兜底放行。
static int arrowTowerTechStartFrame = USR_INVALID_FRAME;
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
        // 检测祭司 6 格内的敌人，派兵主动保护，避免转换时被近身击杀。
        if (dis2 < 6 * 6 && dis2 < bestDis2)
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
    int weight[4] = {15, 7, 0, 0};
    if (info.Meat < 600)
        weight[0] += 5;
    else if (info.civilizationStage != CIVILIZATION_TOOLAGE)
      weight[0] += 3;

    if (info.Wood >= 500) {
      // 木头充足（>=500）时停止伐木，把农民让给食物/黄金/石头。
      weight[1] = 0;
    } else {
      if (info.Wood < 300)
        weight[1] += 8;
      if (!HasBuilding(BUILDING_STABLE))
        weight[1] += 3;
    }

    // 黄金：后勤/阔剑兵需要黄金，缺黄金时补充（采够 100
    // 就停，避免挤占食物农民）。
    if (info.Gold < 100)
      weight[3] += 6;

    // 石头：造箭塔需要石头（每个 150），箭塔未满 8 个时高权重采石。
    if (CountBuilding(BUILDING_ARROWTOWER) < 8)
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
        // 瞪羚需要先击杀（耗时且可能被反击），降低其优先级，优先直接采集的资源。
        const int huntPenalty = resource.Type == RESOURCE_GAZELLE ? 60 : 0;
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

    // 箭塔：围绕市镇中心呈环形（8 个方向，45° 间隔）布置，形成防守圈。
    // 敌方波次以追击祭司/最近的农民为目标，来向不固定，因此均匀覆盖全向。
    if (buildingType == BUILDING_ARROWTOWER)
    {
        // [圈层][方向][xy]：主圈约 9 格，近圈约 7 格，远圈约 12 格。
        static const int TOWER_RINGS[3][8][2] = {
            {{0, -9}, {6, -6}, {9, 0}, {6, 6}, {0, 9}, {-6, 6}, {-9, 0}, {-6, -6}},
            {{0, -7}, {5, -5}, {7, 0}, {5, 5}, {0, 7}, {-5, 5}, {-7, 0}, {-5, -5}},
            {{0, -12}, {8, -8}, {12, 0}, {8, 8}, {0, 12}, {-8, 8}, {-12, 0}, {-8, -8}}};
        // 按已有箭塔数量错开起始方向，保证新塔依次填满不同方向。
        const int startDir = CountBuilding(BUILDING_ARROWTOWER) % 8;
        for (int ring = 0; ring < 3; ring++)
        {
            for (int k = 0; k < 8; k++)
            {
                const int v = (startDir + k) % 8;
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

// 检查单个科技研发订单结果，研发成功则计数。
static void CheckTechOrder(int &orderId)
{
    if (orderId == -1)
        return;
    map<int, int>::const_iterator result = info.ins_ret.find(orderId);
    if (result == info.ins_ret.end())
        return;
    if (result->second == ACTION_SUCCESS)
        researchedTechCount++;
    orderId = -1;
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
}

static bool TryBuildingAction(UsrAI *ai,
                              int buildingType, int action)
{
    const tagBuilding *building = FindReadyBuildingByType(buildingType);
    if (!building)
        return false;
    const int orderId = ai->BuildingAction(building->SN, action);
    return true;
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

static bool TryProduceBowman(UsrAI *ai, bool nearPopulationCap)
{
    if (nearPopulationCap || info.Meat < 40 || info.Wood < 20)
        return false;

    return TryBuildingAction(ai, BUILDING_RANGE,
                             BUILDING_RANGE_CREATE_BOWMAN);
}
static bool TryProduceScout(UsrAI *ai, bool nearPopulationCap)
{
  if (nearPopulationCap || info.Meat < 100)
    return false;

  return TryBuildingAction(ai, BUILDING_STABLE, BUILDING_STABLE_CREATE_SCOUT);
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

  // 每个人种的目标数量；后续可按敌方兵力或时代动态调整。
  int farmerTarget = 22;

  int armyTarget = 0;
  int bowmanTarget = 0;
  int scoutTarget = 0;
  int hopliteTarget = 0;
  if (info.civilizationStage == CIVILIZATION_TOOLAGE) {
    // 升级时代之前只生产农民：士兵会消耗食物并拖慢升时代与经济发展。
    armyTarget = 0;
    bowmanTarget = 0;
    scoutTarget = 0;
  } else {
    armyTarget = 15;
    bowmanTarget = 15;
    scoutTarget = 3;
    hopliteTarget = 8;  // 方阵兵（血120/攻17/近防5），青铜时代主力
  }
  // 敌方阔剑兵近战克制棍棒兵（攻9近防1），转产远程弓兵（阔剑兵远防0）。
  if (CountEnemyBySort(AT_BROADSWORDSMAN) > 0) {
    armyTarget = 0;
    bowmanTarget += 6;
  }
  // 所有关键科技研发完成后，扩充兵力（40 个，足够推平敌方且不耗尽食物）。
  if (researchedTechCount >= TOTAL_REQUIRED_TECH) {
    armyTarget = 20;
    bowmanTarget = 20;
  }
  ProduceIfBelowTarget(ai, nearPopulationCap, farmerCount, farmerTarget,
                       TryProduceFarmer);
  ProduceIfBelowTarget(ai, nearPopulationCap, clubmanCount, armyTarget,
                       TryProduceSoldier);
  ProduceIfBelowTarget(ai, nearPopulationCap, bowmanCount, bowmanTarget,
                       TryProduceBowman);
  ProduceIfBelowTarget(ai, nearPopulationCap, scoutCount, scoutTarget,
                       TryProduceScout);
  ProduceIfBelowTarget(ai, nearPopulationCap, hopliteCount, hopliteTarget,
                       TryProduceHoplite);
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

    const bool nearPopulationCap = info.Human_Num + 1.9 >= info.Human_MaxNum;
    if (nearPopulationCap && info.Wood >= 30 &&
        !HasIncompleteBuilding(BUILDING_HOME))
    {
        TryBuild(ai, BUILDING_HOME);
    }

    // 建造顺序必须满足 Core 的前置链（否则 LOCK 会占用建造通道死循环）：
    // 谷仓 → 兵营 → 市场(需谷仓) → 马厩/靶场(需兵营) → 农场(需市场) →
    // 箭塔(需谷仓研发)
    if (!HasBuilding(BUILDING_MARKET)) {
      if (info.Wood >= 150)
        TryBuild(ai, BUILDING_MARKET);
    }
    // 箭塔防守：环形布置 8 个箭塔（单塔 DPS 2，需约 7 个才能在
    // 方阵兵 9 秒击杀祭司前打死它）。须等箭塔科技研发完成后才建造。
    if (arrowTowerTechnologyReady &&
        CountBuilding(BUILDING_ARROWTOWER) < 8 && info.Stone >= 150) {
      TryBuild(ai, BUILDING_ARROWTOWER);
    }
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
    // 学院：青铜时代且马厩已建成后建造（用于训练方阵兵）。
    if (info.civilizationStage != CIVILIZATION_TOOLAGE &&
        HasBuilding(BUILDING_STABLE)) {
      if (info.Wood >= 180)
        TryBuild(ai, BUILDING_COLLAGE);
    }
    // 农场只在马厩/靶场建成后建：否则木头会被农场（75木）持续消耗，
    // 永远攒不够马厩/靶场（各150木），导致时代无法升级。
    if (HasBuilding(BUILDING_MARKET)) {
      // 农场数量不超过村民数量的三分之一，避免过早扩张。
      const int villagerCount = static_cast<int>(info.farmers.size());
      if (info.Wood >= 75 && CountBuilding(BUILDING_FARM) < villagerCount / 5) {
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
    // 建成市场后优先研发木材加工（伐木效率 +50%），加快木头积累。
    if (HasBuilding(BUILDING_MARKET))
      TryBuildingAction(ai, BUILDING_MARKET, BUILDING_MARKET_WOOD_UPGRADE);
    ResearchTech(ai, clubmanUpgradeOrderId, BUILDING_ARMYCAMP,
                 BUILDING_ARMYCAMP_UPGRADE_CLUBMAN);
    // 驯养动物（农场食物 +75）：4 块以上农场才划算，避免前期浪费食物。
    if (CountBuilding(BUILDING_FARM) >= 4)
        TryBuildingAction(ai, BUILDING_MARKET, BUILDING_MARKET_FARM_UPGRADE);
    // 青铜时代的兵种科技升级：阔剑兵。
    if (info.civilizationStage != CIVILIZATION_TOOLAGE) {
      ResearchTech(ai, broadswordUpgradeOrderId, BUILDING_ARMYCAMP,
                   BUILDING_ARMYCAMP_UPGRADE_BROADSWORD);
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
    const int dx = priest.BlockDR - threat.BlockDR;
    const int dy = priest.BlockUR - threat.BlockUR;
    const int stepDR = dx == 0 ? 0 : (dx > 0 ? 1 : -1);
    const int stepUR = dy == 0 ? 0 : (dy > 0 ? 1 : -1);

    // 沿祭司与威胁的反方向选点；优先走主轴，避免斜向移动重新贴近敌人。
    const bool useDR = abs(dx) >= abs(dy);
    const int offsets[][2] = {
        {12, 0}, {10, 0}, {8, 0}, {6, 0}, {0, 12}, {0, 10}, {0, 8}, {0, 6}};
    const int offsetCount = sizeof(offsets) / sizeof(offsets[0]);
    for (int i = 0; i < offsetCount; ++i)
    {
        int blockDR = priest.BlockDR;
        int blockUR = priest.BlockUR;
        if (useDR)
            blockDR += stepDR * offsets[i][0];
        else
            blockUR += stepUR * offsets[i][1];

        blockDR = max(1, min(MAP_L - 2, blockDR));
        blockUR = max(1, min(MAP_U - 2, blockUR));
        if (IsPriestPointUsable(blockDR, blockUR))
            return make_pair((blockDR + 0.5) * double(BLOCKSIDELENGTH),
                             (blockUR + 0.5) * double(BLOCKSIDELENGTH));
    }

    // 地图边界或地形没有合法反方向格时，保持当前位置，避免向敌人方向乱走。
    return make_pair((priest.BlockDR + 0.5) * double(BLOCKSIDELENGTH),
                     (priest.BlockUR + 0.5) * double(BLOCKSIDELENGTH));
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
  int bestSN = -1;
  int bestPriority = -1;
  double bestRatio = 0.95; // 只治疗血量低于 95% 的单位
  for (const tagArmy &ally : info.armies) {
    if (ally.SN == priest.SN || ally.Blood <= 0 || ally.MaxBlood <= 0)
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
static int EnemyAttackRange(int armySort)
{
    switch (armySort)
    {
    case AT_STONE_THROWER:
        return 10;
    case AT_COMPOSITE_BOWMAN:
    case AT_CHARIOT_ARCHER:
        return 7;
    case AT_BOWMAN:
        return 5;
    default:
        return 2;  // 近战
    }
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
            priestMoveOrderId = -1;
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
    // 转换进行中保持关系，不追加移动或改派目标，避免打断既有转换。
    if (conversionTargetAlive)
        return;

    // 撤退：只对「正在攻击祭司」的敌人反应（WorkObjectSN 指向祭司），
    // 且该敌人已进入自身射程（含 2 格提前量）时才撤；避免被路过或攻击
    // 其他目标的敌人惊动。
    const tagArmy *closeThreat = nullptr;
    int closestDis2 = 1000000000;
    for (const tagArmy &enemy : info.enemy_armies)
    {
        if (enemy.Blood <= 0 || enemy.WorkObjectSN != priest->SN)
            continue;  // 未在攻击祭司

        const int dis2 = BlockDis2(priest->BlockDR, priest->BlockUR,
                                   enemy.BlockDR, enemy.BlockUR);
        const int range = EnemyAttackRange(enemy.Sort) + 2;
        if (dis2 <= range * range && dis2 < closestDis2)
        {
            closeThreat = &enemy;
            closestDis2 = dis2;
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
        // ||
        // g_frame - priestEmergencyTargetFrame >=
        // USR_PRIEST_ORDER_INTERVAL))
        if (priestMoveOrderId == -1 &&
            (target != priestEmergencyTarget))
        {
            priestMoveOrderId = ai->HumanMove(priest->SN,
                                              retreatPoint.first,
                                              retreatPoint.second);
            priestEmergencyTarget = target;
            priestEmergencyTargetFrame = g_frame;
            lastPriestOrderFrame = g_frame;
        }
        return;
    }

    // 安全后不再追加移动，避免移动指令反复中止转换关系。
    priestDangerTargetSN = -1;
    if (priestSafeSinceFrame == USR_INVALID_FRAME)
        priestSafeSinceFrame = g_frame;

    // 祭司攻击距离 12 格（远程施法），Core 会让它在射程边缘施法，无需贴身；
    // 生存由撤退逻辑（被敌人攻击且进入其射程时撤退）与诱饵机制保障。
    const tagArmy *armyTarget = FindPriestConversionTarget(*priest);
    const tagBuilding *buildingTarget = nullptr;
    if (!armyTarget && g_frame >= 30000)
        buildingTarget = FindEnemySiege(*priest);
    const int targetSN = armyTarget ? armyTarget->SN : (buildingTarget ? buildingTarget->SN : -1);

    // 有可转化目标且不在冷却时转化；否则（无目标或冷却中）治疗受伤友军。
    if (targetSN == -1 || priest->ConvertCooldown > 0 ||
        priestMoveOrderId != -1)
    {
      TryPriestHeal(ai, *priest);
      return;
    }
    // 后期依然保留转化技能（不再设置时间截止）。

    // 同一目标已有关系时不重复下达 HumanAction；重复指令会中止原关系。
    if (priest->WorkObjectSN == targetSN)
        return;

    priestMoveOrderId = ai->HumanAction(priest->SN, targetSN);
    priestEmergencyTargetFrame = g_frame;
    lastPriestOrderFrame = g_frame;
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
    for (const tagArmy &enemy : info.enemy_armies)
    {
        if (enemy.Blood > 0 &&
            BlockDis2(blockDR, blockUR, enemy.BlockDR, enemy.BlockUR) <= 36)
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
    const bool useDR = abs(dx) >= abs(dy);
    const int offsets[][2] = {{12, 0}, {10, 0}, {8, 0}, {6, 0},
                              {0, 12}, {0, 10}, {0, 8}, {0, 6}};
    for (const auto &offset : offsets)
    {
        int blockDR = scout.BlockDR;
        int blockUR = scout.BlockUR;
        if (useDR)
            blockDR += stepDR * offset[0];
        else
            blockUR += stepUR * offset[1];
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
            it = scoutLastOrderFrame.erase(it);
        }
        else
            ++it;
    }

    for (const tagArmy &scout : info.armies)
    {
        if (scout.Blood <= 0 || scout.Sort != AT_SCOUT)
            continue;

        const int threatSN = FindScoutThreatSN(scout);
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
          "army=%d "
          "civ=%d tech=%d tower=%d farm=%d market=%d stable=%d range=%d "
          "camp=%d "
          "stoneRes=%d enemyB=%d enemyA=%d baseKnown=%d",
          g_frame, (int)info.Meat, (int)info.Wood, info.Stone, info.Gold,
          (int)info.farmers.size(), CountArmyBySort(AT_SCOUT),
          CountArmyBySort(AT_CLUBMAN) + CountArmyBySort(AT_BOWMAN) +
              CountArmyBySort(AT_BROADSWORDSMAN) +
              CountArmyBySort(AT_COMPOSITE_BOWMAN) + CountArmyBySort(AT_HOPLITE),
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
    }
    // 在任何策略读取静态订单状态前处理新对局帧号回退和采集订单结果。
    ProcessPendingGatherOrders();
    CleanDeadOwnerTargetLocks();
    ManagePriest(this);
    // 临时禁用祭司仇恨转移：验证仅靠箭塔与祭司转化能否撑过三波。
    // AssignPriestDecoy(this);
    ManageEconomyAndProduction(this);
    AssignFieldSelfDefense(this);
    ManageOffensiveArmy(this);
    AssignFarmerSelfDefense(this);
    DispatchScouts(this);
    AssignArrowTowerTargets(this);
}
