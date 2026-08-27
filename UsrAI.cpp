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
#include <algorithm>
#include <cstdlib>
#include <map>
#include <utility>
#include <vector>

static const int USR_FIELD_SELF_DEFENSE_ORDER_INTERVAL = 12;
static const int USR_FIELD_ASSIST_RADIUS = 8;
static const int USR_FIELD_ARMY_AGGRO_RADIUS = 7;
static const int USR_FIELD_FARMER_AGGRO_RADIUS = 6;
static const int USR_TOWER_ORDER_INTERVAL = 20;
static const int USR_ECONOMY_ORDER_INTERVAL = 80;
static const int USR_BUILD_ORDER_INTERVAL = 100;
static const int USR_BUILDING_ACTION_INTERVAL = 80;
static const int USR_PRIEST_ORDER_INTERVAL = 20;
static const int USR_PRIEST_DANGER_RADIUS = 9;
static const int USR_PRIEST_SAFE_RADIUS = 6;
static const int USR_PRIEST_SAFE_FRAMES = 120;
static const int USR_PRIEST_PREPARE_FRAME = 5200;
static const int USR_PRIEST_ADVANCE_FRAME = 21500;
static const int USR_PRIEST_SAFE_BLOCK_DR = 2;
static const int USR_PRIEST_SAFE_BLOCK_UR = 2;
static const int USR_ARROWTOWER_BUILD_RADIUS = 18;
static const int USR_ARROWTOWER_BUILD_MIN_MARGIN = 3;
static const int USR_INVALID_FRAME = -1000000000;

static map<int, int> currentTarget;
static map<int, int> waveRetaliationTarget;
static map<int, int> waveThreatFirstSeenFrame;
static map<int, int> fieldSelfDefenseLastOrderFrame;
static map<int, int> towerLastOrderFrame;
static map<int, pair<double, double>> harassHome;
static map<int, int> farmerLastOrderFrame;
static int lastEconomyOrderFrame = USR_INVALID_FRAME;
static int lastBuildOrderFrame = USR_INVALID_FRAME;
static int lastBuildingActionFrame = USR_INVALID_FRAME;
static int lastPriestOrderFrame = USR_INVALID_FRAME;
static int priestSafeSinceFrame = USR_INVALID_FRAME;
static int priestMoveOrderId = -1;
static int buildCandidateIndex = 0;
static int armyCampOrderId = -1;
static int clubmanOrderId = -1;
static int armyCampResult = ACTION_SUCCESS;
static int clubmanResult = ACTION_SUCCESS;
static int armyCampResultFrame = USR_INVALID_FRAME;
static int clubmanResultFrame = USR_INVALID_FRAME;
static int strategyDiagnosticFrame = USR_INVALID_FRAME;
static int strategyDiagnosticClubmen = 0;
static int strategyDiagnosticHasCamp = 0;
static int strategyDiagnosticWood = 0;
static int strategyDiagnosticMeat = 0;
static int strategyDiagnosticStone = 0;
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
static const tagArmy *FindMyArmyBySN(const tagInfo &info, int sn)
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
static bool EnemyTargetAlive(const tagInfo &info, int sn)
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
static int GetLockedArmyTarget(const tagInfo &info, int armySN)
{
    auto it = currentTarget.find(armySN);
    if (it == currentTarget.end())
        return -1;

    if (EnemyTargetAlive(info, it->second))
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
static void CleanDeadOwnerTargetLocks(const tagInfo &info)
{
    for (auto it = currentTarget.begin(); it != currentTarget.end();)
    {
        if (FindMyArmyBySN(info, it->first) == nullptr)
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
static bool FindEnemyUnitBlockPosition(const tagInfo &info, int sn, int &blockDR, int &blockUR)
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
static int FindDirectThreatToArmySN(const tagInfo &info, int myArmySN)
{
    const tagArmy *myArmy = FindMyArmyBySN(info, myArmySN);
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
static int FindNearbyEnemyFarmerToAttack(const tagInfo &info, const tagArmy &myArmy)
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

// 接口：寻找附近友军正在承受的威胁。
// 用途：实现小范围自动支援，附近队友被打时协助反击。
static int FindAssistThreatNearArmy(const tagInfo &info, const tagArmy &myArmy)
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
static pair<int, int> GetHarassCenterBlock(const tagInfo &info)
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
static void SelectWaveUnitsBySort(const tagInfo &info,
                                  vector<int> &dst,
                                  int unitSort,
                                  int needCount,
                                  const vector<int> &alreadyUsed)
{
    pair<int, int> center = GetHarassCenterBlock(info);
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
        const tagArmy *army = FindMyArmyBySN(info, sn);
        if (!army)
            continue;

        dst.push_back(sn);
        harassHome[sn] = make_pair(army->DR, army->UR);
        needCount--;
    }
}

// 接口：为所有我方军队执行基础野外自卫逻辑。
// 用途：被打自动反击、附近敌农民靠近时主动攻击、附近友军被打时协助。
// 接口：按类型查找已完成且空闲的我方建筑。
// 用途：统一调度时代、科技和生产命令，避免覆盖正在执行的项目。
static const tagBuilding *FindReadyBuildingByType(const tagInfo &info, int type)
{
    for (const tagBuilding &building : info.buildings)
    {
        if (building.Type == type && building.Percent >= 100 && building.Project == -1)
            return &building;
    }
    return nullptr;
}

static const tagBuilding *FindBuildingByType(const tagInfo &info,
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

static bool HasCompletedBuilding(const tagInfo &info, int type)
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
static bool HasBuilding(const tagInfo &info, int type)
{
    for (const tagBuilding &building : info.buildings)
    {
        if (building.Type == type && building.Blood > 0)
            return true;
    }
    return false;
}

static bool HasIncompleteBuilding(const tagInfo &info, int type)
{
    for (const tagBuilding &building : info.buildings)
    {
        if (building.Type == type && building.Blood > 0 && building.Percent < 100)
            return true;
    }
    return false;
}

static int CountArmyBySort(const tagInfo &info, int sort)
{
    int count = 0;
    for (const tagArmy &army : info.armies)
    {
        if (army.Sort == sort && army.Blood > 0)
            count++;
    }
    return count;
}

static const tagBuilding *FindCenter(const tagInfo &info)
{
    for (const tagBuilding &building : info.buildings)
    {
        if (building.Type == BUILDING_CENTER && building.Blood > 0)
            return &building;
    }
    return nullptr;
}

static const tagArmy *FindPriest(const tagInfo &info)
{
    for (const tagArmy &army : info.armies)
    {
        if (army.Sort == AT_PRIEST && army.Blood > 0)
            return &army;
    }
    return nullptr;
}

static int FindThreatToPriestSN(const tagInfo &info, int priestSN)
{
    const tagArmy *priest = FindPriest(info);
    if (!priest)
        return -1;

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

static const tagBuilding *FindEnemySiege(const tagInfo &info)
{
    for (const tagBuilding &building : info.enemy_buildings)
    {
        if (building.Type == BUILDING_SIEGE && building.Blood > 0)
            return &building;
    }
    return nullptr;
}

static const tagArmy *FindNearestEnemyArmy(const tagInfo &info, int blockDR, int blockUR)
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

static bool HasVisibleWaveThreat(const tagInfo &info)
{
    const tagBuilding *center = FindCenter(info);
    const tagArmy *priest = FindPriest(info);
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

static int ArmyTargetPriority(const tagInfo &info, const tagArmy &enemy)
{
    const tagArmy *priest = FindPriest(info);
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
static int FindEnemyArmyInVision(const tagInfo &info, const tagArmy &army)
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
        int priority = ArmyTargetPriority(info, enemy);
        if (priority < bestPriority || (priority == bestPriority && dis2 < bestDis2))
        {
            bestPriority = priority;
            bestDis2 = dis2;
            bestSN = enemy.SN;
        }
    }
    return bestSN;
}

static int ResourcePriority(const tagInfo &info, int resourceType)
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

static int FindBestResourceSN(const tagInfo &info, const tagFarmer &farmer)
{
    int bestSN = -1;
    int bestPriority = 1000000000;
    int bestDis2 = 1000000000;
    for (const tagResource &resource : info.resources)
    {
        if (resource.Cnt <= 0 && resource.Blood <= 0)
            continue;
        if (resource.Type == RESOURCE_LION || resource.Type == RESOURCE_FISH)
            continue;
        int priority = ResourcePriority(info, resource.Type);
        int dis2 = BlockDis2(farmer.BlockDR, farmer.BlockUR,
                             resource.BlockDR, resource.BlockUR);
        if (priority < bestPriority || (priority == bestPriority && dis2 < bestDis2))
        {
            bestPriority = priority;
            bestDis2 = dis2;
            bestSN = resource.SN;
        }
    }
    return bestSN;
}

static int FindBuilderFarmerSN(const tagInfo &info)
{
    int bestSN = -1;
    int bestDis2 = 1000000000;
    const tagBuilding *center = FindBuildingByType(info, BUILDING_CENTER, true);
    for (const tagFarmer &farmer : info.farmers)
    {
        if (farmer.FarmerSort != FARMERTYPE_FARMER || farmer.Blood <= 0)
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

static bool IsBuildCandidateUsable(const tagInfo &info, int blockDR, int blockUR)
{
    if (!info.theMap)
        return true;
    if (blockDR < USR_ARROWTOWER_BUILD_MIN_MARGIN ||
        blockUR < USR_ARROWTOWER_BUILD_MIN_MARGIN ||
        blockDR >= MAP_L - USR_ARROWTOWER_BUILD_MIN_MARGIN ||
        blockUR >= MAP_U - USR_ARROWTOWER_BUILD_MIN_MARGIN)
    {
        return false;
    }
    return (*info.theMap)[blockDR][blockUR].type != MAPPATTERN_OCEAN;
}

static pair<int, int> GetBuildCandidate(const tagInfo &info, int buildingType)
{
    const tagBuilding *center = FindCenter(info);
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
        if (IsBuildCandidateUsable(info, dr, ur))
        {
            buildCandidateIndex = (index + 1) % count;
            return make_pair(dr, ur);
        }
    }
    return make_pair(-1, -1);
}

static bool TryAssignIdleFarmer(UsrAI *ai, const tagInfo &info)
{
    if (g_frame - lastEconomyOrderFrame < USR_ECONOMY_ORDER_INTERVAL)
        return false;

    for (const tagFarmer &farmer : info.farmers)
    {
        if (farmer.FarmerSort != FARMERTYPE_FARMER || farmer.Blood <= 0 ||
            farmer.NowState != HUMAN_STATE_IDLE)
        {
            continue;
        }
        if (g_frame - farmerLastOrderFrame[farmer.SN] < USR_ECONOMY_ORDER_INTERVAL)
            continue;
        int targetSN = FindBestResourceSN(info, farmer);
        if (targetSN == -1)
            continue;
        ai->HumanAction(farmer.SN, targetSN);
        farmerLastOrderFrame[farmer.SN] = g_frame;
        lastEconomyOrderFrame = g_frame;
        return true;
    }
    return false;
}

static void EvacuateFarmersDuringAttackWaves(UsrAI *ai, const tagInfo &info)
{
    const bool inFirstWave = g_frame >= 5600 && g_frame <= 11000;
    const bool inSecondWave = g_frame >= 13000 && g_frame <= 19000;
    const bool inThirdWave = g_frame >= 20500 && g_frame <= 26000;
    if (!inFirstWave && !inSecondWave && !inThirdWave)
        return;

    const tagBuilding *center = FindCenter(info);
    if (!center)
        return;

    for (const tagFarmer &farmer : info.farmers)
    {
        if (farmer.Blood <= 0 || farmer.FarmerSort != FARMERTYPE_FARMER)
            continue;
        const tagArmy *threat = FindNearestEnemyArmy(info,
                                                     farmer.BlockDR,
                                                     farmer.BlockUR);
        if (!threat)
            continue;
        int dis2 = BlockDis2(farmer.BlockDR, farmer.BlockUR,
                             threat->BlockDR, threat->BlockUR);
        if (threat->WorkObjectSN != farmer.SN && dis2 > 8 * 8)
            continue;
        if (g_frame - farmerLastOrderFrame[farmer.SN] < 30)
            continue;

        int dx = center->BlockDR - threat->BlockDR;
        int dy = center->BlockUR - threat->BlockUR;
        int blockDR = center->BlockDR + (dx >= 0 ? 5 : -5);
        int blockUR = center->BlockUR + (dy >= 0 ? 5 : -5);
        blockDR = max(1, min(MAP_L - 2, blockDR));
        blockUR = max(1, min(MAP_U - 2, blockUR));
        ai->HumanMove(farmer.SN,
                      (blockDR + 0.5) * double(BLOCKSIDELENGTH),
                      (blockUR + 0.5) * double(BLOCKSIDELENGTH));
        farmerLastOrderFrame[farmer.SN] = g_frame;
    }
}

static bool TryBuild(UsrAI *ai, const tagInfo &info, int buildingType)
{
    if (g_frame - lastBuildOrderFrame < USR_BUILD_ORDER_INTERVAL)
        return false;
    int farmerSN = FindBuilderFarmerSN(info);
    if (farmerSN == -1)
        return false;
    pair<int, int> position = GetBuildCandidate(info, buildingType);
    if (position.first == -1)
        return false;
    int orderId = ai->HumanBuild(farmerSN, buildingType, position.first, position.second);
    if (buildingType == BUILDING_ARMYCAMP)
        armyCampOrderId = orderId;
    lastBuildOrderFrame = g_frame;
    return true;
}

static bool TryBuildingAction(UsrAI *ai, const tagInfo &info,
                              int buildingType, int action)
{
    if (g_frame - lastBuildingActionFrame < USR_BUILDING_ACTION_INTERVAL)
        return false;
    const tagBuilding *building = FindReadyBuildingByType(info, buildingType);
    if (!building)
        return false;
    int orderId = ai->BuildingAction(building->SN, action);
    if (action == BUILDING_ARMYCAMP_CREATE_CLUBMAN)
        clubmanOrderId = orderId;
    lastBuildingActionFrame = g_frame;
    return true;
}

// 接口：推进最短经济、时代与军队生产链。
// 用途：所有决策均基于可见状态；失败后冷却重试，不依赖作弊资源。
static void ManageEconomyAndProduction(UsrAI *ai, const tagInfo &info)
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
        strategyDiagnosticClubmen = CountArmyBySort(info, AT_CLUBMAN);
        strategyDiagnosticHasCamp = HasBuilding(info, BUILDING_ARMYCAMP) ? 1 : 0;
        strategyDiagnosticWood = info.Wood;
        strategyDiagnosticMeat = info.Meat;
        strategyDiagnosticStone = info.Stone;
        strategyDiagnosticGold = info.Gold;
    }

    const bool nearPopulationCap = info.Human_Num + 1.0 >= info.Human_MaxNum;
    if (nearPopulationCap && info.Wood >= 30 &&
        !HasIncompleteBuilding(info, BUILDING_HOME))
    {
        if (TryBuild(ai, info, BUILDING_HOME))
            return;
    }

    if (!HasBuilding(info, BUILDING_ARMYCAMP) && info.Wood >= 125)
    {
        if (TryBuild(ai, info, BUILDING_ARMYCAMP))
            return;
    }

    // 第一波前先形成最低防线；时代升级与扩张不能挤占早期兵力。
    const bool firstWaveReady = CountArmyBySort(info, AT_CLUBMAN) >= 5;
    if (firstWaveReady && info.civilizationStage == CIVILIZATION_STONEAGE)
    {
        if (HasCompletedBuilding(info, BUILDING_GRANARY) &&
            HasCompletedBuilding(info, BUILDING_STOCK) && info.Meat >= 500)
        {
            if (TryBuildingAction(ai, info, BUILDING_CENTER, BUILDING_CENTER_UPGRADE))
                return;
        }
    }
    else if (info.civilizationStage >= CIVILIZATION_TOOLAGE)
    {
        if (!HasBuilding(info, BUILDING_RANGE) && info.Wood >= 150)
        {
            if (TryBuild(ai, info, BUILDING_RANGE))
                return;
        }
        if (!HasBuilding(info, BUILDING_STABLE) && info.Wood >= 150)
        {
            if (TryBuild(ai, info, BUILDING_STABLE))
                return;
        }
        if (!HasBuilding(info, BUILDING_ARROWTOWER) && info.Stone >= 150 &&
            HasCompletedBuilding(info, BUILDING_GRANARY))
        {
            if (TryBuild(ai, info, BUILDING_ARROWTOWER))
                return;
        }
        if (info.civilizationStage == CIVILIZATION_TOOLAGE && info.Meat >= 800 &&
            HasCompletedBuilding(info, BUILDING_RANGE) &&
            HasCompletedBuilding(info, BUILDING_STABLE))
        {
            if (TryBuildingAction(ai, info, BUILDING_CENTER, BUILDING_CENTER_UPGRADE))
                return;
        }
    }

    if (info.civilizationStage >= CIVILIZATION_TOOLAGE &&
        !HasBuilding(info, BUILDING_ARROWTOWER))
    {
        if (TryBuildingAction(ai, info, BUILDING_GRANARY,
                              BUILDING_GRANARY_ARROWTOWER))
            return;
    }

    int targetClubmen = g_frame < 13000 ? 5 : 8;
    if (!nearPopulationCap && CountArmyBySort(info, AT_CLUBMAN) < targetClubmen &&
        info.Meat >= 50)
    {
        if (TryBuildingAction(ai, info, BUILDING_ARMYCAMP,
                              BUILDING_ARMYCAMP_CREATE_CLUBMAN))
            return;
    }

    if (!nearPopulationCap && static_cast<int>(info.farmers.size()) < 14 && info.Meat >= 50)
    {
        if (TryBuildingAction(ai, info, BUILDING_CENTER,
                              BUILDING_CENTER_CREATEFARMER))
            return;
    }

    if (info.civilizationStage >= CIVILIZATION_TOOLAGE && !nearPopulationCap &&
        CountArmyBySort(info, AT_BOWMAN) < 5 && info.Meat >= 40 && info.Wood >= 20)
    {
        if (TryBuildingAction(ai, info, BUILDING_RANGE,
                              BUILDING_RANGE_CREATE_BOWMAN))
            return;
    }

    if (info.civilizationStage >= CIVILIZATION_TOOLAGE && !nearPopulationCap &&
        CountArmyBySort(info, AT_SCOUT) < 3 && info.Meat >= 100)
    {
        if (TryBuildingAction(ai, info, BUILDING_STABLE,
                              BUILDING_STABLE_CREATE_SCOUT))
            return;
    }

    TryAssignIdleFarmer(ai, info);
}

static pair<double, double> GetPriestRetreatPoint(const tagInfo &info,
                                                   const tagArmy &priest,
                                                   const tagArmy &threat)
{
    const tagBuilding *center = FindCenter(info);
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

static pair<double, double> GetPriestEmergencyPoint(const tagInfo &info,
                                                     const tagArmy &priest,
                                                     const tagArmy &threat)
{
    const tagBuilding *center = FindCenter(info);
    int blockDR = priest.BlockDR;
    int blockUR = priest.BlockUR;
    int dx = priest.BlockDR - threat.BlockDR;
    int dy = priest.BlockUR - threat.BlockUR;
    if (dx == 0 && dy == 0)
        dx = -1;

    // Move toward the center and keep a small offset away from the threat.
    // A nearby reachable point is safer than repeatedly targeting map (2, 2).
    if (center)
    {
        blockDR = (priest.BlockDR + center->BlockDR * 2) / 3;
        blockUR = (priest.BlockUR + center->BlockUR * 2) / 3;
    }
    blockDR += dx > 0 ? -3 : (dx < 0 ? 3 : 0);
    blockUR += dy > 0 ? -3 : (dy < 0 ? 3 : 0);
    blockDR = max(1, min(MAP_L - 2, blockDR));
    blockUR = max(1, min(MAP_U - 2, blockUR));
    return make_pair((blockDR + 0.5) * double(BLOCKSIDELENGTH),
                     (blockUR + 0.5) * double(BLOCKSIDELENGTH));
}

static pair<double, double> GetPriestDefensePoint(const tagInfo &info,
                                                   const tagArmy &priest)
{
    const tagBuilding *center = FindCenter(info);
    if (!center)
        return make_pair(-1.0, -1.0);

    static const int OFFSETS[][2] = {
        {-6, -6}, {-6, 6}, {6, -6}, {6, 6},
        {-8, 0}, {8, 0}, {0, -8}, {0, 8}
    };
    const int count = static_cast<int>(sizeof(OFFSETS) / sizeof(OFFSETS[0]));
    int bestDR = -1;
    int bestUR = -1;
    int bestScore = 1000000000;
    for (int i = 0; i < count; ++i)
    {
        const int blockDR = center->BlockDR + OFFSETS[i][0];
        const int blockUR = center->BlockUR + OFFSETS[i][1];
        if (blockDR < 1 || blockUR < 1 ||
            blockDR >= MAP_L - 2 || blockUR >= MAP_U - 2)
        {
            continue;
        }
        if (info.theMap && (*info.theMap)[blockDR][blockUR].type == MAPPATTERN_OCEAN)
            continue;

        const int priestDistance = BlockDis2(priest.BlockDR, priest.BlockUR,
                                              blockDR, blockUR);
        const int centerDistance = BlockDis2(center->BlockDR, center->BlockUR,
                                              blockDR, blockUR);
        const int score = priestDistance * 4 + centerDistance;
        if (score < bestScore)
        {
            bestScore = score;
            bestDR = blockDR;
            bestUR = blockUR;
        }
    }
    if (bestDR == -1)
        return make_pair(-1.0, -1.0);
    return make_pair((bestDR + 0.5) * double(BLOCKSIDELENGTH),
                     (bestUR + 0.5) * double(BLOCKSIDELENGTH));
}

// 接口：祭司安全与最终转换状态机。
// 用途：危险时立即撤退；第三波后才在安全窗口推进并转换敌方工厂。
static void ManagePriest(UsrAI *ai, const tagInfo &info)
{
    const tagArmy *priest = FindPriest(info);
    if (!priest)
        return;

    if (priestMoveOrderId != -1)
    {
        map<int, int>::const_iterator result = info.ins_ret.find(priestMoveOrderId);
        if (result != info.ins_ret.end())
        {
            priestMoveOrderId = -1;
        }
    }

    const tagArmy *threat = FindNearestEnemyArmy(info, priest->BlockDR, priest->BlockUR);
    bool underDirectAttack = threat && threat->WorkObjectSN == priest->SN;
    int threatDis2 = threat ? BlockDis2(priest->BlockDR, priest->BlockUR,
                                        threat->BlockDR, threat->BlockUR)
                            : 1000000000;
    if (threat && (underDirectAttack ||
                   threatDis2 <= USR_PRIEST_DANGER_RADIUS * USR_PRIEST_DANGER_RADIUS))
    {
        priestSafeSinceFrame = USR_INVALID_FRAME;
        if (g_frame - lastPriestOrderFrame >= USR_PRIEST_ORDER_INTERVAL)
        {
            pair<double, double> retreat = GetPriestEmergencyPoint(info, *priest, *threat);
            priestMoveOrderId = ai->HumanMove(priest->SN, retreat.first, retreat.second);
            lastPriestOrderFrame = g_frame;
        }
        return;
    }

    if (priestSafeSinceFrame == USR_INVALID_FRAME)
        priestSafeSinceFrame = g_frame;

    // 进攻窗口内一直保持撤离，直到中心周围不再有可见敌军。
    const bool firstWaveWindow = g_frame >= USR_PRIEST_PREPARE_FRAME && g_frame < 11000;
    const bool secondWaveWindow = g_frame >= 12500 && g_frame < 19000;
    const bool thirdWaveWindow = g_frame >= 20000 && g_frame < USR_PRIEST_ADVANCE_FRAME;
    if ((firstWaveWindow || secondWaveWindow || thirdWaveWindow) &&
        (HasVisibleWaveThreat(info) ||
         g_frame < 6500 ||
         (g_frame >= 12500 && g_frame < 14500) ||
         (g_frame >= 20000 && g_frame < USR_PRIEST_ADVANCE_FRAME)))
    {
        pair<double, double> defensePoint = GetPriestDefensePoint(info, *priest);
        int defenseBlockDR = static_cast<int>(defensePoint.first / double(BLOCKSIDELENGTH));
        int defenseBlockUR = static_cast<int>(defensePoint.second / double(BLOCKSIDELENGTH));
        if (defensePoint.first >= 0.0 &&
            BlockDis2(priest->BlockDR, priest->BlockUR,
                      defenseBlockDR, defenseBlockUR) > 2 &&
            g_frame - lastPriestOrderFrame >= USR_PRIEST_ORDER_INTERVAL * 5)
        {
            priestMoveOrderId = ai->HumanMove(priest->SN, defensePoint.first, defensePoint.second);
            lastPriestOrderFrame = g_frame;
        }
        return;
    }

    if (g_frame < USR_PRIEST_ADVANCE_FRAME ||
        g_frame - priestSafeSinceFrame < USR_PRIEST_SAFE_FRAMES ||
        priest->ConvertCooldown > 0)
    {
        return;
    }

    const tagBuilding *siege = FindEnemySiege(info);
    if (!siege || g_frame - lastPriestOrderFrame < USR_PRIEST_ORDER_INTERVAL)
        return;

    ai->HumanAction(priest->SN, siege->SN);
    lastPriestOrderFrame = g_frame;
}

static void AssignFieldSelfDefense(UsrAI *ai, const tagInfo &info)
{
    const tagArmy *priest = FindPriest(info);
    const int priestThreatSN = priest ? FindThreatToPriestSN(info, priest->SN) : -1;

    for (const tagArmy &army : info.armies)
    {
        if (army.Sort == AT_PRIEST)
            continue;

        int targetSN = priestThreatSN;
        if (targetSN == -1)
            targetSN = GetLockedArmyTarget(info, army.SN);

        if (targetSN == -1)
            targetSN = FindDirectThreatToArmySN(info, army.SN);

        if (targetSN == -1)
            targetSN = FindEnemyArmyInVision(info, army);

        if (targetSN == -1)
            targetSN = FindNearbyEnemyFarmerToAttack(info, army);

        if (targetSN == -1)
            targetSN = FindAssistThreatNearArmy(info, army);

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
static int FindArrowTowerTarget(const tagInfo &info, const tagBuilding &tower)
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
static void AssignArrowTowerTargets(UsrAI *ai, const tagInfo &info)
{
    for (const tagBuilding &building : info.buildings)
    {
        if (building.Type != BUILDING_ARROWTOWER)
            continue;

        int targetDR = 0;
        int targetUR = 0;
        const bool currentTargetInRange =
            building.Project != -1 &&
            FindEnemyUnitBlockPosition(info, building.Project, targetDR, targetUR) &&
            BlockDis(building.BlockDR, building.BlockUR, targetDR, targetUR) <= ArrowTowerAttackRangeBlocks();

        if (currentTargetInRange)
            continue;
        if (g_frame - towerLastOrderFrame[building.SN] < USR_TOWER_ORDER_INTERVAL)
            continue;

        int targetSN = FindArrowTowerTarget(info, building);
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
    tagInfo info = getInfo();

    CleanDeadOwnerTargetLocks(info);
    ManagePriest(this, info);
    EvacuateFarmersDuringAttackWaves(this, info);
    ManageEconomyAndProduction(this, info);
    AssignFieldSelfDefense(this, info);
    AssignArrowTowerTargets(this, info);
}
