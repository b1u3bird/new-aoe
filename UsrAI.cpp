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
// 祭司开局探路的走法：抓一次「最初视野的边缘」，沿它走一圈就收工。
//
// 【为什么要换成固定一圈】原先用的是侦察骑兵那套动态前沿（FindBestScoutFrontier
// 每轮重新找「已探明且邻接未探明」的点），而结束探路的唯一条件是「视野里出现
// 瞪羚」（FinishPriestExplore 只有那一个调用点）。问题在于：前沿集合随已探明
// 区域外扩而不断更新、几乎永不为空，所以只要祭司没在视野里撞见活瞪羚
// （瞪羚被农民猎杀后 Blood 归零、或种群一直在探索半径之外），
// priestExploreDone 就永远是 false —— 祭司被永久扣在探路分支里，
// 下面那段「回基地」的代码一次都执行不到。
// 固定一圈则是有限步的：抓点 → 依次走 → 走完（含走不到的跳过）即结束，
// 与是否见过瞪羚无关，必然终止。
//
// 抓取时机取「第一次能算出非空边界时」，也就是地图刚建立、只有初始视野的那一帧。
// 按相对市镇中心的极角排序，让它沿边界绕圈而不是来回折返。
static const int USR_PRIEST_LAP_MAX_POINTS = 32;
static vector<pair<int, int> > priestLapPoints;
static size_t priestLapIndex = 0;
static bool priestLapCaptured = false;
// 祭司开局探路是否已结束。
static bool priestExploreDone = false;
// 祭司「走向敌方攻城武器厂」的停止距离（格）。
// 必须小于引擎的转换距离（config.json 的 DIS_PRIEST = 12），确保
// ManagePriest 里「到位就地转换」那一判定成立时，祭司一定真的能转换。
static const int USR_PRIEST_SIEGE_APPROACH_RADIUS = 10;
// 祭司「正在转换」的判定距离（格）。取 config.json 的 DIS_PRIEST = 12，
// 也就是它真正能转换的距离。
//
// 注意这与「该不该把某个敌人选为转换目标」是两件事：
//   · 这里的用途是判断祭司【当下一刻是否真的在转换】—— 决定要不要吃
//     「转换中不撤退」那个代价（见 conversionTargetAlive 的说明）。
//   · 选取目标时不做这个距离限制：祭司本来就需要先走过去再转换，
//     按 12 格过滤会把「主动接近」这条路整个废掉。
static const int USR_PRIEST_CONVERTING_RADIUS = 12;
// 祭司的活动范围：距市镇中心超过这么多格就强制回家。
//
// 祭司只有 100 血、近战与远程防御都是 0、不可补充，走太远既没有箭塔掩护、
// 也脱离部队支援。实测它死在 (78,73) —— 离基地很远，最后 4 个敌人贴到 1 格
// 围杀，而它全程 IDLE 站着不动。
//
// 【与获胜条件冲突，必须知道】获胜要求把祭司送到敌方攻城武器厂去转换它。
// 如果那座厂在 50 格之外，这个硬圈会让 AI 永远赢不了。所以下面刻意给
// 「已建立转换关系」（WorkObjectSN != -1，即正在转换或正要赶往目标）开了豁免 ——
// 只有真正在外面「游荡」时才被拉回来。
static const int USR_PRIEST_LEASH_RADIUS = 50;
// 祭司【开局探路】的半径上限（格，相对市镇中心）：只探这个范围内的前沿点，
// 探完就收工回家。
//
// 前沿集合（已探明陆地且邻接未探明）随探索不断外扩、几乎永不为空，所以
// 必须有这个上限 —— 没有它探路永远不会结束，祭司就被永久扣在探路分支里、
// 回不了基地（这正是它的历史故障）。
// 实现上是「分层推进」：走完一圈 → 前沿外扩一圈 → 再抓一圈，直到最近的前沿点
// 落到这个半径之外为止。
static const int USR_PRIEST_EXPLORE_RADIUS = 50;
// 祭司探路的【时间】截止帧：到这个帧无论如何收工，哪怕 50 格还没探完。
// 与半径上限是并列的两个出口，谁先满足用谁。
// 祭司只有 100 血、近战/远程防御都是 0、不可补充，而三波骚扰从 FAT=6000 就
// 开始了 —— 让它在那之前回基地，比多探几格重要。
static const int USR_PRIEST_EXPLORE_UNTIL_FRAME = 5000;
// 祭司距市镇中心多近算「已经在基地范围」，单位：格。
// 在此范围内不再「退向中心」（那是原地不动），改为绕到中心相对威胁的另一侧。
static const int USR_PRIEST_HOME_RADIUS = 12;
// 第一个侦察骑兵的生产时间点。它执行「视野内出现敌人即撤回市镇中心」的
// 保命策略，不承担基地侦测（角色判定见 DispatchScouts）。
// 原为 33000，按要求提前到 30000（= 祭司进入「保存实力」阶段的时间点）。
static const int USR_SCOUT_FIRST_FRAME = 30000;
// 侦察骑兵的【总数】。用于在人口上限里给它们始终预留位置 ——
// 必须按总数预留、而不是等它该出场时才留：实测人口在 f=30000 之前就已经
// 顶到 50/50，那时再留已经来不及，「侦察兵一个都造不出来」就是这么来的。
static const int USR_SCOUT_TOTAL = 2;
// 后期把农民上限压到 USR_FARMER_LATE_TARGET 的帧号。
// 此时经济已成型、军队才是胜负手，把人口让出来给兵。
// 注意：这不是「立刻裁到 5 个」——只是不再补产，多出来的由
// SacrificeExcessFarmers 逐步自毁腾出人口（每 300 帧一个）。
static const int USR_FARMER_LATE_FRAME = 35000;
static const int USR_FARMER_LATE_TARGET = 5;
// 专职侦测敌方基地的侦察骑兵的生产时间点。
// 在 USR_SCOUT_FIRST_FRAME 之后产出的侦察兵执行「视野内出现敌人即撤回市镇中心」
// 的保命策略，不承担基地侦测；这一帧之后再补 1 个专门负责侦测敌方基地的。
// 原为 36000，按要求提前到 33000。
static const int USR_SCOUT_RECON_FRAME = 33000;
// 开始允许军队主动进攻敌方建筑的帧号。
// 此前军队只做接敌自卫（AssignFieldSelfDefense：看到谁打谁，从不主动推进）；
// 过了这一帧之后，已经侦察到的敌方建筑会被列为攻击目标。
// 与 USR_SCOUT_RECON_FRAME 同帧是有意的：专职侦测兵开始工作时，
// 军队也刚好具备出击的条件。
static const int USR_OFFENSIVE_FRAME = 33000;
// 农民自卫的启动帧。此前的农民遇袭一律靠撤离，不还手 —— 早期被零星骚扰
// 牵着走会白白损失采集力；三波骚扰过后（enemyai.cpp:45 的 TAT = 21000 已过）
// 再让农民挨打时就地反击。
static const int USR_FARMER_SELF_DEFENSE_FRAME = 30000;
// 农民去救祭司的最大距离（格，欧氏）。
//
// 【为什么要有上限】祭司是唯一的获胜路径且不可补充，但农民同样不可替代 ——
// 被杀一个就少一份产出。农民移速 HUMAN_SPEED = 2.236、攻击射程 1，
// 目标在 8 格之外时等它走到，祭司多半已经死了，而它自己成了送人头。
// 所以只让「本来就在祭司附近干活」的农民顺手去救。
// 取 8：与本文件的 USR_FIELD_ARMY_AGGRO_RADIUS(7) 同一量级，
// 又明显小于军队的协防半径 USR_FIELD_ASSIST_RADIUS(12) —— 军队能跑，
// 农民跑不起。
static const int USR_FARMER_PRIEST_HELP_RADIUS = 8;
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
// 靶场目标数量。本 AI 的全部兵力（战车弓兵）都由靶场训练，多一座靶场就是
// 多一倍的出兵速度。策略文档《快速升级和取得胜利》第 71/75 行：
// 「8 分多一点就可以两个靶场同时出兵，10 分钟前 3 个靶场同时出兵」。
// 第 1 座在工具时代就建（它是升时代前置「市场 + 靶场 + 马厩 ≥ 2」之一），
// 第 2、3 座要等升入铜器时代 —— 理由见建造点的注释。
static const int USR_RANGE_TARGET = 3;
// ── 猎物群：一次猎杀任务的基本单位 ──────────────────────────────────
// 文档第 24 行：「一个村民打一个羚羊，打死了去采集。要点是全部打死之后再建设
// 仓库，选择离所有死羚羊位置最近的区域建设。否则有的村民会跑很远。」
//
// 实现方式是：把彼此靠近的瞪羚归成一群，一群同时只派一个猎手，
// 由他把整群清掉；群内全部变成尸体之后，尸体就退化成普通的食物资源点，
// 交给常规采集逻辑按人数上限去采（不再是「猎杀」）。
//
// 为什么是「一群一个猎手」而不是「一只两个」（文档推荐的双人打猎）：
// 本 AI 里多个农民追会逃跑的瞪羚（Animal.cpp:208-218 的 isMonitorObject）
// 会互相挡路，而腾出来的劳动力去采浆果/木材的收益更大。
// 这是对文档的取舍，不是照抄。
static const int USR_HUNT_CLUSTER_RADIUS = 8;
static const int USR_HUNT_HUNTERS_PER_CLUSTER = 1;
// 同类交付建筑（仓库 / 谷仓）的数量上限。
// 原先是 HasBuilding 守卫，即每种至多一座 —— 于是首座建在树多的地方之后，
// 猎物群那边永远不会再补仓库，采肉的农民要横穿地图交付。
// 取 2：让猎场能有一处自己的仓库，又不至于把 120 木/座的仓库铺满地图。
// 【已取消】原先这里有一个 USR_DEPOT_MAX = 2（每类交付建筑至多 2 座）。
// 它造成的问题：通用路径按「交付距离最远」选点，会先落到远处的矿区/林区把名额
// 用光，于是猎物群那条路的前置判断 `CountBuilding(STOCK) < USR_DEPOT_MAX` 直接
// 不成立、整段跳过 —— 实测 [DEPOT] 一直 blocked=atCap、hunt=0，猎场永远没有仓库。
//
// 现在不设名额。**不会因此铺满地图**：每个建仓点都有距离门槛
// （猎物群 > 6 格、其余资源 > 20 格），一座建下去那个方向的距离就掉到门槛以下，
// 不会重复触发；再加上 120 木的门槛与 100 帧的建造节流，天然自限。
//
// 猎物群清完之后、在尸体堆旁建仓库的门槛（距离平方，即 6 格）。
//
// 比通用资源那条 20 格门槛（400）低得多。两条门槛的性质完全不同：
//   · 通用那条是【经济取舍】——「这座资源够远，多建一座仓库才划算」；
//   · 这条是【固定流程】——「这群猎物清完了，就在这里收尸」，不该被距离否掉。
// 只要不是紧贴着已有的交付建筑（再建一座纯属浪费）就该建。
static const int USR_HUNT_DEPOT_MIN_DIS2 = 36;
// 出兵建筑朝前线方向偏移的距离（格）。
// 文档《快速升级和取得胜利》第 75 行：「建设靶场的时候也要考虑尽量往地图中间
// 建，兵造出来很快就能加入战斗」。取 10：比箭塔近圈（6 格）远、比远圈（12 格）
// 近 —— 既明显往前推了一截，又还没跑出箭塔覆盖圈，出兵建筑不会一建成就成孤岛。
static const int USR_FORWARD_BUILD_RADIUS = 10;
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
// 农民目标人数。来自策略文档《快速升级和取得胜利》第 3 行：
// 「村民总人数控制在 20 个左右」。
// 原为工具时代 14 / 青铜及以后 12 —— 那两个值是刻意压低到「刚好不吃掉升时代
// 所需 800 食物」的结果，理由写在 ManageWeightedProduction 的使用处。
// 改成统一 20 会把这个矛盾重新引回来，属于有意接受的取舍：
// 文档的路线靠「不种田、改采第二浆果堆/羚羊群/大象」拉高食物产出，
// 而我们目前仍是种田模型，所以这一条要实测确认升时代有没有被拖后。
static const int USR_FARMER_TARGET = 20;
// 人口生产总数：各兵种配额之和（含农民与祭司）控制在 50，与人口硬上限一致
// （Development.h:134 的 humanNum_Top = 50；实际 Human_MaxNum =
//  min((房屋数 + 中心) × 4, 50)，见 Development.h:63/65/78 与
//  get_homeNum 把市镇中心也算作一处住房）。
//
// 【为什么主力配额要按总数反推】写死的话，任何一项调整都会让总和悄悄超过 50，
// 而超出人口上限的部分永远产不出来 —— 表现为「配额明明没满，却一直不出兵」。
// 实测就是这么把侦察兵挤掉的：20 农民 + 5 弓兵 + 25 战车弓兵 + 2 战车 + 2 侦察
// = 54，另外还有「敌方有阔剑兵 +6」和「科技完成后 =30」两条上调路径，
// 最高能到 65，而生产顺序决定了排在后面的兵种先被饿死。
static const int USR_POP_TARGET = 50;
// 祭司固定占 1 个人口，不可补充，算总数时先扣掉。
static const int USR_PRIEST_POP = 1;

// ── 战车弓兵出厂后散开 ──────────────────────────────────────────────
// 落点环绕市镇中心，按出厂顺序轮流分配。半径取 4 / 7 / 10 三圈 —— 夹在箭塔的
// 三圈（6 / 9 / 12 格，见 GetBuildCandidate 的 TOWER_RINGS）之间，既离开靶场
// 出口，又不出防守圈。每圈 8 个方向，共 24 个互不相同的落点。
static const int USR_SPREAD_RINGS[3][8][2] = {
    {{4, 0}, {3, 3}, {0, 4}, {-3, 3}, {-4, 0}, {-3, -3}, {0, -4}, {3, -3}},
    {{7, 0}, {5, 5}, {0, 7}, {-5, 5}, {-7, 0}, {-5, -5}, {0, -7}, {5, -5}},
    {{10, 0}, {7, 7}, {0, 10}, {-7, 7}, {-10, 0}, {-7, -7}, {0, -10}, {7, -7}},
};
static const int USR_SPREAD_SLOT_COUNT = 24;
// 距靶场多少格以内算「挤在靶场周边」，需要散开。
static const int USR_SPREAD_TRIGGER_RADIUS = 5;
// 距落点多少格以内算「已就位」。
static const int USR_SPREAD_ARRIVED_RADIUS = 2;
// 落点没变、且单位自上次下发后一直没挪窝，超过这么多帧才重发一次。
// 不能定期无条件重发：HumanMove 经 Core_List::addRelation 会先调 suspendRelation
// 清空移动路径（Core_List.cpp:450-469），定期重发等于每次刚起步就把路径清掉。
static const int USR_SPREAD_STUCK_FRAMES = 150;

// 一个战车弓兵当前的散开指令。
struct ChariotSpreadOrder {
    int targetDR;  // 分配的落点（格）
    int targetUR;
    int orderFrame;  // 下发该指令的帧
    int fromDR;      // 下发时单位所在的格，用于「卡住」判定
    int fromUR;
};
static map<int, ChariotSpreadOrder> chariotSpreadOrders;
// 散开槽位游标：只增不减，对 USR_SPREAD_SLOT_COUNT 取模。
static int nextChariotSpreadSlot = 0;

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
// 侦察骑兵的分工由【当前帧号】决定（g_frame >= USR_SCOUT_RECON_FRAME 即转为
// 专责侦测），不再用一张 SN→角色 的表：原先那张表是「首次看到时判定、之后
// 不再改变」，而侦察兵出厂时帧号必然还没到 RECON_FRAME，于是它们永远是
// 谨慎型、视野永远推不出去（详见 DispatchScouts 里的说明）。
// 原来的 map<int,bool> scoutIsRecon 已删除。
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
// 最近一次看到的敌方对象所在格（-1 表示从未看到），由 TrackEnemyContact 每帧更新。
// 与上面 enemyBase* 的区别：后者只在 UpdateEnemyBaseDiscovery 里写，那是为
// 「进攻选目标」服务的（记的是敌方【建筑】，且要等侦察到基地才有值）；
// 这里记的是更宽泛的「最近一次看到敌人在哪」，供建筑选址判断前线方向使用，
// 敌方建筑与敌方部队都算。两者用途不同，不要合并。
// 定义在这里而不是靠近使用点，是因为新对局重置块（ProcessPendingGatherOrders）
// 在这之前，要能清掉它们。
static int lastEnemyBlockDR = -1;
static int lastEnemyBlockUR = -1;
// 本局累计见过的敌方部队 SN（用于判断「敌人是不是打光了」，见
// EnemyArmyStillExists）。声明在这里是为了让新对局重置块能清它。
static set<int> enemyArmySeenSN;
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
// 定义在 GetBuildCandidate 之后，但后者要用它（出兵建筑朝前线的那条分支）。
static pair<int, int> GetBuildCandidateNear(int anchorDR, int anchorUR,
                                            int buildingType);
// 定义在 TryRepairDamagedBuilding 一带，但 TryAssignIdleFarmer 要用它。
static void LogIdleFarmerStuck(const tagFarmer &farmer, int desiredBucket,
                               const int target[4], const int assigned[4]);
// 定义在 ManageOffensiveArmy 之前，但 ManageStandoff 要用它。
static bool IsOffensiveArmy(const tagArmy &army);
// 定义在 ManageStandoff 一带，但 ManagePriest 要用它们
// （「敌人打光了就去转换攻城武器厂」那一段）。
// 【两个判据的分工，不要混用】
//   HasVisibleEnemyArmy()  —— 此刻视野里有没有敌兵（眼前的危险/该不该接敌）
//   EnemyArmyStillExists() —— 按敌方开局总量 51 判断「是不是真打光了」
// 祭司的出门/守家看后者（要等敌人真的清空才去换基地），
// 总攻与站桩仍看前者（有可见敌兵就先打兵）。
static bool HasVisibleEnemyArmy();
static bool EnemyArmyStillExists();
static const tagBuilding *FindEnemySiegeBuilding();
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
// 木材加工链的研发进度计数（两级：木材加工 @工具时代 → 工艺 @青铜时代）。
//
// 【为什么要计数】两级共用同一个 action 编号（Development.cpp:659-690 的
// actCon[BUILDING_MARKET_WOOD_UPGRADE] 下挂两个节点），所以从市场的 Project
// 上只能看到「有木材研发在跑」，分不清是第一级还是第二级。
// 只能数「市场进入木材研发」的次数：进入两次 = 两级都跑过了。
//
// 【用途】给车轮加一道硬门控（见 ManageMarketResearch）：木材两级跑完之前
// 不允许车轮插队。这是必要的，因为市场研发的失败冷却会让顺位颠倒：
// 工具时代木材二级（工艺是青铜科技）必然被 Core 拒绝 → 木材槽位冷却 600 帧；
// 如果这时刚好升入青铜，木材还在冷却、车轮不在，车轮就会先被研发。
// 声明在这里是为了让新对局重置块能清它们。
static int woodResearchSeenCount = 0;
static bool woodResearchRunning = false;
// 市场研发的下发状态（见 ManageMarketResearch）：
// marketCooldownUntil[i] 是 kMarketResearch[i] 的下次可尝试帧。失败只冷却、
// 不永久跳过 —— 原因见 USR_MARKET_RETRY_COOLDOWN 的说明。
static int marketCooldownUntil[3] = {0, 0, 0};
static int marketOrderId = -1;
static int marketOrderSlot = -1;
static int marketOrderFrame = USR_INVALID_FRAME;
// 靶场生产订单：靶场 SN → 订单 ID。
//
// 【为什么是 map 而不是单个 int】多靶场之后，每座靶场各有一张在途订单，
// 回执必须逐项回收。原先用单个 chariotArcherOrderId 只能记住最后一张 ——
// 多靶场下会把前几座的回执漏掉（那些失败就完全静默了）。
static map<int, int> rangeProduceOrder;
// 最近一次战车弓兵生产指令的回执码（0=成功；12=LOCK 表示前置未满足）。仅诊断用。
static int chariotArcherRet = -999;
// 本帧已经被下过生产指令的靶场 SN。
//
// 【为什么需要】普通弓兵与战车弓兵都用靶场，而 Core::deduplicateInstructions
// （Core.cpp:1221-1237）按 cur.SN 去重、只保留【最后】一条 —— 两者同帧落到
// 同一座靶场上时，先下的那条会被静默吞掉（实测弓兵配额就是在战车弓兵被
// noWheel 挡住的那段时间才凑齐的）。所以本帧已占用的靶场要跳过。
// 由 ManageWeightedProduction 在每帧开头重置。
static set<int> rangeOrderedThisFrame;

// 这座靶场本帧能否接单：必须完工、空闲，且本帧还没被下过指令。
static bool IsRangeAvailableForOrder(const tagBuilding &building)
{
    return building.Type == BUILDING_RANGE && building.Blood > 0 &&
           building.Percent >= 100 && building.Project == ACT_NULL &&
           rangeOrderedThisFrame.find(building.SN) == rangeOrderedThisFrame.end();
}

// 当前可接单的靶场数量，仅用于诊断日志。
// 「靶场建了 3 座但只有 1 座在干活」这类故障从别的字段看不出来。
static int CountAvailableRanges()
{
    int count = 0;
    for (const tagBuilding &building : info.buildings)
        if (IsRangeAvailableForOrder(building))
            ++count;
    return count;
}
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

// ── 已知占位图与「工作目标可抵达」判定 ──────────────────────────────
//
// 【为什么要自己重建一份占位图】引擎在寻路前会把【任何对象】所在的格子都记成
// 障碍（Map::loadBarrierMap_ByObjectMap 对 CanCrush != 0 的对象一律
// barrierMap[x][y] = 1），然后 Core_List.cpp:2071-2093 判断目标格的四个正交
// 邻居：全是障碍就直接返回 nullPath，单位停 50 帧后【整个移动指令被取消】
// （Core_List.cpp:1828-1837，判据见 Core_CondiFunc.cpp:586-592）。
// 也就是说目标附近站满了人，命令等于白下 —— 这正是策略文档里说的
// 「位置附近站满人则认为不可达，发命令人不动」。
//
// tagInfo 不导出内核的占用栅格（IsBuildCandidateUsable 里也写着同一句），
// 所以只能按【可见对象】的位置自己重建一份保守副本。
//
// 建筑占地边长（格）。tagObj 只有 BlockDR/BlockUR，没有 get_BlockSizeLen，
// 这里按已知类型做保守估计。三处用到它的地方（本函数、IsBuildCandidateUsable、
// IsScoutFrontierUsable）必须保持一致，所以提出来共用。
static int BuildingBlockSize(int type)
{
    return (type == BUILDING_HOME || type == BUILDING_ARROWTOWER) ? 2 : 3;
}

static vector<vector<unsigned char> > knownOccupied;
static int knownOccupiedFrame = USR_INVALID_FRAME;

// ── 猎物群的状态（每帧惰性重算一次，定义与理由见 USR_HUNT_CLUSTER_RADIUS）──
// 声明在这里而不是靠近使用点：新对局重置块（ProcessPendingGatherOrders）
// 在那些函数之前，需要能清掉它们。
struct GazelleCluster {
    vector<int> members;  // 本群所有瞪羚的 SN（含已死的尸体）
    int aliveCount;       // 其中还活着的（Blood > 0）；为 0 表示已清干净
    int centerDR;         // 质心（格），建仓库时的锚点
    int centerUR;
};
static map<int, int> gazelleClusterOf;              // 瞪羚 SN → 群编号
static map<int, GazelleCluster> gazelleClusters;    // 群编号 → 群信息
static set<int> aliveGazelleSN;                     // 当前活着的瞪羚 SN
static int gazelleClusterFrame = USR_INVALID_FRAME;
// 猎手被「从尸体拉回活瞪羚」的最近一次帧。见 KeepHuntersOnLiveGazelles：
// 那里要防止指令生效前的一两帧里连发多次、把刚建立的攻击关系打断。
static const int USR_HUNT_REDIRECT_INTERVAL = 60;
static map<int, int> hunterRedirectFrame;

static void MarkOccupied(int blockDR, int blockUR, int size)
{
    for (int d = blockDR; d < blockDR + size; ++d)
    {
        for (int u = blockUR; u < blockUR + size; ++u)
        {
            if (d >= 0 && u >= 0 && d < MAP_L && u < MAP_U)
                knownOccupied[d][u] = 1;
        }
    }
}

// 每帧重建一次，同一帧内重复调用直接复用。
static void RebuildKnownOccupied()
{
    if (knownOccupiedFrame == g_frame && !knownOccupied.empty())
        return;
    knownOccupiedFrame = g_frame;
    knownOccupied.assign(MAP_L, vector<unsigned char>(MAP_U, 0));

    for (const tagBuilding &building : info.buildings)
        if (building.Blood > 0)
            MarkOccupied(building.BlockDR, building.BlockUR,
                         BuildingBlockSize(building.Type));
    for (const tagBuilding &building : info.enemy_buildings)
        if (building.Blood > 0)
            MarkOccupied(building.BlockDR, building.BlockUR,
                         BuildingBlockSize(building.Type));
    // 资源：树 / 石头 / 金矿 / 猎物在引擎里都算障碍（CanCrush 返回 1）。
    // 但【灌木丛与鱼群不算】—— Map::CanCrush（Map.cpp:1639-1643）对
    // NUM_STATICRES_Bush / NUM_STATICRES_Fish 直接返回 0，它们根本不进
    // barrierMap。把它们标成障碍的后果很重：浆果丛成簇分布，一株被另外四株
    // 围住就永远判为不可达，而那恰好是农民默认要去的地方。
    for (const tagResource &resource : info.resources)
    {
        if (resource.Blood <= 0)
            continue;
        if (resource.Type == RESOURCE_BUSH || resource.Type == RESOURCE_FISH)
            continue;
        MarkOccupied(resource.BlockDR, resource.BlockUR, 1);
    }

    // 【刻意不统计任何单位】农民、我方部队、可见的敌方单位统统不进这张图。
    //
    // 这张图是用来判断「这个目标【结构上】能不能到达」的，而单位是会动的。
    // 把瞬时占位当成永久障碍会制造一个正反馈死循环：
    //   农民站在资源旁采集 → 该资源的邻居被标记 → 对下一个空闲农民不可达
    //   → 他找不到活干、站在原地 → 又多占一格障碍 → 能选的资源更少。
    // 实测症状就是「一群农民扎堆站着什么也不干」，把 IsReachableAround
    // 停用后立即恢复正常（A/B 已确认）。
    //
    // 「目标当前挤满了人」这件事有两个更合适的判据在管：
    // IsFarmerClusterCrowded（目标 3×3 邻域内 ≥3 个农民）与
    // workers >= ResourceHardCapacity。占位图不该重复承担这件事。
}

// 目标（占地 size×size，左上角 (blockDR, blockUR)）周围是否至少有一格能站人。
//
// 【只对「纯坐标移动」有效，不要用在采集/攻击上】
// 引擎的判据是 Core_List.cpp:2071-2093：目标格的四个正交邻居全是障碍时直接
// 返回 nullPath，单位停 50 帧后整个移动指令被取消。但下一行紧接着限定
// （:2091-2093 的注释原文）：
//     「纯坐标移动可以直接判定封闭终点；对象目标可能允许远程攻击或隔格工作，
//       不能在这里提前否决。」
//     if (isNoPath && goalOb == NULL && !(start == destination))
// 也就是只有 HumanMove（goalOb == NULL）才走这条否决；HumanAction 采集/攻击
// 一个对象时引擎会正常寻路，甚至允许单位进入目标格。
//
// 所以本函数只该用在 HumanMove 的落点上（例如 SpreadChariotArchers 的槽位）。
// 曾经把它套到采集目标上，造成过两次故障：浆果丛成簇、瞪羚尸体成簇时，
// 四个邻居全被同类占着，整片食物源被判成不可达 —— 表现为「有资源却没人采」。
//
// 判据：对占地的每一格检查四个正交邻居，只要有一个空的就算可达。
// 比引擎多判一条「邻居必须是陆地」—— 引擎那个判据看的是障碍图、不含地形，
// 四个邻居全是海时它照样认为有路。未探明的邻居按「可通行」算（乐观）。
static bool IsReachableAround(int blockDR, int blockUR, int size)
{
    static const int OFFSETS[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    RebuildKnownOccupied();

    for (int d = blockDR; d < blockDR + size; ++d)
    {
        for (int u = blockUR; u < blockUR + size; ++u)
        {
            for (int k = 0; k < 4; ++k)
            {
                const int nr = d + OFFSETS[k][0];
                const int nu = u + OFFSETS[k][1];
                if (nr < 0 || nu < 0 || nr >= MAP_L || nu >= MAP_U)
                    continue;
                if (knownOccupied[nr][nu])
                    continue;
                if (info.theMap &&
                    (*info.theMap)[nr][nu].type == MAPPATTERN_OCEAN)
                    continue;
                return true;
            }
        }
    }
    return false;
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

// 【已删除】FindEnemySiege(const tagArmy&)
// 它只找「12 格以内、且附近 2 格没有敌人」的敌方攻城武器厂。
// 它的唯一调用点在 ManagePriest 一个判据自相矛盾的块里，任何帧都执行不到，
// 所以祭司从来没有下发过一次转换攻城厂的指令（实测整局 0 次）。
// 现在转换由 ManagePriest 开头的「没有敌人 → 去转换敌方攻城武器厂」一段负责：
// 它用 FindEnemySiegeBuilding() 找厂（不限距离，靠走过去），
// 走进 USR_PRIEST_SIEGE_APPROACH_RADIUS(10) 格后直接 HumanAction。
// 距离上限与「附近没有敌人」两个条件都被新实现覆盖（后者由
// !HasVisibleEnemyArmy() 覆盖，比原来「2 格内没敌人」更严），故不再保留旧函数。

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

// 「最初视野边缘」的排序锚点（市镇中心格），由 CapturePriestLap 设置。
static int priestLapHomeDR = 0;
static int priestLapHomeUR = 0;

// 按相对市镇中心的极角从小到大排序的比较器。
// 不用 atan2：先按「上半平面（含正 x 轴）」分半区，同半区内用叉积定序，
// 叉积为 0（同一条射线上）时近的排前面。这样得到的是一个规整的顺时针绕圈。
static bool PriestLapAngleLess(const pair<int, int> &a, const pair<int, int> &b)
{
    const int ax = a.first - priestLapHomeDR;
    const int ay = a.second - priestLapHomeUR;
    const int bx = b.first - priestLapHomeDR;
    const int by = b.second - priestLapHomeUR;

    const bool aUpper = (ay > 0) || (ay == 0 && ax > 0);
    const bool bUpper = (by > 0) || (by == 0 && bx > 0);
    if (aUpper != bUpper)
        return aUpper;

    const int cross = ax * by - ay * bx;
    if (cross != 0)
        return cross > 0;
    return ax * ax + ay * ay < bx * bx + by * by;
}

// 抓取「最初视野的边缘」：当前已探明陆地格中，邻接未探明格的那些
// （判定复用侦察兵那套 IsExplorationFrontierBlock）。
//
// 只在第一次能算出非空集合时抓一次，之后 priestLapPoints 固定不变 ——
// 这正是与原先「动态前沿」的关键区别：动态前沿会随已探明区域外扩而不断更新，
// 永远能给祭司找出下一个点，于是探路永远不会结束（见常量处的说明）。
static void CapturePriestLap(const tagBuilding &home)
{
    if (priestLapCaptured)
        return;

    // 水域的判定依据：Core::updateCommon 的 GetTerrainType（Core.cpp:632-645）
    // 只把 MAPTYPE_OCEAN 映射成 MAPPATTERN_OCEAN，其余地形一律 MAPPATTERN_GRASS，
    // 且 playerMap 只对 explored 的格更新（Core.cpp:664-668）。
    // 所以 AI 的地图里只有三种值：UNKNOWN / GRASS / OCEAN，
    // MAPPATTERN_SHOAL 与 MAPPATTERN_DESERT 永远不会出现。
    static const int NEIGHBORS8[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0},
                                         {1, 0},   {-1, 1}, {0, 1},  {1, 1}};

    vector<pair<int, int> > points;     // 去掉水边之后的边界点
    vector<pair<int, int> > rawPoints;  // 未过滤的边界点，仅作兜底
    if (!info.theMap)
        return;
    for (int dr = 0; dr < MAP_L; ++dr)
    {
        for (int ur = 0; ur < MAP_U; ++ur)
        {
            if (!IsExplorationFrontierBlock(dr, ur))
                continue;
            // 只收 USR_PRIEST_EXPLORE_RADIUS 以内的前沿点。这是「探到半径
            // 就收工」的判据 —— 探完这一圈之后前沿会外扩，但一旦最近的都落到
            // 半径之外，集合就是空的，调用方据此结束探路。
            if (BlockDis2(home.BlockDR, home.BlockUR, dr, ur) >
                USR_PRIEST_EXPLORE_RADIUS * USR_PRIEST_EXPLORE_RADIUS)
                continue;
            rawPoints.push_back(make_pair(dr, ur));

            // 排除贴着已知水域的格。视野边缘有一大截就是海岸线，那些格本身是
            // 陆地，但它们的未探明邻居往往正是水 —— 走过去就是站在水边，
            // 再往外一步就下水了。八邻域里只要有一个已知 OCEAN 就整格丢掉。
            bool coastal = false;
            for (int k = 0; k < 8; ++k)
            {
                const int nr = dr + NEIGHBORS8[k][0];
                const int nu = ur + NEIGHBORS8[k][1];
                if (nr < 0 || nu < 0 || nr >= MAP_L || nu >= MAP_U)
                    continue;
                if ((*info.theMap)[nr][nu].type == MAPPATTERN_OCEAN)
                {
                    coastal = true;
                    break;
                }
            }
            if (!coastal)
                points.push_back(make_pair(dr, ur));
        }
    }
    // 整条边界都贴着水（基地建在海边）时不能就这么放弃 —— 那会让祭司永远停在
    // 探路分支里，正是这次要修的问题。退回未过滤的集合：宁可站在水边把这一圈
    // 走完，也不能不结束。
    const int rawCount = (int)rawPoints.size();  // 诊断用：过滤前的边界点数
    bool usedRawFallback = false;
    if (points.empty() && !rawPoints.empty())
    {
        points.swap(rawPoints);
        usedRawFallback = true;
    }
    // 地图还没建立好（theMap 为空或整张 UNKNOWN）时两个集合都为空，下一帧再试。
    if (points.empty())
        return;

    priestLapHomeDR = home.BlockDR;
    priestLapHomeUR = home.BlockUR;
    sort(points.begin(), points.end(), PriestLapAngleLess);

    // 逐格走完整条边界要很久，均匀抽稀到 USR_PRIEST_LAP_MAX_POINTS 个点，
    // 保证这一圈是有限且可控的。
    if ((int)points.size() > USR_PRIEST_LAP_MAX_POINTS)
    {
        const int stride = ((int)points.size() + USR_PRIEST_LAP_MAX_POINTS - 1) /
                           USR_PRIEST_LAP_MAX_POINTS;
        vector<pair<int, int> > sampled;
        for (size_t i = 0; i < points.size(); i += stride)
            sampled.push_back(points[i]);
        points.swap(sampled);
    }

    priestLapPoints.swap(points);
    priestLapIndex = 0;
    priestLapCaptured = true;

    char buf[224];
    snprintf(buf, sizeof(buf),
             "[PRIEST-LAP] f=%d captured=%d raw=%d raw_fallback=%d home=(%d,%d)",
             g_frame, (int)priestLapPoints.size(), rawCount,
             (int)usedRawFallback, home.BlockDR, home.BlockUR);
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
      // 靶场用计数而不是 HasBuilding：靶场目标改成 3 座之后（USR_RANGE_TARGET），
      // HasBuilding 只判有无，第 2、3 座的钱就不再被加权保护，
      // 木头会一直达不到 150+150 的门槛。
      if (!HasBuilding(BUILDING_MARKET) || !HasBuilding(BUILDING_STABLE) ||
          CountBuilding(BUILDING_RANGE) < USR_RANGE_TARGET ||
          !HasBuilding(BUILDING_ARMYCAMP))
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

// 农民「卡住」判定的帧数：NowState == WALKING 但位置连续这么多帧没变，
// 就认为它已经走不动了（路径失效 / 被建筑或人群堵死 / 指令被取消但 DR0 残留）。
//
// 【为什么必须有这条判据】Core 是这样算 NowState 的（Core.cpp 的 updateState）：
//     WorkObjectSN != -1  →  WORKING / ATTACKING
//     WorkObjectSN == -1  →  (DR0,UR0) != (DR,UR) ? WALKING : IDLE
// 也就是说 WALKING 的含义是「有一个没走到的目的地」，而【不是】「正在移动」。
// 一个已经走不动的农民会永远停在 WALKING 上，于是两条派工路径都会跳过它：
//     · TryAssignIdleFarmer       只处理 IDLE
//     · TryRepairDamagedBuilding  只接受 IDLE / WORKING
// 结果就是它永久僵在原地什么也不干 —— 实测症状：6 个农民聚在一座箭塔旁，
// 既不采集、也不修那座塔。
//
// 阈值取 150 帧（6 秒）：引擎的碰撞等待最长 0~49 帧（Core_List.cpp:1893 的
// rand()%50），正常行进一格约 11 帧，150 帧不动必然是卡死而不是在挪动。
static const int USR_FARMER_STUCK_FRAMES = 150;

// 农民 SN → (上次记录到的格, 该格的记录帧)。
struct FarmerWatch {
    int dr;
    int ur;
    int frame;
};
static map<int, FarmerWatch> farmerWatch;

// 每帧记录一遍所有农民的格。必须在任何读取 IsFarmerStuckWalking 的逻辑之前调用，
// 否则位置跟踪会滞后、把正常行进的农民误判成卡死。
static void UpdateFarmerWatch()
{
    set<int> live;
    for (const tagFarmer &farmer : info.farmers)
    {
        if (farmer.Blood <= 0 || farmer.FarmerSort != FARMERTYPE_FARMER)
            continue;
        live.insert(farmer.SN);
        map<int, FarmerWatch>::iterator it = farmerWatch.find(farmer.SN);
        if (it != farmerWatch.end() && it->second.dr == farmer.BlockDR &&
            it->second.ur == farmer.BlockUR)
            continue; // 还停在同一格，保留原来的时间戳
        FarmerWatch watch;
        watch.dr = farmer.BlockDR;
        watch.ur = farmer.BlockUR;
        watch.frame = g_frame;
        farmerWatch[farmer.SN] = watch;
    }
    // 清理已消失的农民，避免 map 无限增长、SN 复用后误判。
    for (map<int, FarmerWatch>::iterator it = farmerWatch.begin();
         it != farmerWatch.end();)
    {
        if (live.find(it->first) == live.end())
            it = farmerWatch.erase(it);
        else
            ++it;
    }
}

// 这个农民是不是「有目的地但已经走不动了」。
// 只对 NowState == WALKING 有意义 —— 状态由调用方判断，本函数只看位置。
static bool IsFarmerStuckWalking(const tagFarmer &farmer)
{
    map<int, FarmerWatch>::const_iterator it = farmerWatch.find(farmer.SN);
    return it != farmerWatch.end() &&
           g_frame - it->second.frame >= USR_FARMER_STUCK_FRAMES;
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

// 【诊断】「近处有闲置农田，农民却派了更远的目标」时记录一行。
//
// 光是读代码分不出成因，因为有三条互斥的路径都会造成这个观感，而它们各自
// 在打分之前就把农田拿掉了，从最终结果上完全看不出来：
//   (a) dB != 0 —— 这个农民本来就被配额派去别的桶（木头/石头），
//       农田压根没进入竞争。选桶只看 target[] - assigned[] 的缺口大小，
//       与距离无关；FindBestResourceSN 只在桶内部排序。
//   (b) 农田不可采 —— pct<100（还在施工）或 cnt=0（已采空）。
//       IsGatherableFarm 会直接跳过，且不留下任何痕迹。
//   (c) 农田被排除 —— busy=1（已有农民，Core 每帧重置地主所以只容一个）
//       或 crowd=1（落在 IsFarmerClusterCrowded 的 3×3 窗口里）。
//
// 只在「选中目标比最近的农田还远」时打印，并做节流，避免刷屏。
static void LogIdleFarmMiss(const tagFarmer &farmer, int desiredBucket,
                            int targetSN, const int target[4],
                            const int assigned[4])
{
    static int lastLogFrame = USR_INVALID_FRAME;
    if (lastLogFrame != USR_INVALID_FRAME && g_frame - lastLogFrame < 500)
        return;

    // 离这个农民最近的农田（不论状态，状态本身就是待查项）
    int farmSN = -1;
    int farmDis2 = 1000000000;
    for (const tagBuilding &building : info.buildings)
    {
        if (building.Type != BUILDING_FARM || building.Blood <= 0)
            continue;
        const int dis2 = BlockDis2(farmer.BlockDR, farmer.BlockUR,
                                   building.BlockDR, building.BlockUR);
        if (dis2 < farmDis2)
        {
            farmDis2 = dis2;
            farmSN = building.SN;
        }
    }
    if (farmSN < 0 || farmSN == targetSN)
        return;

    // 被选中的目标到农民的距离
    int chosenDis2 = 1000000000;
    for (const tagResource &resource : info.resources)
    {
        if (resource.SN == targetSN)
            chosenDis2 = BlockDis2(farmer.BlockDR, farmer.BlockUR,
                                   resource.BlockDR, resource.BlockUR);
    }
    for (const tagBuilding &building : info.buildings)
    {
        if (building.SN == targetSN)
            chosenDis2 = BlockDis2(farmer.BlockDR, farmer.BlockUR,
                                   building.BlockDR, building.BlockUR);
    }
    if (chosenDis2 <= farmDis2)
        return; // 选中的比最近的田还近，正常

    const tagBuilding *farm = nullptr;
    for (const tagBuilding &building : info.buildings)
    {
        if (building.SN == farmSN)
        {
            farm = &building;
            break;
        }
    }
    if (farm == nullptr)
        return;

    bool busy = false;
    for (const tagFarmer &other : info.farmers)
    {
        if (other.Blood > 0 && other.WorkObjectSN == farmSN)
        {
            busy = true;
            break;
        }
    }
    bool cool = false;
    map<int, ResourceAttemptState>::const_iterator attemptIt =
        resourceAttemptState.find(farmSN);
    if (attemptIt != resourceAttemptState.end() &&
        g_frame < attemptIt->second.cooldownUntilFrame)
        cool = true;
    const bool crowd =
        IsFarmerClusterCrowded(farm->BlockDR, farm->BlockUR, farmer.SN);

    lastLogFrame = g_frame;
    char buf[320];
    snprintf(buf, sizeof(buf),
             "[FARFARM] f=%d farmer=%d chosen=%d dChosen=%d dB=%d "
             "tgt=[%d,%d,%d,%d] asg=[%d,%d,%d,%d] | farm=%d dFarm=%d "
             "pct=%d cnt=%d busy=%d crowd=%d cool=%d",
             g_frame, farmer.SN, targetSN, chosenDis2, desiredBucket,
             target[0], target[1], target[2], target[3],
             assigned[0], assigned[1], assigned[2], assigned[3],
             farmSN, farmDis2, farm->Percent, farm->Cnt,
             (int)busy, (int)crowd, (int)cool);
    AiDebugLog(buf);
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
        // 木材链进度也要清零 —— 残留的话新对局里车轮的门控会立刻放行。
        woodResearchSeenCount = 0;
        woodResearchRunning = false;
        // 敌人 SN 是跨局复用的，累计集合不清会让新对局一开局就以为「全见过」。
        enemyArmySeenSN.clear();
        marketCooldownUntil[0] = 0;
        marketCooldownUntil[1] = 0;
        marketCooldownUntil[2] = 0;
        marketOrderId = -1;
        marketOrderSlot = -1;
        marketOrderFrame = USR_INVALID_FRAME;
        rangeProduceOrder.clear();
        rangeOrderedThisFrame.clear();
        chariotArcherRet = -999;
        lastEnemyBlockDR = -1;
        lastEnemyBlockUR = -1;
        // 帧号会随新对局回退，位置记录必须跟着失效，否则 lastFrame 是上一局的
        // 帧号，开局所有农民会被瞬间判成卡死。
        farmerWatch.clear();
        // 猎物群同样是按帧号缓存的，必须一并失效。
        gazelleClusterFrame = USR_INVALID_FRAME;
        gazelleClusterOf.clear();
        gazelleClusters.clear();
        aliveGazelleSN.clear();
        hunterRedirectFrame.clear();
        priestEmergencyTarget = make_pair(-1, -1);
        priestEmergencyTargetFrame = USR_INVALID_FRAME;
        priestFrontierTarget = make_pair(-1, -1);
        priestFrontierStuckCount = 0;
        priestWasConverting = false;
        priestRetreatUntilFrame = USR_INVALID_FRAME;
        priestMoveFromDR = -1;
        priestMoveFromUR = -1;
        priestExploreDone = false;
        priestLapPoints.clear();
        priestLapIndex = 0;
        priestLapCaptured = false;
        priestMoveLastRet = -999;
        priestMoveLastRetFrame = USR_INVALID_FRAME;
        scoutLuredTarget.clear();
        sacrificeOrderId = -1;
        sacrificeFarmerSN = -1;
        sacrificeHpBefore = -1;
        priestDecoyByThreat.clear();
        chariotSpreadOrders.clear();
        nextChariotSpreadSlot = 0;
        // 帧号会随新对局回退，占位图必须跟着失效，否则复用上一局的旧栅格。
        knownOccupiedFrame = USR_INVALID_FRAME;
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
//
// 【在建的也算数（原先写了 Percent < 100 就 skip，是个 bug）】
//
// 交付建筑一旦下单，它就是「这片资源有交付点」这个事实 —— 规划阶段不该当它
// 不存在。原来的过滤会造成重复建仓，因为盖一座仓库要几百帧，而选址每
// USR_BUILD_ORDER_INTERVAL(100) 帧就会重跑一次：
//
//   选锚点 → 下单(HumanBuild) → 订单几十帧内结算，只剩 100 帧节流挡着
//   → 1000 帧后重跑，那座位在建 → Percent<100 被跳过 → 同一个锚点的 dis2
//     纹丝不动 → 判定「还是太远」→ 在旁边找个空位再盖一座 → 循环
//
// 实测 [DEPOT]（同一锚点、同一个 dis2，而建成的数量 have 涨了 1）：
//   f=6211  dis2=317  have=2 anchor=(10,34)
//   f=7211  dis2=317  have=3 anchor=(10,34)
//   f=8211  dis2=866  have=3 anchor=(56,16)
//   f=9211  dis2=866  have=4 anchor=(56,16)
// 整局 have 2→3→4→5→6→7，猎场那几座甚至两两只隔 2~4 格。
//
// 另一条独立证据：CountBuilding() 只筛 Blood>0，所以它数得到新建的那座；
// 两个函数唯一的差别就是这个 Percent 过滤 —— 一个数得到、一个数不到，
// 正好锁死了病因。
// 唯一的代价：万一某座交付建筑中途没盖成，资源会显得比实际近一点、农民多走
// 一段。本 AI 不会主动放弃工地，风险可忽略。
static int FindNearestReturnBuildingDistance(int specialBuilding,
                                             int blockDR, int blockUR)
{
    int bestDis2 = 1000000000;
    for (const tagBuilding &building : info.buildings)
    {
        if (building.Blood <= 0)
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

// 某种资源该送去哪座交付建筑 —— 照抄引擎的 Resource::get_ReturnBuildingType()
// 与 Building::isMatchResourceType()，不要凭直觉写。
//
// 【引擎真值】（这几处是唯一的依据，改动前先回去核对）
//   StaticRes.cpp:41-44   NUM_STATICRES_Bush  → HUMAN_GRANARYFOOD
//   Building_Resource.cpp:41  BUILDING_FARM  → HUMAN_GRANARYFOOD
//   MainWidget.cpp:2450   AnimalResouceSort = {WOOD, STOCKFOOD, ...}
//                         → 瞪羚/大象/狮子尸体是 HUMAN_STOCKFOOD
//   Building.cpp:560-570  CENTER 什么都收；STOCK 收 WOOD/GOLD/STONE/STOCKFOOD；
//                         GRANARY 只收 GRANARYFOOD
//
// 【为什么必须按类型区分】浆果丛和农田一样是 GRANARYFOOD —— 仓库根本收不了它，
// 只能送谷仓或市镇中心。原先这里对所有野生资源一律用 BUILDING_STOCK，于是：
//   · 选点（FindBestResourceSN）按「到最近仓库/中心的距离」挑浆果丛，而农民实际
//     要跑去谷仓 —— 两个方向可以差出几十格，AI 以为近的、农民得横穿地图；
//   · 建仓（TryBuildReturnDepot）看到「浆果丛离仓库很远」就去浆果丛旁边建一座
//     仓库。仓库对浆果毫无用处，但 AI 自己的评分立刻变「近」了，是个自我强化的
//     假象 —— 这正是那些堆在野外的仓库的来源之一。
// 瞪羚尸体是 STOCKFOOD，用仓库是对的，不要一起改掉。
//
// CENTER 由 FindNearestReturnBuildingDistance 自己带上（它什么都收），这里不用管。
static int ReturnBuildingForResource(int resourceType)
{
    return resourceType == RESOURCE_BUSH ? BUILDING_GRANARY : BUILDING_STOCK;
}

// 每帧重建一次猎物群。单链聚类：距离 <= USR_HUNT_CLUSTER_RADIUS 的瞪羚
// 不断并入同一群。瞪羚数量很少（十几只），O(n²) 完全够用。
// 状态（gazelleCluster* 等）的声明在文件上方，因为新对局重置块在这之前要清它们。
static void RebuildGazelleClusters()
{
    if (gazelleClusterFrame == g_frame)
        return;
    gazelleClusterFrame = g_frame;
    gazelleClusterOf.clear();
    gazelleClusters.clear();
    aliveGazelleSN.clear();

    vector<const tagResource *> gazelles;
    for (const tagResource &resource : info.resources)
    {
        if (resource.Type != RESOURCE_GAZELLE)
            continue;
        gazelles.push_back(&resource);
        if (resource.Blood > 0)
            aliveGazelleSN.insert(resource.SN);
    }

    int nextId = 0;
    for (size_t i = 0; i < gazelles.size(); ++i)
    {
        if (gazelleClusterOf.count(gazelles[i]->SN))
            continue;

        const int id = nextId++;
        GazelleCluster cluster;
        cluster.aliveCount = 0;
        long long sumDR = 0;
        long long sumUR = 0;

        vector<size_t> stack;
        stack.push_back(i);
        gazelleClusterOf[gazelles[i]->SN] = id;
        while (!stack.empty())
        {
            const size_t cur = stack.back();
            stack.pop_back();

            cluster.members.push_back(gazelles[cur]->SN);
            if (gazelles[cur]->Blood > 0)
                ++cluster.aliveCount;
            sumDR += gazelles[cur]->BlockDR;
            sumUR += gazelles[cur]->BlockUR;

            for (size_t j = 0; j < gazelles.size(); ++j)
            {
                if (gazelleClusterOf.count(gazelles[j]->SN))
                    continue;
                if (max(abs(gazelles[cur]->BlockDR - gazelles[j]->BlockDR),
                        abs(gazelles[cur]->BlockUR - gazelles[j]->BlockUR)) >
                    USR_HUNT_CLUSTER_RADIUS)
                    continue;
                gazelleClusterOf[gazelles[j]->SN] = id;
                stack.push_back(j);
            }
        }

        const int n = static_cast<int>(cluster.members.size());
        cluster.centerDR = static_cast<int>(sumDR / n);
        cluster.centerUR = static_cast<int>(sumUR / n);
        gazelleClusters[id] = cluster;
    }
}

// 某个猎物群当前有几个农民在打【活着的】瞪羚。
// 只统计传入的 resourceWorkers（已在采 / 已在赶路的农民数），不额外扫描
// info.farmers —— 这个函数在选点的内层循环里被调用，必须便宜。
static int CountClusterHunters(int clusterId,
                               const map<int, int> &resourceWorkers)
{
    int hunters = 0;
    for (map<int, int>::const_iterator it = resourceWorkers.begin();
         it != resourceWorkers.end(); ++it)
    {
        if (it->second <= 0 ||
            aliveGazelleSN.find(it->first) == aliveGazelleSN.end())
            continue;
        map<int, int>::const_iterator clusterIt = gazelleClusterOf.find(it->first);
        if (clusterIt != gazelleClusterOf.end() &&
            clusterIt->second == clusterId)
            ++hunters;
    }
    return hunters;
}

static int FindBestResourceSN(const tagFarmer &farmer, int desiredBucket,
                              const int current[4])
{
    int bestSN = -1;
    int bestScore = 1000000000;
    map<int, int> resourceWorkers;

    // 猎物群每帧只重算一次（内部有帧号缓存），供下面的猎手配额判定使用。
    RebuildGazelleClusters();

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

        // 【猎物】分两个阶段，由「群里还有没有活的」决定：
        //   猎杀阶段（aliveCount > 0）：整群只派一个猎手；尸体一律不许采 ——
        //     先把这群清干净，否则采肉会把这个唯一的猎手拖住。
        //   采集阶段（aliveCount == 0）：尸体退化成普通食物资源点，
        //     交给上面的常规人数上限，多人一起采。
        if (resource.Type == RESOURCE_GAZELLE)
        {
            map<int, int>::const_iterator clusterIt =
                gazelleClusterOf.find(resource.SN);
            const bool inCluster = clusterIt != gazelleClusterOf.end();
            int aliveInCluster = 0;
            if (inCluster)
            {
                map<int, GazelleCluster>::const_iterator git =
                    gazelleClusters.find(clusterIt->second);
                if (git != gazelleClusters.end())
                    aliveInCluster = git->second.aliveCount;
            }

            if (resource.Blood > 0)
            {
                // 活瞪羚：猎杀目标，整群一个猎手。
                if (inCluster &&
                    CountClusterHunters(clusterIt->second, resourceWorkers) >=
                        USR_HUNT_HUNTERS_PER_CLUSTER)
                    continue;
            }
            else if (aliveInCluster > 0)
            {
                // 尸体，但群还没清完 → 先别采。
                continue;
            }
        }

        if (IsFarmerClusterCrowded(resource.BlockDR, resource.BlockUR,
                                   farmer.SN))
            continue;

        // 【这里刻意不做「可抵达」判定】采集是把目标对象交给引擎的
        // （HumanAction → goalOb != NULL），而引擎对对象目标【豁免】了
        // 「四邻全被占就判不可达」那条规则 —— 见 Core_List.cpp:2091-2093 的注释：
        // 「纯坐标移动可以直接判定封闭终点；对象目标可能允许远程攻击或隔格工作，
        // 不能在这里提前否决。」引擎会正常寻路并允许单位走进目标格。
        // 所以这个判据在这里不成立：浆果丛成簇、瞪羚尸体成簇时，四个邻居全被
        // 同类占着，套上去会把整片食物源判成不可达 —— 实测症状就是
        // 「瞪羚尸体没人采集」。判据只对纯坐标移动有效，见 IsReachableAround。

        // 【已取消「农民↔资源」的距离项】按需求去掉：选点不再看农民离目标多远，
        // 只看「离交付建筑多近 + 存量大不大 + 挤不挤」。returnDistance 保留 ——
        // 那是资源自身的属性，与哪个农民去采无关。
        //
        // 副作用：原先 distance 是打分里唯一因人而异的一项，去掉之后同一时刻的
        // 多个空闲农民对同一个资源会算出同一个分数，分派退化为按固定顺序取用。
        // 错开靠两道硬闸：ResourceHardCapacity（食物 4 / 其它 3）与
        // IsFarmerClusterCrowded（目标 3×3 邻域内 ≥3 个农民就跳过）——
        // 注意原先还有一份 crowdPenalty 轻罚，但它是死代码（见下面的说明）已删，
        // 所以现在完全靠这两道硬闸。
        // 【交付建筑按资源类型选】浆果丛是 GRANARYFOOD、只能送谷仓，仓库收不了它；
        // 瞪羚尸体与树/石/金才走仓库。类型用错会让「AI 认为近、农民跑断腿」——
        // 详见 ReturnBuildingForResource 那张引擎真值表。
        const int returnDistance = FindNearestReturnBuildingDistance(
            ReturnBuildingForResource(resource.Type), resource.BlockDR,
            resource.BlockUR);
        // 【删除】原先这里还有 crowdPenalty（软上限内 workers*12，超过则
        // (workers-soft+1)*80）。它**从未生效过**：下面的 softCapacity 与上面
        // 1651 行的 ResourceHardCapacity(bucket) 是同一组数（食物 4 / 其它 3），
        // 而那一行已经在 workers >= 硬上限时 continue —— 能走到这里的 workers
        // 必然小于软上限，*80 那个分支永远不可达，实际只剩 workers*12，
        // 与距离平方（10 格 = 100）相比微不足道。
        // 注意不要「修复」它：把软上限调小于硬上限会让拥挤惩罚真的生效，把农民
        // 从近处挤到更远但更空的资源上，与「减少农民走远路」的目标正好相反。
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
        const int score = returnDistance + remainingPenalty + huntPenalty -
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

        // 【同资源循环，刻意不做「可抵达」判定】采农田同样是对象目标，
        // 引擎豁免了那条判据。理由见资源循环里的说明。

        // 【同资源循环，去掉「农民↔农田」的距离项】理由与后果见那里。
        const int returnDistance = FindNearestReturnBuildingDistance(
            BUILDING_GRANARY, building.BlockDR, building.BlockUR);
        // 【删除】原先这里还有 crowdPenalty，但它恒为 0：上面 `if (workers >= 1)
        // continue;`（一块田只容一个农民，Core 每帧重置地主，见 Building_Resource
        // ::nextframe 的 initGatherer）保证了走到这里的 workers 必然是 0，
        // 两档算出来都是 0，写进 score 等于没写。
        // 【取消】原先还有一项 remainingPenalty（Cnt < 100 时 +100）。它会造成
        // 一个不会自愈的陷阱：农田 Cnt 从 250 减到跌破 100 就挂上 +100，而 100
        // 恰好等于 distance² 在 10 格处的值 —— 近距离区间 distance² 只有几十，
        // 压不住这个常量，农民会放弃近处的半存量农田、跑去采远处的浆果丛。
        // 更糟的是被放弃的田没人采，Cnt 就停在低位，永远不跌破 is_Surplus 的
        // 0.5 阈值（Core.cpp:255），也就永远不会被删除 —— 一块田会永久挂在
        // +100 上闲置。实测症状正是「基地里有没人耕种的近处农田，农民去采浆果」。
        // 它想防的「采到一半资源消失、白跑一趟」其实已由引擎兜住：
        // Resource::is_Surplus 在 Cnt < 0.5 时直接把资源判死，不存在采不完的余量。
        // 注意：资源循环（浆果丛/瞪羚/树）里的同名惩罚保留，本次只动农田。
        const int score = returnDistance - (current[0] > 0 ? 0 : 2);
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
    const int buildSize = BuildingBlockSize(buildingType);
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
        const int objectSize = BuildingBlockSize(building.Type);
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
//
// 【注意，当前无人调用，不要直接拿去做建筑选址】上面那句「房屋与敌方方向的
// 夹角 > 90°」恰恰说明这个函数返回的方向精度有限：它返回的是房屋方向的【镜像】，
// 所以与真实敌方方向的夹角是「< 90°」—— 最坏可以偏到垂直。用来给箭塔选朝向
// 尚可（四方向均匀布置本就不太依赖方向），用来把一座建筑推出基地 10 格则是
// 拿一个可能垂直的方向下注。建筑选址改用实际看到的敌方位置，
// 见 lastEnemyBlockDR 与 GetBuildCandidate 里对应的分支。
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

// 记录最近一次敌方接触点（状态见文件上方 lastEnemyBlockDR 的声明处）：
// 敌方建筑优先（位置固定、不会追着农民跑），
// 没有建筑时退到敌方部队。每帧调用一次。
// 只在真的看到敌人时才覆盖 —— 「敌人从哪个方向来」这个信息在敌人离开视野后
// 仍然有意义，所以不做超时清除。
static void TrackEnemyContact()
{
    for (const tagBuilding &building : info.enemy_buildings)
    {
        if (building.Blood > 0)
        {
            lastEnemyBlockDR = building.BlockDR;
            lastEnemyBlockUR = building.BlockUR;
            return;
        }
    }
    for (const tagArmy &enemy : info.enemy_armies)
    {
        if (enemy.Blood > 0)
        {
            lastEnemyBlockDR = enemy.BlockDR;
            lastEnemyBlockUR = enemy.BlockUR;
            return;
        }
    }
}

// 是否为「出兵建筑」——这类建筑生产单位，建得离前线近一点，新兵出厂就能
// 投入战斗（文档第 75 行）。
//
// 只列靶场：它是本 AI 唯一真正量产的建筑（全部兵力都是战车弓兵）。
// 马厩虽然也出兵（2 个侦察骑兵），但它同时是升时代前置的第 3 票
// （市场 + 靶场 + 马厩 ≥ 2），盖一座要 1000 帧 —— 把它推到前线只会推迟升时代、
// 增加被拆风险，换不来任何收益。
// 兵营同理更不在列（兵种配额 armyTarget 是 0，一个兵都不产）；
// 房屋/农场服务于人口与采集，箭塔服务于防守，三者也都该继续围绕市镇中心。
static bool IsForwardBuildType(int buildingType)
{
    return buildingType == BUILDING_RANGE;
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

    // 出兵建筑：先试「朝最近一次看到的敌人方向推出去 USR_FORWARD_BUILD_RADIUS
    // 格」的位置，让新兵出厂就在前线附近（见 lastEnemyBlockDR 处对方向来源的说明）。
    //
    // 复用 GetBuildCandidateNear 在锚点周围 1~4 格找空位，避免自己再写一张偏移表。
    // 还没见过敌人、或前线附近放不下时，直接落到下面的通用策略 ——
    // 宁可建在中心附近，也不能因为前线没位置就不建（第 1 座靶场还是升时代前置，
    // 它必须建得出来）。
    if (IsForwardBuildType(buildingType) && lastEnemyBlockDR >= 0)
    {
        const int dx = lastEnemyBlockDR - center->BlockDR;
        const int dy = lastEnemyBlockUR - center->BlockUR;
        // 切比雪夫长度：保证锚点到中心的偏移恰好是 USR_FORWARD_BUILD_RADIUS 格，
        // 且 dx/dy 同为 0 时不会除零。溢出问题不存在 —— 地图只有 100×100。
        const int len = max(abs(dx), abs(dy));
        if (len > 0)
        {
            const int anchorDR =
                center->BlockDR + dx * USR_FORWARD_BUILD_RADIUS / len;
            const int anchorUR =
                center->BlockUR + dy * USR_FORWARD_BUILD_RADIUS / len;
            const pair<int, int> forward =
                GetBuildCandidateNear(anchorDR, anchorUR, buildingType);
            if (forward.first != -1)
                return forward;
        }
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
        if (farmer.FarmerSort != FARMERTYPE_FARMER || farmer.Blood <= 0)
            continue;
        // 正常只派空闲农民；此外把「有目的地但已经走不动」的僵尸农民也接过来 ——
        // 它们的 NowState 永远是 WALKING、回不到 IDLE，不救就是永久僵在原地。
        // 判据见 USR_FARMER_STUCK_FRAMES。
        if (farmer.NowState != HUMAN_STATE_IDLE &&
            !(farmer.NowState == HUMAN_STATE_WALKING &&
              IsFarmerStuckWalking(farmer)))
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
            // 第一轮：只找【还有配额缺口】的桶。
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
        {
            // 第二轮：【忽略配额】，只要那个桶还有可采资源就去。
            //
            // 【为什么必须有这一轮】配额是按权重算出来的，不代表那个桶实际能容纳
            // 多少人。食物桶最典型：野生浆果/猎物采光后只剩农场，而一块农场同一
            // 时刻只容一个农民（Core 每帧重置地主），于是配额要 11 个人、实际最多
            // 站 5 个。多出来的 6 个人既找不到食物目标，又因为木头配额恰好满了
            // 而被第一轮跳过 —— 只能站着不动。
            // 实测 [FARIDLE] 行：`dB=0 tgt=[11,9,0,0] asg=[5,9,0,0] res=[0,78,6,17]`
            // —— 食物桶可采资源是 0，而木头桶还有 78 棵树，却有 3~7 个农民闲置，
            // 同一局木材长期只有 11~40。这既是「村民站着不动」的原因，
            // 也是木材长期短缺的原因。
            for (int bucket = 0; bucket < 4 && targetSN < 0; bucket++)
            {
                if (bucket == desiredBucket)
                    continue;
                targetSN = FindBestResourceSN(farmer, bucket, assigned);
                if (targetSN >= 0)
                    desiredBucket = bucket;
            }
        }
        if (targetSN < 0)
        {
            LogIdleFarmerStuck(farmer, desiredBucket, target, assigned);
            continue;
        }

        LogIdleFarmMiss(farmer, desiredBucket, targetSN, target, assigned);

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
  // 【在途订单的回执不在这里处理】ManageEconomyAndProduction 开头有一个块
  // 专门消费回执、拿到就清 buildOrderId，而它每次调用本函数之前先跑 ——
  // 所以走到这里时 buildOrderId 只可能是「还没结算」。原先这里还写着一整套
  // 回执处理（[RESULT] 日志、buildFailCodes = 100 + ret、位置失败换候选点），
  // 那些代码【永远执行不到】，实测 [RESULT] 一条都没打过、[BUILD] 行的
  // failCode 恒为 0。已全部移到 ManageEconomyAndProduction 那个活着的块里。
  if (buildOrderId != -1) {
    buildFailCodes[typeIdx] = 1; // 上一个建造指令还没结算
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

    // ── 候选一：猎物群（优先，不参与下面的距离比较）────────────────────
    // 文档第 24 行：「要点是全部打死之后再建设仓库，选择离所有死羚羊位置最近
    // 的区域建设。否则有的村民会跑很远，效率不高。」
    //
    // 【为什么必须单独判定】原先这条和下面那条通用候选共用 `dis2 > worstReturnDis2`
    // 的「谁远谁赢」，于是猎物群永远抢不过远处的树/石头（实测日志里锚点全是
    // dis2=580~692 的资源，猎物群十几格的 dis2 根本进不了比较），
    // [DEPOT] 诊断的 hunt= 字段从头到尾都是 0。
    // 这两件事的性质本来就不同：通用那条是【经济取舍】（够远才值得多建一座），
    // 猎物群这条是【固定流程】（清完就在这里收尸）。所以让它直接胜出。
    //
    // 只在仓库还没建满时才优先，否则会永久堵死通用候选（清出来的群会一直存在，
    // 谷仓就再也建不了了）。
    bool fromHuntCluster = false;
    int huntDR = 0;
    int huntUR = 0;
    int huntDis2 = 0;
    RebuildGazelleClusters();
    {
        for (map<int, GazelleCluster>::const_iterator it =
                 gazelleClusters.begin();
             it != gazelleClusters.end(); ++it)
        {
            // 只对【已经清干净】的群建仓 —— 还没杀完时尸体还在陆续产生、
            // 位置也还在变，此时落点会偏。单只的群不算「猎场」，不单独建仓。
            if (it->second.aliveCount > 0 ||
                static_cast<int>(it->second.members.size()) < 2)
                continue;
            const int dis2 = FindNearestReturnBuildingDistance(
                BUILDING_STOCK, it->second.centerDR, it->second.centerUR);
            if (dis2 > USR_HUNT_DEPOT_MIN_DIS2 && dis2 > huntDis2)
            {
                huntDis2 = dis2;
                huntDR = it->second.centerDR;
                huntUR = it->second.centerUR;
                fromHuntCluster = true;
            }
        }
    }

    // ── 候选二：交付距离最远的资源 / 农田（经济取舍）──────────────────
    int worstBuildingType = -1;
    int worstReturnDis2 = 0;
    int anchorDR = 0, anchorUR = 0;
    if (fromHuntCluster)
    {
        worstBuildingType = BUILDING_STOCK;
        worstReturnDis2 = huntDis2;
        anchorDR = huntDR;
        anchorUR = huntUR;
    }
    else
    {
        for (const tagResource &resource : info.resources)
        {
            if (!IsGatherableResource(resource))
                continue;
            // 建哪座交付建筑由资源类型决定：浆果丛要的是【谷仓】。原先一律当成
            // 仓库，于是会在浆果丛旁边盖一座对它毫无用处的仓库，而 AI 自己的评分
            // 立刻变「近」、继续往那儿派人 —— 自我强化的假象，也是野外那些多余
            // 仓库的来源之一。详见 ReturnBuildingForResource 的引擎真值表。
            const int returnBuilding = ReturnBuildingForResource(resource.Type);
            const int dis2 = FindNearestReturnBuildingDistance(
                returnBuilding, resource.BlockDR, resource.BlockUR);
            if (dis2 > worstReturnDis2)
            {
                worstReturnDis2 = dis2;
                worstBuildingType = returnBuilding;
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
    }

    // 交付距离平方超过 400（约 20 格）才值得建仓。
    // 【名额限制已取消】见文件上方 USR_DEPOT_MAX 那段的说明。
    //
    // 【诊断】这条链上的每一道门原先都是静默 return，出了问题从日志上完全看不出
    // 是哪一道挡的（「为什么没建仓库」就属于这类）。这里统一收集原因，最后打一行。
    const char *blocked = NULL;
    if (worstBuildingType == -1)
        blocked = "noAnchor";       // 一个可采资源都没有
    else if (worstReturnDis2 <=
             (fromHuntCluster ? USR_HUNT_DEPOT_MIN_DIS2 : 400))
        // 猎物群用低门槛（见 USR_HUNT_DEPOT_MIN_DIS2），
        // 其余资源/农田用 20 格那条经济门槛。
        blocked = "tooClose";
    else if (info.Wood < 120)
        blocked = "noWood";

    pair<int, int> pos = make_pair(-1, -1);
    int farmerSN = -1;
    if (blocked == NULL)
    {
        // 注意：锚点本身就是一格资源（树/石头/猎物），而仓库要 3×3 空间。
        // 资源成簇时，锚点周围 1~4 格内可能全被同类占着，这里就会失败。
        pos = GetBuildCandidateNear(anchorDR, anchorUR, worstBuildingType);
        if (pos.first == -1)
        {
            blocked = "noSpot";
        }
        else
        {
            farmerSN = FindBuilderFarmerSN();
            if (farmerSN == -1)
                blocked = "noBuilder";
        }
    }

    if (blocked != NULL)
    {
        static int lastDepotLogFrame = USR_INVALID_FRAME;
        if (lastDepotLogFrame == USR_INVALID_FRAME ||
            g_frame - lastDepotLogFrame >= 1000)
        {
            lastDepotLogFrame = g_frame;
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "[DEPOT] f=%d blocked=%s type=%d dis2=%d wood=%d have=%d "
                     "hunt=%d anchor=(%d,%d)",
                     g_frame, blocked, worstBuildingType, worstReturnDis2,
                     info.Wood,
                     worstBuildingType >= 0
                         ? CountBuilding(worstBuildingType)
                         : -1,
                     (int)fromHuntCluster, anchorDR, anchorUR);
            AiDebugLog(buf);
        }
        return;
    }

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
// （状态 woodResearchSeenCount / woodResearchRunning 的声明在文件上方 ——
//   新对局重置块在这之前要清它们。）
static void TrackWoodResearch(const tagBuilding *market)
{
  if (market == nullptr)
    return;
  if (market->Project == BUILDING_MARKET_WOOD_UPGRADE)
  {
    if (!woodResearchRunning)
    {
      woodResearchRunning = true;
      ++woodResearchSeenCount;  // 每进入一次算一级
      {
        char buf[192];
        snprintf(buf, sizeof(buf), "[MARKET] f=%d wood research #%d started",
                 g_frame, woodResearchSeenCount);
        AiDebugLog(buf);
      }
    }
    return;
  }
  // 离开木材研发 → 允许下一次重新计数（下一级）
  woodResearchRunning = false;
}

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
    // 【诊断】判定这次下发失败（市场没进研发）。它会让该槽位冷却
    // USR_MARKET_RETRY_COOLDOWN，于是【排在第一位的木材加工一旦失败，
    // 接下来 600 帧里车轮就会顶上来】—— 这一行是还原研发顺序的关键。
    {
      char buf[192];
      snprintf(buf, sizeof(buf), "[MARKET] f=%d timeout slot=%d action=%d",
               g_frame, marketOrderSlot,
               (marketOrderSlot >= 0 && marketOrderSlot < 3)
                   ? kMarketResearch[marketOrderSlot]
                   : -1);
      AiDebugLog(buf);
    }
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
    // 【车轮必须排在木材两级之后】木材加工链是两级（木材加工 → 工艺），
    // 必须跑两次才算研发完。而上面的冷却机制会让顺位颠倒：工具时代木材二级
    // 必然被拒（工艺是青铜科技）→ 木材槽位冷却 600 帧；若此时正好升入青铜，
    // 木材还在冷却、车轮不在，车轮就抢先了（窗口约 120/720）。
    // 这里用 woodResearchSeenCount 硬挡：没跑满两级就不许车轮上。
    // 代价：木材链若因资源不足长期跑不动，车轮也会一起等 —— 但它俩抢的是
    // 同一批资源，先让木材（+50% 伐木）跑起来，车轮反而更快攒够。
    if (kMarketResearch[i] == BUILDING_MARKET_WHEEL_UPGRADE &&
        woodResearchSeenCount < 2)
      continue;
    marketOrderId = ai->BuildingAction(market->SN, kMarketResearch[i]);
    marketOrderSlot = i;
    marketOrderFrame = g_frame;
    // 【诊断】市场研发此前完全没有日志，所以「伐木是不是真的排在车轮前面」
    // 只能靠推理、无法验证。这一行把每次下发的槽位与 action 记下来，
    // 配合下面的超时行就能还原完整的研发顺序。
    // action：6 = 木材加工(→工艺) ，8 = 农田(→犁) ，10 = 车轮。
    {
      char buf[192];
      snprintf(buf, sizeof(buf), "[MARKET] f=%d issue slot=%d action=%d", g_frame,
               i, kMarketResearch[i]);
      AiDebugLog(buf);
    }
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

  // 挑一座本帧还没被占用的靶场。不能走 TryBuildingAction —— 它内部用
  // FindReadyBuildingByType，永远返回第一座，与战车弓兵撞在同一座靶场上时
  // 会被 Core 按 SN 去重吞掉一条（见 rangeOrderedThisFrame 的说明）。
  for (const tagBuilding &building : info.buildings)
  {
    if (!IsRangeAvailableForOrder(building))
      continue;
    ai->BuildingAction(building.SN, BUILDING_RANGE_CREATE_BOWMAN);
    rangeOrderedThisFrame.insert(building.SN);
    return true;
  }
  return false;
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
               "wheel=%d range=%d rangeReady=%d",
               g_frame, blockReason, info.Human_Num,
               (int)info.Human_MaxNum, (int)info.Meat, info.Wood,
               (int)wheelTechReady, CountBuilding(BUILDING_RANGE),
               CountAvailableRanges());
      AiDebugLog(buf);
    }
    return false;
  }

  // 逐项回收在途订单的回执：Core 对前置不满足（如车轮科技未完成）的订单会返回
  // ACTION_INVALID_BUILDACT_LOCK(12)。下发是发后不管的、看不到 Core 是否接受，
  // 所以这类失败是完全静默的 —— 这里把它暴露出来（chariotArcherRet 只保留
  // 最后一条，供诊断日志用；回执的价值在于会把条目从表里清掉、允许重试）。
  for (map<int, int>::iterator it = rangeProduceOrder.begin();
       it != rangeProduceOrder.end();)
  {
    map<int, int>::const_iterator result = info.ins_ret.find(it->second);
    if (result == info.ins_ret.end()) {
      ++it;
      continue;
    }
    chariotArcherRet = result->second;
    it = rangeProduceOrder.erase(it);
  }

  // 给【每一座】完工、空闲、本帧未被占用的靶场各下一单。
  //
  // 【为什么必须遍历全部】原实现用 FindReadyBuildingByType 取第一座 —— 那个
  // 函数在循环里 `return &building`，只返回 info.buildings 里的第一座。多靶场
  // 之后第 2、3 座会全程空闲：花了 150 木/座却不多产一个兵，比不建更糟。
  //
  // 【预算必须逐项递减】info.Meat/info.Wood 是当帧快照，不会因为下单而变化。
  // 不递减的话 3 座靶场会在同一帧各自按同一份资源判断、同时下单，后两单会被
  // Core 按实时资源以 ACTION_INVALID_RESOURCE 拒掉，白占指令通道。
  int budgetFood = static_cast<int>(info.Meat);
  int budgetWood = info.Wood;
  int ordered = 0;
  for (const tagBuilding &building : info.buildings)
  {
    if (!IsRangeAvailableForOrder(building))
      continue;
    if (budgetFood < 40 || budgetWood < 70)
      break;
    budgetFood -= 40;
    budgetWood -= 70;
    rangeProduceOrder[building.SN] =
        ai->BuildingAction(building.SN, BUILDING_RANGE_CREATE_CHARIOT_ARCHER);
    rangeOrderedThisFrame.insert(building.SN);
    ++ordered;
  }

  if (ordered == 0) {
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
  return true;
}
static bool TryProduceScout(UsrAI *ai, bool nearPopulationCap)
{
  // 【刻意忽略 nearPopulationCap】在这里它是错的判据：
  // ManageEconomyAndProduction 为了给侦察兵留位置，已经把人口上限压低了
  // scoutReserve —— 如果侦察兵自己也吃这个被压低的门槛，它就会被【自己的预留】
  // 挡住。实测就是因此一直造不出来（pop=50/50、scout=0 持续到最后）。
  // 改用硬判据：人口还有空位就允许，让预留出来的位置真正归它用。
  (void)nearPopulationCap;
  if (info.Human_Num >= info.Human_MaxNum || info.Meat < 100)
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

// 侦察骑兵的目标数量。
//
// 提成函数是因为有两个地方要用同一份判据：ManageWeightedProduction 用它定配额，
// ManageEconomyAndProduction 用它在人口上限里给侦察兵【预留位置】。
// 各写一遍的话，改了帧号常量只改一处就会静默失效。
static int ScoutTargetCount()
{
    if (g_frame >= USR_SCOUT_RECON_FRAME)
        return USR_SCOUT_TOTAL;
    if (g_frame >= USR_SCOUT_FIRST_FRAME)
        return 1;
    return 0;
}

static void ManageWeightedProduction(UsrAI *ai, bool nearPopulationCap) {
  // 重置「本帧已占用靶场」表。必须在任何生产函数之前 —— 弓兵与战车弓兵都用
  // 靶场，同帧落到同一座会被 Core::deduplicateInstructions 按 SN 去重吞掉一条。
  rangeOrderedThisFrame.clear();

  const int farmerCount = static_cast<int>(info.farmers.size());
  const int clubmanCount = CountArmyBySort(AT_CLUBMAN);
  const int bowmanCount = CountArmyBySort(AT_BOWMAN);
  const int scoutCount = CountArmyBySort(AT_SCOUT);
  const int hopliteCount = CountArmyBySort(AT_HOPLITE);
  const int broadswordCount = CountArmyBySort(AT_BROADSWORDSMAN);
  const int chariotCount = CountArmyBySort(AT_CHARIOT);
  const int chariotArcherCount = CountArmyBySort(AT_CHARIOT_ARCHER);

  // 每个人种的目标数量；后续可按敌方兵力或时代动态调整。
  //
  // 【农民目标 = 20，不分时代】见 USR_FARMER_TARGET 的说明。
  // 取代原先的「工具时代 14 / 青铜及以后 12」——那两个数是刻意压低的结果，
  // 理由如下，改回 20 等于把那个矛盾重新引回来，需要实测确认：
  //   每个农民 50 食物，18 个就是 900 食物，正好把升铜器所需的 800 食物吃掉，
  //   导致升时代推迟到第二波（SAT = 13500）之后，而那时食物清零、且按
  //   「升时代前只生产农民」的策略 army=0，完全无力防守。
  //   另一个方向：青铜后只给 5 个农民也撑不起开销（车轮要 100 木 + 150 食、
  //   25 个战车弓兵要 1750 木 + 1000 食），所以那一档后来提到 12。
  // 若实测升时代被明显拖后，回退方案：工具时代保持 14，升铜器后再提到 20。
  //
  // 这只是「是否继续补产」——已有农民不会被主动裁掉，只会随战损与
  // SacrificeExcessFarmers（人口顶到 50 后启动）逐步减少。
  //
  // 【35000 帧之后降到 5】见 USR_FARMER_LATE_FRAME 的说明：后期人口让给军队。
  const int farmerTarget = (g_frame >= USR_FARMER_LATE_FRAME)
                               ? USR_FARMER_LATE_TARGET
                               : USR_FARMER_TARGET;

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
    // 侦察骑兵：USR_SCOUT_FIRST_FRAME 后生产 1 个保命型（遇敌即撤回基地，
    // 不承担侦测）；USR_SCOUT_RECON_FRAME 后再补 1 个，由它专职侦测敌方基地
    // （见 DispatchScouts 的角色判定）。
    scoutTarget = ScoutTargetCount();
    // 战车（40 食 + 60 木）保留 2 个：速度 4.07，可作为前排挡一下远程。
    chariotTarget = 2;
    // 主力（战车弓兵，40 食 + 70 木，血70/攻4/射程7/速度4.07）的配额
    // = 人口总数减去上面所有非主力配额。见 USR_POP_TARGET 的说明：
    // 这样调整任何一项都不会让总和超过人口上限。
    // 当前算式：50 − 1(祭司) − 20(农民) − 5(弓兵) − 2(侦察) − 2(战车) = 20。
    chariotArcherTarget = max(0, USR_POP_TARGET - USR_PRIEST_POP -
                                     farmerTarget - bowmanTarget - scoutTarget -
                                     chariotTarget);
  }
  // 【已取消】原先这里还有两条上调主力配额的分支：
  //   · 「敌方出现阔剑兵 → 战车弓兵 +6」
  //   · 「关键科技完成 → 战车弓兵 = 30」
  // 它们都会让配额之和超过人口上限（最高到 65），而超出上限的部分永远产不出来，
  // 只会让排在后面的兵种（侦察兵）被饿死。兵种多了人口自然挤，
  // 真要针对阔剑兵，应该从别的兵种里挪配额，而不是往上加。
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
// 是否为「紧急修复目标」：这类建筑受损时允许从采集队里抽调农民去修，
// 不必等自然空闲。
//
// 只把箭塔算进来：它是防守的骨架，被打到重伤就等于基地火力直接掉一档，
// 值得为它打断采集。其余建筑（房屋、农场、市场等）只等空闲农民顺手修 ——
// 为它们抽人等于用持续的食物/木材产出换一次不紧急的修理。
static bool IsUrgentRepairTarget(int buildingType)
{
    return buildingType == BUILDING_ARROWTOWER;
}

// 【诊断】空闲农民一个工作目标都没找出来时记录一行（节流）。
//
// 与 [FARFARM] 互补：[FARFARM] 记录「选了一个比近处农田更远的目标」，
// 这里记录「一个都没选出来」。两种卡法的区分：
//   —— [FARFARM] 出现而农场里的农民在动 → 选点偏远，但还在工作；
//   —— 本行出现 → 配额有缺口却选不出目标，说明候选全被排除了；
//   —— 两行都不出现、农民仍扎堆站着 → 它们根本不是 IDLE（卡在移动上），
//      不在 TryAssignIdleFarmer 的管辖范围里，得往「指令被取消/路径被清」方向查。
// 逐桶打出 target/assigned 与「可采资源数」，用来区分是「配额已满」还是
// 「有缺口但资源全被排除」。
static void LogIdleFarmerStuck(const tagFarmer &farmer, int desiredBucket,
                               const int target[4], const int assigned[4])
{
    static int lastLogFrame = USR_INVALID_FRAME;
    if (lastLogFrame != USR_INVALID_FRAME && g_frame - lastLogFrame < 500)
        return;
    lastLogFrame = g_frame;

    // 各桶的可采资源数：0 说明这个桶压根没资源（全被采光/没探到），
    // 与「有资源但被排除」是两回事。
    int gatherable[4] = {0, 0, 0, 0};
    for (const tagResource &resource : info.resources)
    {
        const int bucket = ResourceBucket(resource.Type);
        if (bucket >= 0 && IsGatherableResource(resource))
            ++gatherable[bucket];
    }
    int idleFarmers = 0;
    for (const tagFarmer &other : info.farmers)
        if (other.Blood > 0 && other.FarmerSort == FARMERTYPE_FARMER &&
            other.NowState == HUMAN_STATE_IDLE)
            ++idleFarmers;

    char buf[320];
    snprintf(buf, sizeof(buf),
             "[FARIDLE] f=%d farmer=%d pos=(%d,%d) dB=%d "
             "tgt=[%d,%d,%d,%d] asg=[%d,%d,%d,%d] res=[%d,%d,%d,%d] idle=%d",
             g_frame, farmer.SN, farmer.BlockDR, farmer.BlockUR, desiredBucket,
             target[0], target[1], target[2], target[3],
             assigned[0], assigned[1], assigned[2], assigned[3],
             gatherable[0], gatherable[1], gatherable[2], gatherable[3],
             idleFarmers);
    AiDebugLog(buf);
}

// 找一个可以去修这栋建筑的农民。
//   idleOnly = true  只考虑空闲农民；
//   idleOnly = false 额外接受正在采集的农民（紧急目标在没人空闲时用）。
// 一律排除正在施工的农民 —— 建造中的农民不能被新指令抢占，
// 与 FindBuilderFarmerSN 第二遍的约束一致。
static int FindRepairFarmerSN(const tagBuilding &building,
                              const set<int> &usedFarmers, bool idleOnly)
{
    int bestFarmerSN = -1;
    int bestDis2 = 2000000000;
    for (const tagFarmer &farmer : info.farmers) {
        if (farmer.Blood <= 0 || farmer.FarmerSort != FARMERTYPE_FARMER)
            continue;
        if (usedFarmers.find(farmer.SN) != usedFarmers.end())
            continue;
        if (IsFarmerBuilding(farmer))
            continue;

        // 「有目的地但已经走不动」的僵尸农民也接受：它们的 NowState 永远是
        // WALKING，不接受就永远没人去修（实测症状之一就是箭塔受损却没人修）。
        const bool stuckWalking = farmer.NowState == HUMAN_STATE_WALKING &&
                                  IsFarmerStuckWalking(farmer);
        const bool stateOk =
            stuckWalking || (idleOnly ? (farmer.NowState == HUMAN_STATE_IDLE)
                                      : (farmer.NowState == HUMAN_STATE_IDLE ||
                                         farmer.NowState == HUMAN_STATE_WORKING));
        if (!stateOk)
            continue;

        const int dis2 = BlockDis2(farmer.BlockDR, farmer.BlockUR,
                                   building.BlockDR, building.BlockUR);
        if (dis2 < bestDis2) {
            bestDis2 = dis2;
            bestFarmerSN = farmer.SN;
        }
    }
    return bestFarmerSN;
}

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

    // 先找空闲农民；箭塔这类紧急目标在没人空闲时，再从采集队里抽最靠近的一个。
    //
    // 【原先只挑空闲农民】正常对局里农民几乎全在采集（TryAssignIdleFarmer
    // 每帧都会把空闲农民派出去），空闲农民常年为 0 —— 于是这里恒为
    // bestFarmerSN == -1，箭塔根本不会被修，一直掉血到被拆。
    int bestFarmerSN = FindRepairFarmerSN(building, usedFarmers, true);
    bool pulledFromWork = false;  // 诊断用：是不是从采集队里抽出来的
    if (bestFarmerSN == -1 && IsUrgentRepairTarget(building.Type)) {
      bestFarmerSN = FindRepairFarmerSN(building, usedFarmers, false);
      pulledFromWork = (bestFarmerSN != -1);
    }
    if (bestFarmerSN == -1)
      continue;  // 没人可派，处理下一个建筑

    usedFarmers.insert(bestFarmerSN);
    CancelPendingGatherOrder(bestFarmerSN);
    ai->HumanAction(bestFarmerSN, building.SN);
    farmerLastOrderFrame[bestFarmerSN] = g_frame;
    repairedAny = true;
    {
      char buf[256];
      snprintf(buf, sizeof(buf),
               "[REPAIR] f=%d type=%d sn=%d blood=%d/%d farmer=%d pulled=%d",
               g_frame, building.Type, building.SN, building.Blood,
               building.MaxBlood, bestFarmerSN, (int)pulledFromWork);
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
    // 建造在途订单的回执消费。这是【唯一】处理建造回执的地方 ——
    // TryBuild 里原先那套同构的代码永远执行不到（本块先跑、先把 buildOrderId
    // 清掉），已删除并把有价值的诊断移到这里。
    if (buildOrderId != -1)
    {
        map<int, int>::const_iterator result = info.ins_ret.find(buildOrderId);
        // 【-1 不算结算】instruction::ret 的初值就是 -1（GlobalVariate.h 的
        // struct instruction），不是有效的 ACTION 码 —— 枚举从 ACTION_SUCCESS = 0
        // 递增到 ACTION_INVALID_SN = 25。把它当成「已结算的回执」会提前清掉订单
        // 通道；实测日志里 ret=-1 有 1367 条，占了 [RESET] 的四成。
        // 真正的兜底是下面那个 300 帧超时。
        const bool settled =
            result != info.ins_ret.end() && result->second >= 0;
        if (settled || g_frame - lastBuildOrderFrame >= 300)
        {
            if (settled)
            {
                // 位置类失败要换候选点重试，否则会一直撞同一个放不下的位置。
                // 原先只判 POSITION_NOT_FIT，补全成与 TryBuild 里那份一致的五种。
                if (result->second == ACTION_INVALID_POSITION_NOT_FIT ||
                    result->second == ACTION_INVALID_HUMANBUILD_OVERLAP ||
                    result->second == ACTION_INVALID_HUMANBUILD_DIFFERENTHIGH ||
                    result->second == ACTION_INVALID_HUMANBUILD_OVERBORDER ||
                    result->second == ACTION_INVALID_HUMANBUILD_UNEXPLORE)
                    buildCandidateIndex++;
                // 供 [BUILD] 行的 failCode 字段使用。这段原先写在 TryBuild 的
                // 死代码里，因此该字段从启用起就恒为 0，等于没有诊断。
                const int typeIdx =
                    (buildOrderType >= 0 && buildOrderType < 16) ? buildOrderType
                                                                 : 15;
                buildFailCodes[typeIdx] = 100 + result->second;
            }
            {
              char buf[256];
              snprintf(buf, sizeof(buf),
                       "[RESET] f=%d type=%d orderId=%d ret=%d", g_frame,
                       buildOrderType, buildOrderId,
                       settled ? result->second : -999);
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
    // 【给侦察骑兵预留人口】侦察兵只有 1~2 个，但一旦人口被吃满它就永远轮不到 ——
    // 实测 `pop=50/50 scout=0` 一直持续到最后，食物木材都堆成山也没用。
    //
    // 预留必须【始终按总数】扣，而不是等 ScoutTargetCount() 该出场时才扣：
    // 人口在 f=30000 之前就已经顶到 50/50，那时再留已经来不及。
    // 造出来之后 reserve 自动变小、位置归还。
    //
    // 另外 Human_MaxNum 本身还会被下面那个 +1.9 的余量压掉约两格
    // （50 的上限实际只能填到 48），所以预留更有必要。
    const int scoutReserve =
        max(0, USR_SCOUT_TOTAL - CountArmyBySort(AT_SCOUT));
    const bool nearPopulationCap =
        info.Human_Num + 1.9 >= info.Human_MaxNum - scoutReserve;
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
    // 靶场：第 1 座是升时代前置之一（市场 + 靶场 + 马厩 ≥ 2，见下方升时代分支），
    // 必须最早建；第 2、3 座只在升入铜器时代之后补。
    //
    // 【为什么后两座要等】本 AI 的兵全是战车弓兵，多靶场就是多倍出兵速度，
    // 但升时代本身要囤 800 食物、同时还要掏 150 木的靶场钱，两者抢在同一时段
    // 会把升时代推后（与农民数从 14 调回 20 是同一个矛盾）。
    // 升时代只需要「市场 + 靶场 + 马厩 ≥ 2」，第 1 座靶场已经够用，
    // 追加的靶场对升时代没有贡献，纯粹是抢木头，所以推迟到青铜之后。
    // 用 CountBuilding 而不是 HasBuilding：后者含在建建筑，配合计数上限
    // 才能既不重复下单、又允许追加。
    const int rangeCount = CountBuilding(BUILDING_RANGE);
    if (rangeCount < USR_RANGE_TARGET &&
        (rangeCount == 0 || info.civilizationStage != CIVILIZATION_TOOLAGE)) {
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
    // 仓库/谷仓：排在农场【之前】。
    //
    // 【为什么必须提前】建造是单槽 + 100 帧节流（USR_BUILD_ORDER_INTERVAL，
    // lastBuildOrderFrame 共用），一次只有一个建筑能开工。原先仓库排在建造序列
    // 最后一位（农场之后），而农场上限 6~12 座、长期有人想建，于是仓库几乎
    // 永远轮不到 —— 实测日志里 [DEPOT] 连续几帧都是 blocked=noWood，
    // 而木材正是被农场（75 木/座）吃光的。
    // 放在这里的位置考虑：仍在市场/兵营/靶场/马厩之后（它们有前置链，
    // 且升时代需要它们），但抢在农场前面。
    TryBuildReturnDepot(ai);

    // 农场只在马厩/靶场建成后建：否则木头会被农场（75木）持续消耗，
    // 永远攒不够马厩/靶场（各150木），导致时代无法升级。
    if (HasBuilding(BUILDING_MARKET)) {
      // 农场数量上限 = 村民数 / 3。
      //
      // 【历史】这里换过多次：
      //   /3 → /2：当年 14 个农民时 /3 只允许 4 座农场，食物产能被农场数量卡死，
      //            实测食物长期停在 40~125，凑不齐车轮升级所需的 150。
      //   两段式：第三波后改成「农民数 − 8」。
      //   现在回到 /3，依据是实测日志而不是推算：
      //     农民目标提到 20（USR_FARMER_TARGET）之后，/2 会放到 10~12 座农场，
      //     每座 75 木 = 750~900 木，把建造预算吃光。实测木材长期只有 5~95，
      //     而同一局的**食物是 170~790（严重过剩）** —— 瓶颈明明是木材，
      //     再往食物上堆农场只会继续挤压马厩/靶场/仓库/仓库的钱。
      //     /3 = 6 座，食物依然够（野生浆果 + 猎物 + 6 座农场），木材省下 300~450。
      const int villagerCount = static_cast<int>(info.farmers.size());
      const int farmLimit = villagerCount / 3;
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
        TrackWoodResearch(market);        // 木材链两级是否跑完（车轮的门控）
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
    // 【取消】仓库的所有科技研发（原三项：近战攻击 +2 / 弓兵护甲 +2 /
    // 近战攻击再 +2，成本 100~200 食，第三项还要 120 金）。
    //
    // 它们加的是【近战攻击】与【弓兵护甲】：
    //   · 近战攻击（USETOOL）—— 当前配比里唯一的近战单位是 2 辆战车，
    //     占比不到 5%，为它们花 300 食 + 120 金不划算；
    //   · 弓兵护甲（DEFENSE_ARCHER）—— 主力是战车弓兵，但远程交火里
    //     我们靠的是射程与站桩消耗，不是换血。
    // 而且食物在这套经济里长期紧张（实测 10~790 波动，多数时间在低位），
    // 这三项会和造兵、升时代直接抢。取消后仓库不再消耗任何资源。
    //
    // 注意：保留这段的注释与数组结构是为了将来恢复时能直接对照，
    // 但【不要再顺手打开】—— 先确认当时的兵种配比里真的有近战单位。
    //
    // if (info.civilizationStage != CIVILIZATION_TOOLAGE &&
    //     HasBuilding(BUILDING_STOCK)) {
    //   static const int kStockTechs[3] = {
    //       BUILDING_STOCK_UPGRADE_USETOOL,
    //       BUILDING_STOCK_UPGRADE_DEFENSE_ARCHER,
    //       BUILDING_STOCK_UPGRADE_USETOOL};
    //   static const int kStockTechFood[3] = {
    //       BUILDING_STOCK_UPGRADE_CLOSER_ATTACK_FOOD,
    //       BUILDING_STOCK_UPGRADE_DEFENSE_ARCHER_FOOD,
    //       BUILDING_STOCK_UPGRADE_CLOSER_ATTACK_2_FOOD};
    //   static const int kStockTechGold[3] = {
    //       0, 0, BUILDING_STOCK_UPGRADE_CLOSER_ATTACK_2_GOLD};
    //   ResearchTechQueue(ai, BUILDING_STOCK, kStockTechs, kStockTechFood,
    //                     kStockTechGold, 3, stockTechCursor, stockTechOrderId);
    // }
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
  // 【USR_PRIEST_PASSIVE_FRAME(30000) 帧之后一律禁用】
  //
  // 治疗是 HumanAction：它建立对象关系之后，Core 的流程是「走过去再治疗」，
  // 也就是这条指令会【把祭司拽到伤员身边】。总攻阶段祭司只保存实力，任何
  // 会被敌人牵着走的行动都要掐掉。
  //
  // 【为什么拦在函数入口，而不是只拦调用点】唯一的调用点在 ManagePriest 的
  // `!enemyVisible` 分支里，那个分支本身已经有三处 return；将来任何改动都
  // 可能把治疗重新放出来。规则写在入口，谁调用都绕不过。
  //
  // 实测（ai_debug.log）：18500~21000 帧之间祭司在 (77,18)↔(78,18) 之间
  // 原地摆动 2500 帧，就是这条治疗指令与「守家 12 格」规则互相拉扯 ——
  // 两者的半径都恰好是 12 格，谁都赢不了，路径每次都被 suspendRelation 清空。
  if (g_frame >= USR_PRIEST_PASSIVE_FRAME)
    return false;

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
  // 目标没变：判据是「这段时间有没有在靠近」，而不是「位置有没有变」。
  //
  // 【为什么不能用位置】原判据是「当前位置 == 下发时的位置」，也就是只有
  // 一步都没动才算卡住。而最常见的卡法恰恰是：下发后走了一小段、然后停住。
  // 此时位置 != 下发时的位置，判据不成立 —— 于是【永远不再重发】，祭司被冻在
  // 半路上。实测它在 (28,36) 一动不动挂了 1000+ 帧（hp=12、周围一个敌人都没有、
  // 离基地 50 格），就是这么来的：它走了一半停住，而系统认为「它在动」。
  //
  // 改用「到目标的距离有没有缩短」：只要没靠近，无论动没动都算卡住、就重发。
  // 不需要额外状态 —— priestMoveFromDR/UR 存的就是下发时祭司所在的格。
  if (g_frame - priestEmergencyTargetFrame < USR_PRIEST_STUCK_FRAMES)
    return false;
  const int fromDis2 = BlockDis2(priestMoveFromDR, priestMoveFromUR, tx, ty);
  const int nowDis2 = BlockDis2(priest->BlockDR, priest->BlockUR, tx, ty);
  return nowDis2 >= fromDis2;
}

static void ManagePriest(UsrAI *ai)
{
    const tagArmy *priest = FindPriest();
    if (priest == nullptr)
        return;  // 祭司死亡后不再执行祭司逻辑，避免解引用空指针

    // 【先结算上一条订单，再让任何分支去读 priestMoveOrderId】
    //
    // 这一段原先在守家规则【之后】—— 也就是所有决策都读完 priestMoveOrderId
    // 才轮到它清零。而下面「走到攻城厂就地转换」那条分支会立刻 return，永远
    // 走不到这里，于是 priestMoveOrderId 一旦被赋值就再也不归零：转换指令只
    // 下发一次，成功还算好，被 Core 拒绝（ret=14 ISNTFREE 之类）就永久卡死。
    // 结算逻辑必须排在使用之前，这才是「每帧先清账、再做决策」的顺序。
    // 记录回执码：0 = 关系已建立；非 0 说明指令被 Core 拒绝，祭司不会动。
    if (priestMoveOrderId != -1)
    {
        map<int, int>::const_iterator result = info.ins_ret.find(priestMoveOrderId);
        if (result != info.ins_ret.end() ||
            g_frame - priestEmergencyTargetFrame >=
                USR_PRODUCTION_ORDER_TIMEOUT)
        {
            priestMoveLastRet =
                result != info.ins_ret.end() ? result->second : -1;
            priestMoveLastRetFrame = g_frame;
            priestMoveOrderId = -1;
        }
    }

    // 【没有敌人 → 去转换敌方攻城武器厂（唯一的获胜条件）】
    //
    // 【为什么需要这一整段】这一段原先只做了半件事：把祭司「送出门」，
    // 却没有「到位后转换」那一步 —— 唯一的转换调用点 FindEnemySiege() 被卡在
    // 一个判据自相矛盾的块里（详见下面 4789 起的说明），任何帧都执行不到。
    // 结果就是祭司一路走到厂门口、转身、回家，重来。这里把后半件补上。
    //
    // 【为什么放在最前面】它是获胜路径，优先级高于守家（12 格）与硬圈（50 格）——
    // 那两条都是为了保命，而这里是唯一的取胜手段，不能互相否决。
    //
    // 【为什么要求「敌人打光了」，而不是「视野里没有敌人」】
    //
    // 判据是 EnemyArmyStillExists()（按敌方开局总量 51 判断），不是
    // HasVisibleEnemyArmy()。
    //
    // 原先用「视野内没有敌人」，后果是祭司走到一半、远处任何一个敌兵进入视野，
    // 这个条件立刻失效、它掉到 50 格缰绳被拽回家；等敌人不可见了再出门 ——
    // 在 130 格的路程上来回摇摆，永远到不了厂门口。
    //
    // 用总量判据则相反：只要敌方还有任何兵力（哪怕此刻在迷雾里），祭司就不出门；
    // 只有「51 个全部见过、且此刻一个都看不见」才认定真打光了、该去换基地了。
    // 这与「有敌人就不要靠近基地」是同一条策略，只是把「敌人」的口径从
    // 「此刻看得见的」换成「战略上还活着的」。
    //
    // 保留 g_frame >= USR_PRIEST_PASSIVE_FRAME(30000) 门槛：在那之前军队还没成型，
    // 这时候押上祭司去换基地是亏的。
    if (g_frame >= USR_PRIEST_PASSIVE_FRAME && !EnemyArmyStillExists())
    {
        const tagBuilding *siegeTarget = FindEnemySiegeBuilding();
        if (siegeTarget != nullptr)
        {
            const int siegeDis2 =
                BlockDis2(priest->BlockDR, priest->BlockUR,
                          siegeTarget->BlockDR, siegeTarget->BlockUR);
            if (siegeDis2 >
                USR_PRIEST_SIEGE_APPROACH_RADIUS *
                    USR_PRIEST_SIEGE_APPROACH_RADIUS)
            {
                // 还没到 → 走过去。
                //
                // 【有了转换关系就不能再发移动】WorkObjectSN 已经是这座厂，
                // 说明转换关系已建立、祭司正在途中；此时再发 HumanMove 会经
                // Core_List::addRelation → suspendRelation 把关系清掉。实测
                // 这条 10 格边界会被反复跨越，一跨就互毁，必须在这里让路。
                if (priest->WorkObjectSN != siegeTarget->SN &&
                    ShouldReissuePriestMove(priest, siegeTarget->BlockDR,
                                            siegeTarget->BlockUR))
                {
                    priestMoveOrderId = ai->HumanMove(
                        priest->SN,
                        (siegeTarget->BlockDR + 0.5) * double(BLOCKSIDELENGTH),
                        (siegeTarget->BlockUR + 0.5) * double(BLOCKSIDELENGTH));
                    priestEmergencyTarget =
                        make_pair(siegeTarget->BlockDR, siegeTarget->BlockUR);
                    priestEmergencyTargetFrame = g_frame;
                    priestMoveFromDR = priest->BlockDR;
                    priestMoveFromUR = priest->BlockUR;
                    lastPriestOrderFrame = g_frame;
                }
                return;
            }

            // 【走到位了 → 就地转换】
            //
            // 【为什么必须写在这里】全文件唯一会转换攻城厂的是 FindEnemySiege()，
            // 而它唯一的调用点在下面那个「有可见敌人」的块里 —— 而那个块的上方
            // 就有一句 `if (HasVisibleEnemyArmy()) return;`，判据完全相同。于是
            //   有敌人 → 提前 return，到不了调用点；
            //   没敌人 → 到了，但 anyEnemyVisible 为假，整块不执行。
            // 也就是说那行在任何帧都不可达，祭司站在厂门口也不会动手
            // （实测 baseKnown=1、enemyB=6、祭司距厂 130 格，全程 0 次转换）。
            // 转换攻城厂是唯一的获胜条件，指令必须在这里下发。
            //
            // 距离用 USR_PRIEST_SIEGE_APPROACH_RADIUS(10)，它比引擎的转换距离
            // （config.json 的 DIS_PRIEST = 12）小 2 格，保证「判定能转换」时
            // 一定真的在转换距离内。
            if (priest->ConvertCooldown <= 0 && priestMoveOrderId == -1 &&
                priest->WorkObjectSN != siegeTarget->SN)
            {
                // 已经有关系时不重复下发 —— 重复指令会中止 Core 里的原关系。
                priestMoveOrderId = ai->HumanAction(priest->SN, siegeTarget->SN);
                priestEmergencyTargetFrame = g_frame;
                lastPriestOrderFrame = g_frame;
                char buf[256];
                snprintf(buf, sizeof(buf),
                         "[SIEGECONV] f=%d priest=(%d,%d) siegeSN=%d "
                         "siegePos=(%d,%d) dis2=%d siegeHP=%d/%d prevRet=%d",
                         g_frame, priest->BlockDR, priest->BlockUR,
                         siegeTarget->SN, siegeTarget->BlockDR,
                         siegeTarget->BlockUR, siegeDis2, siegeTarget->Blood,
                         siegeTarget->MaxBlood, priestMoveLastRet);
                AiDebugLog(buf);
            }
            // 【无论有没有下发指令都到此为止】
            // 既然攻城厂已知、视野里又没有敌人，祭司唯一的任务就是转换它。
            // 不能落到下面的守家(12)/缰绳(50)/治疗逻辑 —— 敌方基地离我方中心
            // 130 格，一落到缰绳就会被 HumanMove 一路拽回家，形成
            // 「走到厂门口 → 掉头 → 再走过去」的大范围来回。
            return;
        }
    }

    // 【活动范围】离市镇中心超过 USR_PRIEST_LEASH_RADIUS 就直接拉回来，
    // 不做任何别的判断。豁免条件是 WorkObjectSN != -1 —— 那表示它已经建立了
    // 对象关系（正在转换、或正赶往转换目标），属于获胜路径，见常量的说明。
    // 注意用 WorkObjectSN 而不是 NowState：纯移动不会产生对象关系
    // （实测祭司带着移动目标时 wo 一直是 -1），所以这个判据只放过「有活干」的情况。
    {
        const tagBuilding *leashHome = FindCenter();
        if (leashHome != nullptr && priest->WorkObjectSN == -1)
        {
            const int homeDis2 =
                BlockDis2(priest->BlockDR, priest->BlockUR, leashHome->BlockDR,
                          leashHome->BlockUR);
            if (homeDis2 >
                USR_PRIEST_LEASH_RADIUS * USR_PRIEST_LEASH_RADIUS)
            {
                if (ShouldReissuePriestMove(priest, leashHome->BlockDR,
                                            leashHome->BlockUR))
                {
                    priestMoveOrderId = ai->HumanMove(
                        priest->SN,
                        (leashHome->BlockDR + 0.5) * double(BLOCKSIDELENGTH),
                        (leashHome->BlockUR + 0.5) * double(BLOCKSIDELENGTH));
                    priestEmergencyTarget =
                        make_pair(leashHome->BlockDR, leashHome->BlockUR);
                    priestEmergencyTargetFrame = g_frame;
                    priestMoveFromDR = priest->BlockDR;
                    priestMoveFromUR = priest->BlockUR;
                    lastPriestOrderFrame = g_frame;
                }
                return;
            }
        }
    }

    // WorkObjectSN 是 Core 当前关系的目标。转换关系从接近阶段开始就不能
    // 被新的移动指令中止，不能只依赖 NowState 已经变成攻击状态。
    // 「正在转换」＝ 有关系【并且】目标就在转换距离内。
    //
    // 【为什么必须加距离限制】工作对象非空只表示「有关系」，不表示「转得了」。
    // 实测祭司与一个 109 格外的目标建立了关系（relation 建立时不校验距离，
    // 它会一路走过去），convAlive 因此一直为真；而下面「转换中不撤退」那条
    // 取舍被永久触发 —— 它在基地原地站着，被 5 个贴上来的敌人从 98 血打到 0，
    // 全程一次撤退都没有。
    //
    // 只有目标在转换距离（config.json 的 DIS_PRIEST = 12）以内，才算真的在转换，
    // 才值得吃那个「可能死在转换里」的代价；在外面只是在赶路，随时可以撤。
    bool conversionTargetAlive = false;
    if (priest->WorkObjectSN != -1)
    {
        const int convertDis2 =
            USR_PRIEST_CONVERTING_RADIUS * USR_PRIEST_CONVERTING_RADIUS;
        for (const tagArmy &enemy : info.enemy_armies)
        {
            if (enemy.SN != priest->WorkObjectSN || enemy.Blood <= 0)
                continue;
            if (BlockDis2(priest->BlockDR, priest->BlockUR, enemy.BlockDR,
                          enemy.BlockUR) > convertDis2)
                continue;  // 太远，只是在赶路，不算在转换
            conversionTargetAlive = true;
            break;
        }
        if (!conversionTargetAlive)
        {
            for (const tagBuilding &building : info.enemy_buildings)
            {
                if (building.SN != priest->WorkObjectSN || building.Blood <= 0)
                    continue;
                if (BlockDis2(priest->BlockDR, priest->BlockUR,
                              building.BlockDR, building.BlockUR) > convertDis2)
                    continue;
                conversionTargetAlive = true;
                break;
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

    // 【总攻阶段（priestPassive = f >= USR_PRIEST_PASSIVE_FRAME，即 30000）
    //   之后，祭司守家，不追出去转换】
    //
    // 【为什么必须放在转换之前】原先「回基地待机」那段逻辑裹在下面的
    // `if (!enemyVisible)` 里面，而转换判断排在它【前面】—— 所以只要视野里出现
    // 敌人，祭司就会去追转换目标，回基地那段根本执行不到。实测它就是这样跑到
    // 离基地很远的地方、被 4 个敌人贴到 1 格围死的。
    //
    // 判据是「超出 USR_PRIEST_HOME_RADIUS(12) 格就回家并 return」：
    // 在圈内时不拦，下面的转换逻辑照常跑（敌人都送到家门口了，转换是获胜路径，
    // 不该关掉）。所以效果是「只转换送上门的，不追出去」。
    // 【三万帧以后：敌方还有兵力就待在家里，什么也不做】
    //
    // 与函数开头那段是一对，两边合起来才是完整的规则 —— 判据都是
    // EnemyArmyStillExists()（按敌方开局总量 51 判「打光了没有」）：
    //     敌人打光了 → 去转换敌方攻城武器厂（唯一的获胜路径）
    //     还有兵力   → 待在家里，不转换、不治疗、不追
    // 两处必须同口径，否则会出现「上面放它出门、这里又拦下来」的互否。
    //
    // 【为什么「在 12 格内」也要 return】原实现只在超出 12 格时 return，
    // 圈内会落到下面的转换逻辑 —— 于是祭司会去追一个转换目标、走出 12 格，
    // 又被这段拉回来，形成 HumanAction ↔ HumanMove 的来回拉扯（游戏日志里
    // 表现为「设置工作目标为 X」和「移动至同一坐标」交替刷屏）。
    // 有敌人时直接停手，就不存在这个循环。
    if (priestPassive)
    {
        const tagBuilding *passiveHome = FindCenter();
        if (passiveHome != nullptr)
        {
            const int homeDis2 =
                BlockDis2(priest->BlockDR, priest->BlockUR,
                          passiveHome->BlockDR, passiveHome->BlockUR);
            if (homeDis2 > USR_PRIEST_HOME_RADIUS * USR_PRIEST_HOME_RADIUS &&
                ShouldReissuePriestMove(priest, passiveHome->BlockDR,
                                        passiveHome->BlockUR))
            {
                priestMoveOrderId = ai->HumanMove(
                    priest->SN,
                    (passiveHome->BlockDR + 0.5) * double(BLOCKSIDELENGTH),
                    (passiveHome->BlockUR + 0.5) * double(BLOCKSIDELENGTH));
                priestEmergencyTarget =
                    make_pair(passiveHome->BlockDR, passiveHome->BlockUR);
                priestEmergencyTargetFrame = g_frame;
                priestMoveFromDR = priest->BlockDR;
                priestMoveFromUR = priest->BlockUR;
                lastPriestOrderFrame = g_frame;
            }
        }
        // 敌方还有兵力 → 到此为止：不转换、不撤退、不治疗。
        // 敌人真的打光了 → 落到下面（转换攻城厂那段在本函数开头已经处理过，
        // 这里放行是给「还没侦察到攻城厂 / 还没走到」的情况留余地）。
        //
        // 【判据用总量，不用视野】理由与上面 4796 那处相同：用视野判据会让祭司
        // 在 130 格的路程上被「远处敌兵一闪」反复拽回家。总量判据只在敌方真的
        // 一个不剩时才放它出门。
        // 注意两处的语义必须一致 —— 一处用视野、一处用总量，会出现「上面放它
        // 出门、这里又拦下来」的互否。
        if (EnemyArmyStillExists())
            return;
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
            // 【这里只转换敌方部队，不碰建筑】
            // 原先还有一个 `if (!armyTarget && g_frame >= 30000)
            // buildingTarget = FindEnemySiege(*priest);` 的分支，但它是死代码：
            // 外层 `if (anyEnemyVisible)` 与上面的 `if (HasVisibleEnemyArmy())
            // return;` 判据完全相同，所以「有敌人」时早就 return 了、走不到这里，
            // 「没敌人」时这个块整体不执行。攻城厂的转换已挪到本函数开头
            // 「没有敌人 → 去转换敌方攻城武器厂」那一段（见 4774 起）。
            const tagArmy *armyTarget = FindPriestConversionTarget(*priest);
            const int targetSN = armyTarget ? armyTarget->SN : -1;
            // 同一目标已有关系时不重复下达 HumanAction；重复指令会中止原关系。
            if (targetSN != -1 && priest->ConvertCooldown <= 0 &&
                priestMoveOrderId == -1 && priest->WorkObjectSN != targetSN) {
                priestMoveOrderId = ai->HumanAction(priest->SN, targetSN);
                priestEmergencyTargetFrame = g_frame;
                lastPriestOrderFrame = g_frame;
                // 【诊断】祭司反复追一个「根本不存在」的目标时，只有这行能定性。
                // 它把 AI 当时选中的目标、该目标在 info.enemy_armies 里的兵种与
                // 坐标、距祭司多远、以及本帧总共看得见几个敌人全部记下来：
                //   visible=0 而 target 有值 → 是悬空引用（代码 bug）
                //   visible>0 而用户在画面上看不到 → 是迷雾/视野口径不一致
                //   dis 很大 → 是「优先级压过距离」选中了远处的目标
                int targetDR = 0;
                int targetUR = 0;
                const bool located =
                    FindEnemyUnitBlockPosition(targetSN, targetDR, targetUR);
                char buf[256];
                snprintf(buf, sizeof(buf),
                         "[CONV] f=%d priest=(%d,%d) target=%d sort=%d "
                         "located=%d tpos=(%d,%d) dis=%d visible=%d",
                         g_frame, priest->BlockDR, priest->BlockUR, targetSN,
                         armyTarget ? armyTarget->Sort : -1, (int)located,
                         targetDR, targetUR,
                         located ? BlockDis(priest->BlockDR, priest->BlockUR,
                                            targetDR, targetUR)
                                 : -1,
                         (int)info.enemy_armies.size());
                AiDebugLog(buf);
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
        // 开局探路：分层推进，把市镇中心 USR_PRIEST_EXPLORE_RADIUS(50) 格
        // 以内的区域探开为止。走法与终止条件见 CapturePriestLap 与那里的说明。
        //
        // 放在「无敌人」分支里：有敌人时仍走威胁/转换逻辑，不会为了探路挨打。
        //
        // 【已取消】原先这里还有一个「视野里出现瞪羚就提前收工」的出口
        // （HasVisibleGazelle + FinishPriestExplore("gazelle")）。按需求去掉：
        // 探路的目的不再只是找食物，而是把基地周边 50 格探开 —— 瞪羚出现得早，
        // 按那个出口走的话祭司会刚出门就收工，外围根本没探。
        // 【时间截止】到帧就收工，与「50 格内探干净」并列，谁先满足用谁。
        // 放在半径检查之前，保证即使外围还没探完也一定回家。
        if (!priestExploreDone && g_frame >= USR_PRIEST_EXPLORE_UNTIL_FRAME)
            FinishPriestExplore("timeout");

        if (!priestExploreDone)
        {
            const tagBuilding *lapHome = FindCenter();
            if (lapHome == nullptr)
                return;
            // 第一次能算出非空边界时抓一次，之后 priestLapPoints 固定不变。
            CapturePriestLap(*lapHome);
            if (priestLapPoints.empty())
            {
                // 两种可能，必须分开处理：
                //   ① 地图还没建立（theMap 为空，或连基地那一格都还是 UNKNOWN）
                //      → 下一帧再试。开局头几帧就是这种情况，误判会让祭司一步不探。
                //   ② 地图已建立、但半径以内的前沿点一个都没有 → 近处探干净了，收工。
                const bool mapReady =
                    info.theMap &&
                    (*info.theMap)[lapHome->BlockDR][lapHome->BlockUR].type !=
                        MAPPATTERN_UNKNOWN;
                if (!mapReady)
                    return;
                FinishPriestExplore("radius-clear");
                return;
            }

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
                    priestFrontierTarget = make_pair(-1, -1);  // 到达 → 走下一个
                    priestFrontierStuckCount = 0;
                    ++priestLapIndex;
                }
                else if (g_frame - priestEmergencyTargetFrame >=
                             USR_PRIEST_STUCK_FRAMES &&
                         priest->BlockDR == priestMoveFromDR &&
                         priest->BlockUR == priestMoveFromUR)
                {
                    // 给足整段 USR_PRIEST_STUCK_FRAMES 都没挪窝 → 判定走不到
                    // （隔着海洋 / 被建筑围住），跳过该点继续走下一个。
                    // 用这么长的窗口是为了不把「正常行进」误判成卡住 ——
                    // 误判就会重发，重发就会清空路径。
                    priestFrontierTarget = make_pair(-1, -1);
                    ++priestFrontierStuckCount;
                    ++priestLapIndex;
                }
                return;
            }

            // 一圈走完（含走不到而跳过的点）。
            // 【不再直接收工】前沿会随探索向外扩，所以再抓一圈继续往外推 ——
            // 这就是「探完基地 50 格以内」的分层推进。CapturePriestLap 只收
            // USR_PRIEST_EXPLORE_RADIUS 以内的点，近处探干净之后它会返回空集合，
            // 那才是真正收工的时候。
            if (priestLapIndex >= priestLapPoints.size())
            {
                priestLapCaptured = false;  // 解除抓取锁，允许重新抓
                priestLapPoints.clear();
                priestLapIndex = 0;
                CapturePriestLap(*lapHome);
                if (priestLapPoints.empty())
                {
                    FinishPriestExplore("radius-clear");
                    return;
                }
                return;  // 下一帧从新一圈的第一个点开始
            }

            // 没有目标时才选点下发，并按 USR_PRIEST_EXPLORE_ORDER_INTERVAL 节流
            if (g_frame - priestEmergencyTargetFrame <
                USR_PRIEST_EXPLORE_ORDER_INTERVAL)
                return;

            const int tx = priestLapPoints[priestLapIndex].first;
            const int ty = priestLapPoints[priestLapIndex].second;
            priestFrontierTarget = make_pair(tx, ty);
            priestMoveOrderId = ai->HumanMove(
                priest->SN, (tx + 0.5) * double(BLOCKSIDELENGTH),
                (ty + 0.5) * double(BLOCKSIDELENGTH));
            priestEmergencyTarget = priestFrontierTarget;
            priestEmergencyTargetFrame = g_frame;
            priestMoveFromDR = priest->BlockDR;
            priestMoveFromUR = priest->BlockUR;
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
        // 总攻阶段（priestPassive）治疗已停用 —— 判据不在这一层，而是在
        // TryPriestHeal 的函数入口，那里是唯一权威的规则点（见该函数注释）。
        // 这里不再重复判断，避免两处条件将来各自漂移。
        TryPriestHeal(ai, *priest);
        return;
    }

    // 转换全程保留，不因总攻阶段停用。
    // 这里只处理「敌方部队」的转换；敌方攻城武器厂（唯一的获胜条件）已挪到
    // 本函数开头那段处理 —— 原先挂在上面那个块里的 `g_frame >= 30000` 与
    // priestPassive 的门槛相同，两条规则互相抵消，那一行从来没有被执行过。
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
        const int size = BuildingBlockSize(building.Type);
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

        // 角色由【当前帧号】决定，不是出厂时锁死的。
        //
        // 【原先的写法及其后果】原先是「首次看到该侦察兵时判定，并永久保存」：
        //     scoutIsRecon[SN] = (g_frame >= USR_SCOUT_RECON_FRAME)
        // 侦察兵训练要 30 秒（750 帧），所以 f≈31000 下单、f≈32000 出厂，
        // 首次被看到时 32000 < 33000 → 判成「保命型」，此后永远是保命型：
        // 遇敌就撤回基地，视野永远推不出去。实测敌方基地直到 f=40000 才被发现，
        // 连带总攻与站桩消耗（ManageStandoff）一次都没被执行过。
        //
        // 【为什么改成按帧号】保命型的「勾引 + 撤回」是为【波次防御】服务的
        // （FAT=6000 / SAT=13500 / TAT=21000，第三波在 21000 就结束了），
        // 而 USR_SCOUT_RECON_FRAME(33000) 在那之后 —— 过了这个点，波次防御已经
        // 用不上它了，让它转去侦测敌方基地收益更大。
        const bool isRecon = (g_frame >= USR_SCOUT_RECON_FRAME);

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

// 找一个「正在攻击我方祭司、且离这个农民够近」的敌人。
// 只认 WorkObjectSN 指向祭司的敌人 —— 与 FindDirectThreatToFarmerSN 同一判据，
// 只是把「我」换成了祭司。距离上限见 USR_FARMER_PRIEST_HELP_RADIUS。
static int FindThreatToPriestForFarmer(const tagFarmer &farmer,
                                       const tagArmy *priest)
{
    if (priest == nullptr)
        return -1;

    int bestSN = -1;
    int bestDis2 =
        USR_FARMER_PRIEST_HELP_RADIUS * USR_FARMER_PRIEST_HELP_RADIUS;
    for (const tagArmy &enemy : info.enemy_armies)
    {
        if (enemy.Blood <= 0 || enemy.WorkObjectSN != priest->SN)
            continue;
        const int dis2 = BlockDis2(farmer.BlockDR, farmer.BlockUR,
                                   enemy.BlockDR, enemy.BlockUR);
        if (dis2 < bestDis2)
        {
            bestDis2 = dis2;
            bestSN = enemy.SN;
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
    // 祭司只找一次，别在农民循环里反复遍历。
    const tagArmy *priest = FindPriest();

    map<int, int> targetWorkers;
    for (const tagFarmer &farmer : info.farmers)
    {
        if (farmer.Blood <= 0 || farmer.FarmerSort != FARMERTYPE_FARMER)
            continue;

        // 目标优先级：
        //   ① 正在打我自己的敌人（最急，就在身边）
        //   ② 正在打我方祭司的敌人（祭司是唯一的获胜路径且不可补充）
        //   ③ 3 格内的任意敌人（原来的行为）
        int targetSN = FindDirectThreatToFarmerSN(farmer);
        bool helpingPriest = false;
        if (targetSN == -1)
        {
            targetSN = FindThreatToPriestForFarmer(farmer, priest);
            helpingPriest = (targetSN != -1);
        }
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
            // 诊断：只对「去救祭司」这条新分支打日志。另两条分支的量太大，
            // 全打会把日志淹掉；而这条需要能被验证。
            if (helpingPriest)
            {
                static int lastHelpLogFrame = USR_INVALID_FRAME;
                if (lastHelpLogFrame == USR_INVALID_FRAME ||
                    g_frame - lastHelpLogFrame >= 500)
                {
                    lastHelpLogFrame = g_frame;
                    char buf[224];
                    snprintf(buf, sizeof(buf),
                             "[FARMDEF] f=%d farmer=%d -> enemy=%d "
                             "helpingPriest pos=(%d,%d) priest=(%d,%d)",
                             g_frame, farmer.SN, targetSN, farmer.BlockDR,
                             farmer.BlockUR, priest->BlockDR, priest->BlockUR);
                    AiDebugLog(buf);
                }
            }
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

// ── 站桩消耗（拉扯战术的自动化版）──────────────────────────────────
//
// 【机制】敌方守军有一条追击上限，超过就放弃目标、走回防区：
//     enemyai.cpp:559  DefenseChaseLimitBlocks(army)
//                      = DEFENSE_CHASE_LIMIT(25)           近战
//                      = 25 − ceil(自身远程射程)             远程
//     enemyai.cpp:2119 的判定位置在【目标选择之前】，所以连「谁打我我就锁谁」
//                      的反击锁也压不过它。
// 而它们的「核心」是【攻城武器厂】，不是市镇中心：
//     enemyai.cpp:68    static pair<double,double>Enemy_Center;  //enemy武器工程厂
//     enemyai.cpp:2022  Initialize_Enemycenter() 取的是 BUILDING_SIEGE
//
// 【所以】把远程部队停在它 26~30 格处：守军冲出来到 25 格就折返，而那 1~5 格
// 正好落在我方战车弓兵射程（7）内 —— 白挨打，永远够不到我们。
// 这比文档描述的「突击者反复进退」更省事：站对距离，对方会自己完成拉扯。
//
// 【锚点为什么用攻城武器厂】因为敌方守军的判据就是量到它的。用市镇中心会算偏，
// 偏多少取决于两座建筑隔多远。
static const int USR_STANDOFF_RADIUS = 28;
// 环上落点数。半径 28 的圆周长约 176 格，16 个点平均间隔约 11 格 ——
// 足够分散，不会互相挤。
static const int USR_STANDOFF_SLOTS = 16;
// （原先这里还有 USR_STANDOFF_ADVANCE_ENEMIES 与 USR_STANDOFF_COUNT_RADIUS
//   两个常量，用来数「锚点附近还有几个敌人」。已删除 —— 判据换成了
//   HasVisibleEnemyArmy()：只要有敌人在，无论在哪，都不靠近基地。）

// 视野里是否有活着的敌方部队。
//
// 【适用场景：眼前的危险】祭司用它判断「现在出不出门」——看重的是
// 【此刻看得见的威胁】，不该被统计口径干扰。
// 军队判断「敌人打光了没有」用下面那个 EnemyArmyStillExists()。
static bool HasVisibleEnemyArmy()
{
    for (const tagArmy &enemy : info.enemy_armies)
    {
        if (enemy.Blood > 0)
            return true;
    }
    return false;
}

// 敌方开局总兵力。四张地图的 LZ 条目完全一致（见 map*.njust）：
//   棍棒兵 2 + 弓兵 1 + 骑兵 6 + 投石车 6 + 方阵兵 9 + 战车 4
//   + 战车弓兵 7 + 阔剑兵 7 + 复合弓兵 9 = 51
// 地图换了要同步改这里（scripts 里没有现成的提取脚本，用 json 解析 map*.njust
// 里 Own=="LZ" 的 Human_* 条目即可）。
static const int USR_ENEMY_ARMY_TOTAL = 51;
// （累计集合 enemyArmySeenSN 的声明在文件上方 —— 新对局重置块在这之前要清它。）
//
// 更新累计集合，返回当前可见的敌军数。
//
// 【调用点在 processData 每帧主序列，不在这里自己调】
//
// 它原先【只被 EnemyArmyStillExists() 调用】，而 EnemyArmyStillExists() 又只被
// ManageStandoff 调用 —— 于是「谁见过」这件事被挂在了管理站桩这条【策略】分支
// 的副作用上。而 ManageStandoff 有自己的前置：FindEnemySiegeBuilding() 非空。
// 后果是自锁：没侦察到攻城厂 → 一次都不数 → 累计集合永远是空的 → 永远凑不满
// 51 个 → EnemyArmyStillExists() 永远为真 → ManageStandoff 永远接管 →
// 底下「拆敌方建筑」的代码永远执行不到；而要不被站桩接管又得先凑满 51 个。
//
// 实测（ai_debug.log）：敌方攻城厂直到 f≈41704 才进 info.enemy_buildings，
// 也就是说整局只有在最后约 3000 帧里才有可能开始积累，而敌方基地在 130 格外。
//
// 事实收集与策略判断必须分开：见过谁，每帧无条件地记；该不该推进，另说。
static int UpdateEnemyArmySeen()
{
    int visible = 0;
    for (const tagArmy &enemy : info.enemy_armies)
    {
        if (enemy.Blood <= 0)
            continue;
        ++visible;
        enemyArmySeenSN.insert(enemy.SN);
    }
    return visible;
}

// 敌方【战略上】是否还有兵力。用于「守军清完了没有 / 该不该推进」这类判断。
// 只有「已经见过全部 51 个，且此刻一个都看不见」才算清空。
//
// 这里【只读】，不再自己收集 —— 累计集合由 processData 每帧喂（见上）。
// 之前把 UpdateEnemyArmySeen() 写在这里，是上面那段自锁的根源之一。
static bool EnemyArmyStillExists()
{
    if (HasVisibleEnemyArmy())
        return true;
    return static_cast<int>(enemyArmySeenSN.size()) < USR_ENEMY_ARMY_TOTAL;
}

// 这个 SN 是不是敌方建筑。
static bool IsEnemyBuildingSN(int sn)
{
    for (const tagBuilding &building : info.enemy_buildings)
    {
        if (building.SN == sn)
            return true;
    }
    return false;
}

// 找一个存活的敌方攻城武器厂。敌方守军的追击上限就是量到它的。
static const tagBuilding *FindEnemySiegeBuilding()
{
    for (const tagBuilding &building : info.enemy_buildings)
    {
        if (building.Blood > 0 && building.Type == BUILDING_SIEGE)
            return &building;
    }
    return nullptr;
}

// 站位圈上第 index 个落点相对锚点的格偏移。
// 用固定的 16 方向表而不是 sin/cos：这个文件里其它环状布局（箭塔 TOWER_RINGS、
// 战车弓兵 USR_SPREAD_RINGS）也都是整数表，保持一致且没有浮点取整问题。
static void StandoffRingOffset(int index, int &ddr, int &dur)
{
    static const int COS16[USR_STANDOFF_SLOTS] = {
        100, 92, 71, 38, 0, -38, -71, -92, -100, -92, -71, -38, 0, 38, 71, 92};
    static const int SIN16[USR_STANDOFF_SLOTS] = {
        0, 38, 71, 92, 100, 92, 71, 38, 0, -38, -71, -92, -100, -92, -71, -38};
    const int i = ((index % USR_STANDOFF_SLOTS) + USR_STANDOFF_SLOTS) %
                  USR_STANDOFF_SLOTS;
    ddr = COS16[i] * USR_STANDOFF_RADIUS / 100;
    dur = SIN16[i] * USR_STANDOFF_RADIUS / 100;
}

// 站桩阶段：返回 true 表示本帧已处理（部队正在圈上待机，不要再去打建筑）。
static bool ManageStandoff(UsrAI *ai)
{
    const tagBuilding *siege = FindEnemySiegeBuilding();
    if (siege == nullptr)
        return false; // 还没侦察到攻城武器厂，退回原来的进攻逻辑

    // 【只要视野里还有敌人，就不靠近基地】
    //
    // 判据是【当前视野】，与 ManageOffensiveArmy 下面那句 `if (HasVisibleEnemyArmy())`
    // 同口径 —— 两处必须一致，否则会出现「这里放行、下面又拦下」的互否。
    // 战线上的分工（见文件上方那两个前向声明的说明）：
    //     总攻 / 站桩 —— 看视野：有敌兵就先打兵，一个都看不见就推进拆建筑
    //     祭司        —— 看总量：敌方真打光了才出门去转换攻城厂
    //
    // 【为什么不再用 EnemyArmyStillExists()】它要求「敌方开局 51 个全部至少见过
    // 一次、且此刻一个都看不见」才返回 false。而敌方基地在地图对角（实测曼哈顿
    // 130 格），基地守军从不出门 —— 那 51 个里永远有一部分待在迷雾里，累计集合
    // 凑不满 → EnemyArmyStillExists() 恒为真 → 本函数【每一帧】都 return true
    // → ManageOffensiveArmy 下面「全体打目标建筑」那段永远执行不到。
    // 实测：整局 `enemyB` 最大到 6（敌方建筑全被侦察到）、`baseKnown=1`，士兵却
    // 一次都没碰过敌方建筑。
    //
    // 【代价，必须知道】HasVisibleEnemyArmy() 是全局视野判断：敌方守军退到迷雾里
    // 就会让它翻假，部队于是往里压、可能撞上回防的守军。这正是当初把它换成
    // 「总量」判据的理由（更早还有一版是数「锚点 34 格内的敌人数」，同样会抖）。
    // 取舍是【能不能拆掉基地】优先于【少送几波】—— 总量判据更保守，但它让获胜
    // 路径永远不启动，等于没有。
    if (!HasVisibleEnemyArmy())
        return false;

    const int orderInterval = 60;
    if (g_frame - offensiveLastOrderFrame < orderInterval)
        return true; // 还在节流窗口里，但阶段判定已经做完
    offensiveLastOrderFrame = g_frame;

    int slot = 0;
    for (const tagArmy &army : info.armies)
    {
        if (!IsOffensiveArmy(army))
            continue;

        // 【不搬动正在交战的单位】WorkObjectSN != -1 表示它当前有攻击/工作关系。
        //
        // 【为什么必须加这一条】站桩发 HumanMove、AssignFieldSelfDefense 发
        // HumanAction，两者都作用于同一批单位。而 HumanMove 会清空移动路径、
        // 顺带把攻击关系一起打断 —— 于是形成死循环：
        //     站桩 Move → 自卫重新 Attack → 站桩发现它不在圈上、又 Move → …
        // 实测游戏日志里表现为：24 个战车弓兵每隔 48 帧被重复下同一条
        // 「设置攻击目标为 方阵兵 91177」，中间夹着一批 HumanMove。
        // 交战中不动它，等它打完（WorkObjectSN 归 -1）自然会被送回圈上。
        if (army.WorkObjectSN != -1)
            continue;

        // 清掉进攻锁：否则 AssignFieldSelfDefense 的第 2 优先级会捧着旧锁
        // （可能是上一阶段留下的攻城武器厂），把部队带去冲建筑而不是守圈。
        // 清掉之后它会落到本地分支，只打视野/协防半径内的敌人 ——
        // 也就是「站圈只打送上门的」。注意这本身【不会】造成指令互相覆盖，
        // 覆盖的根源是 HumanMove 打断攻击关系，已由上面的「不搬动交战单位」解决。
        ClearArmyTargetLock(army.SN);

        // 【有敌人就退到 28 格以外 —— 只退不进】
        // 判据是「到锚点的距离」，不是「到槽位的距离」。
        //
        // 【为什么不能沿用「移到槽位」】原实现是「移到环上的槽位」，于是已经
        // 被推到 30 格外的单位会被【拉回】28 格的圈上 —— 等于主动走进敌方守军的
        // 追击范围（DEFENSE_CHASE_LIMIT = 25 格，量到它们的攻城武器厂），
        // 正对着要躲的东西。现在已经在圈外的单位一个都不动。
        const int anchorDis2 = BlockDis2(army.BlockDR, army.BlockUR,
                                         siege->BlockDR, siege->BlockUR);
        if (anchorDis2 > USR_STANDOFF_RADIUS * USR_STANDOFF_RADIUS)
            continue;

        // 太近了 → 退到自己的槽位（半径正好 USR_STANDOFF_RADIUS）。
        // 每个单位一个固定槽位（按它在 info.armies 里的顺序），保证散开；
        // 错开方向避免所有人都挤在正对家门口的那一格。
        int ddr = 0;
        int dur = 0;
        StandoffRingOffset(slot++, ddr, dur);
        int tx = siege->BlockDR + ddr;
        int ty = siege->BlockUR + dur;
        tx = max(1, min(MAP_L - 2, tx));
        ty = max(1, min(MAP_U - 2, ty));

        // 已经站在自己的槽位附近（3 格内）就不必重发 —— HumanMove 每次都会
        // 清空路径，反复下发会让部队原地打转，反而走不到。
        if (BlockDis2(army.BlockDR, army.BlockUR, tx, ty) <= 9)
            continue;
        ai->HumanMove(army.SN, (tx + 0.5) * double(BLOCKSIDELENGTH),
                      (ty + 0.5) * double(BLOCKSIDELENGTH));
    }
    return true;
}

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
  // 进攻时机：过了 USR_OFFENSIVE_FRAME（33000）之后。此前军队只做接敌自卫
  // —— AssignFieldSelfDefense 是「看到谁打谁」，从不主动推进。
  if (g_frame < USR_OFFENSIVE_FRAME)
    return;

  // 升时代前不进攻；升时代后需等【关键兵种科技】完成 —— 也就是车轮科技，
  // 它是战车弓兵解锁的前置（同一道门也用在 TryProduceChariotArcher）。
  //
  // 【为什么不用 researchedTechCount】它只在 CheckTechOrder() 里自增，而
  // CheckTechOrder 的调用者只有 ResearchTech（全文件无人调用）与
  // ResearchTechQueue（唯一调用者是仓库科技队列）。仓库科技取消之后它恒为 0，
  // `0 < TOTAL_REQUIRED_TECH(1)` 恒成立 —— 这道门会把整个进攻分支永久关死：
  // 士兵一局都不会碰敌方建筑，而且 UpdateEnemyBaseDiscovery() 排在这个 return
  // 之后也永远不执行，日志里表现为 `tech=0` 与 `baseKnown=0` 同时出现。
  if (info.civilizationStage == CIVILIZATION_TOOLAGE || !wheelTechReady)
    return;

  // 兵力不足时不进攻，避免送死。
  //
  // 【原先这里恒不成立】原计数只数近战步兵与普通/复合弓兵，
  // 而本 AI 的兵力全是战车弓兵（AT_CHARIOT_ARCHER）与少量战车 —— 那几种兵
  // 一个都不产，于是计数恒为 5（只有 5 个普通弓兵），`< 8` 永远成立，
  // 整个进攻分支等于被永久关死。改成数实际会出击的兵种。
  const int offensiveArmyCount =
      CountArmyBySort(AT_CHARIOT_ARCHER) + CountArmyBySort(AT_CHARIOT) +
      CountArmyBySort(AT_BOWMAN) + CountArmyBySort(AT_CLUBMAN) +
      CountArmyBySort(AT_BROADSWORDSMAN) + CountArmyBySort(AT_COMPOSITE_BOWMAN) +
      CountArmyBySort(AT_HOPLITE);
  if (offensiveArmyCount < 8)
    return;

  UpdateEnemyBaseDiscovery();
  if (!enemyBaseDiscovered)
    return;

  // 站桩阶段：视野里还有敌兵时，只把部队摆在敌方攻城武器厂外围的站位圈上，
  // 不直接冲基地。返回 true 表示本帧由它接管（机制详见 ManageStandoff）。
  // 视野里一个敌兵都看不见时它返回 false，落到下面「全体打目标建筑」。
  //
  // 判据与下面那句 `if (HasVisibleEnemyArmy())` 同口径（都是视野）——
  // 这样「站桩接管」与「打兵优先」不会互相否定。详见 ManageStandoff 的说明。
  if (ManageStandoff(ai))
    return;

  // 【敌方士兵在场时，放弃对建筑的仇恨】打兵优先于拆建筑。
  //
  // 为什么必须显式清锁：进攻会给每支部队写 currentTarget[SN] = 建筑 SN，
  // 而 AssignFieldSelfDefense 的第 2 优先级正是读这个锁 —— 不清的话部队会一路
  // 顶着守军硬拆建筑。
  //
  // 清完之后 AssignFieldSelfDefense 落到本地分支，去接视野内（7 格）/
  // 协防半径（12 格）的敌兵。效果正是需求要的「卡住敌方士兵的回退范围」：
  // 敌方守军有一条追击上限（enemyai.cpp 的 DEFENSE_CHASE_LIMIT = 25，量到它的
  // 攻城武器厂），超过就放弃目标往回走；我们的部队把战线压在它附近，那些守军
  // 就会在边界上来回摇摆、被反复消耗，而不是换掉我们拆建筑的单位。
  //
  // 只有真的一支敌兵都看不到时才回去拆建筑 —— 拆建筑是「守军清完了」的信号，
  // 不是「打不过就换目标」的退路。
  if (HasVisibleEnemyArmy())
  {
    for (const tagArmy &army : info.armies)
    {
      if (!IsOffensiveArmy(army))
        continue;
      const int locked = GetLockedArmyTarget(army.SN);
      if (locked != -1 && IsEnemyBuildingSN(locked))
        ClearArmyTargetLock(army.SN);
    }
    return;
  }

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
      // 【不限距离】这里是刻意不做距离判断的：只要目标建筑已被侦察到，
      // 全军就出发，哪怕它在地图另一头。引擎会自己寻路过去
      // （目标建筑是 goalOb，不受「目标格四邻全被占就 nullPath」那条影响）。
      // 不要把「超过 N 格就放弃」加进来 —— 那是防守逻辑的取舍，不是进攻的。
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

// 战车弓兵出厂后散开，不要全部堵在靶场出口。
//
// 【为什么必须 AI 自己做】引擎没有任何编队/集结/扇形展开接口 —— AI 能用的只有
// 四个指令（AI.h:24-27 的 HumanMove / HumanAction / HumanBuild / BuildingAction），
// 引擎自身也只有单位级的随机侧移（Core_List::crashHandle）。而 JustMoveTo 会
// 精确走到 DR0/UR0，路径为空时直线逼近、没有任何落点随机化
// （MoveObject.cpp:70-83）—— 所以把多个单位派到同一坐标的结果是【精确重叠】，
// 不是散开。
//
// 【为什么落点必须互不相同且可走】目标格的 4 个正交邻居若全是障碍（含任何单位），
// A* 直接返回 nullPath（Core_List.cpp:2071-2093），单位停 50 帧后
// 【整个移动指令被取消】（Core_List.cpp:1828-1837；判据见
// Core_CondiFunc.cpp:586-592，TIME_NOPATH_RETRY_MS=2000 / TimePerFrame=40）。
// 不是原地待命，是行动作废。所以这里给每个单位分配不同的槽位，并且靠
// USR_SPREAD_STUCK_FRAMES 的「卡住」判定换槽重试。
// 猎手在整群清完之前不采尸体 —— 把被打死的猎物留到最后一起收。
//
// 【为什么需要「事后纠正」而不是直接拦住】引擎的 CoreEven_Gather 状态机
// 在猎物死后会自动从「攻击」跳到「采集」（Core_List.cpp 的 setJump(3,6)：
// 猎物一旦 condition_Object2CanbeGather 成立就转去采），AI 拦不住那一跳。
// 所以这里做的是：一旦发现某个农民手上是一具【尸体】、而他所在的猎物群
// 还有活瞪羚，就把他重新指到群里最近的活瞪羚，让他接着杀。
// 整群清完后 aliveCount 归零，本函数不再干预，尸体自然进入常规采集
// （FindBestResourceSN 里也让它们重新变为可选）。
//
// 触发条件天然自限：重定向成功后他手上就是活瞪羚，条件不再成立，不会每帧重发。
// 这里再加一道节流，防止指令生效前的一两帧里连发多次、把刚建立的攻击关系打断。
// （状态 hunterRedirectFrame 的声明在文件上方，因为新对局重置块在这之前要清它。）
static void KeepHuntersOnLiveGazelles(UsrAI *ai)
{
    RebuildGazelleClusters();
    if (aliveGazelleSN.empty())
        return;  // 全场没有活瞪羚，无事可做

    for (const tagFarmer &farmer : info.farmers)
    {
        if (farmer.Blood <= 0 || farmer.FarmerSort != FARMERTYPE_FARMER)
            continue;
        if (farmer.WorkObjectSN < 0)
            continue;
        // 手上就是活瞪羚 → 正在猎杀，不管。
        if (aliveGazelleSN.find(farmer.WorkObjectSN) != aliveGazelleSN.end())
            continue;

        map<int, int>::const_iterator clusterIt =
            gazelleClusterOf.find(farmer.WorkObjectSN);
        if (clusterIt == gazelleClusterOf.end())
            continue;  // 手上不是猎物（采浆果 / 伐木 / 修建筑…）
        map<int, GazelleCluster>::const_iterator git =
            gazelleClusters.find(clusterIt->second);
        if (git == gazelleClusters.end() || git->second.aliveCount <= 0)
            continue;  // 这群已经清完，让他接着采

        map<int, int>::const_iterator lastIt =
            hunterRedirectFrame.find(farmer.SN);
        if (lastIt != hunterRedirectFrame.end() &&
            g_frame - lastIt->second < USR_HUNT_REDIRECT_INTERVAL)
            continue;

        // 挑群里离他最近的活瞪羚。
        int bestSN = -1;
        int bestDis2 = 1000000000;
        for (size_t i = 0; i < git->second.members.size(); ++i)
        {
            const int memberSN = git->second.members[i];
            if (aliveGazelleSN.find(memberSN) == aliveGazelleSN.end())
                continue;
            for (const tagResource &resource : info.resources)
            {
                if (resource.SN != memberSN)
                    continue;
                const int dis2 = BlockDis2(farmer.BlockDR, farmer.BlockUR,
                                           resource.BlockDR, resource.BlockUR);
                if (dis2 < bestDis2)
                {
                    bestDis2 = dis2;
                    bestSN = memberSN;
                }
                break;
            }
        }
        if (bestSN == -1)
            continue;

        CancelPendingGatherOrder(farmer.SN);
        ai->HumanAction(farmer.SN, bestSN);
        farmerLastOrderFrame[farmer.SN] = g_frame;
        hunterRedirectFrame[farmer.SN] = g_frame;
        {
            char buf[192];
            snprintf(buf, sizeof(buf),
                     "[HUNT] f=%d hunter=%d back-to-live-gazelle=%d", g_frame,
                     farmer.SN, bestSN);
            AiDebugLog(buf);
        }
    }
}

static void SpreadChariotArchers(UsrAI *ai)
{
    const tagBuilding *range = FindBuildingByType(BUILDING_RANGE, true);
    const tagBuilding *home = FindCenter();
    if (range == nullptr || home == nullptr)
        return;

    // 清理已消失单位的记录：避免 map 无限增长，也让 SN 复用后能被当成新单位。
    for (map<int, ChariotSpreadOrder>::iterator it = chariotSpreadOrders.begin();
         it != chariotSpreadOrders.end();)
    {
        const tagArmy *army = FindMyArmyBySN(it->first);
        if (army == nullptr || army->Blood <= 0)
            it = chariotSpreadOrders.erase(it);
        else
            ++it;
    }

    for (const tagArmy &army : info.armies)
    {
        if (army.Sort != AT_CHARIOT_ARCHER || army.Blood <= 0)
            continue;
        // 只处理空闲单位：正在战斗/执行任务、或在行军途中的一律不打扰，
        // 军令优先于散开。
        if (army.NowState != HUMAN_STATE_IDLE || army.WorkObjectSN >= 0)
            continue;
        if (BlockDis(army.BlockDR, army.BlockUR, range->BlockDR,
                     range->BlockUR) > USR_SPREAD_TRIGGER_RADIUS)
            continue; // 已经离开靶场周边，散开完成

        map<int, ChariotSpreadOrder>::iterator it =
            chariotSpreadOrders.find(army.SN);
        if (it != chariotSpreadOrders.end())
        {
            // 已到过落点：不再干预。注意不能删记录 —— 落点可能本身就落在靶场
            // 半径之内，删掉的话下一帧会被当成新单位重新分配，形成来回搬运。
            if (BlockDis(army.BlockDR, army.BlockUR, it->second.targetDR,
                         it->second.targetUR) <= USR_SPREAD_ARRIVED_RADIUS)
                continue;
            // 落点没变，且自上次下发后一直没挪窝（多半是落点被封死、
            // 寻路 nullPath 导致指令被取消）→ 换一个槽位重试。
            if (g_frame - it->second.orderFrame < USR_SPREAD_STUCK_FRAMES ||
                army.BlockDR != it->second.fromDR ||
                army.BlockUR != it->second.fromUR)
                continue;
        }

        // 落点用 HumanMove 下发，是【纯坐标移动】—— 引擎对这类移动会执行
        // 「目标格四邻全是障碍就 nullPath、停 50 帧后取消指令」那条判据
        // （Core_List.cpp:2091-2093，goalOb == NULL 才走）。所以这里正是
        // IsReachableAround 该用的地方：槽位被建筑或别的单位堵死时跳过它，
        // 换下一个 —— 否则指令会白发一次、还要等 150 帧的「卡住」兜底才重试。
        int tx = -1;
        int ty = -1;
        for (int attempt = 0; attempt < USR_SPREAD_SLOT_COUNT; ++attempt)
        {
            const int slot = nextChariotSpreadSlot++ % USR_SPREAD_SLOT_COUNT;
            const int ring = slot / 8;
            const int dir = slot % 8;
            const int cx = max(1, min(MAP_L - 2,
                                      home->BlockDR +
                                          USR_SPREAD_RINGS[ring][dir][0]));
            const int cy = max(1, min(MAP_U - 2,
                                      home->BlockUR +
                                          USR_SPREAD_RINGS[ring][dir][1]));
            if (!IsReachableAround(cx, cy, 1))
                continue;
            tx = cx;
            ty = cy;
            break;
        }
        if (tx < 0)
            continue;  // 所有槽位都被堵死，这一帧先不散开

        ChariotSpreadOrder order;
        order.targetDR = tx;
        order.targetUR = ty;
        order.orderFrame = g_frame;
        order.fromDR = army.BlockDR;
        order.fromUR = army.BlockUR;
        chariotSpreadOrders[army.SN] = order;

        ai->HumanMove(army.SN, (tx + 0.5) * double(BLOCKSIDELENGTH),
                      (ty + 0.5) * double(BLOCKSIDELENGTH));
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

// 接口：寻找箭塔射程内的攻击目标。
// 用途：玩家箭塔自动攻击。
//
// 【索敌优先级】射程内优先打「当前目标不是本塔」的敌人，只有射程内所有敌人的
// 目标都是本塔时才退回到「最近」的选法。
//
// 理由（策略文档《快速升级和取得胜利》的箭塔部分）：敌方近战兵一旦把攻击目标
// 设成本塔，它就会停在塔下拆塔，不会再去追祭司；而「没在打我」的那些敌人正是
// 冲着祭司去的。按「最近」选的话，塔会把火力全花在已经在拆塔的兵身上，
// 真正威胁祭司的那些反而没人管。这条策略的目的就是让祭司站在箭塔附近时
// 只被远程攻击（远程本来就在塔的射程外输出，塔也够不到）。
//
// 射程取自 ArrowTowerAttackRangeBlocks()，与 config.json:261 的
// DIS_ARROWTOWER = 7 一致，正是文档说的 7 格。
static int FindArrowTowerTarget(const tagBuilding &tower)
{
    const int towerRange = ArrowTowerAttackRangeBlocks();

    int bestFreeSN = -1;  // 目标不是本塔的敌人里最近的一个
    int bestFreeDis = 1000000000;
    int bestAnySN = -1;  // 射程内最近的敌人（不论它在打谁）
    int bestAnyDis = 1000000000;

    for (const tagArmy &enemyArmy : info.enemy_armies)
    {
        if (enemyArmy.Blood <= 0)
            continue;
        const int d = BlockDis(tower.BlockDR, tower.BlockUR,
                               enemyArmy.BlockDR, enemyArmy.BlockUR);
        if (d > towerRange)
            continue;
        if (d < bestAnyDis)
        {
            bestAnyDis = d;
            bestAnySN = enemyArmy.SN;
        }
        if (enemyArmy.WorkObjectSN != tower.SN && d < bestFreeDis)
        {
            bestFreeDis = d;
            bestFreeSN = enemyArmy.SN;
        }
    }

    for (const tagFarmer &enemyFarmer : info.enemy_farmers)
    {
        if (enemyFarmer.Blood <= 0)
            continue;
        const int d = BlockDis(tower.BlockDR, tower.BlockUR,
                               enemyFarmer.BlockDR, enemyFarmer.BlockUR);
        if (d > towerRange)
            continue;
        if (d < bestAnyDis)
        {
            bestAnyDis = d;
            bestAnySN = enemyFarmer.SN;
        }
        if (enemyFarmer.WorkObjectSN != tower.SN && d < bestFreeDis)
        {
            bestFreeDis = d;
            bestFreeSN = enemyFarmer.SN;
        }
    }

    // 全都在打本塔时退回最近的那个：此时没有更值得打的目标，也没必要来回切。
    return bestFreeSN != -1 ? bestFreeSN : bestAnySN;
}

// 接口：为所有我方箭塔执行自动索敌。
// 用途：当前目标离开射程或不存在时，自动攻击射程内最近敌军/敌农民。
static void AssignArrowTowerTargets(UsrAI *ai)
{
    for (const tagBuilding &building : info.buildings)
    {
        if (building.Type != BUILDING_ARROWTOWER)
            continue;
        if (g_frame - towerLastOrderFrame[building.SN] < USR_TOWER_ORDER_INTERVAL)
            continue;

        const int targetSN = FindArrowTowerTarget(building);
        if (targetSN == -1)
            continue;
        // 目标没变就什么都不做：对同一目标重复下达 HumanAction 会中止并重建关系，
        // 攻击进度会被反复清零。
        //
        // 【原先在这里提前 return 的那条】原实现是「当前目标还在射程内就 continue」，
        // 也就是塔一旦锁定就永不换目标。那样「敌军的目的是本塔时换打别人」这条
        // 索敌策略根本没有机会生效 —— 塔会一直打那个已经在拆塔的兵。
        // 现在改成每次（按 USR_TOWER_ORDER_INTERVAL 节流）都用 FindArrowTowerTarget
        // 重算一遍期望目标，只在它确实变了时才重新下达。
        if (building.Project == targetSN)
            continue;

        ai->HumanAction(building.SN, targetSN);
        towerLastOrderFrame[building.SN] = g_frame;
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
  // 【已移除】原先这里有一道 `if (!logisticsReady) return;` —— 理由是「后勤
  // 科技把兵营单位的人口占用从 1 降到 0.5，没点它就先腾人口等于白扔采集力」。
  // 但后勤研发本身早已停用（ManageEconomyAndProduction 里那段被注释掉了，
  // 因为它只对兵营单位生效、而兵营单位的配额 armyTarget 是 0），
  // logisticsReady 因此【恒为 false】—— 这道门让自毁从来没触发过。
  // 现在移除，自毁按下面的条件正常工作。
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

    // 【每帧无条件累计「敌方哪些单位见过」】
    //
    // 这只是一条事实记录：见过谁就记谁。它原先被塞在 ManageStandoff 里当
    // 副作用，于是被「有没有侦察到攻城厂」这个策略条件挡在门外，导致累计集合
    // 长期是空的（详见 UpdateEnemyArmySeen 的注释）。事实收集不该有前置条件，
    // 放主序列最前面，下游所有判断读到的都是本帧最新的集合。
    UpdateEnemyArmySeen();

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
    // 升时代帧号一次性记录。
    // 定期日志每 2000 帧才写一次，靠 civ= 字段只能看出「在这 2000 帧里的某处」
    // 升的。而升时代帧号正是第 1 批（农民 14→20、靶场 ×3 抬高消耗）的验收口径，
    // 需要精确值。
    {
        static int lastCiv = CIVILIZATION_UNKNOWN;
        if (info.civilizationStage != lastCiv)
        {
            char buf[192];
            snprintf(buf, sizeof(buf),
                     "[AGEUP] f=%d civ %d -> %d food=%d wood=%d farmers=%d",
                     g_frame, lastCiv, info.civilizationStage, (int)info.Meat,
                     info.Wood, (int)info.farmers.size());
            AiDebugLog(buf);
            lastCiv = info.civilizationStage;
        }
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
          "rangeReady=%d camp=%d "
          "home=%d pop=%.1f/%d "
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
          // range 用计数而不是 HasBuilding：靶场目标是 3 座（USR_RANGE_TARGET），
          // 只报 0/1 就看不出第 2、3 座到底建了没有、有没有在同时出兵。
          // rangeReady 是可接单的数量 —— 它长期小于 range 就说明有靶场闲置。
          CountBuilding(BUILDING_RANGE), CountAvailableRanges(),
          (int)HasBuilding(BUILDING_ARMYCAMP),
          // 房屋数与人口：Human_MaxNum = min((房屋数 + 中心) × 4, 50)
          // （Development.h:63/65/78）。生产侧的门槛是
          // nearPopulationCap = Human_Num + 1.9 >= Human_MaxNum ——
          // 「侦察兵造不出来」这类问题要靠这三个数才能判断：
          // 是人口真的满了，还是房屋没建够导致上限上不去。
          CountBuilding(BUILDING_HOME), (double)info.Human_Num,
          info.Human_MaxNum,
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
        //
        // 【本行已失效】仓库的所有科技研发已在 ManageEconomyAndProduction 里取消，
        // 所以 stockCur / stockPend 会一直是 0 / -1，need= 也永远是 0。
        // 数组留着只为将来恢复时能直接对照；不要据此判断「仓库研发卡住了」。
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
    // 先更新「最近一次敌方接触点」，供建筑选址（出兵建筑朝前线）使用。
    TrackEnemyContact();
    // 记录每个农民本帧所在的格，供「僵尸农民」判定使用。
    // 必须排在所有派工/抢修逻辑之前 —— 否则位置跟踪滞后，会把正常行进的
    // 农民误判成卡死。
    UpdateFarmerWatch();
    ManageEconomyAndProduction(this);
    // 猎手在整群清完之前只杀不采：引擎在猎物死后会自动把他跳到采集，
    // 这里做事后纠正，把他拉回群里还活着的瞪羚。
    KeepHuntersOnLiveGazelles(this);
    // 散开必须排在自卫之前：自卫是军令，会覆盖这里的移动指令；
    // 反过来放就会把已经下达的接敌指令改写成去集结，等于撤回进攻。
    SpreadChariotArchers(this);
    // 进攻排在自卫【之前】是有意的：进攻会把目标锁写进 currentTarget，
    // 而 AssignFieldSelfDefense 的第 2 优先级正是读这个锁 —— 先写后读，
    // 部队就会一路打向目标建筑、不会被本地的小股敌人拉走。
    // 例外是祭司被打（自卫的第 1 优先级），那一条不接受锁、会把人叫回来。
    ManageOffensiveArmy(this);
    AssignFieldSelfDefense(this);
    // AssignFarmerSelfDefense(this);   // 无条件版已停用，见下方 30000 帧的门控
    SacrificeExcessFarmers(this);
    // 农民自卫：30000 帧后启动（对应「第三波骚扰之后」，enemyai.cpp:45 的
    // TAT = 21000 已过）。此前农民遇袭一律靠撤离、不还手，避免早期被零星
    // 骚扰牵着走、把采集力从食物/木头上拽开；进入后期农民挨打时则就地反击。
    if (g_frame >= USR_FARMER_SELF_DEFENSE_FRAME)
        AssignFarmerSelfDefense(this);
    DispatchScouts(this);
    AssignArrowTowerTargets(this);
}
