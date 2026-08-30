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
static const int USR_FIELD_ASSIST_RADIUS = 8;
// 普通军队主动发现敌军的最大欧氏距离，单位为地图格。
static const int USR_FIELD_ARMY_AGGRO_RADIUS = 7;
// 农民遭遇敌人时触发主动处理的最大欧氏距离，单位为地图格。
static const int USR_FIELD_FARMER_AGGRO_RADIUS = 6;
// 箭塔重新选择攻击目标的最小间隔，单位为游戏帧。
static const int USR_TOWER_ORDER_INTERVAL = 20;
// 经济采集指令的最小发送间隔，单位为游戏帧。
static const int USR_ECONOMY_ORDER_INTERVAL = 80;
// 建造指令的最小发送间隔，单位为游戏帧。
static const int USR_BUILD_ORDER_INTERVAL = 100;
// 建筑研发或升级动作之间的最小发送间隔，单位为游戏帧。
static const int USR_BUILDING_ACTION_INTERVAL = 80;
// 同一生产通道没有收到异步结果时，允许恢复的超时帧数。
static const int USR_PRODUCTION_ORDER_TIMEOUT = 300;
// 调试面板状态输出间隔，避免逐帧刷屏。
static const int USR_DEBUG_TEXT_INTERVAL = 200;
// 祭司转换或移动指令的最小发送间隔，单位为游戏帧。
static const int USR_PRIEST_ORDER_INTERVAL = 20;
// 祭司危险判定半径，单位为地图格。
static const int USR_PRIEST_DANGER_RADIUS = 9;
// 祭司安全距离阈值，单位为地图格。
static const int USR_PRIEST_SAFE_RADIUS = 6;
// 祭司确认安全后等待的帧数。
static const int USR_PRIEST_SAFE_FRAMES = 120;
// 祭司开始进入防守准备状态的游戏帧。
static const int USR_PRIEST_PREPARE_FRAME = 5200;
// 祭司允许推进并尝试转换的最早游戏帧。
static const int USR_PRIEST_ADVANCE_FRAME = 21500;
// 兼容旧版祭司安全点的地图横向坐标，当前仅作为保留配置。
static const int USR_PRIEST_SAFE_BLOCK_DR = 2;
// 兼容旧版祭司安全点的地图纵向坐标，当前仅作为保留配置。
static const int USR_PRIEST_SAFE_BLOCK_UR = 2;
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
static bool scoutMissionStarted = false;
static bool enemyBaseDiscovered = false;
static set<int> farmerScouters;
static map<int, pair<int, int>> farmerScoutTarget;
static map<int, pair<int, int>> farmerScoutLastBlock;
static map<int, int> farmerScoutLastOrderFrame;
static map<int, int> farmerScoutStuckCount;
static map<int, pair<int, int>> farmerScoutEmergencyTarget;
static map<int, int> farmerScoutEmergencyLastOrderFrame;
// 上次提交经济采集指令的游戏帧。
static int lastEconomyOrderFrame = USR_INVALID_FRAME;
// 上次提交建造指令的游戏帧。
static int lastBuildOrderFrame = USR_INVALID_FRAME;
// 上次为未完成建筑恢复建造的游戏帧。
static int lastConstructionRecoveryFrame = USR_INVALID_FRAME;

static int FindDirectThreatToFarmerSN(const tagFarmer &farmer);
static int FindNearbyEnemyForFarmer(const tagFarmer &farmer);
static bool IsKnownLandBlock(int blockDR, int blockUR);
static bool IsExplorationFrontierBlock(int blockDR, int blockUR);
static bool FindBestScoutFrontier(const tagArmy &scout, int &targetDR, int &targetUR);
static bool FindBestFarmerScoutFrontier(const tagFarmer &farmer, int &targetDR,
                                        int &targetUR);
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
// 当前普通建造指令对应的建筑类型。
static int buildOrderType = -1;
static int buildFarmerSN = -1;
// 棍棒兵生产指令的异步指令 ID，-1 表示没有等待中的指令。
static int clubmanOrderId = -1;
// 农民生产指令的异步指令 ID，-1 表示没有等待中的指令。
static int farmerOrderId = -1;
// 农民生产指令提交时的游戏帧。
static int farmerOrderFrame = USR_INVALID_FRAME;
// 士兵生产指令的异步指令 ID，-1 表示没有等待中的指令。
static int soldierOrderId = -1;
// 士兵生产指令提交时的游戏帧。
static int soldierOrderFrame = USR_INVALID_FRAME;
// 靶场生产指令的异步指令 ID，-1 表示没有等待中的指令。
static int rangeOrderId = -1;
// 靶场生产指令提交时的游戏帧。
static int rangeOrderFrame = USR_INVALID_FRAME;
// 马厩生产指令的异步指令 ID，-1 表示没有等待中的指令。
static int stableOrderId = -1;
// 马厩生产指令提交时的游戏帧。
static int stableOrderFrame = USR_INVALID_FRAME;
// 最近一次非生产建筑动作的异步指令 ID。
static int technologyOrderId = -1;
// 最近一次非生产建筑动作提交时的游戏帧。
static int technologyOrderFrame = USR_INVALID_FRAME;
// 最近一次非生产建筑动作的动作枚举，用于异步成功后推进科技里程碑。
static int technologyPendingAction = -1;
// 已成功完成谷仓箭塔研发；当前接口不暴露科技树，按成功返回值缓存。
static bool arrowTowerTechnologyReady = false;
// 最近一次兵营建造指令的返回结果。
static int armyCampResult = ACTION_SUCCESS;
// 最近一次棍棒兵生产指令的返回结果。
static int clubmanResult = ACTION_SUCCESS;
// 兵营建造指令返回结果对应的游戏帧。
static int armyCampResultFrame = USR_INVALID_FRAME;
// 棍棒兵生产指令返回结果对应的游戏帧。
static int clubmanResultFrame = USR_INVALID_FRAME;
// 策略诊断快照记录的游戏帧。
static int strategyDiagnosticFrame = USR_INVALID_FRAME;
// 策略诊断快照中的棍棒兵数量。
static int strategyDiagnosticClubmen = 0;
// 策略诊断快照中是否存在兵营，1 表示存在，0 表示不存在。
static int strategyDiagnosticHasCamp = 0;
// 策略诊断快照中的木材数量。
static int strategyDiagnosticWood = 0;
// 策略诊断快照中的食物数量。
static int strategyDiagnosticMeat = 0;
// 策略诊断快照中的石头数量。
static int strategyDiagnosticStone = 0;
// 策略诊断快照中的黄金数量。
static int strategyDiagnosticGold = 0;

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
static void AddUnique(vector<int> &values, int value)
{
    if (!ContainsInt(values, value))
        values.push_back(value);
}

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
static void SelectWaveUnitsBySort(
    vector<int> &dst,
    int unitSort,
    int needCount,
    const vector<int> &alreadyUsed)
{
    pair<int, int> center = GetHarassCenterBlock();
    vector<pair<int, int>> candidates;

    for (const tagArmy &army : info.armies)
    {
        if (ContainsInt(dst, army.SN))
            continue;
        if (ContainsInt(alreadyUsed, army.SN))
            continue;
        if (army.Sort != unitSort)
            continue;

        int d = BlockDis2(army.BlockDR, army.BlockUR, center.first, center.second);
        candidates.push_back(make_pair(army.SN, d));
    }

    sort(candidates.begin(), candidates.end(),
         [](const pair<int, int> &lhs, const pair<int, int> &rhs)
         {
             return lhs.second < rhs.second;
         });

    for (int i = 0; i < static_cast<int>(candidates.size()) && needCount > 0; i++)
    {
        int sn = candidates[i].first;
        const tagArmy *army = FindMyArmyBySN(sn);
        if (!army)
            continue;

        dst.push_back(sn);
        harassHome[sn] = make_pair(army->DR, army->UR);
        needCount--;
    }
}

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

static bool HasCompletedBuilding(int type)
{
    for (const tagBuilding &building : info.buildings)
    {
        if (building.Type == type && building.Percent >= 100)
            return true;
    }
    return false;
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

    int bestSN = -1;
    int bestDis2 = 1000000000;
    for (const tagArmy &enemy : info.enemy_armies)
    {
        if (enemy.Blood <= 0 || enemy.WorkObjectSN != priestSN)
            continue;
        int dis2 = BlockDis2(priest->BlockDR, priest->BlockUR,
                             enemy.BlockDR, enemy.BlockUR);
        if (dis2 < bestDis2)
        {
            bestDis2 = dis2;
            bestSN = enemy.SN;
        }
    }
    return bestSN;
}

static const tagBuilding *FindEnemySiege()
{
    for (const tagBuilding &building : info.enemy_buildings)
    {
        if (building.Type == BUILDING_SIEGE && building.Blood > 0)
            return &building;
    }
    return nullptr;
}

static const tagArmy *FindNearestEnemyArmy(int blockDR, int blockUR)
{
    const tagArmy *best = nullptr;
    int bestDis2 = 1000000000;
    for (const tagArmy &enemy : info.enemy_armies)
    {
        if (enemy.Blood <= 0)
            continue;
        int dis2 = BlockDis2(blockDR, blockUR, enemy.BlockDR, enemy.BlockUR);
        if (dis2 < bestDis2)
        {
            bestDis2 = dis2;
            best = &enemy;
        }
    }
    return best;
}

static bool HasVisibleWaveThreat()
{
    const tagBuilding *center = FindCenter();
    const tagArmy *priest = FindPriest();
    for (const tagArmy &enemy : info.enemy_armies)
    {
        if (enemy.Blood <= 0)
            continue;
        if (priest && enemy.WorkObjectSN == priest->SN)
            return true;
        if (center && BlockDis2(center->BlockDR, center->BlockUR,
                                enemy.BlockDR, enemy.BlockUR) <= 20 * 20)
        {
            return true;
        }
    }
    return false;
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

static int ResourcePriority(int resourceType)
{
    if (info.Meat < 550)
    {
        if (resourceType == RESOURCE_BUSH || resourceType == RESOURCE_GAZELLE ||
            resourceType == RESOURCE_ELEPHANT)
            return 0;
    }
    if (info.Wood < 350 && resourceType == RESOURCE_TREE)
        return 1;
    if (info.Stone < 180 && resourceType == RESOURCE_STONE)
        return 2;
    if (info.Gold < 200 && resourceType == RESOURCE_GOLD)
        return 3;
    if (resourceType == RESOURCE_BUSH || resourceType == RESOURCE_GAZELLE ||
        resourceType == RESOURCE_ELEPHANT)
        return 4;
    if (resourceType == RESOURCE_TREE)
        return 5;
    if (resourceType == RESOURCE_STONE)
        return 6;
    if (resourceType == RESOURCE_GOLD)
        return 7;
    return 100;
}

static int ResourceBucket(int resourceType)
{
    if (resourceType == RESOURCE_BUSH || resourceType == RESOURCE_GAZELLE ||
        resourceType == RESOURCE_ELEPHANT)
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

static bool HasGatherableResourceType(int bucket)
{
    for (const tagResource &resource : info.resources)
    {
        if (ResourceBucket(resource.Type) == bucket &&
            IsGatherableResource(resource))
            return true;
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
    }

    if (farmerCount == 0)
        return;

    // 前期优先保障食物，避免生产和侦察计划因食物短缺停滞。
    int weight[4] = {8, 2, 0, 0};
    if (info.Meat < 600)
        weight[0] += 5;
    else if (info.Meat < 1000)
        weight[0] += 3;

    const bool needWoodBuilding =
        HasIncompleteBuilding(BUILDING_HOME) ||
        HasIncompleteBuilding(BUILDING_ARMYCAMP) ||
        HasIncompleteBuilding(BUILDING_RANGE) ||
        HasIncompleteBuilding(BUILDING_STABLE);
    if (info.Wood < 250)
        weight[1] += 4;
    else if (info.Wood < 500)
        weight[1] += 2;
    if (needWoodBuilding)
        weight[1] += 4;

    const bool needStone = HasIncompleteBuilding(BUILDING_ARROWTOWER);
    if (needStone && info.Stone < 300)
        weight[2] += 5;

    // 工具时代升级、兵种升级和高级兵生产由黄金需求拉动；没有需求时保持零配额。
    // const bool needGold = info.civilizationStage == CIVILIZATION_TOOLAGE &&
    //                       (info.Gold < 250 || HasBuilding(BUILDING_MARKET));
    // if (needGold)
    //     weight[3] += 3;

    const bool nearPopulationCap = info.Human_Num + 1 >= info.Human_MaxNum;
    if (nearPopulationCap || HasIncompleteBuilding(BUILDING_HOME))
        weight[1] += 3;

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

    for (const tagResource &resource : info.resources)
    {
        const int bucket = ResourceBucket(resource.Type);
        if (bucket != desiredBucket || !IsGatherableResource(resource))
            continue;

        if (IsFarmerClusterCrowded(resource.BlockDR, resource.BlockUR,
                                   farmer.SN))
            continue;

        const int workers = resourceWorkers[resource.SN];
        const int distance = BlockDis2(farmer.BlockDR, farmer.BlockUR,
                                       resource.BlockDR, resource.BlockUR);
        const int softCapacity = bucket == 0 ? 4 : 3;
        const int crowdPenalty = workers >= softCapacity
                                     ? (workers - softCapacity + 1) * 80
                                     : workers * 12;
        const int remainingPenalty = resource.Cnt > 0 && resource.Cnt < 100
                                         ? 100
                                         : 0;
        const int score = distance + crowdPenalty + remainingPenalty -
                          (current[bucket] > 0 ? 0 : 2);
        if (score < bestScore)
        {
            bestScore = score;
            bestSN = resource.SN;
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

static pair<int, int> GetBuildCandidate(int buildingType)
{
    const tagBuilding *center = FindCenter();
    if (!center)
        return make_pair(-1, -1);

    static const int OFFSETS[][2] = {
        {6, 0}, {0, 6}, {-6, 0}, {0, -6},
        {7, 4}, {4, 7}, {-7, 4}, {-4, 7},
        {7, -4}, {4, -7}, {-7, -4}, {-4, -7},
        {10, 0}, {0, 10}, {-10, 0}, {0, -10},
        {13, 7}, {7, 13}, {-13, 7}, {-7, 13}
    };
    const int count = static_cast<int>(sizeof(OFFSETS) / sizeof(OFFSETS[0]));
    const int radiusAddition = buildingType == BUILDING_ARROWTOWER
                                   ? USR_ARROWTOWER_BUILD_RADIUS - 10
                                   : 0;
    for (int step = 0; step < count; step++)
    {
        int index = (buildCandidateIndex + step) % count;
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

static bool TryAssignIdleFarmer(UsrAI *ai)
{
    if (g_frame - lastEconomyOrderFrame < USR_ECONOMY_ORDER_INTERVAL)
        return false;

    static int target[4] = {0, 0, 0, 0};
    static int assigned[4] = {0, 0, 0, 0};
    static int quotaFrame = -1000000;
    if (g_frame - quotaFrame >= 240)
    {
        CalculateFarmerTargets(target, assigned);
        quotaFrame = g_frame;
    }

    for (const tagFarmer &farmer : info.farmers)
    {
        if (farmer.FarmerSort != FARMERTYPE_FARMER || farmer.Blood <= 0 ||
            farmer.NowState != HUMAN_STATE_IDLE)
            continue;
        if (IsFarmerBuilding(farmer) || farmer.SN == buildFarmerSN)
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

    ai->HumanAction(bestFarmerSN, target->SN);
    farmerLastOrderFrame[bestFarmerSN] = g_frame;
    lastConstructionRecoveryFrame = g_frame;
    buildFarmerSN = bestFarmerSN;
    return true;
}

static bool TryBuild(UsrAI *ai, int buildingType)
{
    if (buildOrderId != -1)
    {
        map<int, int>::const_iterator result = info.ins_ret.find(buildOrderId);
        if (result == info.ins_ret.end())
            return false;
        if (result->second == ACTION_INVALID_POSITION_NOT_FIT ||
            result->second == ACTION_INVALID_HUMANBUILD_OVERLAP ||
            result->second == ACTION_INVALID_HUMANBUILD_DIFFERENTHIGH ||
            result->second == ACTION_INVALID_HUMANBUILD_OVERBORDER ||
            result->second == ACTION_INVALID_HUMANBUILD_UNEXPLORE)
            buildCandidateIndex++;
        buildOrderId = -1;
        buildOrderType = -1;
        buildFarmerSN = -1;
    }
    if (g_frame - lastBuildOrderFrame < USR_BUILD_ORDER_INTERVAL)
        return false;
    int farmerSN = FindBuilderFarmerSN();
    if (farmerSN == -1)
        return false;
    pair<int, int> position = GetBuildCandidate(buildingType);
    if (position.first == -1)
        return false;
    buildOrderId = ai->HumanBuild(farmerSN, buildingType, position.first, position.second);
    buildOrderType = buildingType;
    buildFarmerSN = farmerSN;
    if (buildingType == BUILDING_ARMYCAMP)
        armyCampOrderId = buildOrderId;
    lastBuildOrderFrame = g_frame;
    return true;
}

static bool TryBuildingAction(UsrAI *ai,
                              int buildingType, int action)
{
    int *pendingOrderId = nullptr;
    int *pendingOrderFrame = nullptr;
    if (action == BUILDING_CENTER_CREATEFARMER)
    {
        pendingOrderId = &farmerOrderId;
        pendingOrderFrame = &farmerOrderFrame;
    }
    else if (buildingType == BUILDING_ARMYCAMP &&
             (action == BUILDING_ARMYCAMP_CREATE_CLUBMAN ||
              action == BUILDING_ARMYCAMP_CREATE_SLINGER ||
              action == BUILDING_ARMYCAMP_CREATE_BROADSWORD))
    {
        pendingOrderId = &soldierOrderId;
        pendingOrderFrame = &soldierOrderFrame;
    }
    else if (buildingType == BUILDING_RANGE &&
             (action == BUILDING_RANGE_CREATE_BOWMAN ||
              action == BUILDING_RANGE_CREATE_CHARIOT_ARCHER ||
              action == BUILDING_RANGE_CREATE_COMPOSITE_BOWMAN))
    {
        pendingOrderId = &rangeOrderId;
        pendingOrderFrame = &rangeOrderFrame;
    }
    else if (buildingType == BUILDING_STABLE &&
             (action == BUILDING_STABLE_CREATE_SCOUT ||
              action == BUILDING_STABLE_CREATE_CHARIOT ||
              action == BUILDING_STABLE_CREATE_CAVALRY))
    {
        pendingOrderId = &stableOrderId;
        pendingOrderFrame = &stableOrderFrame;
    }
    else
    {
        pendingOrderId = &technologyOrderId;
        pendingOrderFrame = &technologyOrderFrame;
    }

    if (*pendingOrderId != -1)
    {
        map<int, int>::const_iterator result = info.ins_ret.find(*pendingOrderId);
        if (result == info.ins_ret.end() &&
            g_frame - *pendingOrderFrame < USR_PRODUCTION_ORDER_TIMEOUT)
            return false;

        if (result != info.ins_ret.end() &&
            pendingOrderId == &technologyOrderId &&
            technologyPendingAction == BUILDING_GRANARY_ARROWTOWER &&
            result->second == ACTION_SUCCESS)
        {
            arrowTowerTechnologyReady = true;
        }
        *pendingOrderId = -1;
        if (pendingOrderId == &technologyOrderId)
            technologyPendingAction = -1;
    }

    const bool productionAction = pendingOrderId != &technologyOrderId;
    if (!productionAction &&
        g_frame - lastBuildingActionFrame < USR_BUILDING_ACTION_INTERVAL)
        return false;

    const tagBuilding *building = FindReadyBuildingByType(buildingType);
    if (!building)
        return false;

    const int orderId = ai->BuildingAction(building->SN, action);
    *pendingOrderId = orderId;
    *pendingOrderFrame = g_frame;
    if (pendingOrderId == &technologyOrderId)
        technologyPendingAction = action;
    if (productionAction)
        lastProductionActionFrame = g_frame;
    else
        lastBuildingActionFrame = g_frame;

    if (action == BUILDING_ARMYCAMP_CREATE_CLUBMAN)
        clubmanOrderId = orderId;
    return true;
}

static bool TryProduceFarmer(UsrAI *ai,
                             bool nearPopulationCap)
{ //     static_cast<int>(info.farmers.size()) >= 14
    if (nearPopulationCap || info.Meat < 50)
        return false;

    return TryBuildingAction(ai, BUILDING_CENTER,
                             BUILDING_CENTER_CREATEFARMER);
}

static bool TryProduceSoldier(UsrAI *ai,
                              bool nearPopulationCap)
{
    const int targetSoldiers = g_frame < 13000 ? 6 : 10;
    // CountArmyBySort(AT_CLUBMAN) >= targetSoldiers
    if (nearPopulationCap || info.Meat < 50)
        return false;
    return TryBuildingAction(ai, BUILDING_ARMYCAMP,
                             BUILDING_ARMYCAMP_CREATE_CLUBMAN);
}

static bool TryProduceBowman(UsrAI *ai, bool nearPopulationCap)
{ // CountArmyBySort(AT_BOWMAN) < 5 &&
    if (!nearPopulationCap &&
        info.Meat >= 40 && info.Wood >= 20)
    {
        TryBuildingAction(ai, BUILDING_RANGE,
                          BUILDING_RANGE_CREATE_BOWMAN);
    }
}
static bool TryProduceScout(UsrAI *ai, bool nearPopulationCap)
{ // CountArmyBySort(AT_SCOUT) < 3 &&
    if (!nearPopulationCap &&
        info.Meat >= 100)
    {
        TryBuildingAction(ai, BUILDING_STABLE,
                          BUILDING_STABLE_CREATE_SCOUT);
    }
}
static int CountEnemyArmy()
{
    int count = 0;
    for (const tagArmy &army : info.enemy_armies)
    {
        if (army.Blood > 0)
            count++;
    }
    return count;
}

static int ProductionPendingCount(int orderId)
{
    return orderId == -1 ? 0 : 1;
}

static int ClampProductionWeight(int weight)
{
    return max(0, min(100, weight));
}

static bool IsProductionSlotSelected(int weight, int offset)
{
    if (weight <= 0)
        return false;
    if (weight >= 100)
        return true;

    return (g_frame + offset) % 100 < weight;
}

static int CalculateProductionWeight(int currentCount, int pendingCount,
                                     int targetCount, int baseWeight,
                                     int enemyPressure)
{
    const int projectedCount = currentCount + pendingCount;
    const int deficit = max(0, targetCount - projectedCount);
    return ClampProductionWeight(baseWeight + deficit * 12 +
                                 enemyPressure * 5);
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

static void ManageWeightedProduction(UsrAI *ai, bool nearPopulationCap)
{
    const int enemyCount = CountEnemyArmy();
    const int farmerCount = static_cast<int>(info.farmers.size());
    const int clubmanCount = CountArmyBySort(AT_CLUBMAN);
    const int bowmanCount = CountArmyBySort(AT_BOWMAN);
    const int scoutCount = CountArmyBySort(AT_SCOUT);

    // 经济目标随敌方可见兵力上升，避免军队扩张时农民数量停滞。
    const int farmerTarget = 10 + min(6, enemyCount / 3);
    const int armyTarget = 6 + min(10, enemyCount);
    const int bowmanTarget = max(3, enemyCount / 2);
    const int scoutTarget = 3;

    const int farmerWeight = CalculateProductionWeight(
        farmerCount, ProductionPendingCount(farmerOrderId), farmerTarget,
        farmerCount < 8 ? 35 : 8, enemyCount / 4);
    const int soldierWeight = CalculateProductionWeight(
        clubmanCount, ProductionPendingCount(soldierOrderId), armyTarget,
        18, enemyCount);
    const int bowmanWeight = CalculateProductionWeight(
        bowmanCount, ProductionPendingCount(rangeOrderId), bowmanTarget,
        12, enemyCount / 2);
    const int scoutWeight = CalculateProductionWeight(
        scoutCount, ProductionPendingCount(stableOrderId), scoutTarget,
        g_frame >= 26000 ? 10 : 4, enemyCount / 3);

    if (HasProductionCapacity(BUILDING_CENTER, farmerOrderId,
                              nearPopulationCap) &&
        info.Meat >= 50 && IsProductionSlotSelected(farmerWeight, 0))
    {
        TryProduceFarmer(ai, nearPopulationCap);
    }

    if (HasProductionCapacity(BUILDING_ARMYCAMP, soldierOrderId,
                              nearPopulationCap) &&
        info.Meat >= 50 && IsProductionSlotSelected(soldierWeight, 25))
    {
        TryProduceSoldier(ai, nearPopulationCap);
    }

    if (HasProductionCapacity(BUILDING_RANGE, rangeOrderId,
                              nearPopulationCap) &&
        info.Meat >= 40 && info.Wood >= 20 &&
        IsProductionSlotSelected(bowmanWeight, 50))
    {
        TryProduceBowman(ai, nearPopulationCap);
    }

    if (HasProductionCapacity(BUILDING_STABLE, stableOrderId,
                              nearPopulationCap) &&
        scoutCount + ProductionPendingCount(stableOrderId) < scoutTarget &&
        info.Meat >= 100 && IsProductionSlotSelected(scoutWeight, 75))
    {
        TryProduceScout(ai, nearPopulationCap);
    }
}
// 接口：推进最短经济、时代与军队生产链。
// 用途：所有决策均基于可见状态；失败后冷却重试，不依赖作弊资源。
static void ManageEconomyAndProduction(UsrAI *ai)
{
    if (armyCampOrderId != -1)
    {
        map<int, int>::const_iterator result = info.ins_ret.find(armyCampOrderId);
        if (result != info.ins_ret.end())
        {
            armyCampResult = result->second;
            armyCampResultFrame = g_frame;
            armyCampOrderId = -1;
        }
    }
    if (clubmanOrderId != -1)
    {
        map<int, int>::const_iterator result = info.ins_ret.find(clubmanOrderId);
        if (result != info.ins_ret.end())
        {
            clubmanResult = result->second;
            clubmanResultFrame = g_frame;
            clubmanOrderId = -1;
        }
    }

    if (g_frame >= 5000 && strategyDiagnosticFrame == USR_INVALID_FRAME)
    {
        strategyDiagnosticFrame = g_frame;
        strategyDiagnosticClubmen = CountArmyBySort(AT_CLUBMAN);
        strategyDiagnosticHasCamp = HasBuilding(BUILDING_ARMYCAMP) ? 1 : 0;
        strategyDiagnosticWood = info.Wood;
        strategyDiagnosticMeat = info.Meat;
        strategyDiagnosticStone = info.Stone;
        strategyDiagnosticGold = info.Gold;
    }

    if (buildOrderId != -1)
    {
        map<int, int>::const_iterator result = info.ins_ret.find(buildOrderId);
        if (result != info.ins_ret.end() ||
            g_frame - lastBuildOrderFrame >= 300)
        {
            if (result != info.ins_ret.end() &&
                result->second == ACTION_INVALID_POSITION_NOT_FIT)
                buildCandidateIndex++;
            buildOrderId = -1;
            buildOrderType = -1;
            buildFarmerSN = -1;
        }
    }

    // 建筑可能因农民被攻击、碰撞或其他关系覆盖而中断；优先恢复已有工地，
    // 防止未完成建筑长期占位却无人继续建造。
    TryResumeIncompleteBuilding(ai);

    const bool nearPopulationCap = info.Human_Num + 1.9 >= info.Human_MaxNum;
    if (nearPopulationCap && info.Wood >= 30 &&
        !HasIncompleteBuilding(BUILDING_HOME))
    {
        TryBuild(ai, BUILDING_HOME);
    }

    if (!HasBuilding(BUILDING_ARMYCAMP) && info.Wood >= 125)
    {
        TryBuild(ai, BUILDING_ARMYCAMP);
    }
    if (!HasBuilding(BUILDING_RANGE) && info.Wood >= 150)
    {
        TryBuild(ai, BUILDING_RANGE);
    }
    if (!HasBuilding(BUILDING_STABLE) && info.Wood >= 150)
    {
        TryBuild(ai, BUILDING_STABLE);
    }
    // if (info.civilizationStage == CIVILIZATION_TOOLAGE && info.Meat >= 800 &&
    //     HasCompletedBuilding(info, BUILDING_RANGE) &&
    //     HasCompletedBuilding(info, BUILDING_STABLE))
    // {
    //     TryBuildingAction(ai, info, BUILDING_CENTER,
    //                       BUILDING_CENTER_UPGRADE);
    // }

    // 生产可以和农民建造并行；两类命令只在各自主体上等待返回。
    ManageWeightedProduction(ai, nearPopulationCap);
    TryAssignIdleFarmer(ai);
}

static pair<double, double> GetPriestRetreatPoint(
    const tagArmy &priest,
    const tagArmy &threat)
{
    const tagBuilding *center = FindCenter();
    int dx = priest.BlockDR - threat.BlockDR;
    int dy = priest.BlockUR - threat.BlockUR;
    if (dx == 0 && dy == 0)
        dx = 1;
    int blockDR = priest.BlockDR + (dx > 0 ? USR_PRIEST_SAFE_RADIUS : -USR_PRIEST_SAFE_RADIUS);
    int blockUR = priest.BlockUR + (dy > 0 ? USR_PRIEST_SAFE_RADIUS : -USR_PRIEST_SAFE_RADIUS);
    if (center)
    {
        blockDR = (blockDR + center->BlockDR * 2) / 3;
        blockUR = (blockUR + center->BlockUR * 2) / 3;
    }
    blockDR = max(1, min(MAP_L - 2, blockDR));
    blockUR = max(1, min(MAP_U - 2, blockUR));
    return make_pair((blockDR + 0.5) * double(BLOCKSIDELENGTH),
                     (blockUR + 0.5) * double(BLOCKSIDELENGTH));
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

static void ManagePriest(UsrAI *ai)
{
    const tagArmy *priest = FindPriest();

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
    if (conversionTargetAlive)
        return;

    // 只有严格小于两格的敌方单位才会触发撤退。
    const tagArmy *closeThreat = nullptr;
    int closestDis2 = 1000000000;
    for (const tagArmy &enemy : info.enemy_armies)
    {
        if (enemy.Blood <= 0)
            continue;

        const int dis2 = BlockDis2(priest->BlockDR, priest->BlockUR,
                                   enemy.BlockDR, enemy.BlockUR);
        if (dis2 < 2 * 2 && dis2 < closestDis2)
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
        if (priestMoveOrderId == -1 &&
            (target != priestEmergencyTarget ||
             g_frame - priestEmergencyTargetFrame >=
                 USR_PRIEST_ORDER_INTERVAL))
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
    if (priest->ConvertCooldown > 0 || priestMoveOrderId != -1 ||
        g_frame - lastPriestOrderFrame < USR_PRIEST_ORDER_INTERVAL)
        return;

    const tagArmy *armyTarget = FindPriestConversionTarget(*priest);
    const tagBuilding *buildingTarget = nullptr;
    if (!armyTarget)
        buildingTarget = FindEnemySiege();

    const int targetSN = armyTarget ? armyTarget->SN : (buildingTarget ? buildingTarget->SN : -1);
    if (targetSN == -1)
        return;

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
    const int scoutOrderInterval = 180;
    const int scoutEmergencyOrderInterval = 20;
    const int scoutSafeRadius = 6;
    const int scoutWaypointCount = 8;
    const int scoutMargin = 10;
    static const int scoutWaypoints[][2] = {
        {scoutMargin, scoutMargin},
        {MAP_L / 2, scoutMargin},
        {MAP_L - scoutMargin - 1, scoutMargin},
        {MAP_L - scoutMargin - 1, MAP_U / 2},
        {MAP_L - scoutMargin - 1, MAP_U - scoutMargin - 1},
        {MAP_L / 2, MAP_U - scoutMargin - 1},
        {scoutMargin, MAP_U - scoutMargin - 1},
        {scoutMargin, MAP_U / 2}};

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

static int enemyBaseSN = -1;
static int enemyBaseLastSeenFrame = USR_INVALID_FRAME;
static int enemyBaseBlockDR = -1;
static int enemyBaseBlockUR = -1;
static int offensiveLastOrderFrame = USR_INVALID_FRAME;

static void UpdateEnemyBaseDiscovery()
{
    // 30000 帧前即使已看到建筑，也不能触发主力总攻。
    if (g_frame <= 30000)
        return;

    scoutMissionStarted = true;
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

static const tagBuilding *FindOffensiveTarget()
{
    for (const tagBuilding &building : info.enemy_buildings)
    {
        if (building.Blood > 0 && building.SN == enemyBaseSN)
            return &building;
    }
    for (const tagBuilding &building : info.enemy_buildings)
    {
        if (building.Blood > 0 && building.Type == BUILDING_CENTER)
            return &building;
    }
    for (const tagBuilding &building : info.enemy_buildings)
    {
        if (building.Blood > 0)
            return &building;
    }
    return nullptr;
}

static bool IsOffensiveArmy(const tagArmy &army)
{
    // 侦察骑兵继续承担视野任务，不加入主力攻坚编队；祭司由独立逻辑管理。
    return army.Blood > 0 && army.Sort != AT_PRIEST &&
           army.Sort != AT_SCOUT;
}

static void ManageOffensiveArmy(UsrAI *ai)
{
    if (g_frame <= 30000)
        return;

    UpdateEnemyBaseDiscovery();
    if (!enemyBaseDiscovered)
        return;

    const tagBuilding *target = FindOffensiveTarget();
    const int orderInterval = 60;
    if (g_frame - offensiveLastOrderFrame < orderInterval)
        return;
    offensiveLastOrderFrame = g_frame;

    bool issuedAttack = false;
    for (const tagArmy &army : info.armies)
    {
        if (!IsOffensiveArmy(army))
            continue;

        if (target)
        {
            if (GetLockedArmyTarget(army.SN) == target->SN)
                continue;
            ClearArmyTargetLock(army.SN);
            currentTarget[army.SN] = target->SN;
            ai->HumanAction(army.SN, target->SN);
            issuedAttack = true;
        }
        else if (enemyBaseBlockDR >= 0 && enemyBaseBlockUR >= 0)
        {
            // 基地暂时离开视野时，先向最后已知位置推进，等待重新发现。
            ClearArmyTargetLock(army.SN);
            ai->HumanMove(army.SN,
                          (enemyBaseBlockDR + 0.5) * double(BLOCKSIDELENGTH),
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

string GetUsrAIStrategyDiagnostic()
{
    string message = "diag_frame=" + to_string(strategyDiagnosticFrame);
    message += ";clubmen=" + to_string(strategyDiagnosticClubmen);
    message += ";camp=" + to_string(strategyDiagnosticHasCamp);
    message += ";camp_ret=" + to_string(armyCampResult);
    message += ";camp_ret_frame=" + to_string(armyCampResultFrame);
    message += ";wood=" + to_string(strategyDiagnosticWood);
    message += ";meat=" + to_string(strategyDiagnosticMeat);
    message += ";stone=" + to_string(strategyDiagnosticStone);
    message += ";gold=" + to_string(strategyDiagnosticGold);
    message += ";club_ret=" + to_string(clubmanResult);
    message += ";club_ret_frame=" + to_string(clubmanResultFrame);
    return message;
}

void UsrAI::processData()
{
    info = getInfo();
    CleanDeadOwnerTargetLocks();
    ManagePriest(this);

    ManageEconomyAndProduction(this);
    AssignFieldSelfDefense(this);
    ManageOffensiveArmy(this);
    AssignFarmerSelfDefense(this);
    DispatchScouts(this);
    AssignArrowTowerTargets(this);
}
