# enemyai.cpp 函数表

本文档整理 `enemyai.cpp` 中的主要全局函数、静态辅助函数和 `EnemyAI` 成员函数，重点说明它们的用途、依赖状态，以及是否适合迁移到 `UsrAI.cpp`。

## 总体说明

`enemyai.cpp` 中很多函数不是纯函数，而是依赖文件级全局状态和 `enemyInfo`。

在 `EnemyAI` 视角中：

- `enemyInfo.armies / buildings / farmers` 表示敌方 AI 自己的单位。
- `enemyInfo.enemy_armies / enemy_buildings / enemy_farmers` 表示玩家单位。

迁移到 `UsrAI.cpp` 后，语义正好变成：

- `info.armies / buildings / farmers` 表示玩家自己的单位。
- `info.enemy_armies / enemy_buildings / enemy_farmers` 表示敌方单位。

所以大多数函数可以迁移，但建议把变量名从 `Enemy_*` 改成玩家侧语义，例如 `Enemy_Center` 可以改成 `DefenseCenter` 或 `UsrBaseCenter`。

## 文件级状态

| 名称 | 类型 | 作用 | 迁移建议 |
|---|---|---|---|
| `tagEnemyGame` | `tagGame` | 敌方 AI 读取的游戏信息 | `UsrAI` 中用 `tagUsrGame` |
| `EnemyIns` | `ins` | 敌方 AI 指令队列 | `UsrAI` 中用 `UsrIns` |
| `enemyInfo` | `tagInfo` | 当前帧敌方 AI 信息快照 | `UsrAI` 中建议命名为 `info` |
| `Enemy_Center` | `pair<double,double>` | 防守/兵力调度中心 | 可迁移，但应重命名 |
| `Defend_Center_Enemy` | `unordered_map<int,int>` | 记录防守单位 SN | 可迁移，用于基地防守 |
| `PriestGuard_Center_Enemy` | `unordered_map<int,int>` | 记录专门反祭司单位 | 可迁移，但不是基础行为必需 |
| `currentTarget` | `map<int,int>` | 每个单位当前锁定目标 | 强烈建议迁移 |
| `waveRetaliationTarget` | `map<int,int>` | 波次单位反击目标 | 可迁移 |
| `waveThreatFirstSeenFrame` | `map<int,int>` | 威胁首次发现帧 | 可迁移 |
| `wave1Units / wave2Units / wave3Units` | `vector<int>` | 波次单位集合 | 只在写波次进攻时迁移 |
| `HarassHome` | `map<int,pair<double,double>>` | 骚扰单位回撤点 | 写骚扰 AI 时迁移 |
| `DefenseHome` | `map<int,pair<double,double>>` | 防守单位回防点 | 写防守 AI 时迁移 |
| `timer` | `map<int,int>` | 控制重复下令间隔 | 可迁移，避免每帧重复命令 |

## 基础工具函数

| 函数 | 作用 | 主要依赖 | 是否适合迁移到 `UsrAI.cpp` | 注意点 |
|---|---|---|---|---|
| `countdistance` | 计算细坐标欧氏距离 | `cmath` | 可迁移 | 多数 AI 决策用格子距离即可 |
| `BlockDis` | 计算格子曼哈顿距离 | 无 | 强烈建议迁移 | 简单、够用，适合射程和警戒范围判断 |
| `BlockDis2` | 计算格子欧氏距离平方 | 无 | 强烈建议迁移 | 找最近目标时推荐用它，避免开根号 |
| `ContainsInt` | 判断 `vector<int>` 是否包含某个 SN | `std::find` | 建议迁移 | 比旧的 `isElement` 更清晰 |
| `AddUnique` | 去重加入 `vector<int>` | `ContainsInt` | 建议迁移 | 适合目标保留、单位编队 |
| `isElement` | 判断 vector 中是否存在元素 | `std::find` | 不建议优先迁移 | 可用 `ContainsInt` 替代 |

## 旧视野/旧攻击体系

这些函数属于较早的一套 AI 逻辑，和后面的目标锁、防守、自卫、波次调度有重复。除非你要研究历史逻辑，否则不建议直接搬到 `UsrAI.cpp`。

| 函数 | 作用 | 主要依赖 | 迁移建议 |
|---|---|---|---|
| `visionChange` | 根据单位视野刷新 `vision` 数组 | `vision`、单位状态 | 不建议优先迁移 |
| `seek` | 根据 `vision` 维护已发现目标列表 | `vision`、目标缓存数组 | 不建议优先迁移 |
| `ifVisible` | 移除离开视野的目标 | `vision`、目标缓存数组 | 不建议优先迁移 |
| `ifATTACK` | 根据目标缓存给单位下攻击命令 | `ifA`、`timer`、目标缓存 | 不建议优先迁移，容易重复下令 |
| `ifDestory` | 清理已不存在目标 | `ifA`、`timer` | 不建议优先迁移 |
| `ifDead` | 从缓存列表中删除死亡目标 | 目标缓存数组 | 不建议优先迁移 |
| `EnemyAI::Around` | 巡逻和回原点 | `around`、`timer`、`HumanMove` | 可参考，不建议直接搬 |
| `EnemyAI::Attack` | 旧攻击调度 | `HumanAction`、目标缓存 | 不建议优先迁移 |
| `EnemyAI::Threated` | 检查附近是否有敌军攻击自己 | `WorkObjectSN` | 不建议优先迁移，后面有更完整版本 |

## 存活判断和目标锁

这一组非常适合迁移，是写“受到攻击自动反击”“不重复下令”“目标死了换目标”的基础。

| 函数 | 作用 | 主要依赖 | 是否适合迁移 | 注意点 |
|---|---|---|---|---|
| `FindMyArmyBySN` | 按 SN 找我方军队 | `enemyInfo.armies` | 强烈建议迁移 | 搬到玩家 AI 后可改名 `FindUsrArmyBySN` |
| `EnemyFarmerAlive` | 判断敌方农民是否存活 | `enemyInfo.enemy_farmers` | 建议迁移 | 适合骚扰经济 |
| `EnemyArmyAlive` | 判断敌方军队是否存活 | `enemyInfo.enemy_armies` | 建议迁移 | 基础目标判断 |
| `EnemyPriestAlive` | 判断敌方祭司是否存活 | `enemyInfo.enemy_armies` | 建议迁移 | 祭司威胁高，适合优先处理 |
| `EnemyTargetAlive` | 判断任意敌方单位/建筑是否存在 | `enemyInfo.enemy_*` | 强烈建议迁移 | 目标锁核心函数 |
| `GetLockedArmyTarget` | 获取单位当前锁定目标，目标不存在则清理 | `currentTarget`、`EnemyTargetAlive` | 强烈建议迁移 | 防止每帧重新选目标 |
| `ClearArmyTargetLock` | 清除单位锁定目标 | `currentTarget` | 强烈建议迁移 | 目标死亡、撤退时使用 |
| `CleanDeadOwnerTargetLocks` | 清理死亡我方单位的目标锁 | `currentTarget`、`FindMyArmyBySN` | 强烈建议迁移 | 建议每帧 AI 开头调用 |
| `FindEnemyUnitBlockPosition` | 按 SN 查敌方单位格子坐标 | `enemyInfo.enemy_armies/farmers` | 建议迁移 | 箭塔射程、防守判断常用 |
| `FindEnemyTargetDetailPosition` | 按 SN 查敌方目标细坐标 | `enemyInfo.enemy_armies/farmers` | 可迁移 | 投石车避让更需要 |

## 自动反击和野外自卫

这是最适合你搬到 `UsrAI.cpp` 的核心部分。

| 函数 | 作用 | 主要依赖 | 是否适合迁移 | 注意点 |
|---|---|---|---|---|
| `FindThreatToArmy` | 找正在攻击某个单位的敌方目标，包含军队、农民、箭塔 | `WorkObjectSN`、`Project`、`currentTarget` | 强烈建议迁移 | “受到攻击自动反击”的核心 |
| `FindDirectThreatToArmySN` | 找直接攻击某个单位的最近威胁 | `WorkObjectSN`、`Project` | 强烈建议迁移 | 比 `FindThreatToArmy` 更偏野外自卫 |
| `FindNearbyEnemyFarmerToAttack` | 附近农民进入警戒范围时主动攻击 | `FIELD_FARMER_AGGRO_RADIUS` | 可迁移 | 会让军队主动追农民，可能影响阵型 |
| `FindAssistThreatNearArmy` | 附近友军被攻击时协助反击 | `FIELD_ASSIST_RADIUS` | 强烈建议迁移 | 小队互助核心 |
| `EnemyAI::AssignFieldSelfDefense` | 非防守/非波次单位自动反击、主动警戒、协助友军 | 上面几个威胁函数、`HumanAction` | 强烈建议迁移 | 迁移价值最高，但要改掉敌方命名 |

推荐迁移顺序：

1. `BlockDis2`
2. `EnemyTargetAlive`
3. `GetLockedArmyTarget`
4. `ClearArmyTargetLock`
5. `CleanDeadOwnerTargetLocks`
6. `FindDirectThreatToArmySN`
7. `FindAssistThreatNearArmy`
8. `AssignFieldSelfDefense`

## 箭塔自动攻击

敌方箭塔自动攻击是在 `EnemyAI::processData` 中显式写的，不是只靠建筑属性自动完成。

| 函数/逻辑 | 作用 | 是否适合迁移 | 注意点 |
|---|---|---|---|
| `ArrowTowerAttackRangeBlocks` | 返回箭塔攻击范围 | 建议迁移 | 使用 `DIS_ARROWTOWER` |
| `FindEnemyUnitBlockPosition` | 判断当前目标是否还在射程内 | 建议迁移 | 箭塔已有目标时不要重复下令 |
| `EnemyAI::processData` 中箭塔循环 | 箭塔扫描射程内最近敌军/农民并攻击 | 建议参考迁移 | 攻击使用 `HumanAction(towerSN, targetSN)` |

迁移时要记住：

```cpp
HumanAction(towerSN, targetSN);
```

不要写成：

```cpp
BuildingAction(towerSN, targetSN);
```

`BuildingAction` 是生产、研发、升级用的，不是攻击目标用的。

## 防守逻辑

这一组适合后续增强 AI，但不建议一开始整套复制，因为状态依赖比较多。

| 函数 | 作用 | 主要依赖 | 是否适合迁移 | 注意点 |
|---|---|---|---|---|
| `DefenseRangedAttackRangeBlocks` | 获取远程防守单位射程 | 兵种常量 | 可迁移 | 注意玩家侧兵种常量一致 |
| `DefenseChaseLimitBlocks` | 根据射程限制追击范围 | `DEFENSE_CHASE_LIMIT` | 可迁移 | 防止守军追太远 |
| `IsDefenseArmySN` | 判断某个兵是否防守单位 | `Defend_Center_Enemy` | 可迁移 | 需要维护防守集合 |
| `NeedPriestGuardCountBySort` | 计算需要多少反祭司护卫 | 兵种常量 | 可迁移 | 不是基础 AI 必需 |
| `CountPriestGuardsBySort` | 统计反祭司护卫数量 | `PriestGuard_Center_Enemy` | 可迁移 | 依赖护卫集合 |
| `TryAddPriestGuard` | 从守军中挑反祭司护卫 | 防守集合 | 可迁移 | 依赖初始化时机 |
| `FindNearestPriestNearSiegeCenter` | 找防守中心附近最近敌祭司 | `Enemy_Center` | 建议参考 | 玩家侧防祭司很有价值 |
| `IsStoneThrowerRangedDefenseTarget` | 判断投石车防守优先目标类型 | 兵种常量 | 可迁移 | 投石车专用 |
| `StoneThrowerDefensePriority` | 投石车防守目标优先级 | 上一函数 | 可迁移 | 投石车专用 |
| `FindStoneThrowerDefenseTarget` | 给防守投石车找目标 | 敌军、敌农民、中心点 | 可迁移 | 攻防都可用 |
| `GetStoneThrowerEvadePoint` | 投石车被贴脸时计算后撤点 | 地图边界、回防点 | 可迁移 | 需要确认坐标合法性 |
| `EnemyAI::Initialize_Enemycenter` | 初始化防守中心 | 建筑列表 | 可迁移 | 玩家侧可选基地、城镇中心、攻城厂 |
| `EnemyAI::Initialize_Enemymap` | 初始化防守单位和回防点 | 防守中心、单位列表 | 可迁移 | 只应初始化一次或按策略更新 |
| `EnemyAI::AssignDefense` | 完整基地防守逻辑 | 上述所有防守状态 | 可迁移但较重 | 建议后续再搬 |

## 主动进攻和兵力调度

这一组适合你实现“自动调度距离敌人最近的兵力”。

| 函数 | 作用 | 主要依赖 | 是否适合迁移 | 注意点 |
|---|---|---|---|---|
| `GetHarassCenterBlock` | 选择骚扰中心：敌箭塔、敌农民、敌建筑、默认点 | 敌建筑/农民 | 建议迁移 | 可以作为进攻集结点 |
| `FindNearestEnemyTower` | 找最近敌箭塔 | 敌建筑 | 建议迁移 | 攻坚优先级高 |
| `FindNearestEnemyPriest` | 找最近敌祭司 | 敌军 | 强烈建议迁移 | 祭司优先级高 |
| `FindNearestWaveFarmerAvoiding` | 找最近且未被其他单位占用的敌农民 | `reservedFarmers` | 建议迁移 | 避免所有兵打同一个农民 |
| `ReserveCurrentFarmerTargets` | 收集已经被锁定的农民目标 | `currentTarget` | 建议迁移 | 和上一个配套 |
| `FindWaveTargetByPriority` | 波次单位目标优先级：反击目标、祭司、农民 | 多个查找函数 | 强烈建议迁移 | 比简单最近目标更合理 |
| `IsArcherTargetSort` | 判断目标是否弓兵类 | 兵种常量 | 可迁移 | 投石车或骑兵选目标用 |
| `FindNearestEnemyArcher` | 找最近弓兵类敌人 | 敌军 | 可迁移 | 投石车优先打远程单位 |
| `FindNearestEnemyFarmer` | 找最近敌农民 | 敌农民 | 建议迁移 | 骚扰经济 |
| `FindNearestEnemyNonTowerBuilding` | 找最近非箭塔建筑 | 敌建筑 | 建议迁移 | 拆家用 |
| `FindNearestOtherEnemyUnit` | 找其他普通敌军 | 敌军 | 可迁移 | 兜底目标 |
| `FindStoneThrowerWaveTarget` | 投石车进攻优先级：箭塔、弓兵、建筑、祭司、农民、其他 | 多个目标选择函数 | 建议迁移 | 攻坚时很有价值 |
| `SelectWaveUnitsBySort` | 按兵种选择离骚扰中心最近的单位 | 我方军队、中心点 | 强烈建议迁移 | 自动调度最近兵力核心 |
| `EnemyAI::OrderWaveUnitsToAttackTarget` | 给一组单位统一下攻击命令 | `HumanAction`、目标锁 | 建议迁移 | 玩家侧可改成通用 `OrderUnitsToAttackTarget` |
| `IsCurrentActiveHarassUnit` | 判断单位是否正在参与波次进攻 | 波次状态 | 只在搬波次时需要 | 基础 AI 可不迁移 |

## 协同视野和目标选择

| 函数 | 作用 | 主要依赖 | 是否适合迁移 | 注意点 |
|---|---|---|---|---|
| `EnemyAI::assignTargetsBasedOnVision` | 基于视野共享目标，给陆军和箭塔协同下令 | `getVisionRange`、`findBestTargetInVision` | 可参考 | 不建议和 `AssignFieldSelfDefense` 混用过深 |
| `EnemyAI::getVisionRange` | 按兵种获取视野 | 兵种常量 | 可迁移 | 需要确认所有兵种覆盖 |
| `EnemyAI::findBestTargetInVision` | 视野范围内按农民、军队、建筑找目标 | `enemyInfo.enemy_*` | 可迁移 | 优先级比较简单 |
| `EnemyAI::calculateDistance` | 计算方格欧氏距离 | `cmath` | 可迁移 | 可用 `BlockDis2` 替代 |
| `EnemyAI::findNearestTarget` | 从候选 SN 列表中找最近目标 | 目标列表、敌方对象列表 | 可迁移 | 复杂度偏高但可接受 |
| `EnemyAI::getEnemyStatus` | 通过 `g_mainWidget->player[1]` 查单位状态 | UI/对象指针 | 不建议迁移 | 玩家侧容易查错对象 |
| `EnemyAI::isLandUnit` | 判断是否陆地单位 | `AT_SHIP` | 可迁移 | 简单工具 |
| `EnemyAI::shouldCooperateAttack` | 判断是否应该参加协同攻击 | 状态字符串、兵种判断 | 可参考 | 依赖 `getEnemyStatus`，不建议直接搬 |

## 波次进攻脚本

这些函数是敌方 AI 的固定节奏脚本。适合参考写法，不建议直接作为玩家基础 AI。

| 函数 | 作用 | 是否适合迁移 | 注意点 |
|---|---|---|---|
| `EnemyAI::onWaveAttack` | 根据波次编号触发进攻 | 可参考 | 玩家 AI 可以改成条件触发 |
| `EnemyAI::FirstAttack` | 第一波小规模骚扰 | 可参考 | 固定兵种和数量，脚本味较重 |
| `EnemyAI::SecondAttack` | 第二波中规模骚扰 | 可参考 | 依赖第一波残兵和杀农民统计 |
| `EnemyAI::ThirdAttack` | 第三波总攻，投石车有独立优先级 | 可参考 | 攻坚逻辑有价值，但整体较重 |

## 建议迁移路线

如果目标是在 `UsrAI.cpp` 里做一个可靠的基础 AI，建议按这个顺序搬：

1. 搬距离函数：`BlockDis`、`BlockDis2`。
2. 搬目标存活判断：`EnemyTargetAlive`、`FindEnemyUnitBlockPosition`。
3. 搬目标锁：`GetLockedArmyTarget`、`ClearArmyTargetLock`、`CleanDeadOwnerTargetLocks`。
4. 搬自动反击：`FindDirectThreatToArmySN`、`FindAssistThreatNearArmy`、`AssignFieldSelfDefense`。
5. 搬箭塔索敌：`ArrowTowerAttackRangeBlocks` 和 `processData` 中箭塔循环。
6. 搬最近兵力调度：`SelectWaveUnitsBySort`、`FindWaveTargetByPriority`。
7. 最后再考虑防守中心和波次进攻：`Initialize_Enemycenter`、`AssignDefense`、`FirstAttack/SecondAttack/ThirdAttack`。

## 最小可用组合

如果只想先实现基础行为，不要一次搬太多。推荐最小组合：

```cpp
BlockDis2
EnemyTargetAlive
GetLockedArmyTarget
ClearArmyTargetLock
CleanDeadOwnerTargetLocks
FindDirectThreatToArmySN
FindAssistThreatNearArmy
AssignFieldSelfDefense
ArrowTowerAttackRangeBlocks
```

这套能覆盖：

- 被打自动反击。
- 队友被打时支援。
- 目标死亡后换目标。
- 避免每帧重复下令。
- 箭塔自动攻击射程内目标。

## 注意事项

- 箭塔攻击、军队攻击、农民反击都应该使用 `HumanAction(selfSN, targetSN)`。
- `BuildingAction()` 只用于建筑生产、科技研发、时代升级，不用于指定攻击目标。
- 不要每帧对同一个单位重复下达相同攻击命令，否则可能重置攻击关系，让远程单位还没完成攻击动作就被打断。
- 从 `enemyai.cpp` 迁移代码时，所有 `enemyInfo.armies` 在 `UsrAI.cpp` 中对应 `info.armies`，所有 `enemyInfo.enemy_armies` 对应 `info.enemy_armies`。
- 旧视野体系和新目标锁体系不要混用，优先采用目标锁体系。
