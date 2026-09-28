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
//
// 【12 → 40，按需求试】原值 12 只能覆盖「身边一起作战的部队」（比如两辆战车
// 互相掩护）。而侦察骑兵的任务是深入敌方基地（130 格外）把守军勾引出来，
// 12 格根本够不到它 —— 那条协防逻辑对侦察兵等于死代码。
//
// 40 的来源：军队在 USR_ARMY_RALLY_FRAME 之后会先到地图中间集结，从那儿到
// 敌方基地外 25 格（守军被勾引后的折返线）大约就是这个量级；再算上站桩环
// 32 格，40 能把「中场 → 前线」这一段都罩住。
//
// 【副作用（已知）】半径变大 = 部队会被更远的交火吸走。这条判据的触发条件
// 是「友军正被敌人攻击」（对方的 WorkObjectSN 指向它），所以它只在真的有人
// 挨打时才生效，不会因为「远处看得见敌人」就跑 —— 但 40 格确实比 12 格
// 容易踩到，可能把正在推进的部队拽去救另一个方向的队友。
// 如果实测发现部队因此散掉，往回收到 25~30 之间。
static const int USR_FIELD_ASSIST_RADIUS = 40;
// 普通军队【主动发现敌军】的最大欧氏距离（格）。这是 AssignFieldSelfDefense
// 第 4 优先级（FindEnemyArmyInVision）的半径。
//
// 【7 → 12，按需求试】7 格的原意是"只打送上门的"：站桩环在 32 格、敌方守军追到
// DEFENSE_CHASE_LIMIT(25) 折返，两者相距正好 7 格 —— 所以 7 格刚好能在守军停在
// 折返线上的那一刻把它纳入射程（塔射程也是 7）。
// 提到 12 之后，视野外的敌人也会被主动纳入：守军停在 25 格处时离环上的我们 7 格、
// 本来就够得着；但【守军还没冲到 25、还在往里走】的那一段，以及停在更里侧（比如
// 20 格）的敌人，现在也会把部队从环上叫下去。
// 代价：敌方远程（投石车射程 10）在这个距离上能打到我们，而我们战车弓兵只有 7
// （研究完木材/工艺后 9）—— 会被迫在挨打的情况下靠近。
static const int USR_FIELD_ARMY_AGGRO_RADIUS = 12;
// 军队【主动去打狮子】的最大欧氏距离（格）。
// 原先与上面那条共用一个常量；上面从 7 提到 12 时把狮子一起放大会让部队为了打猎
// 从阵线上跑开 12 格，所以拆出独立的一条，保持原值不动。
static const int USR_FIELD_LION_AGGRO_RADIUS = 7;
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
// 总攻阶段起始帧：此后祭司进入「只保存实力」状态 —— 不再治疗、不再【转换
// 敌方士兵】，视野内没有敌人时留在基地；唯一保留的动作是转换敌方攻城武器厂
// （那是获胜条件本身，见 ManagePriest 里那段转换分支）。
// 祭司只有 100 血、近战与远程防御都是 0、不能自愈且不可补充（兵种列表里没有
// 第二个），总攻期间让它待在箭塔覆盖圈里，比跟部队出去换血更稳。
// 【转士兵为什么也必须停】换来的一个士兵对胜负没有贡献，却会把祭司按在
// 敌方基地里读条换血；它挨不起这个交换。
//
// 【改动历史】原为 30000（第三波骚扰 TAT=21000 之后的总攻起点），2026-09-28
// 按要求提前到 26000。
// 【为什么必须跟着军队总攻一起提前】军队总攻已提前到 USR_OFFENSIVE_FRAME = 28000
// （见那里）。而祭司在这一帧【之前】是「主动走出去转换敌方士兵」的状态 —— 两个
// 时间点若仍按原先那样差 2000 帧（28000 打、30000 才收敛），就会出现
// 28000~30000 这 2000 帧「军队已经在拆塔、祭司还在满地图追着转换士兵」：它会被
// 按在敌方基地里读条换血，而它只有 100 血、近战与远程防御都是 0、不可补充。
// 【取值理由】26000 比总攻(28000)早 2000 帧，与「集结(25000) → 祭司先收手回位 →
// 军队压上」的顺序一致。
static const int USR_PRIEST_PASSIVE_FRAME = 26000;
// 挨打时触发撤退的敌人搜索半径（格，欧氏）。
// 比各兵种射程更远：实测真正打伤祭司的战车弓箭手恰好停在判定边缘
// （tE2=82 对阈值 81），而锁定祭司的近战兵从 10~20 格外走过来。
static const int USR_PRIEST_HURT_THREAT_RADIUS = 14;
// 祭司开局探路的走法：抓一次「最初视野的边缘」，沿它走一圈就收工。
//
// 【为什么要换成固定一圈】原先用的是侦察骑兵那套动态前沿（FindBestScoutFrontier
// 每轮重新找「已探明且邻接未探明」的点），而当时结束探路的唯一条件是「视野里
// 出现瞪羚」（也就是 FinishPriestExplore 当时的唯一调用点）。问题在于：前沿集合随已探明
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
// 祭司「贴到敌方攻城武器厂边上」的判定（格）。BlockDis2 <= 本值 表示"已经在厂的
// 正交相邻格里"：厂是 3×3，中心到正交相邻格中心的距离正好 2 格 → BlockDis2 = 4。
//
// 【为什么不取 10 格】引擎对「祭司转建筑」有它自己的一套判据，
// 与转士兵的 DIS_PRIEST = 12 完全不同（Core_CondiFunc.cpp:290-297）：
//     disAttack = 建筑 SideLength/2 + 2 * CRASHBOX_SINGLEOB
// 代进 config.json：3×3 的厂 SideLength = 3 × BLOCKSIDELENGTH(35.777) = 107.3 px，
// CRASHBOX_SINGLEOB = 5.96 px
//     → 53.7 + 11.9 = 65.6 px = 1.833 格（中心到中心）
// 而斜邻格的中心在 2.83 格 —— 已经超出 1.833；正交相邻格的中心在 2.0 格，
// 也要靠碰撞盒压到 1.667 格才算进范围。
// 【所以必须贴上去】，10 格处是绝对转不到的 —— 而且原实现"到 10 格就不管了"
// 会让祭司停在厂外干等一个可见敌兵来触发转换（实测它停在 (15,77)、minDis=9999）。
static const int USR_PRIEST_SIEGE_TOUCH_DIS2 = 4;
// 攻城厂【还没被侦察到】时，祭司朝估算方位逐步接近的步长与最近距离（格）。
//
// 【为什么需要「走过去」这一步】转换攻城厂需要它的 SN，而 SN 只能从
// info.enemy_buildings 拿 —— 也就是必须【已经看到】它。可祭司平时只在 40 格带上
// 待命，那个距离的视野覆盖不到厂；而「走向厂」那段又要求 FindEnemySiegeBuilding()
// 非空（同样要求已经看到它）。于是形成死锁：看不到就不走过去、不走过去就永远
// 看不到。实测某局祭司停在 (14,7)、离锚点曼哈顿只有 6 格，满血、视野内无敌人，
// 却整整 1500 帧没发起过一次转换。
//
// 【为什么朝估算方位走是可靠的】四张图的敌方基地都在我方市镇中心的对角线上
// （实测偏移 map(-66,+64) / map1(+73,-65) / map2(-57,-83) / map3(+64,-58)，
// 见 EstimateEnemySiegeAnchor 的注释），所以"往锚点方向走"这个方向不会错，
// 只有估算误差（最差曼哈顿 26 格）会让厂晚一点进入视野。
//
// 【为什么是「逐步」而不是「走到固定距离」】固定距离的话，走到那儿还没看见厂
// 就会永久停住 —— 而我们并不知道厂离锚点究竟有多远。每轮推进一个步长，
// 走到最近距离为止，视野扫过的范围就一路扩过去。
static const int USR_PRIEST_SIEGE_PROBE_STEP = 4;
static const int USR_PRIEST_SIEGE_PROBE_MIN = 3;

// 祭司【选取】攻城厂作为转换目标的最大距离（格）。
//
// 【12 → 40】原值 12 要求祭司先靠 HumanMove 精确走到厂 12 格以内，才肯把厂
// 选为转换目标；走到那个位置的过程里最后要贴到厂边上（USR_PRIEST_SIEGE_TOUCH_DIS2），
// 而厂边那两格极可能被我们自己的单位挤着 —— 拆塔的农民是【近战、必须贴到厂边上】，
// 牵制箭塔的士兵也在那一带。IsReachableAround 只查建筑与地形、【不查单位】，
// 所以这些落点会被判成"可用"，而 HumanMove 是纯坐标移动：目标格站着人就挤不
// 进去 —— 表现为「祭司满血、原地不动」，日志里还看不出原因（实测一局它停在
// (14,7) 整整 500 帧，[CONV] 与 [CONVBLOCK] 都没有输出）。
//
// 放宽到 40 格（= 祭司的前线驻留距离）之后，它在驻留带上就能对厂建立
// HumanAction 关系，由【引擎】驱动它走完最后那一段：关系式移动会自己找到能
// 转换的位置（祭司转建筑的判据是「贴邻 + 已建成」，见 Core_CondiFunc.cpp），
// 不需要我们替它算精确落点，也就绕开了「落点被自己人占住」这件事。
static const int USR_PRIEST_SIEGE_PICK_DISTANCE = 40;

// 祭司进厂前，攻城厂这个半径内的箭塔必须【已被摧毁】，而不是"被牵制"。
//
// 【为什么"被牵制"不够】门控最初写的是「每座塔都有人以它为工作目标」，
// 但那只是"有人在打它"，塔照样开火。实测致命一局：
//   [FARMESC-TOWER] f=41285 sn=11099 blood=15/125 disToPriest=3
//   [FARMESC-TOWER] f=41285 sn=11091 blood=34/125 disToPriest=4
//   [FARMESC-TOWER] f=41285 sn=11097 blood=15/125 disToPriest=9
// 三座塔（15/15/34 血）离祭司只有 3~9 格、全部在射程 7 之内，一齐开火 ——
// 祭司那时刚进入转换状态（convAlive=1 convB=1），200 帧内从 100 血被打到 13。
// 而同一时刻 near10=0（10 格内没有敌方军队），说明这份伤害跟敌方部队无关。
//
// 【半径为什么是 10】塔射程 7（config.json:261）；祭司贴到厂边时自己在厂的
// 2 格内，所以「厂 10 格内的塔」正好覆盖"够得到祭司"的全部塔，再留 3 格余量
// 应对贴邻判据的取整。
//
// 【当前已不使用】门控按需求改成了「所有敌方箭塔全部被摧毁」才放行
// （见 SiegeTowersCleared），比这个半径严得多，所以这个常量不再参与判断 ——
// 留着是为了记录下面那组实测数据与半径的推导，将来若要放宽回「只清厂周围」
// 可以直接启用。
static const int USR_PRIEST_SIEGE_TOWER_CLEAR = 10;
// 祭司「正在转换」的判定距离（格）。取 config.json 的 DIS_PRIEST = 12，
// 也就是它真正能转换的距离。
//
// 注意这与「该不该把某个敌人选为转换目标」是两件事：
//   · 这里的用途是判断祭司【当下一刻是否真的在转换】—— 决定要不要吃
//     「转换中不撤退」那个代价（见 conversionTargetAlive 的说明）。
//   · 选取目标时不做这个距离限制：祭司本来就需要先走过去再转换，
//     按 12 格过滤会把「主动接近」这条路整个废掉。
static const int USR_PRIEST_CONVERTING_RADIUS = 12;
// 祭司的位置约束：不得进入【敌方基地】周围 USR_PRIEST_ENEMY_BASE_KEEPOUT 格以内。
//
// 【取代了原先的两条】祭司原来有两条以【我方市镇中心】为参照的位置规则：
//   · USR_PRIEST_LEASH_RADIUS = 50 —— 离中心超过 50 格就拉回来（活动缰绳）
//   · USR_PRIEST_HOME_RADIUS = 12  —— 离中心超过 12 格就「回家」（守家）
// 两条都已删除，改成以【敌方基地】为参照的一条红线：有敌人时不许靠近敌方基地，
// 其余地方可以自由活动。
//
// 【为什么必须连缰绳一起去掉】敌方基地离我方中心约 130 格（四张图的曼哈顿距离
// 122~140，见 EstimateEnemySiegeAnchor 的实测）。只要缰绳还在（把祭司拴在中心
// 50 格内），祭司离敌方基地就永远 ≥80 格，这条 32 格红线一次都不会触发 ——
// 等于没改。所以两条必须一起去掉。
//
// 【与「去转换攻城厂」的关系】转换要求走到厂 10 格内，与 32 格红线直接矛盾。
// 解法是红线【只挂在「有可见敌人」上】：敌人还在时祭司远离敌方基地，
// 敌人清空了才放它去转换（那条路本来就排在上面、会先 return）。
static const int USR_PRIEST_ENEMY_BASE_KEEPOUT = 32;
// 祭司【跟随军队出征】时在前线的驻留半径（格，相对敌方基地锚点）。
//
// 【为什么是 40】敌方守军的追击上限是 DEFENSE_CHASE_LIMIT(25)（量到攻城武器厂，
// 也就是我们的锚点），远程兵射程 9（config 的 DIS_* 上限 + 科技）。40 格在追击
// 上限外 15 格 —— 站在这儿敌人正常够不到，祭司才能在前线待得住。这就是"站
// 40 格以外"的直接目的：**用距离换生存**，而不是靠撤退。
static const int USR_PRIEST_FRONT_POST_DISTANCE = 40;
// 驻留半径的容差带（格）：距离落在 [40−6, 40+6] 里就不动。
// 没有带的话，它每帧都要为"差一格"重算一次落点，而 HumanMove 每次都会清空路径
// —— 表现就是原地抖。上边界同时是"别缩在家里"的门槛：超过 46 格就往里拉。
static const int USR_PRIEST_FRONT_POST_BAND = 6;
// 祭司遇袭时的撤退距离（格）：退到「市镇中心背离威胁那一侧」这么多格处。
// 见 GetPriestEmergencyPoint —— 旧公式的实际位移只有 4 格，等于没退。
// 取 14：大于箭塔射程（7）的两倍，退出去之后追兵要重新跑一段；也不至于远到
// 脱离基地的掩护范围。
static const int USR_PRIEST_RETREAT_DISTANCE = 14;
// 一个撤退点被判「走不到」之后拉黑多少帧（期间选点绕开它）。
// 取 600：与 USR_PRIEST_POST_CONVERSION_RETREAT_FRAMES 同量级 —— 都是"这段时间
// 里别再做那件事"。太短会立刻被重新选中（敌人没变的话它还是最优解），
// 太长会把一个其实只是被临时堵住的点浪费掉。
static const int USR_PRIEST_BAD_POINT_FRAMES = 600;
// 祭司【开局探路】的半径上限（格，相对市镇中心）：只探这个范围内的前沿点，
// 探完就收工。
//
// 【历史：50 → 120】原值 50 探不到敌方基地 —— 实测四张图上敌方基地离我方中心
// 曼哈顿 122~140 格（欧氏 91~104），所以 50 格的探路半径意味着
// FindEnemySiegeBuilding() 整局都是 nullptr（实测 `enemyB` 最多到 1），
// 站桩圈和祭司的出门段全部卡死在那上面。120 覆盖整张图：四张图的中心都在
// 角落一侧，到最远角落的欧氏距离最大约 110。
//
// 【120 → 40（按需求）】按上面那组数据，40 格比原来的 50 格【更够不到】敌方
// 基地，所以「靠祭司开局探路发现攻城厂」这条路等于彻底关死。这是有意的取舍：
// 发现敌方基地由侦察骑兵（DispatchScouts，已改成优先探对角线）和军队推进
// （ManageOffensiveArmy → UpdateEnemyBaseDiscovery）负责，那两条本来也在做
// 这件事。收益是祭司开局探得更少、更早回到基地避险 —— 它只有 100 血、防御
// 为 0、不可补充，实测有过开局探路收工时停在离家几十格处、两边规则都不管的
// 情况。
//
// 前沿集合（已探明陆地且邻接未探明）随探索不断外扩，所以必须有这个上限 ——
// 没有它探路永远不会结束，祭司被永久扣在探路分支里回不了基地（历史故障）。
// 实现是「分层推进」：走完一圈 → 前沿外扩一圈 → 再抓一圈，直到半径之外没有
// 前沿点为止。
static const int USR_PRIEST_EXPLORE_RADIUS = 40;
// 祭司探路的【时间】截止帧：到这个帧无论如何收工（回基地），哪怕半径还没探完。
// 与半径上限是并列的两个出口，谁先满足用谁。
//
// 【为什么是 5000、不能放宽】祭司只有 100 血、近战/远程防御都是 0、不可补充，
// 而三波骚扰从 FAT=6000 就开始了 —— 让它在那之前回基地，比多探几格重要。
//
// 【与半径的关系，必须知道】5000 帧最多只够探 50~60 格（祭司移动约 1 格 / 12 帧，
// 而且探路走的是「前沿环」不是直线）。所以：
//   · USR_PRIEST_EXPLORE_RADIUS 放宽到 120 只是【解除了半径这个提前收工的出口】，
//     让祭司把 5000 帧用满、走到时间上限才回，而不是探完 50 格就停；
//   · 它【不会】让祭司真的走到 120 格 —— 那是时间上限在管。
// 想靠祭司找到 130 格外的敌方基地是不现实的，那件事归侦察骑兵。
static const int USR_PRIEST_EXPLORE_UNTIL_FRAME = 5000;
// 【已删除】USR_PRIEST_HOME_RADIUS = 12（「离市镇中心超过 12 格就回家」的守家规则）。
// 按需求改成以敌方基地为参照的 USR_PRIEST_ENEMY_BASE_KEEPOUT，见文件上方。
// 第一个侦察骑兵的生产时间点。它执行「视野内出现敌人即撤回市镇中心」的
// 保命策略，不承担基地侦测（角色判定见 DispatchScouts）。
//
// 【改动历史】原为 33000 → 按要求提前到 30000（= 祭司进入「保存实力」的时间点）
// → 2026-09-28 按要求再提前到 25000，与军队集结（USR_ARMY_RALLY_FRAME）同帧。
// 【与祭司保存实力的关系】侦察兵 25000 出厂，祭司 26000 进入保存实力
// （USR_PRIEST_PASSIVE_FRAME，已从 30000 提前到 26000），两者相差 1000 帧。
static const int USR_SCOUT_FIRST_FRAME = 25000;
// 侦察骑兵的【总数】。用于在人口上限里给它们始终预留位置 ——
// 必须按总数预留、而不是等它该出场时才留：实测人口在 f=30000 之前就已经
// 顶到 50/50，那时再留已经来不及，「侦察兵一个都造不出来」就是这么来的。
static const int USR_SCOUT_TOTAL = 2;
// 【开关】是否保留「保命型」侦察兵。true = 保留（当前取值）。
//
// 保命型（`g_frame < USR_SCOUT_RECON_FRAME` 时出厂的那个）会做两件事：
//   ① 20 格内有敌人 → 先打一下建立仇恨，再撤回市镇中心当诱饵，把追兵引进
//      箭塔与部队的火力圈（见 DispatchScouts 里「撤回市镇中心」那一段）；
//   ② 9 格内有敌人 → 紧急撤离到 12/10/8/6 格外的安全点，撤离完成前探索挂起。
// 侦测型两样都不做：硬闯敌方基地、专职探图。
//
// 【曾短暂设成 false 试过】取消保命型能让侦察兵从一开始就朝敌方基地方向探，
// 但代价是它一进敌方视野就没了（60 血 / 3 攻 / 0 防御、不会补充），
// 而敌方基地周围必然有驻军。已改回 true。
//
// 用开关而不是把 isRecon 的帧号比较删掉：两种行为都保持可达、便于切换，
// 也不会产生 -Wunused-function 警告。
static const bool USR_SCOUT_KEEP_ALIVE_TYPE_ENABLED = true;
// 后期把农民上限压到 USR_FARMER_LATE_TARGET 的帧号。
// 此时经济已成型、军队才是胜负手，把人口让出来给兵。
// 注意：这不是「立刻裁到 5 个」——只是不再补产，多出来的由
// SacrificeExcessFarmers 逐步自毁腾出人口（每 300 帧一个）。
static const int USR_FARMER_LATE_FRAME = 35000;
static const int USR_FARMER_LATE_TARGET = 5;
// 专职侦测敌方基地的侦察骑兵的生产时间点。
// 在 USR_SCOUT_FIRST_FRAME 之后产出的侦察兵执行「视野内出现敌人即撤回市镇中心」
// 的保命策略，不承担基地侦测；这一帧之后再补 1 个专门负责侦测敌方基地的。
//
// 【改动历史】原为 36000 → 按要求提前到 33000 → 2026-09-28 按要求提前到 28000
// （与总攻 USR_OFFENSIVE_FRAME 同帧，保持下面那条「专职侦测兵开始工作 = 军队
// 具备出击条件」的关系）。
// 【为什么不能比总攻晚】ManageOffensiveArmy 里有一道
// `if (!enemyBaseDiscovered) return;` —— 基地没被发现时总攻根本不启动，而专职
// 侦测兵是发现敌方基地的主要手段。它若留在 33000，28000 这个总攻时间点就只是
// 摆设（军队会一直等到基地被发现为止）。
static const int USR_SCOUT_RECON_FRAME = 28000;
// 开始允许军队主动进攻敌方建筑的帧号。
// 此前军队只做接敌自卫（AssignFieldSelfDefense：看到谁打谁，从不主动推进）；
// 过了这一帧之后，已经侦察到的敌方建筑会被列为攻击目标。
// 与 USR_SCOUT_RECON_FRAME 同帧是有意的：专职侦测兵开始工作时，
// 军队也刚好具备出击的条件。
//
// 【改动历史】原为 33000，2026-09-28 按要求提前到 28000 —— 集结窗口随之整体
// 前移（见 USR_ARMY_RALLY_FRAME）。
// 【提前它的实际效果有上限】上面那道 `!enemyBaseDiscovered` 门控：敌方基地在
// 28000 之前没被侦察到的话，军队仍要等到发现它的那一帧才动。判据看 [AI] 行的
// baseKnown=。
static const int USR_OFFENSIVE_FRAME = 28000;
// 军队开始向【地图中间】集结的帧号。取 25000 —— 2026-09-28 总攻提前到 28000 后
// 一并前移，保持原来的 3000 帧集结窗口（25000~28000）。原先取 30000，与侦察骑兵
// 出厂（USR_SCOUT_FIRST_FRAME）、祭司「保存实力」（USR_PRIEST_PASSIVE_FRAME）同帧。
//
// 【必须严格早于 USR_OFFENSIVE_FRAME】集结分支的判据是
// `g_frame >= USR_ARMY_RALLY_FRAME && g_frame < USR_OFFENSIVE_FRAME` ——
// 一旦反超（集结帧 ≥ 总攻帧），这个区间恒为空，集结永远不会执行、而且不报错。
// 【与祭司保存实力的关系】它不再与 USR_PRIEST_PASSIVE_FRAME 同帧 —— 该常量已从
// 30000 提前到 26000，所以现在的顺序是「集结 25000 → 祭司 26000 收手回位 →
// 军队 28000 压上」：祭司先收敛、军队后出发。
//
// 【为什么要有集结这一步】USR_OFFENSIVE_FRAME 一到，ManageOffensiveArmy 会让
// 全军直奔目标建筑。而部队是零散生产出来的、散在基地各处，一起出发会拉成一条
// 长线，先到的被守军逐个吃掉。先在地图中心（双方基地之间的中点，四张图的敌方
// 基地都在我方市镇中心的对角线上）把队伍收拢，到点再整体压上。
static const int USR_ARMY_RALLY_FRAME = 25000;
// 集结指令的下发间隔。取 120：基地到地图中心约 60~70 格，按 HUMAN_SPEED
// 2.236 走完约 1000 帧；这个间隔够走一段，又不至于频繁重发（每次 HumanMove
// 都会经 suspendRelation 清一次路径）。
static const int USR_ARMY_RALLY_ORDER_INTERVAL = 120;
// 离地图中心的距离平方在这个值以内就算到位（20 格 = 400）。
// 取 20 而不是更小：落点本身分布在 8~16 格的圈上（见下面两个常量），
// 判定半径必须罩得住最外圈，否则外圈的单位会被反复要求「去中间」。
static const int USR_ARMY_RALLY_ARRIVED_DIS2 = 400;
// 【当前未使用】集结圈的基准半径与层距（格）。
//
// 曾经用它们把部队铺成 16 方位 × 3 圈（48 个落点），但实现有 bug 已回退：
// 槽位是按「遍历顺序」现算的，而到位的单位会被 `continue` 跳过、不占槽位 ——
// 于是每有一个单位到位，后面所有单位就往前顶一格、集体换位置，表现为部队在
// 中心来回跑。
//
// 【回退后的现状】所有单位 HumanMove 到地图中心同一格。这确实会挤：后来者
// 进不去，引擎还会因为目标格四邻全被占而按 nullPath 反复取消指令。若要重新
// 做散开，槽位必须按【单位 SN】固定映射、不能依赖遍历时到没到位。
static const int USR_ARMY_RALLY_RING_BASE = 8;
static const int USR_ARMY_RALLY_RING_STEP = 4;
// 农民自卫的启动帧。此前的农民遇袭一律靠撤离，不还手 —— 早期被零星骚扰
// 牵着走会白白损失采集力；三波骚扰过后（enemyai.cpp:45 的 TAT = 21000 已过）
// 再让农民挨打时就地反击。
static const int USR_FARMER_SELF_DEFENSE_FRAME = 30000;
// 【开关：农民是否参与战斗】2026-09-28 按要求关闭 —— 农民不还手、也不去救祭司。
//
// 【关掉的是哪三件事】AssignFarmerSelfDefense 一个函数同时管三件，会一起停：
//   ① 挨打就反击（FindDirectThreatToFarmerSN）；
//   ② 去救正在被敌人攻击的祭司（FindThreatToPriestForFarmer，半径
//      USR_FARMER_PRIEST_HELP_RADIUS = 8）—— 按需求一并取消；
//   ③ 3 格内的任意敌人就打（FindNearbyEnemyForFarmer）。
//
// 【为什么用开关而不是注释掉调用】函数仍被引用，不产生 -Wunused-function 警告；
// 也不至于让 USR_FARMER_SELF_DEFENSE_FRAME 变成未使用的常量。恢复只改一个
// true/false。注意要恢复的只有 ①③ 的话，得改函数内部（把 ② 那一段挪出开关），
// 单改这个常量做不到。
//
// 【关掉之后农民遇袭会怎样】仍然会撤离 —— 那是【另一套机制】
// （farmerThreatLastFrame / farmerSafeSinceFrame 两个表驱动的那段撤离判定），
// 与自卫无关、不受本开关影响。所以是「不还手、只跑」，不是「站着挨打」。
//
// 【代价，已知取舍】祭司从此失去最后一条被救的路径：农民护送已停用，这里再关掉，
// 祭司挨打时不会有任何农民去支援 —— 而它只有 100 血、近战与远程防御都是 0、
// 不可补充。
static const bool USR_FARMER_SELF_DEFENSE_ENABLED = false;
// ── 农民护送祭司：总攻阶段的第二支力量 ──────────────────────────────
//
// 【这条机制的赌注，必须先知道】到 USR_FARMER_ESCORT_FRAME 会把【全部农民】
// 一次性投入前线（USR_FARMER_ESCORT_MAX = 20），此后经济不再有采集力。
// 三条既有设计正好接住这个取舍：
//   · USR_FARMER_LATE_FRAME(35000) 之后本来就只保留 5 个农民；
//   · 祭司 26000 帧后进入「保存实力」，唯一的获胜路径是转换敌方攻城厂
//     （MainWidget::isWin 只认 isConverted() 的 BUILDING_SIEGE）；
//   · 敌方箭塔【不会攻击农民】—— enemyai.cpp:238-241 的 Defend 集合只收
//     enemyInfo.enemy_armies（我方军队），农民与建筑那两行 push_back 都被
//     注释掉了；enemyai.cpp:447-453 的塔目标就是 Defend.back()。
//     所以拆塔对农民是【单方面输出】，这是这件事在后期做得成的前提。
// 反过来说，若实测发现农民在路上被敌方【军队】吃光（军队会主动打 Farmer，
// enemyai.cpp:440-442），就该把 USR_FARMER_ESCORT_MAX 调小、或把
// USR_FARMER_ESCORT_FRAME 推后。

// 农民投入的帧号。取 35000 = USR_FARMER_LATE_FRAME —— 那一帧之后农民上限
// 本就压到 5、经济进入收缩期，把采集力一次性交给前线，与「此时经济已成型、
// 军队才是胜负手」的既有判断一致。
// 【与军队总攻(USR_OFFENSIVE_FRAME = 28000)相差 7000 帧是有意的】：军队先出发
// 去清守军，农民后走，避免在完全没有掩护的情况下先撞上敌方军队。
// （这个机制目前已由 USR_FARMER_ESCORT_ENABLED=false 停用，消息仅供参考。）
static const int USR_FARMER_ESCORT_FRAME = 35000;

// 祭司【跟随军队出征】到前线驻留带的帧号。取 35000 = USR_FARMER_ESCORT_FRAME，
// 也就是「农民和祭司一起出发」—— 两者同帧动身、路程与移速都相近（农民 2.236、
// 祭司 2.03 px/帧），才能同时到位；任何一方先走都会让另一方在敌方基地门口
// 单独暴露几百帧。
//
// 【为什么不直接改 USR_OFFENSIVE_FRAME(28000)】那个常量还被 ManageOffensiveArmy
// 用作军队总攻的时机，一起改会把军队也推迟 2000 帧 —— 而需求只要求祭司与农民
// 同步，军队仍应提前出发去清守军（这正是上面那条「差 2000 帧是有意的」）。
static const int USR_PRIEST_FRONT_POST_FRAME = 35000;

// 征召人数上限。取 20 = USR_FARMER_TARGET（村民总数目标）—— 即「全部农民」。
// 留这条口子的用途：实测若发现经济归零导致军队断粮（造兵在跑但产出为零），
// 把它调小就是「留几个人在家采集」，不需要改别的地方。
static const int USR_FARMER_ESCORT_MAX = 20;

// 阶段 A 的集结半径（格，相对敌方锚点）。
// 取 46 = USR_PRIEST_FRONT_POST_DISTANCE(40) + USR_PRIEST_FRONT_POST_BAND(6) ——
// 正好是祭司驻留带的【外沿】。两个理由：
//   · 需求是「待在 40 格以外」，取带的外沿让这一条由构造保证，不靠额外判断；
//   · 落在祭司靠家那一侧，不会插到祭司与敌方基地之间（那会把守军先引到农民身上）。
// 不用 40：那是祭司自己的驻留点，会和它抢格。
static const int USR_FARMER_ESCORT_POST_DISTANCE = 46;
// 第二圈半径。取 54 = 内圈 + 8 —— 两圈之间隔 8 格，远大于两个单位的碰撞盒
// （CRASHBOX_SINGLEOB ≈ 5.96 px ≈ 0.17 格），不会自己挤自己。
static const int USR_FARMER_ESCORT_OUTER_RING = 54;
// 槽位总数 = 16 方位 × 2 圈。20 个农民需要 20 个互不相同的落点，
// 32 给「落点在海里 / 被建筑占住 / 走不到」留了 12 个备槽。
static const int USR_FARMER_ESCORT_SLOTS = 32;

// 落点离祭司的最大距离（格，欧氏）。取 28 —— 需求是「和祭司一起」：
// 没有这一条，外圈背面那半圈会把队伍拆成两处。28 覆盖得住最坏几何关系
// （农民在 54 格外圈、祭司在 40−6=34 格内圈，轴向差 20，加上角度偏移仍在 28 内）。
static const int USR_FARMER_ESCORT_LEASH = 28;

// 距落点多少格算「已就位」——平方值 16，即 4 格。
// 比战车散开的 USR_SPREAD_ARRIVED_RADIUS(2 格) 松得多，原因有两个：
// 环心是【锚点】而不是固定建筑，落点会随祭司方位与估算锚点抖动；
// 判太严会让「刚一就位又被判定没到位」，而每次重发 HumanMove 都清一次路径。
static const int USR_FARMER_ESCORT_ARRIVED_DIS2 = 16;

// 集结指令的下发节流。取 60 = ManageStandoff / DispatchScouts / 祭司探路
// 同一个值 —— 本项目里「编队 / 巡逻」这条粒度统一定在 60 帧。
// 必须显著大于走完一格的时间（约 16 帧/格），否则会「刚起步就重算」。
static const int USR_FARMER_ESCORT_ORDER_INTERVAL = 60;

// 被敌人缠住（正在交战）时给的宽限帧数。取 400：
// 大于一整个攻击循环（约 40 帧）一个数量级，让他有机会脱身；
// 又远小于「从 46 格带被一路引到敌方基地」所需的约 700 帧，
// 所以不会顺着敌人一路漂到基地门口 —— 超过这个时间就把他硬拉回集结带。
static const int USR_FARMER_ESCORT_ENGAGED_FRAMES = 400;

// 走不到的落点拉黑多少帧。取 600 = USR_PRIEST_BAD_POINT_FRAMES —— 同量级，
// 「这段时间里别再做那件事」。太短会立刻被重新选中（敌人没变的话它还是最优解），
// 太长会把一个其实只是被临时堵住的点浪费掉。
static const int USR_FARMER_ESCORT_BAD_POINT_FRAMES = 600;

// ── 阶段 B：拆敌方箭塔 ──────────────────────────────────────────────
// 同一座箭塔最多安排几个农民。取 8，两条独立理由指向同一个数：
//   · 物理：箭塔是 2×2（BuildingBlockSize），正交相邻只有 8 格，第 9 个农民
//     根本贴不到近战伤害阈值（约 SideLength/2 + 0.5 格，见 Farmer::getDis_attack）；
//   · 收益：农民对建筑伤害是 3 × 20% → 每次命中 1 点，125 血要 125 次；
//     8 人同打约 625 帧清一座（按单次命中周期 40 帧估），再加人只是把 625 压到
//     500，不如把第二座也开工。20 个农民对这个上限正好铺 2.5 座塔。
static const int USR_FARMER_TOWER_PER_TARGET_MAX = 8;

// 拆塔指令的下发节流。取 30：介于军队自卫的 12 与箭塔索敌的 20 之间。
// 必须大于「HumanAction 之后 WorkObjectSN 变成新值」所需的结算时间，
// 否则会在关系还没建立时重发，把刚起手的攻击进度清零。
static const int USR_FARMER_TOWER_ORDER_INTERVAL = 30;

// 「已经站在塔跟前」的判定距离平方。取 9（3 格）。
// 【为什么必须有这一条】20 个农民打一座 2×2 的箭塔，只有 8 个贴得上（正交相邻
// 只有 8 格），其余的在周围排队等位置 —— 它们的 BlockDR/UR 自然不变，
// 会被 IsFarmerStuckWalking 判成「走不动」。没有这个排除，排队的农民会被逐个
// 判成「这座塔走不到」而换塔，超员兜底又把它们压回来，形成来回换目标的抖动。
// 3 格比近战伤害阈值（约 SideLength/2 + 0.5 ≈ 1.5 格）宽一些，留出挤压余量。
static const int USR_FARMER_TOWER_ENGAGE_DIS2 = 9;

// 卡住重试次数上限。取 3：配合 USR_FARMER_TOWER_ORDER_INTERVAL(30)，
// 3 次约 90 帧确认还没贴上就换塔 —— 再加上 IsFarmerStuckWalking 本身要求的
// 300 帧没动，合计约 390 帧，接近单塔击杀时间（约 625 帧）的六成。
static const int USR_FARMER_TOWER_STUCK_MAX_TRIES = 3;
// 「该农民 → 该塔」拉黑多少帧。取 900：比落点黑名单长 —— 塔周围被己方单位挤死
// 这件事会持续得比较久（所有农民都往同一座塔挤），拉黑太短会立刻被重新选中。
static const int USR_FARMER_TOWER_BLACKLIST_FRAMES = 900;

// ── 士兵牵制敌方箭塔 ────────────────────────────────────────────────
// 每座箭塔最多派几个士兵去牵制。取 4：
//   · 塔一次只打一个目标（enemyai.cpp:447-453 打的是 Defend.back()），
//     4 个已经足以把祭司的挨打概率压到 1/5 以下；
//   · 士兵同时还是推基地的主力，不能全填进塔里。军队总数约 20，
//     5 座塔 × 4 = 20 是上限，实际按塔数摊薄（填不满就少填）。
//
// 【这块挂在哪儿】不另起一路，而是 ManageOffensiveArmy 里「视野里没有敌方士兵
// → 打建筑」那一段的目标选择 —— 所以视野里一出现敌兵，军队立刻丢下塔回去打敌人。
static const int USR_ARMY_TOWER_PER_TARGET_MAX = 4;
// 【开关：军队打建筑时是否分散去牵制各座敌方箭塔】
//
// true  = 按单位把军队分配到各座塔上（每塔上限 USR_ARMY_TOWER_PER_TARGET_MAX）
// false = 全军统一打 FindOffensiveTargetSN() 选出的那一个目标（优先市镇中心）
//
// 按需求取消均摊，取 false。保留开关而不是删掉那段调用，是为了让
// PickEscortTowerForArmy 仍被「引用」，不产生 -Wunused-function 警告；
// 想恢复分散牵制时只改这一个常量。
static const bool USR_ARMY_TOWER_SPREAD_ENABLED = false;

// 阶段 B 的时间兜底帧。取 39000 = 农民出发(35000)之后 4000 帧，也就是农民
// 走完那 130 格（约 1500 帧）到达环上之后再过 2500 帧。
// 【为什么需要】EstimateEnemySiegeAnchor 的注释记录过一个真实故障：攻城厂整局
// 没被侦察到（`enemyB` 最多到 1，那还是座箭塔），于是 ManagePriest 那条冲厂分支
// 因为 FindEnemySiegeBuilding() == nullptr 永远不执行，阶段 B 的 ①②③ 条全部关死，
// 护送队会在环上白站到底。到这一帧时军队已压了近 6000 帧、敌方建筑必然已进
// info.enemy_buildings，此时让农民去拆塔至少能替军队与祭司分担火力。
static const int USR_FARMER_TOWER_FALLBACK_FRAME = 39000;

// 农民去救祭司的最大距离（格，欧氏）。
//
// 【为什么要有上限】祭司是唯一的获胜路径且不可补充，但农民同样不可替代 ——
// 被杀一个就少一份产出。农民移速 HUMAN_SPEED = 2.236、攻击射程 1，
// 目标在 8 格之外时等它走到，祭司多半已经死了，而它自己成了送人头。
// 所以只让「本来就在祭司附近干活」的农民顺手去救。
// 取 8：明显小于军队的协防半径 USR_FIELD_ASSIST_RADIUS(12) —— 军队能跑，
// 农民跑不起。
static const int USR_FARMER_PRIEST_HELP_RADIUS = 8;
// 自毁腾人口时至少保留的农民数（按【全部】农民算，含护送队）。
//
// 【为什么按总数算】原先这条门槛数的是「非护送农民」，那是个错误的口径：
// 护送启动后非护送农民本来就少（RECRUIT 常常只征到 16 个，剩 4 个 + 中心
// 补产的），于是「还剩 5 个」这个判断反而更容易越线，把新农民当多余的清掉。
// 用总数才是"还剩多少人在干活"的正确口径；选人自毁时再排除护送队
// （见 SacrificeExcessFarmers 的选人循环）。
static const int USR_FARMER_KEEP_MIN = 5;
// 箭塔建筑候选点相对中心的目标距离，单位为地图格。
static const int USR_ARROWTOWER_BUILD_RADIUS = 18;
// 箭塔目标数量。取 4 = 朝敌三个方向各一座 + 最朝敌那个方向补一座
// （方向与距离见 GetBuildCandidate 里箭塔那一段）。
// 注意：建造与采石共用 ArrowTowerStillWanted()（见 CalculateFarmerTargets）——
// 建满 4 座后它返回 false，于是同时停建箭塔、并停止采石把农民让给食物与木头。
// 只改建造而不改采石的话，塔数永远停在目标值以下，农民会一直采无用的石头。
static const int USR_ARROWTOWER_TARGET = 4;
// 箭塔停止建造的帧号：第三波骚扰（enemyai.cpp:45 的 TAT=21000）之后不再建造，
// 石头与采集力让给兵力与科技。
// 注意 enemyai.cpp 的 FAT/SAT/TAT 是那个文件内部的宏，UsrAI.cpp 里看不到，
// 所以这里是独立取值 —— 两处若要调整需要同步。
static const int USR_ARROWTOWER_STOP_FRAME = 21000;
// 过了 USR_ARROWTOWER_STOP_FRAME 之后，石头本来就没用了（箭塔停建）——
// 但【修塔】还要花石头：内核按 REPAIR_COST_RATIO(0.5) × 本次回血比例 ×
// 建筑原造价 扣料（Building.cpp:453），一座箭塔修满约耗 75 石。
// 所以在这两个阈值之内额外保一条采石线：只要库存低于该值就继续采石。
// 实测（改之前）石头在 f=42000 被修塔耗到 0，之后塔只能看着它烂。
static const int USR_STONE_KEEP_FRAME = 24000;
static const int USR_STONE_KEEP_AMOUNT = 300;
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
// 交付建筑（仓库 / 谷仓）离我方市镇中心的【最大】距离（格，曼哈顿）。
//
// 【为什么必须有这条上限】TryBuildReturnDepot 的选点判据是「谁离现有的交付建筑
// 最远谁赢」（见那两处 `dis2 > worstReturnDis2`），而它【只有下限】（> 20 格才
// 值得多建一座），没有上限 —— 于是离我方最远的那片林子/矿区永远是赢家，仓库就
// 建到那儿去了。而资源评分里又【刻意不含「农民到资源的距离」】（见
// FindBestResourceSN 里那段「已取消距离项」的说明），仓库一落成、那片资源的交付
// 距离归零，它在评分里立刻反超近处的林子 —— 农民于是横穿半张地图去砍树。
// 实测症状就是「农民跑到对面家旁边建仓库、然后在那里砍树」。
//
// 【为什么是 65 格】敌方基地离我方中心曼哈顿 122~140 格（四张图，见
// EstimateEnemySiegeAnchor 的实测记录），65 大致是中场。取它就把交付建筑挡在
// 我方半场＋中场之内，农民的活动半径随之被压住。
// 【关键：这条判据不依赖侦察】从开局第一帧就生效 —— 不像
// USR_FARMER_ENEMY_BASE_KEEPOUT 要等敌方建筑被发现（实测前 ~30000 帧
// enemyB=0，那段时间禁区整段失效）。
//
// 【代价】若某张图把主矿区/主林区放在离我方中心 65 格以外，那片资源就不会有自己
// 的交付建筑，农民走得远一些（但不会再横穿到敌方那侧）。真出现「农民跑很远去采
// 近处明明有的资源」，先回来看这个值是不是划得太紧。
static const int USR_DEPOT_MAX_HOME_DISTANCE = 65;
// 【开关：是否为伐木/采矿建【仓库】（BUILDING_STOCK）】2026-09-28 按要求停用。
//
// 【为什么可以停：市镇中心本来就是通用交付点】
// 引擎挑交付建筑的是 Core_List::findResourceBuiding（Core_List.cpp:738-757），
// 判据是
//     (judeBuild->getNum() == BUILDING_CENTER && flag) || judeBuild->getNum() == type
// 也就是说【市镇中心一直是被接受的交付建筑】，`flag` 只对帆船
// （FARMERTYPE_SAILING）为假、陆上农民恒为真。Resource::get_ReturnBuildingType()
// 那张表（HUMAN_WOOD → BUILDING_STOCK）只决定「优先送哪一类」，并不排除中心。
// 所以木头/石头/黄金/羚羊尸体不送仓库也照样能交付 —— 送市镇中心即可。
// 而家门口 30 格内就有 25,950 木（四张图实测 9,525~28,875，见本常量上方的表），
// 整局需求约 2,500 —— 完全不需要为伐木铺仓库。
//
// 【为什么必须【保留】谷仓（BUILDING_GRANARY）的那条路】
// 见 ManageEconomyAndProduction 里「箭塔科技」那一段：箭塔必须先完成谷仓的
// 「研发:建造箭塔」(BUILDING_GRANARY_ARROWTOWER) 才能建造，否则 Core 以
// ACTION_INVALID_HUMANBUILD_LOCK 拒绝；而全文件【没有任何地方
// TryBuild(BUILDING_GRANARY)】—— 建谷仓的唯一路径就是本函数（浆果丛 / 农田那两条
// 候选，ReturnBuildingForResource(RESOURCE_BUSH) == BUILDING_GRANARY）。
// 所以把整条路一起停掉会让箭塔永远造不出来。本开关【只停 STOCK】。
// 【补充：对当前四张图，上面这段顾虑实际不成立】地图自带一座谷仓，所以谷仓那
// 条路对本函数也是多余的 —— 详见 USR_DEPOT_ENABLED 上方「停掉它安全吗」那段
// （那里有实测证据）。这段保留，是为了说明【假如某张图没有初始谷仓】时该保留
// 哪一条。
static const bool USR_DEPOT_STOCK_ENABLED = false;
// 【上级开关：整个「为资源建交付建筑」的功能】2026-09-28 按要求全部停用。
//
// 它把上面那个 USR_DEPOT_STOCK_ENABLED 一起盖住。那个开关现在只在恢复时才有
// 意义：把本开关改回 true 并保持 STOCK 为 false，就只恢复谷仓那条路。
//
// 【停掉它安全吗：安全 —— 四张图【初始就带谷仓与仓库】，所以这条链不会断】
// 【这里原先写的是「停掉它会断掉整条军事链」，那是错的，已被实测推翻】
// 当时的推理是：谷仓是引擎前置的起点（Development.cpp:635 市场需要谷仓、
// :583 箭塔科技在谷仓研发），所以没有谷仓 ⇒ 无市场 ⇒ 无车轮科技 ⇒ 无战车弓兵 ⇒
// 永不进攻。推理本身没错，但【漏查了运行时的初始状态】：谷仓并不是本函数建的，
// 它是地图自带的初始建筑。逐张解码 map*.njust，四张图完全一致：
//     我方 = CENTER×1 + HOME×2 + 【GRANARY×1】 + 【STOCK×1】 + ARROWTOWER×1
//     敌方 = ARROWTOWER×5 + SIEGE×1（没有市镇中心）
// 也就是说：
//   · 市场的引擎前置由【初始那座谷仓】满足，与本函数建的额外谷仓无关；
//   · 农民交付木头/石头/黄金本来就有初始那座仓库（引擎还额外接受市镇中心，
//     见 Core_List.cpp:738-757）。
// 所以本函数建的交付建筑对这几张图【完全是多余的】—— 停掉它既堵住了「跑到对面
// 建仓库」，又不影响市场 / 车轮科技 / 战车弓兵 / 箭塔 / 农田中的任何一项。
//
// 【实测印证，第二轮 16 局】market 在 f=2000 就已存在、wheel=1、carcher=15、
// range=3、tower=1；enemyB 6→4→1（军队拆掉 5 座箭塔），f=44163 祭司转化厂获胜 ——
// 设计中的获胜路径完整走通。
//
// 【若将来换一张【初始没有谷仓】的地图】上面那条链才会真的断掉。届时的做法：
// 把本开关改回 true，或给那张图单独补一条建谷仓的路。
static const bool USR_DEPOT_ENABLED = false;
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
// 落点环绕市镇中心，按出厂顺序轮流分配。半径取 4 / 7 / 10 三圈，每圈 8 个方向，
// 共 24 个互不相同的落点 —— 既离开靶场出口，又不至于一出生就挤成一堆。
// （箭塔改成围着中心的四角一簇之后，7/10 圈会落在簇外；这不影响散开的目的，
//  单位会绕开塔走。）
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
// ── 农民护送祭司（详见 UsrAI.cpp 顶部「农民护送祭司」那组常量）────────────
// 两阶段：BAND = 随祭司在敌方锚点外 46/54 两圈待命；TOWER = 祭司冲厂时转为拆箭塔。
enum
{
    FARMER_ESCORT_STAGE_BAND = 0,
    FARMER_ESCORT_STAGE_TOWER = 1
};
static int farmerEscortStage = FARMER_ESCORT_STAGE_BAND;
// 已被征召护送祭司的农民 SN → 征召帧。【存在即护送中，一旦进入就不再回经济】
// 这张表是「脱离经济」的唯一真相来源：所有会抢农民的入口都读 IsFarmerEscorting。
// 写成 latch 而不是「按需切换」的理由：半途回经济要同时恢复
// farmerLastOrderFrame 节流、pendingGatherOrders 记账、以及威胁迟滞三套状态，
// 半恢复状态下农民会在箭塔与浆果丛之间每帧互抢，而每次 HumanAction 都经
// Core_List::suspendRelation 清路径（Core_List.cpp:450-469）—— 表现为原地抖。
static map<int, int> farmerEscortDuty;
// 首次征召的帧号（USR_INVALID_FRAME = 尚未征召）。
// 用它而不是「farmerEscortDuty 非空」做「只征召一次」的判据：全体阵亡时后者
// 会重新变空，于是又征召一轮新产出的农民去送人头。
static int farmerEscortDutyFrame = USR_INVALID_FRAME;
// 阶段 A 的集结命令。字段照搬 ChariotSpreadOrder 的三件套，各有用途：
// 落点做到达判定、下发帧做节流、下发时位置做「卡住」判定。
struct FarmerEscortOrder
{
    int targetDR;
    int targetUR;
    int orderFrame;
    int fromDR;
    int fromUR;
};
static map<int, FarmerEscortOrder> farmerEscortOrders;
// 走不到的落点：坐标 → 拉黑到期帧（与 priestBadRetreatPoints 同构）。
static map<pair<int, int>, int> farmerEscortBadPoints;
// 阶段 B：农民 SN → 正在拆的敌方箭塔 SN。
//
// 【不能复用 currentTarget】CleanDeadOwnerTargetLocks 用 FindMyArmyBySN 判断
// 「锁的主人是否还活着」，而它只查 info.armies —— 农民不在其中，所以农民写进
// currentTarget 的条目【每一帧都会被清掉】。表现会是「每帧重选目标 → 攻击关系
// 被反复重建 → 伤害永远打不出去」。必须用独立表。
static map<int, int> farmerEscortTowerTarget;
// 农民 SN → 上次下发拆塔指令的帧（节流）。
static map<int, int> farmerEscortLastOrderFrame;
// 农民 SN → 对当前这座塔已经重试过几次（卡住时递增，换塔后归零）。
static map<int, int> farmerEscortStuckTries;
// (农民 SN, 塔 SN) → 该农民对这座塔「走不到」，拉黑到哪一帧。
static map<pair<int, int>, int> farmerEscortTowerBadForFarmer;
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
// 上一次「远程兵被近战贴上 → 全体远程兵后撤两格」的帧。
// 详见 KiteRangedBackFromMelee（定义在 ManageOffensiveArmy 之前）。
// 声明在这里同样是为了让新对局重置块能清它。
static int lastKiteFrame = USR_INVALID_FRAME;
// 站桩撤退的执行窗口：SN → 该单位「退出 32 格环」任务结束的帧。
// 窗口内该单位被排除在 AssignFieldDefense 之外，让撤退真正走完。
// 详见 ManageStandoff 里那段说明。声明在这里是为了让新对局重置块能清它。
static map<int, int> standoffRetreatUntil;
// 撤退封口窗口内部用的「进度基准」：SN → 上次检查时该单位到锚点的距离平方。
// 一个站桩周期（60 帧）过去还没变近，就判定撤退失败、解封交还自卫权。
// 没有这个出口的话，走不出去的单位会陷入「封 200 帧 → 到期 → 再发一次 →
// 再封」的循环，既不动也不还手（实测表现为战车弓兵站着不动）。
static map<int, int> standoffRetreatFromDis2;
// ── 突击者（勾引守军出来）────────────────────────────────────────────
// 站桩的前提是「守军会自己出来」。对面缩在基地里不出来时，站桩就变成双方干站着。
// 这时派【一个】单位上前去勾 —— 就是策略文档第 48 行说的「必须拉扯」。
//
// 【为什么只派一个】敌方守军的追击上限是 25 格（DEFENSE_CHASE_LIMIT，量到它们的
// 攻城武器厂），追一个目标追过界就放弃。全军压上时守军面对的是「一堆目标」，
// 反而不会追出来；单个目标才有诱饵效果。
//
// 【流程照搬另一套实现的 manageAttack】选离敌方基地最近的空闲单位当突击者，
// 记下它出发时的位置；它 8 格内无敌军且未受伤 → 朝敌方基地前进 3 格；
// 一旦挨打、或 8 格内出现敌人 → 往起点退；退到起点 2 格内就交还指挥权，
// 作为普通部队参战。详见 ManageStandoffAssault。
static const int USR_ASSAULT_IDLE_FRAMES = 1500;  // 站桩僵住多少帧之后才派突击者
static const int USR_ASSAULT_TRIGGER_DIS2 = 64;   // 威胁判定：8 格（欧氏平方）
static const int USR_ASSAULT_STEP = 3;            // 每次前进/后退几格
// 站桩连续生效的起始帧。用来判「僵住多久了」。站桩一结束就复位。
static int standoffEngagedSince = USR_INVALID_FRAME;
static int assaultSN = -1;        // 当前突击者的 SN（-1 = 没有）
static int assaultStartDR = -1;   // 突击者出发时的位置，也是它后退的目标
static int assaultStartUR = -1;
static bool assaultRetreating = false;
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
// （「没有敌人就去转换攻城武器厂」那一段）。
static bool HasVisibleEnemyArmy();
static const tagBuilding *FindEnemySiegeBuilding();
// 所有已知敌方箭塔是否都被牵制住了（每个塔都有我方单位以它为工作目标）。
// 定义在农民护送那一段，但 ManagePriest 的「走向攻城厂」要用它做进门条件。
static bool AllEnemyArrowTowersEngaged();
// 攻城厂附近的箭塔是否【已被摧毁】—— 不只是"被牵制"。见它的定义处的说明。
static bool SiegeTowersCleared();
// 祭司现在可不可以冲向攻城厂（冲厂 / 转换 / 驻留三条分支共用）。见定义处。
static bool CanPriestApproachSiege();
// 敌方基地锚点：侦察到就用真的，否则用「我方市镇中心的地图对极点」估。
// 定义在 ManageStandoff 之前，但 ManagePriest 的禁区判定也要用它。
static bool EstimateEnemySiegeAnchor(int &anchorDR, int &anchorUR);
;
static bool IsAliveFarmerSN(int farmerSN);
static bool IsFarmerRelationEstablished(int farmerSN, int targetSN);
// 该农民是否已被征召护送祭司（详见文件顶部那组常量）。
// 前向声明放在这里：建造 / 恢复工地 / 修塔 / 经济派工四处都要读它，
// 而它的定义在 IsAliveFarmerSN 旁边（与本块其余判据一致）。
static bool IsFarmerEscorting(int farmerSN);
// 【已停用：以下 2 个是死变量】见文件下方「新对局重置块」那段 #if 0 的说明。
// 全文件 grep 过：除了自己的声明，它们没有任何引用 —— 用它们的「建筑动作 /
// 单位生产的节流」代码已经不在了，唯一还引用它们的地方就是那段重置块。
// 重置块被注释掉之后，它们就触发了 -Wunused-variable。与下面 researchedTechCount
// 前后那 6 个同批处理（那里有完整的排查记录与恢复方法）。
// // 上次提交建筑研发、升级或生产动作的游戏帧。
// static int lastBuildingActionFrame = USR_INVALID_FRAME;
// // 上次提交单位生产动作的游戏帧。
// static int lastProductionActionFrame = USR_INVALID_FRAME;
// 上次提交祭司移动或转换指令的游戏帧。
static int lastPriestOrderFrame = USR_INVALID_FRAME;
// 主力开始总攻后的祭司跟随门控状态。
static bool offensiveAttackStarted = false;
static int offensiveAttackStartFrame = USR_INVALID_FRAME;
// 上一次下发「进攻 / 站桩」指令的帧。ManageStandoff 与 ManageOffensiveArmy 共用，
// 两处判据都是 `g_frame - offensiveLastOrderFrame < 60`。
//
// 【为什么声明在这个位置、不放函数区】它必须能在新对局重置块里被清掉 —— 帧号随
// 新对局回退后那个差是【负数】、条件恒成立，两条分支会每帧 return，新对局里军队
// 一次进攻指令都不下发（与 lastArmyRallyFrame 是同一个坑，见重置块里的说明）。
// 重置块在文件靠前处，所以声明也得在它之前。
// （该块目前已 #if 0 停用 —— 判据在本项目里永不成立，见其上方说明；位置保持不动，
//   这样将来恢复它时不用再挪声明。）
static int offensiveLastOrderFrame = USR_INVALID_FRAME;
// 上一次下发「向地图中间集结」指令的帧。见 ManageOffensiveArmy 开头那一段。
static int lastArmyRallyFrame = USR_INVALID_FRAME;
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
// 探路收工后「正在回家」。由 FinishPriestExplore 置起，走到市镇中心 2 格内
// （或找不到中心）复位。为什么是一个状态而不是那里发一次移动，见它的说明。
static bool priestGoingHome = false;
// 【最近被判"走不到"的撤退点】落点 → 解禁帧。见 GetPriestEmergencyPoint 的说明：
// 走不到的点每 150 帧会被原样重发一次（每次都清空路径），加个短名单避开它。
// 是一组而不是一个：只记一个点时，第二个走不到的点会把它覆盖掉，最优的那个又
// 变回可选的 —— 于是在两个走不到的点之间来回弹。
static map<pair<int, int>, int> priestBadRetreatPoints;
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
// 祭司当前【锁定】的转换目标 SN（-1 = 没有锁定）。
//
// 【为什么需要锁定】原先每次进 ManagePriest 都用 FindPriestConversionTarget
// 按兵种优先级重选一遍，于是目标一变就重新下发 HumanAction —— 而每次重发都会
// 经 Core_List::suspendRelation 中止并重建关系，转换进度清零。实测最新一局
// 到 f=25274 为止新开了 15 次转换指令、换了 10 个目标，其中有两次间隔只有
// 6~18 帧（f=13989→14007、f=21540→21546）—— 一个都没转完。
// 锁定的语义：一旦选定，就一直转到「转换完成 / 目标死亡 / 目标离开视野」，
// 中途不因为出现更高优先级的兵而改选。代价是放弃「优先转投石车」的机会，
// 换来的是转换能真正完成（早期被骚扰时白赚一个兵）。
static int priestConversionLockSN = -1;
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
// 最近一次建造指令的目标格（左上角）。建造被拒时要靠它记住「这块地放不下」。
static int buildOrderDR = -1;
static int buildOrderUR = -1;
// 【坏点表】地块 → 被判为「放不下」的帧。见 MarkBuildSpotBad。
// 用 map 而不是 120×120 的数组：建造次数很少，条目也少。
static map<pair<int, int>, int> badBuildSpot;
// 坏点的有效期（帧）。照搬另一套实现的 badPlace 机制，取值也一致。
static const int USR_BAD_BUILD_SPOT_FRAMES = 1500;
static int buildFarmerSN = -1;
// 【已停用：以下 6 个是死变量】用它们的代码（市场 / 兵营那条科技链：市场研发、
// 兵营升级、后勤研究）早已被注释掉，唯一还引用它们的地方是文件上方那段
// 「新对局重置块」—— 那个块因为判据在本项目里永不成立也已被 #if 0 停用。
// 于是它们成了「定义了但没有任何引用」，触发 -Wunused-variable。
//
// 【逐个核对过】stableOrderFrame / technologyOrderId / technologyOrderFrame /
// technologyPendingAction 全文件只剩自己的声明；clubmanUpgradeOrderId /
// broadswordUpgradeOrderId 另各有一处引用，但那两处在研究链那段里是
// // 注释掉的 ResearchTech 调用，不算活的引用。
//
// 【恢复方法】把这 6 行、上面 IsFarmerEscorting 附近那 2 行、以及重置块里对应的
// 赋值一起解开 —— 只解重置块会因变量未定义而编译不过。
// static int stableOrderFrame = USR_INVALID_FRAME;
// // 最近一次非生产建筑动作的异步指令 ID。
// static int technologyOrderId = -1;
// // 最近一次非生产建筑动作提交时的游戏帧。
// static int technologyOrderFrame = USR_INVALID_FRAME;
// // 最近一次非生产建筑动作的动作枚举，用于异步成功后推进科技里程碑。
// static int technologyPendingAction = -1;
// 研发完成的兵种科技数量，全部完成后解锁兵力生产上限并开始进攻。
static int researchedTechCount = 0;
// 各兵种科技的研发订单 ID（-1 表示无 pending）。
// static int clubmanUpgradeOrderId = -1;
// static int broadswordUpgradeOrderId = -1;
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
  // 【日志路径可被环境变量覆盖，默认仍是项目根的 ai_debug.log】
  //
  // 并行跑多局评测时（scripts/run_one_map.sh），多个游戏实例共用同一个 cwd
  // 会把日志交织进同一份文件、事后没法读。而 cwd 又【不能】换成各自的目录 ——
  // 游戏要从 cwd 读 config.json / res.rcc 这些资源，换了就直接起不来
  // （实测换 cwd 后进程在 initPlayers 阶段 segfault）。
  //
  // 所以反过来：cwd 保持项目根不动，让每个实例通过 AOE_AI_LOG 指定自己的日志。
  const char *overridePath = getenv("AOE_AI_LOG");
  FILE *f = fopen(overridePath != nullptr ? overridePath : "ai_debug.log", "a");
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
    int bestDis2 = USR_FIELD_LION_AGGRO_RADIUS * USR_FIELD_LION_AGGRO_RADIUS;

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
        // 距离上限见 USR_PRIEST_SIEGE_PICK_DISTANCE 的说明（12 → 40 的理由）。
        if (dis2 >= USR_PRIEST_SIEGE_PICK_DISTANCE *
                        USR_PRIEST_SIEGE_PICK_DISTANCE)
            continue;
        // 攻城武器厂附近 2 格内有敌人则视为不安全，等军队清理后再转换。
        // （注释原先写的是 5 格，与下面的 2*2 不符，以代码为准。）
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

// 结束祭司探路并记录原因。
// 几个退出条件（找到瞪羚 / 没有可用前沿 / 前沿都超出半径 / 连续卡住）
// 在观感上都是「祭司不动了」，只有这行日志能把它们区分开。
//
// 【收工之后要显式回家】原先这里只置一个 bool 就返回 —— 不下发任何移动指令。
// 而待机段里唯一的位置规则是「敌方基地禁区」（USR_PRIEST_ENEMY_BASE_KEEPOUT，
// 以【敌方基地】为参照的 32 格红线），跟我们自己的基地无关。
// 于是探路在离家很远的地方收工时，祭司就原地站住了 —— 实测某局 f=5000 探路超时
// 收工，它停在 (15,44)：离市镇中心 31 格、离敌方基地锚点 97 格，两边规则都不管它，
// 从 f=5100 一直站到 f=6300（1200 帧）。
//
// 【为什么是一个状态而不是这里发一次移动】这里发的话，同一帧后面还会走到待机段的
// TryPriestHeal —— 它一旦找到 12 格内的伤员就会 HumanAction 覆盖掉这条移动，
// 而之后没有任何机制重发，祭司又站住了。所以只置「回家中」这个标志，
// 由待机段那段（排在任何会换目标的分支之前）负责下发与重发，直到真的到家。
static void FinishPriestExplore(const char *reason)
{
  if (priestExploreDone)
    return;
  priestExploreDone = true;
  priestGoingHome = true;
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
        // 护送祭司的农民已经永久退出经济，不能算进配额的分母。
        // 不排除的话，配额会按「全部农民」来算，而实际能采集的只剩少数几个 ——
        // 于是 [FARIDLE] 那类经济诊断日志整段失真（日志里挂着 tgt=[20,0,0,0]
        // 而 asg=[0,0,0,0]，看起来像派工坏了，其实人根本不在经济里）。
        if (IsFarmerEscorting(farmer.SN))
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
    //
    // 【补一条：24000 帧前只要不足 300 就一直采】
    // 上面那两条判据（ArrowTowerStillWanted = 21000 帧前 && 塔没建满）过了
    // 就完全不采石 —— 而修塔是要花石头的：内核按 REPAIR_COST_RATIO(0.5)
    // × 本次回血比例 × 建筑原造价 扣料（Building.cpp:453），一座箭塔修满
    // 约耗 75 石、三人同修则按三倍速率消耗。实测石头在 f=42000 被修塔耗到 0，
    // 之后塔只能看着它烂。所以在这两个阈值之内额外保一条供给线。
    if (ArrowTowerStillWanted() ||
        (g_frame < USR_STONE_KEEP_FRAME && info.Stone < USR_STONE_KEEP_AMOUNT))
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

// 该农民是否已被征召护送祭司 —— 也就是「已永久退出经济」。
//
// 【为什么按 SN 查表而不是按位置/状态判】护送是【编制】不是临时任务：
// 一旦征召，这个农民到死都归护送队（见 farmerEscortDuty 的说明）。
// 所有会抢农民的经济入口（派工 / 建造 / 恢复工地 / 修塔 / 自卫 / 自毁）
// 都靠这一个判据让路，否则会把正在赶路或拆塔的人拽回采集。
static bool IsFarmerEscorting(int farmerSN)
{
    return farmerEscortDuty.find(farmerSN) != farmerEscortDuty.end();
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
    // 【已停用：新对局重置块】
    //
    // 这段代码原本用来「检测到换了一局，就把上一局残留的状态全部清掉」，判据是
    // 帧号倒退（g_frame < farmerResourceStateFrame）。但实测当前代码里
    // 【一个进程只跑一局，这个判据永远不成立】：
    //   · MainWidget 只在 main() 里构造一次（main.cpp:52），initMap/initAI 也只在
    //     它的构造函数里被调用（MainWidget.cpp:132/136），没有第二条重新初始化的
    //     路径；
    //   · gameframe 是 MainWidget 成员、初值 0，全文件只有 gameframe++，
    //     没有任何地方清零或回退（MainWidget.cpp:2639）；
    //   · 一局结束就 HandleGameOver() → exit(0)（MainWidget.cpp:2628）直接结束进程；
    //   · 评测脚本也是一次 run 起一个进程（run_ai_trials.py:156）。
    // 既然判据不成立，这段清理就是死代码 —— 注释掉它不改变任何行为。
    //
    // 【为什么整体留着而不是删掉】里面每一行都是一次实测踩坑的记录（见各个
    // 变量的注释），删掉就把「哪些状态是跨局敏感的」这份清单弄丢了。将来真加了
    // 重开一局的入口，把它解开就能直接用。
    //
    // 【恢复要做的三件事，缺一不可】
    //   (i) 把下面这段 #if 0 解开；
    //   (ii) 确认 #endif 之后那句 `farmerResourceStateFrame = g_frame;` 仍在
    //        if 之外 —— 它是检测器本身的上弦动作；
    //   (iii) 恢复被一并注释掉的【8 个声明】：lastBuildingActionFrame /
    //        lastProductionActionFrame（在 IsFarmerEscorting 前向声明之后那一段，
    //        标注「【已停用：以下 2 个是死变量】」），以及 stableOrderFrame /
    //        technologyOrderId / technologyOrderFrame / technologyPendingAction /
    //        clubmanUpgradeOrderId / broadswordUpgradeOrderId（在
    //        researchedTechCount 前后那一段，标注「【已停用：以下 6 个是死变量】」）。
    //        搜这两个标注就能定位。这一步不做，本段会因变量未定义而编译不过 ——
    //        那 8 个变量在别处没有任何引用，就是靠这段重置块「活着」的。
    //
    // 【不要动下面那句 farmerResourceStateFrame = g_frame】它在 if 之外，
    // 是检测器本身的上弦动作，解开这段代码时它必须原样存在。
#if 0
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
        lastKiteFrame = USR_INVALID_FRAME;
        // 帧号会随新对局回退，集结的节流时间戳必须跟着失效 ——
        // 否则新局的 30000 帧会减去上一局的帧号、算出负数，条件永不成立，
        // 集结一次都不下发。
        lastArmyRallyFrame = USR_INVALID_FRAME;
        // 【同一个坑，总攻 / 站桩的节流时间戳】ManageStandoff 与
        // ManageOffensiveArmy 都用 `g_frame - offensiveLastOrderFrame < 60` 做
        // 节流，差是负数时条件恒成立 → 两条分支每帧都 return → 新对局里军队
        // 【一次进攻指令都不下发】，要等帧号重新爬过上一局那条时间线才恢复。
        // 这两个门控状态一并清掉。
        offensiveLastOrderFrame = USR_INVALID_FRAME;
        offensiveAttackStarted = false;
        offensiveAttackStartFrame = USR_INVALID_FRAME;
        standoffRetreatUntil.clear();
        standoffRetreatFromDis2.clear();
        badBuildSpot.clear();
        buildOrderDR = -1;
        buildOrderUR = -1;
        standoffEngagedSince = USR_INVALID_FRAME;
        assaultSN = -1;
        assaultStartDR = -1;
        assaultStartUR = -1;
        assaultRetreating = false;
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
        priestGoingHome = false;
        priestBadRetreatPoints.clear();
        priestEmergencyTargetFrame = USR_INVALID_FRAME;
        priestFrontierTarget = make_pair(-1, -1);
        priestFrontierStuckCount = 0;
        priestWasConverting = false;
        // 转换目标的锁定也跨局失效：SN 在下一局会指向别的单位，
        // 留着会让祭司一开局就去追一个不认识的目标。
        priestConversionLockSN = -1;
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
        // 农民护送祭司的编制必须整组清掉：SN 是跨局复用的，漏清会让新对局
        // 开局就带着上一局的护送名单（那些 SN 现在指向别的单位），
        // 于是新对局的农民一出场就被判成「已退出经济」而站着不动。
        farmerEscortStage = FARMER_ESCORT_STAGE_BAND;
        farmerEscortDuty.clear();
        farmerEscortDutyFrame = USR_INVALID_FRAME;
        farmerEscortOrders.clear();
        farmerEscortBadPoints.clear();
        farmerEscortTowerTarget.clear();
        farmerEscortLastOrderFrame.clear();
        farmerEscortStuckTries.clear();
        farmerEscortTowerBadForFarmer.clear();
    }
#endif
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

// 农民禁区的半径（格）：到任何【已知】敌方建筑的这一距离以内，既不派农民去采集
// 资源，也不在那里盖仓库/谷仓。
//
// 【28 → 50】原值 28 是照站桩圈 USR_STANDOFF_RADIUS 取的（那正是敌方守军够得到
// 的范围：DEFENSE_CHASE_LIMIT = 25，量到它们的攻城武器厂）。但守军不是唯一的
// 威胁 —— 敌方 AI 还会主动派出骚扰部队去打 Farmer（enemyai.cpp:440-442），
// 它们不受那条 25 格折返线约束，可以追得更远。28 格只挡住了"守在基地里的敌人"，
// 挡不住"出来巡逻的那批"。
//
// 50 格的意义：把整个敌方基地及其外围活动区一并划出去。后期农民本来就要全部
// 撤出经济去护送祭司（USR_FARMER_ESCORT_FRAME），那之前也不该为了几片林子
// 把采集力押到敌方门口的。
//
// 【代价】地图上如果有资源正好落在敌方基地 50 格内，那片资源就永久放弃。
// 实测四张图的主矿区/林区都在我方一侧或中场，不受影响 —— 但若某张图上出现
// "农民宁可干等也不去采"的闲置，先回来看这个值是不是划得太宽了。
static const int USR_FARMER_ENEMY_BASE_KEEPOUT = 50;

// 「把已经陷在禁区里的农民撤出来」这条规则的重复下发间隔（帧）。
//
// 【为什么必须节流】撤离用的是 HumanMove，而它每次都经 suspendRelation 清空
// 路径（Core_List.cpp:450-469）—— 逐帧重发等于让农民原地不动（站桩、集结那几处
// 都因为同一原因做了节流）。取 120：与 TryAssignIdleFarmer 里那段安全滞后的
// 120 帧同量级，也远大于走一格所需的帧数；真被挡路卡住时，最多 120 帧就会重发
// 一次，能自愈。
static const int USR_FARMER_KEEPOUT_ORDER_INTERVAL = 120;

// 这个坐标是不是落在农民禁区里。
//
// 【为什么需要】农民的选点原先完全不看敌方位置：只要资源在那边就派人去 ——
// 采到一半被守军杀掉，既丢农民，又可能把我们的部队一路牵过去。实测军队全灭
// 那一局，farmers 从 20 掉到 0，大半死在前线方向。
//
// 【锚点为什么用「所有已知敌方建筑」而不是攻城武器厂】
// 攻城武器厂要到很晚才被侦察到（上一局 f≈36000 才进 info.enemy_buildings），
// 而那之前农民早就跑过去了。用已知建筑集合则是「侦察到哪座就管哪座」，越探越严。
//
// 【没侦察到任何敌方建筑时不设限】不知道的东西没法避开 —— 这是有意留的口子，
// 否则开局就会把全场资源判成禁区、农民集体发呆。
static bool IsInsideEnemyKeepout(int blockDR, int blockUR)
{
    const int r2 = USR_FARMER_ENEMY_BASE_KEEPOUT *
                   USR_FARMER_ENEMY_BASE_KEEPOUT;
    for (const tagBuilding &building : info.enemy_buildings)
    {
        if (building.Blood <= 0)
            continue;
        if (BlockDis2(blockDR, blockUR, building.BlockDR, building.BlockUR) <= r2)
            return true;
    }
    return false;
}

// 【硬保证：任何农民一旦落进敌方基地禁区，立刻把他撤向市镇中心】
//
// 【为什么光有「派工时不选禁区内的目标」不够】IsInsideEnemyKeepout 已经用在 5 处
// 过滤目标点（采资源 / 建造点 / 猎物群 / 另外两处建筑与资源选址），但挡不住三种
// 情况：
//   ① 敌方建筑是【逐渐被侦察到】的 —— 一个农民早就在那片林子里伐木，而那座建筑
//      要到后来才进 info.enemy_buildings（实测攻城厂 f≈36000 才发现）；
//   ② 目标点在禁区外，但寻路会穿过禁区；
//   ③ 猎物自己会走动，集群中心只是个缓存值。
// 所以这里再加一道与「目标是谁」无关的判据：人在禁区里，就把他撤出来。
//
// 【为什么复用现成的两张表、不新造状态】farmerThreatLastFrame /
// farmerSafeSinceFrame 本来就是为「敌袭撤离」设计的（见它们上方的注释），而且
// TryAssignIdleFarmer 里已经有一段现成的滞后判定：带威胁标记的农民，在「恢复
// 安全」之后 120 帧内不再被派活 —— 那正是这条规则需要的防抖。直接写这两张表，
// 就自动获得那层滞后，既不必新增 map，也不必在派工处再加条件。
//
// 【落点为什么取市镇中心】它是唯一一个能【确定】离敌方基地足够远的固定点
// （四张图敌方基地都在我方中心的对角线上，实测曼哈顿 122~140 格，远大于 50 格的
// 禁区半径）。不做「撤到禁区边界最近点」那种精算：那要解约束，而多走几十格对
// 农民没有额外代价。
static void EvacuateFarmersFromEnemyKeepout(UsrAI *ai)
{
    const tagBuilding *home = FindCenter();

    for (const tagFarmer &farmer : info.farmers)
    {
        if (farmer.Blood <= 0 || farmer.FarmerSort != FARMERTYPE_FARMER)
            continue;
        // 护送队是 latched 编制，它的位置本来就该在敌方基地附近（见
        // USR_FARMER_ESCORT_ENABLED）。该机制当前已停用，这一句是为了将来恢复时
        // 不被这条规则反复往家拽。
        if (IsFarmerEscorting(farmer.SN))
            continue;

        if (!IsInsideEnemyKeepout(farmer.BlockDR, farmer.BlockUR))
        {
            // 已经在禁区外：清掉威胁标记，并记下「安全起始帧」—— 那 120 帧的滞后
            // 从这一刻起算。erase 返回 0 表示本来就没有标记，不必再写时间戳。
            if (farmerThreatLastFrame.erase(farmer.SN) > 0)
                farmerSafeSinceFrame[farmer.SN] = g_frame;
            continue;
        }

        if (home == nullptr)
            continue;   // 没有市镇中心（开局头几帧）就没法给撤离方向

        const map<int, int>::const_iterator lastIt =
            farmerThreatLastFrame.find(farmer.SN);
        const bool hadThreat = (lastIt != farmerThreatLastFrame.end());
        const int prevThreatFrame = hadThreat ? lastIt->second : USR_INVALID_FRAME;
        const bool needOrder =
            !hadThreat ||
            (g_frame - prevThreatFrame >= USR_FARMER_KEEPOUT_ORDER_INTERVAL);
        // 标记每帧刷新（不只是下发那一刻）—— 这样「他还在禁区里」对
        // TryAssignIdleFarmer 的滞后判定始终成立，不会有空隙被派回经济。
        farmerThreatLastFrame[farmer.SN] = g_frame;
        if (!needOrder)
            continue;

        // 手上那条采集指令要撤掉：不撤的话 pendingGatherOrders 会继续替它记账，
        // 别的农民可能被重复派去同一处，他自己也会被当成「正在采集」。
        CancelPendingGatherOrder(farmer.SN);
        ai->HumanMove(farmer.SN, (home->BlockDR + 0.5) * double(BLOCKSIDELENGTH),
                      (home->BlockUR + 0.5) * double(BLOCKSIDELENGTH));
        {
            char buf[224];
            snprintf(buf, sizeof(buf),
                     "[KEEPOUT] f=%d farmer=%d pos=(%d,%d) home=(%d,%d) "
                     "inZoneFrames=%d",
                     g_frame, farmer.SN, farmer.BlockDR, farmer.BlockUR,
                     home->BlockDR, home->BlockUR,
                     hadThreat ? g_frame - prevThreatFrame : 0);
            AiDebugLog(buf);
        }
    }
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

        // 【农民禁区】离任何已知敌方建筑 USR_FARMER_ENEMY_BASE_KEEPOUT 格以内的
        // 资源不派人 —— 采到一半被守军杀掉，既丢农民又可能把部队牵过去。
        // 半径的取值理由见那个常量上方的说明。见 IsInsideEnemyKeepout。
        if (IsInsideEnemyKeepout(resource.BlockDR, resource.BlockUR))
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

        // 【农民禁区】同资源循环：自己建在敌方基地附近的农田也不派人去。
        if (IsInsideEnemyKeepout(building.BlockDR, building.BlockUR))
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
        // 护送祭司的农民已退出经济。这一条必须加：农民在集结环上待命时
        // NowState 正是 IDLE，会被下面这条当成「最理想的建造人选」抽走。
        if (IsFarmerEscorting(farmer.SN))
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
        if (IsFarmerEscorting(farmer.SN))
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

// 【把放不下的地块记成「坏点」】
//
// 建造因位置类原因被 Core 拒绝时（POSITION_NOT_FIT / OVERLAP / DIFFERENTHIGH /
// OVERBORDER / UNEXPLORE），把该建筑占地范围内的每一格标记为坏点、记下帧号，
// USR_BAD_BUILD_SPOT_FRAMES 帧内不再选它们。
//
// 【为什么需要】原先位置类失败只做 `buildCandidateIndex++`（换一个候选序号）。
// 那是个【全局游标】：绕一圈之后还会回到同一个放不下的位置，反复撞同一堵墙。
// 粒度错了 —— 该记的是「哪块地不行」，不是「上次试到第几个」。
// 这个做法照搬自另一套实现（它的 badPlace[120][120] + badPlaceTime）。
//
// 失效期 1500 帧是有意的：地块被占的原因（农田/树木/没探明）大多不会很快消失，
// 但也可能是暂时的（别人在造的工地挡着），所以不永久拉黑。
static void MarkBuildSpotBad(int dr, int ur, int buildingType)
{
    if (dr < 0 || ur < 0)
        return;
    const int size = BuildingBlockSize(buildingType);
    for (int x = dr; x < dr + size; ++x)
    {
        for (int y = ur; y < ur + size; ++y)
            badBuildSpot[make_pair(x, y)] = g_frame;
    }
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
            // 【坏点排除】这块地最近因「放不下」被 Core 拒过，别再选它。
            // 见 MarkBuildSpotBad —— 没有这一步，失败重试会绕回同一个位置。
            map<pair<int, int>, int>::const_iterator bad =
                badBuildSpot.find(make_pair(dr, ur));
            if (bad != badBuildSpot.end() &&
                g_frame - bad->second <= USR_BAD_BUILD_SPOT_FRAMES)
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

    // 箭塔：建在【朝敌方基地那一侧的近处】—— 八方向里最靠敌的三个方向各一座。
    //
    // 【为什么是"朝敌那一侧"】敌方波次以追击祭司/最近的农民为目标，来向基本就在
    // 敌方基地那一侧（四张图上敌方基地都在我方市镇中心的地图对足点，实测曼哈顿
    // 122~140）。塔摆到那一侧，敌人一进射程就同时被几座塔开火，仇恨也最早被从
    // 基地核心拉走；绕中心均布的话总有一半塔背对来敌，永远不开火。
    //
    // 【为什么是八方向、只取三个】四个正方向做不到"三座都在 ±45° 内" —— 敌方在
    // 45° 对角上时只有两条臂落在 ±90° 内，第三个必然跑到 135°（等于背对一半）。
    // 用八方向（含斜向）取最靠敌的三个，三座就全落在 ±45° 内。
    //
    // 【为什么距离是 6 / 9 格】塔射程 DIS_ARROWTOWER = 7，放在离中心 6 格处，
    // 火力覆盖「离中心 −1~13 格」—— 市镇中心和祭司的待机位置都罩得住
    // （这正是原近圈取 6 的原因：6 < 7）。第 4 座放在最朝敌那个方向的 9 格处，
    // 往里再叠一层。
    //
    // 【斜向的 6 格怎么算】方向用 16 方向整数表（×100），斜向分量是 71 ——
    // 6 * 71 / 100 = 4，即每轴 4 格、欧氏约 5.7 格，与正方向的 6 格覆盖范围基本
    // 一致，不会因为选了斜向就把塔推远。
    if (buildingType == BUILDING_ARROWTOWER)
    {
        // 16 方向单位向量（×100 整数表），与 StandoffRingOffset 是同一套。
        static const int kCos16[16] = {100, 92, 71, 38, 0, -38, -71, -92,
                                       -100, -92, -71, -38, 0, 38, 71, 92};
        static const int kSin16[16] = {0, 38, 71, 92, 100, 92, 71, 38,
                                       0, -38, -71, -92, -100, -92, -71, -38};
        // 八个主方向在 16 表里的下标：东 东南 南 西南 西 西北 北 东北。
        static const int kDir8[8] = {0, 2, 4, 6, 8, 10, 12, 14};
        // 拿不到敌方锚点时的兜底：四方向（东南西北）各一座，只看距离不偏袒方向。
        static const int kCard16[4] = {0, 4, 8, 12};
        // 4 个格位 = 朝敌前三个方向各一座（6 格），最朝敌那个方向再补一座（9 格）。
        static const int kSlotIdx[4] = {0, 1, 2, 0};
        static const int kSlotDist[4] = {6, 6, 6, 9};

        // 【敌方方向】取「中心 → 敌方基地锚点」的向量 (dx, dy)。
        // 锚点走 EstimateEnemySiegeAnchor（侦察到就用真坐标，否则用我方中心的
        // 地图对极点估算）。拿不到（或锚点压在中心上）时走四方向兜底。
        int dx = 0;
        int dy = 0;
        {
            int anchorDR = 0;
            int anchorUR = 0;
            if (EstimateEnemySiegeAnchor(anchorDR, anchorUR))
            {
                dx = anchorDR - center->BlockDR;
                dy = anchorUR - center->BlockUR;
            }
        }
        const bool haveEnemyDir = (dx != 0 || dy != 0);

        // 【八个方向按「与敌方方向的对齐度」降序排序】点积越大越朝敌。
        // 插入排序，平局保持原下标顺序（严格小于才前移）—— 结果只取决于输入，
        // 不会每帧翻。前三个就是「最靠敌的三个方向」。
        int order[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        if (haveEnemyDir)
        {
            int dot[8];
            for (int i = 0; i < 8; ++i)
                dot[i] = dx * kCos16[kDir8[i]] + dy * kSin16[kDir8[i]];
            for (int i = 1; i < 8; ++i)
            {
                const int key = order[i];
                int j = i - 1;
                while (j >= 0 && dot[order[j]] < dot[key])
                {
                    order[j + 1] = order[j];
                    --j;
                }
                order[j + 1] = key;
            }
        }

        for (int slot = 0; slot < 4; ++slot)
        {
            const int dir16 = haveEnemyDir ? kDir8[order[kSlotIdx[slot]]]
                                           : kCard16[slot];
            const int dist = haveEnemyDir ? kSlotDist[slot] : 6;
            int dr = center->BlockDR + kCos16[dir16] * dist / 100;
            int ur = center->BlockUR + kSin16[dir16] * dist / 100;
            // 【夹进地图】朝敌那三个方向通常指向地图内部，越界是少数情况，
            // 夹取只是安全网 —— 等价于「那一侧能走多远走多远」。
            const int kMargin = USR_ARROWTOWER_BUILD_MIN_MARGIN;
            const int kSize = BuildingBlockSize(BUILDING_ARROWTOWER);
            dr = max(kMargin, min(MAP_L - kMargin - kSize, dr));
            ur = max(kMargin, min(MAP_U - kMargin - kSize, ur));
            if (IsBuildCandidateUsable(dr, ur, buildingType))
                return make_pair(dr, ur);
        }
        // 四个格位全不可用 → 落到通用扫描。
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
        // 【护送祭司的农民归 ManageFarmerEscort 管，经济一律不碰】必须放在
        // 下面那条「僵尸农民救援」之前 —— 那条会把 WALKING 且卡住的农民接过来
        // 派去采集，而护送农民在赶路时卡住是常态，被接管就等于中途退出队伍。
        // 他的卡住由 ManageFarmerEscortBand 自己处理（换落点 + 拉黑）。
        if (IsFarmerEscorting(farmer.SN))
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
        // 护送祭司的农民不回头干工地（他是 latched 编制，见 farmerEscortDuty）。
        if (IsFarmerEscorting(farmer.SN))
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
    // 记下这次下单的位置：被 Core 拒时要靠它把这块地记成坏点（见 MarkBuildSpotBad）。
    buildOrderDR = position.first;
    buildOrderUR = position.second;
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
// 交付建筑的候选落点是否**够靠近我方**。
//
// 见 USR_DEPOT_MAX_HOME_DISTANCE：没有这条上限时，「谁离现有交付建筑最远谁赢」
// 会把仓库一路推到敌方那一侧，随后那片资源在评分里反超近处的资源、农民横穿地图。
//
// home == nullptr（开局头几帧、市镇中心还没进 info.buildings）时一律放行：
// 那个时刻本来就还没得挑，卡住这里只会让前置建筑凑不齐、拖慢升时代。
static bool IsDepotSpotNearHome(const tagBuilding *home, int blockDR, int blockUR)
{
    if (home == nullptr)
        return true;
    return BlockDis(home->BlockDR, home->BlockUR, blockDR, blockUR) <=
           USR_DEPOT_MAX_HOME_DISTANCE;
}

static void TryBuildReturnDepot(UsrAI *ai)
{
    if (buildOrderId != -1 ||
        g_frame - lastBuildOrderFrame < USR_BUILD_ORDER_INTERVAL)
        return;

    // 我方市镇中心 —— 下面每一处候选都要用它挡一道「别建到敌方那侧去」。
    const tagBuilding *home = FindCenter();

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
    // 【仓库候选已停用，见 USR_DEPOT_STOCK_ENABLED】猎物群这条锚的是
    // BUILDING_STOCK（羚羊尸体走 HUMAN_STOCKFOOD → 仓库），一并停掉：
    // 尸体采集改送市镇中心（引擎允许），不必在猎场旁边铺仓库。
    if (USR_DEPOT_STOCK_ENABLED)
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
            // 【农民禁区】在敌方基地旁边建仓库＝给禁区里的资源盖交付点，
            // 农民根本不会去采（FindBestResourceSN 已挡），白花 120 木。
            if (IsInsideEnemyKeepout(it->second.centerDR, it->second.centerUR))
                continue;
            // 【只在我方半场】见 USR_DEPOT_MAX_HOME_DISTANCE。
            if (!IsDepotSpotNearHome(home, it->second.centerDR,
                                     it->second.centerUR))
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
            // 【农民禁区】不为禁区里的资源建交付点 —— 农民不会去采，仓库/谷仓
            // 建在那儿纯属浪费。见 IsInsideEnemyKeepout。
            if (IsInsideEnemyKeepout(resource.BlockDR, resource.BlockUR))
                continue;
            // 【只在我方半场】见 USR_DEPOT_MAX_HOME_DISTANCE。这道是这条循环的
            // 关键：不加的话「离现有交付建筑最远」会把落点一路推到敌方那一侧。
            if (!IsDepotSpotNearHome(home, resource.BlockDR, resource.BlockUR))
                continue;
            // 建哪座交付建筑由资源类型决定：浆果丛要的是【谷仓】。原先一律当成
            // 仓库，于是会在浆果丛旁边盖一座对它毫无用处的仓库，而 AI 自己的评分
            // 立刻变「近」、继续往那儿派人 —— 自我强化的假象，也是野外那些多余
            // 仓库的来源之一。详见 ReturnBuildingForResource 的引擎真值表。
            const int returnBuilding = ReturnBuildingForResource(resource.Type);
            // 【仓库候选已停用，见 USR_DEPOT_STOCK_ENABLED】树/石/金/鱼都走
            // BUILDING_STOCK，一律跳过；只留下浆果丛（→ BUILDING_GRANARY）——
            // 谷仓是我方箭塔科技的前置，那条路不能停。
            // 跳过的后果：这些资源不再有自己的交付点，农民改送市镇中心。
            if (!USR_DEPOT_STOCK_ENABLED && returnBuilding == BUILDING_STOCK)
                continue;
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
            // 【农民禁区】同资源循环。
            if (IsInsideEnemyKeepout(building.BlockDR, building.BlockUR))
                continue;
            // 【只在我方半场】见 USR_DEPOT_MAX_HOME_DISTANCE。
            if (!IsDepotSpotNearHome(home, building.BlockDR, building.BlockUR))
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
    // 普通弓兵 5 个（40 食 + 20 木，便宜、前期即可产）。
    // 【车轮科技完成后归零】普通弓兵是战车弓兵的下位替代（血 35 / 攻 3 /
    // 射程 5 vs 血 70 / 攻 4 / 射程 7），科技一出来就把它整个让给主力 ——
    // 下面 chariotArcherTarget 是按人口上限倒推的，bowmanTarget 归零后
    // 那 5 个人口会自动落到战车弓兵上（20 → 25）。
    // 已经在场上的普通弓兵不会因此消失，只是不再补充。
    bowmanTarget = wheelTechReady ? 0 : 5;
    // 侦察骑兵：USR_SCOUT_FIRST_FRAME 后生产 1 个保命型（遇敌即撤回基地，
    // 不承担侦测）；USR_SCOUT_RECON_FRAME 后再补 1 个，由它专职侦测敌方基地
    // （见 DispatchScouts 的角色判定）。
    scoutTarget = ScoutTargetCount();
    // 【战车配额 2 → 0，让给战车弓兵】原先是「保留 2 个速度 4.07 的前排挡远程」，
    // 但它们的实际作用不成立：本 AI 的伤害全靠战车弓兵在射程外输出，而战车是
    // 近战（DIS_CHARIOT = 0）、40 食 + 60 木，冲到敌方守军面前只是换血 ——
    // 换掉的还是我们最缺的人口。腾出来的 2 个人口自动落到下面的主力配额上，
    // 战车弓兵 20 → 22（见下面那个算式的注释，它是按人口上限倒推的）。
    // 已经在场上的战车不会因此消失，只是不再补充。
    chariotTarget = 0;
    // 主力（战车弓兵，40 食 + 70 木，血70/攻4/射程7/速度4.07）的配额
    // = 人口总数减去上面所有非主力配额。见 USR_POP_TARGET 的说明：
    // 这样调整任何一项都不会让总和超过人口上限。
    // 当前算式：50 − 1(祭司) − 20(农民) − 5(弓兵) − 2(侦察) − 0(战车) = 22。
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

// 修理一座建筑最多同时派几个人。
//
// 【为什么可以多人同修】内核的修理进度是按「农民 ↔ 建筑」的关系记账的：
// Core_List::manageRelationList（Core_List.cpp:471）每帧遍历所有 relation，
// 每个处于 CoreDetail_UpdateRatio 阶段的农民各自执行一次 object_RatioChange
// （Core_List.cpp:1405），里面【无条件】调用 buildGoalOb->update_Build()
// （Building.cpp:494）。既没有「目标已被占用」的独占判据，也没有「每建筑
// 每帧只推进一次」的合并 —— 所以 N 个人同修就是 N 倍速度。
// 唯一的代价是资源也按 N 倍扣：tryDeductRepairHpCost（Building.cpp:453）
// 按 REPAIR_COST_RATIO(0.5) × 本次回血比例 × 建筑原造价 扣木材/石料等，
// 钱不够就 suspendRelation 当场停工（这是速度的唯一硬上限）。
//
// 注：config 里的 FARMER_CONSTRUCTSPEED(0.02) 是根【没接上的线】——
// 除了定义与 json 那一行，全仓库没有任何地方读取它；真正的速率来自
// Building::get_retio_Build()（Building.cpp:530），只看建筑自身的建造时长。
//
// 【为什么箭塔给 3 个、其余只给 1 个】箭塔是防守骨架，被打掉等于基地火力
// 直接掉一档，值得为它多抽人；其余建筑（房屋、农场、市场）只等空闲农民
// 顺手修，多抽等于用持续的采集产出换一次不紧急的修理。
// 取值 3 的依据：单人修不住实测的那座塔（血线 86 → 27 → 11 一路掉到被拆），
// 3 倍速才可能压过损耗；再高就要考虑石头消耗了（塔造价 150 石，修满约耗 75）。
static const int USR_REPAIR_CREW_TOWER = 3;
static const int USR_REPAIR_CREW_OTHER = 1;

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
        // 护送祭司的农民不修建筑。必须放在下面那条「僵尸农民也接受」之前 ——
        // 那条会收走 WALKING 且卡住的农民，而护送农民赶路时卡住是常态。
        if (IsFarmerEscorting(farmer.SN))
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

    // 【统计当前有几个农民在修这座建筑】
    //
    // 原先这里是「只要有一个在修（含正在赶路的）就跳过」，等于每座建筑
    // 永远只有一个人修。而内核是按 relation 逐人推进修理的、没有任何合并
    // （理由见 USR_REPAIR_CREW_TOWER 上方的说明）—— 多人同修就是多倍速度，
    // 那道判断把这条路堵死了。实测一座 125 血的塔，单人修的血线是
    // 86 → 27 → 11 一路掉到被拆，修补量根本抵不过损耗。
    const int crewLimit = IsUrgentRepairTarget(building.Type)
                              ? USR_REPAIR_CREW_TOWER
                              : USR_REPAIR_CREW_OTHER;
    int repairers = 0;
    for (const tagFarmer &farmer : info.farmers) {
      if (farmer.Blood > 0 && farmer.WorkObjectSN == building.SN) {
        ++repairers;
      }
    }
    if (repairers >= crewLimit)
      continue;

    // 【一次把缺口补满，而不是每次只补一个人】
    // 这个函数每 200 帧才跑一次；若一次只派一个，凑齐 3 个人要 600 帧，
    // 塔早就没了。所以在这一轮里循环补齐到 crewLimit。
    for (int slot = repairers; slot < crewLimit; ++slot) {
      // 先找空闲农民；箭塔这类紧急目标在没人空闲时，再从采集队里抽最靠近的。
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
        break;  // 没人可派了，剩下的名额留到下一轮

      usedFarmers.insert(bestFarmerSN);
      CancelPendingGatherOrder(bestFarmerSN);
      ai->HumanAction(bestFarmerSN, building.SN);
      farmerLastOrderFrame[bestFarmerSN] = g_frame;
      repairedAny = true;
      {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "[REPAIR] f=%d type=%d sn=%d blood=%d/%d farmer=%d pulled=%d "
                 "crew=%d/%d",
                 g_frame, building.Type, building.SN, building.Blood,
                 building.MaxBlood, bestFarmerSN, (int)pulledFromWork,
                 slot + 1, crewLimit);
        AiDebugLog(buf);
      }
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
                {
                    buildCandidateIndex++;
                    // 【把这块地记成坏点】只换候选序号是不够的：buildCandidateIndex
                    // 是个全局游标，绕一圈还会回到同一个放不下的位置。
                    // 见 MarkBuildSpotBad。
                    MarkBuildSpotBad(buildOrderDR, buildOrderUR, buildOrderType);
                }
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
    // 箭塔防守：围绕市镇中心三圈 × 四方向分散布置
    // （方向与距离的理由见 GetBuildCandidate 里箭塔那一段），
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
    // 【默认关闭，见 USR_DEPOT_ENABLED】为资源建交付建筑（仓库 / 谷仓）。
    // 注意：关掉它不只是「不建仓库」—— 谷仓是市场与箭塔科技的引擎前置，
    // 所以会连市场、车轮科技、战车弓兵、箭塔、农田一起断掉，详见那个常量的说明。
    if (USR_DEPOT_ENABLED)
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
    // 【撤退点 = 市镇中心 + 「背离威胁」方向 × USR_PRIEST_RETREAT_DISTANCE】
    //
    // 【为什么不再用「朝中心走 2/3，再逐轴偏 3 格」】那个公式的实际位移上限只有
    // 4 格：祭司本来就在中心附近时，(priest + center*2)/3 ≈ 中心本身，再 ±3 就
    // 落在中心 ±3 格内，等于没退；而 dx/dy 是「祭司相对威胁」逐轴取符号，
    // 威胁一换人（实测 tSort 在 6 骑兵 / 10 方阵兵 / 13 阔剑兵 之间跳）符号就
    // 翻到对角。实测 f=21396~22405 发出的十二个撤退点全落在中心 ±4 格的方框里
    // —— 祭司在里面打转 1300 帧，被追兵从容磨死。
    //
    // 【新公式的两点改变】
    //   ① 退到离中心【固定 14 格】的位置 —— 保证有真实位移，能跟追兵拉开距离；
    //   ② 方向取【从威胁指向中心】的单位向量 —— 也就是往中心的另一侧退，
    //      离威胁最远。它比「祭司相对威胁」稳定：敌人的位置变化比祭司慢得多。
    //
    // 【已知局限】威胁换人时方向仍可能翻（新威胁在中心的另一侧），那样目标会跳
    // 到中心对面（最多 28 格）。这是一次有意的取舍：宁可偶尔跳一次，也不要
    // 四个格子的位移 —— 后者在任何情况下都救不了祭司。
    int cDR = priest.BlockDR;
    int cUR = priest.BlockUR;
    const tagBuilding *center = FindCenter();
    if (center != nullptr) {
        cDR = center->BlockDR;
        cUR = center->BlockUR;
    }

    // 背离威胁的方向：优先「从威胁指向中心」；威胁压在中心上时退化为
    // 「从中心指向祭司」（继续往外走）；两者重合时给一个确定方向。
    double dx = double(cDR - threat.BlockDR);
    double dy = double(cUR - threat.BlockUR);
    double len = sqrt(dx * dx + dy * dy);
    if (len < 0.5) {
        dx = double(priest.BlockDR - cDR);
        dy = double(priest.BlockUR - cUR);
        len = sqrt(dx * dx + dy * dy);
        if (len < 0.5) {
            dx = 1.0;
            dy = 0.0;
            len = 1.0;
        }
    }

    // 【落点必须是可达的 —— 否则祭司会原地不动】
    //
    // 上面只算出【一个】方向。若那个落点落在障碍里、或四邻全被挡，HumanMove 会被
    // 引擎按「目标格四正交邻居全是障碍 → nullPath」取消（Core_List.cpp:2091）；
    // 而 ShouldReissuePriestMove 判「卡住」之后会每 USR_PRIEST_STUCK_FRAMES(150)
    // 帧重发一次【同一个点】—— 每次重发都经 suspendRelation 清空路径，于是它一步
    // 都走不了。实测某局：祭司在 (75,79) 连续 5 个采样、660 帧没动一格，
    // mvTgt=(83,88) 一直挂着而 mvF 每 150 帧跳一次；那 660 帧里身边 4 个敌人都在
    // 10 格内、最近的 6 格，最后被一个贴上来的战车弓兵打死。
    //
    // 【候选怎么排】以原公式那个"背离威胁"的方位为基准，按 0、−1、+1、−2、+2 …
    // 的顺序绕一整圈（16 分度、每步 22.5°）—— 正常情况选中的仍然是原方向，
    // 只有它不可用时才偏一点。判据三条：IsPriestPointUsable（界内/非海洋/无建筑）、
    // IsReachableAround（四邻不全被挡），以及「最近被判走不到」的黑名单。
    // 【第一个候选是原公式那个点本身】—— 它可用时结果与改动前【完全一致】，
    // 只有它不可用才退到下面的 16 分度环。绝不能把"正常情况"也换成 22.5° 的整数
    // 方位（那会让落点偏出最多 11.25°、半径 14 上约 2.7 格）。
    {
        int exactDR = cDR + int(dx / len * double(USR_PRIEST_RETREAT_DISTANCE));
        int exactUR = cUR + int(dy / len * double(USR_PRIEST_RETREAT_DISTANCE));
        exactDR = max(1, min(MAP_L - 2, exactDR));
        exactUR = max(1, min(MAP_U - 2, exactUR));
        const map<pair<int, int>, int>::const_iterator bad =
            priestBadRetreatPoints.find(make_pair(exactDR, exactUR));
        const bool blacklisted =
            bad != priestBadRetreatPoints.end() && g_frame < bad->second;
        if (!blacklisted && IsPriestPointUsable(exactDR, exactUR) &&
            IsReachableAround(exactDR, exactUR, 1))
        {
            return make_pair((exactDR + 0.5) * double(BLOCKSIDELENGTH),
                             (exactUR + 0.5) * double(BLOCKSIDELENGTH));
        }
    }

    static const int kCos16[16] = {100, 92, 71, 38, 0, -38, -71, -92,
                                   -100, -92, -71, -38, 0, 38, 71, 92};
    static const int kSin16[16] = {0, 38, 71, 92, 100, 92, 71, 38,
                                   0, -38, -71, -92, -100, -92, -71, -38};
    int base = 0;   // 原方向落在哪个方位（点积最大）
    {
        const double ux = dx / len;
        const double uy = dy / len;
        double bestDot = -1e18;
        for (int i = 0; i < 16; ++i)
        {
            const double dot = double(kCos16[i]) * ux + double(kSin16[i]) * uy;
            if (dot > bestDot)
            {
                bestDot = dot;
                base = i;
            }
        }
    }
    for (int step = 0; step < 16; ++step)
    {
        const int offset = ((step + 1) / 2) * ((step % 2 == 1) ? -1 : 1);
        const int i = ((base + offset) % 16 + 16) % 16;
        int candDR = cDR + kCos16[i] * USR_PRIEST_RETREAT_DISTANCE / 100;
        int candUR = cUR + kSin16[i] * USR_PRIEST_RETREAT_DISTANCE / 100;
        candDR = max(1, min(MAP_L - 2, candDR));
        candUR = max(1, min(MAP_U - 2, candUR));
        if (!IsPriestPointUsable(candDR, candUR))
            continue;   // 在海里 / 被建筑占住
        if (!IsReachableAround(candDR, candUR, 1))
            continue;   // 四邻全是障碍 → 指令会被 nullPath 取消
        const map<pair<int, int>, int>::const_iterator bad =
            priestBadRetreatPoints.find(make_pair(candDR, candUR));
        if (bad != priestBadRetreatPoints.end() && g_frame < bad->second)
            continue;   // 最近被判"走不到"，先别再用它
        return make_pair((candDR + 0.5) * double(BLOCKSIDELENGTH),
                         (candUR + 0.5) * double(BLOCKSIDELENGTH));
    }

    // 【兜底】16 个方位全不可用 → 仍然用原公式那一个点（夹进地图）。
    int blockDR = cDR + int(dx / len * double(USR_PRIEST_RETREAT_DISTANCE));
    int blockUR = cUR + int(dy / len * double(USR_PRIEST_RETREAT_DISTANCE));
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

// 取祭司当前锁定的转换目标。锁定失效（目标死亡 / 离开视野 / 已被转成我方的）
// 时返回 nullptr —— 调用方会重新按优先级选一个并改写锁定。
//
// 【判据为什么就是「还在 info.enemy_armies 里」】info.enemy_armies 是引擎给的
// 「当前可见的敌方军队」：目标走出视野就不在列表里，转换成功后归属改变、
// 同样不再出现在敌方列表里（那正是我们想要的「转换完成」信号）。
// 两个出口自动合一，不需要额外的状态跟踪。
static const tagArmy *FindLockedConversionTarget()
{
    if (priestConversionLockSN == -1)
        return nullptr;
    for (const tagArmy &enemy : info.enemy_armies)
    {
        if (enemy.SN == priestConversionLockSN && enemy.Blood > 0)
            return &enemy;
    }
    return nullptr;
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
static bool ShouldReissuePriestMove(const tagArmy *priest, int tx, int ty,
                                    bool *wasStuck = nullptr)
{
  if (wasStuck != nullptr)
    *wasStuck = false;
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
  if (nowDis2 < fromDis2)
    return false;
  // 走到这里 = 「目标没变 + 超过 STUCK_FRAMES 帧 + 一直没靠近」= 确实是卡住。
  // 通过 wasStuck 把这个结论告诉调用方：撤退那条路要用它把这个落点记进黑名单
  // （重发同一个走不到的点只是白清一次路径）。
  if (wasStuck != nullptr)
    *wasStuck = true;
  return true;
}

// 祭司是否已经闯进「敌方基地禁区」（USR_PRIEST_ENEMY_BASE_KEEPOUT 格以内）。
// 是则把「往市镇中心方向退」的落点写进 outDR/outUR 并返回 true。
//
// 【为什么退向市镇中心】中心离敌方基地上百格，朝它走一定出圈；而且那边有箭塔
// 与部队。不需要算「背离敌方基地的精确方向」—— 那正是历史上反复出问题的地方
// （逐轴取符号会随参照点翻转，见 GetPriestEmergencyPoint 的记录）。
//
// 【锚点用估算】敌方基地通常整局没被侦察到（实测 `enemyB` 最多到 1），
// 用真坐标就没有红线可言。估算误差最差 26 格（见 EstimateEnemySiegeAnchor），
// 副作用只是"退早了/退晚了"一点。
static bool PriestInEnemyBaseKeepout(const tagArmy &priest_int,
                                     int &outDR, int &outUR)
{
    int anchorDR = 0;
    int anchorUR = 0;
    if (!EstimateEnemySiegeAnchor(anchorDR, anchorUR))
        return false;
    if (BlockDis2(priest_int.BlockDR, priest_int.BlockUR, anchorDR, anchorUR) >=
        USR_PRIEST_ENEMY_BASE_KEEPOUT * USR_PRIEST_ENEMY_BASE_KEEPOUT)
        return false;

    const tagBuilding *center = FindCenter();
    if (center == nullptr)
        return false;
    outDR = center->BlockDR;
    outUR = center->BlockUR;
    return true;
}

// 祭司的【前线驻留点】：在「敌方锚点 ↔ 祭司」这条射线上、离锚点 postDistance 格处。
//
// 【为什么沿祭司自己那条射线取】祭司是从家里走过来的；沿它当前所在的射线取点，
// "已经在 40 格上"时算出来的点就落在它脚下附近 —— 它不需要横穿一段去换方位。
//
// 【候选与兜底】射线那一格优先；它不可用（海里 / 被建筑占住 / 四邻全被挡 ——
// 后者会让 HumanMove 被引擎按 nullPath 取消）时，绕锚点转 16 个方位取第一个
// 可用的。与祭司撤退点的候选做法一致。返回 false 表示 16 个方位全不可用。
static bool PriestFrontPostBlock(int anchorDR, int anchorUR, int priestDR,
                                 int priestUR, int postDistance, int &outDR,
                                 int &outUR)
{
    static const int kCos16[16] = {100, 92, 71, 38, 0, -38, -71, -92,
                                   -100, -92, -71, -38, 0, 38, 71, 92};
    static const int kSin16[16] = {0, 38, 71, 92, 100, 92, 71, 38,
                                   0, -38, -71, -92, -100, -92, -71, -38};
    // 祭司相对锚点的方位（16 分度，点积最大）
    int base = 0;
    {
        const double dx = double(priestDR - anchorDR);
        const double dy = double(priestUR - anchorUR);
        const double len = sqrt(dx * dx + dy * dy);
        if (len < 0.5)
            return false;   // 与锚点重合，算不出方位：本帧不做
        double bestDot = -1e18;
        for (int i = 0; i < 16; ++i)
        {
            const double dot = double(kCos16[i]) * (dx / len) +
                               double(kSin16[i]) * (dy / len);
            if (dot > bestDot)
            {
                bestDot = dot;
                base = i;
            }
        }
    }
    for (int step = 0; step < 16; ++step)
    {
        const int offset = ((step + 1) / 2) * ((step % 2 == 1) ? -1 : 1);
        const int i = ((base + offset) % 16 + 16) % 16;
        int dr = anchorDR + kCos16[i] * postDistance / 100;
        int ur = anchorUR + kSin16[i] * postDistance / 100;
        dr = max(1, min(MAP_L - 2, dr));
        ur = max(1, min(MAP_U - 2, ur));
        if (!IsPriestPointUsable(dr, ur))
            continue;
        if (!IsReachableAround(dr, ur, 1))
            continue;
        outDR = dr;
        outUR = ur;
        return true;
    }
    return false;
}

static void ManagePriest(UsrAI *ai)
{
    const tagArmy *priest = FindPriest();
    if (priest == nullptr)
        return;  // 祭司死亡后不再执行祭司逻辑，避免解引用空指针

    // 【没有敌人 → 去转换敌方攻城武器厂（唯一的获胜条件）】
    //
    // 【为什么需要这一整段】原先【没有任何「走过去」的逻辑】：
    // FindEnemySiege 的判据里有 `if (dis2 >= 12 * 12) continue`，要求攻城厂
    // 已经在祭司 12 格以内 —— 所以祭司只有在【偶然晃到旁边】时才会转换它，
    // 其余时间都在基地附近待命。获胜路径实际上从来没被主动执行过。
    //
    // 【为什么放在最前面】它是获胜路径，优先级高于守家（12 格）与硬圈（50 格）——
    // 那两条都是为了保命，而这里是唯一的取胜手段，不能互相否决。
    //
    // 【为什么要求「没有敌人」】有敌人在的时候让祭司出门就是送；而且一旦它靠近
    // 敌方基地，守军进入视野会让这个条件自动失效、它就会退回来，天然自限。
    // 保留原先的 g_frame >= USR_PRIEST_PASSIVE_FRAME(26000) 门槛：在那之前
    // 军队还没成型，这时候押上祭司去换基地是亏的。
    //
    // 【为什么还要加 WorkObjectSN == -1】这一段发的是 HumanMove，而 HumanMove
    // 会经 suspendRelation 打断既有的工作关系。它是唯一排在「转换不打断」
    // 那道 return 之前、又能下发移动的分支 —— 不加这个条件的话，祭司在
    // 「离厂 10~12 格」处转换厂时会被它重新拽向厂那一格，这次读条作废重来。
    // （转敌方士兵时 HasVisibleEnemyArmy() 已经为真、本就进不来；
    //   转建筑时目标不是兵，这个判据才是真正的防线。）
    //
    // 【为什么必须等塔清完】需求原话：「祭司要等到所有敌方箭塔都被摧毁了才
    // 可以转化敌方攻城武器厂」。这道门把「祭司冲厂」和「清塔」串成了顺序关系：
    // 塔没清完，祭司就留在 40 格带上等（条件不成立时它会落到下面的驻留带分支）。
    //
    // 【当初为什么先做成「被牵制」、后来改成「被摧毁」】最早的版本只要求每座塔
    // 都有人正以它为工作目标（WorkObjectSN == 塔 SN）。实测证明不够 ——
    // 【被牵制的塔照样开火】，射程 7 格足够打死祭司：那一局农民 38000 出发、
    // 到 42001 只剩 1 个，5 座塔只清掉 2 座，而祭司 39637 就已经动身；它冲进厂区
    // 时围着厂的 4 座塔（距厂 3~6.4 格，全在射程内）一起开火，57 血约 500 帧掉到 0。
    // 所以按需求改成「一座【活着的】敌塔都不能有」。那段实测出自农民护送机制，
    // 该机制现已由 USR_FARMER_ESCORT_ENABLED 停用；数据留在这里，是因为它解释了
    // 这个判据为什么必须这么严。
    //
    // 【两道门都在 CanPriestApproachSiege() 里，是 ③ 与 ④】
    //   ③ AllEnemyArrowTowersEngaged —— 每座【活着的】敌塔都要有人以它为工作目标；
    //   ④ SiegeTowersCleared      —— 一座【活着的】敌塔都不能有。
    // ④ 成立时 ③ 必然成立（③ 的循环里已经没有活塔可查）。所以实际起决定作用的
    // 是 ④；③ 保留的价值在于「塔还活着但已经没人管」这个中间状态下能提供更细的
    // 诊断 —— [SIEGE] 行的 allEngaged= 与 towersCleared= 两个字段分别对应它们，
    // 排查「祭司为什么不往厂走」时先看这两列。
    //
    // 【代价】拆一座 125 血的塔要 125 次命中，所以「等塔全清」会把祭司的出门时间
    // 推得很晚。而当前这版（USR_DEPOT_ENABLED=false → 无市场 → 无车轮科技 →
    // 军队永不进攻）没有任何单位会去拆塔 —— 于是 ④ 完全取决于「敌方箭塔有没有被
    // 侦察到」：一座都没探出来，④ 就平凡成立，祭司照样能进厂。
    // 【实测印证】第二轮 16 局评测里 map.njust 的前两个旋转就是这样赢下来的
    // （win=True，f=44163 / 41911）—— 所以「没军队 ⇒ 祭司一定不出门」是错的推论，
    // 不要照着它判断。
    // 【诊断：祭司为什么不往厂走】这一段是整条获胜路径的入口，而它要过
    // CanPriestApproachSiege() 的四道门（① 过了 26000 / ② 视野无敌兵 /
    // ③ 每座活敌塔都被牵制 / ④ 一座活敌塔都没有），外加「厂是否已侦察到」。
    //
    // 【为什么必须加】此前日志里能看到的只有 [CONVBLOCK]（那是「有目标却不发起
    // 转换」），而「根本走不到转换那一步」是完全空白的 —— 实测有一局祭司停在
    // (14,7)（离锚点曼哈顿 6 格、满血、视野内无敌人）整整 500 帧没动，
    // [CONV] 与 [CONVBLOCK] 都没有输出，只能靠猜是三道门里的哪一道关着，
    // 还是"厂边的落点根本走不到"。
    {
        static int lastSiegeLogFrame = USR_INVALID_FRAME;
        if (lastSiegeLogFrame == USR_INVALID_FRAME ||
            g_frame - lastSiegeLogFrame >= 500)
        {
            lastSiegeLogFrame = g_frame;
            const tagBuilding *seen = FindEnemySiegeBuilding();
            int anchorDR = 0;
            int anchorUR = 0;
            EstimateEnemySiegeAnchor(anchorDR, anchorUR);
            const int seenDis2 =
                seen != nullptr
                    ? BlockDis2(priest->BlockDR, priest->BlockUR, seen->BlockDR,
                                seen->BlockUR)
                    : -1;
            char buf[288];
            snprintf(buf, sizeof(buf),
                     "[SIEGE] f=%d priest=(%d,%d) hp=%d visibleEnemy=%d wo=%d "
                     "allEngaged=%d towersCleared=%d siegeSeen=%d "
                     "siege=(%d,%d) dis2=%d anchor=(%d,%d)",
                     g_frame, priest->BlockDR, priest->BlockUR, priest->Blood,
                     HasVisibleEnemyArmy() ? 1 : 0, priest->WorkObjectSN,
                     AllEnemyArrowTowersEngaged() ? 1 : 0,
                     SiegeTowersCleared() ? 1 : 0, seen != nullptr ? 1 : 0,
                     seen != nullptr ? seen->BlockDR : -1,
                     seen != nullptr ? seen->BlockUR : -1, seenDis2, anchorDR,
                     anchorUR);
            AiDebugLog(buf);
        }
    }

    if (CanPriestApproachSiege() && priest->WorkObjectSN == -1)
    {
        const tagBuilding *siegeTarget = FindEnemySiegeBuilding();
        if (siegeTarget != nullptr)
        {
            const int siegeDis2 =
                BlockDis2(priest->BlockDR, priest->BlockUR,
                          siegeTarget->BlockDR, siegeTarget->BlockUR);

            // 【只需进到「选取距离」以内，不再要求贴到厂边上】
            //
            // 【原来那套为什么会死锁】原判据是 siegeDis2 > USR_PRIEST_SIEGE_TOUCH_DIS2
            // （4，即贴到厂 2 格内）就去"走到厂边"，而落点是在【厂周围一圈】里挑
            // 离祭司最近的一个。厂被建筑或自己人围着时，那一圈里离祭司最近的格
            // 可能就是他【自己脚下】—— 实测：祭司停在 (14,7)、到厂平方距离 18
            // （4.24 格），goal 选出来正是 (14,7)，于是 ShouldReissuePriestMove
            // 判定"目标没变"而不下发，代码 return。它每帧都在这一分支里原地返回，
            // 【永远走不到下面的转换分支】。日志表现就是 goal 恒等于 priest 坐标、
            // reissue=0 —— 那正是"满血、站着不动、一次转换都没发起过"的成因。
            //
            // 现在门槛放到 USR_PRIEST_SIEGE_PICK_DISTANCE —— 与转换分支【选取】
            // 目标用的是同一个距离。进到那个圈里，本分支就不再插手，交给转换分支
            // 用 HumanAction 建立关系、由【引擎】驱动走完最后一段：引擎自己知道
            // 要贴到多近才转得动（判据是"贴邻 + 已建成"，见 Core_CondiFunc.cpp），
            // 不需要我们替它算一个可能被自己人占住的落点。
            const int pickDis2 = USR_PRIEST_SIEGE_PICK_DISTANCE *
                                 USR_PRIEST_SIEGE_PICK_DISTANCE;
            if (siegeDis2 > pickDis2)
            {
                // 厂还远：沿"祭司相对厂"的方位走到选取距离的边上。
                // 复用祭司驻留点那套取点，走的是它自己那条射线，不会横穿敌方基地。
                int goalDR = -1;
                int goalUR = -1;
                const bool haveGoal = PriestFrontPostBlock(
                    siegeTarget->BlockDR, siegeTarget->BlockUR,
                    priest->BlockDR, priest->BlockUR,
                    USR_PRIEST_SIEGE_PICK_DISTANCE, goalDR, goalUR);
                const int reissue = haveGoal
                                        ? (ShouldReissuePriestMove(
                                               priest, goalDR, goalUR)
                                               ? 1
                                               : 0)
                                        : -1;
                {
                    static int lastGoalLogFrame = USR_INVALID_FRAME;
                    if (lastGoalLogFrame == USR_INVALID_FRAME ||
                        g_frame - lastGoalLogFrame >= 500)
                    {
                        lastGoalLogFrame = g_frame;
                        char buf[256];
                        snprintf(buf, sizeof(buf),
                                 "[SIEGE] f=%d goal=(%d,%d) reissue=%d dis2=%d "
                                 "siege=(%d,%d) priest=(%d,%d)",
                                 g_frame, goalDR, goalUR, reissue, siegeDis2,
                                 siegeTarget->BlockDR, siegeTarget->BlockUR,
                                 priest->BlockDR, priest->BlockUR);
                        AiDebugLog(buf);
                    }
                }
                if (haveGoal && reissue == 1)
                {
                    priestMoveOrderId = ai->HumanMove(
                        priest->SN, (goalDR + 0.5) * double(BLOCKSIDELENGTH),
                        (goalUR + 0.5) * double(BLOCKSIDELENGTH));
                    priestEmergencyTarget = make_pair(goalDR, goalUR);
                    priestEmergencyTargetFrame = g_frame;
                    priestMoveFromDR = priest->BlockDR;
                    priestMoveFromUR = priest->BlockUR;
                    lastPriestOrderFrame = g_frame;
                }
                return;
            }
        }
        else
        {
            // 【攻城厂还没被侦察到 → 朝估算的基地方位逐步探过去】
            // 这一步是为了打破「看不到就不走过去、不走过去就永远看不到」的死锁，
            // 完整理由见 USR_PRIEST_SIEGE_PROBE_STEP 上方的说明。
            int anchorDR = 0;
            int anchorUR = 0;
            if (EstimateEnemySiegeAnchor(anchorDR, anchorUR))
            {
                const int curDis = BlockDis(priest->BlockDR, priest->BlockUR,
                                            anchorDR, anchorUR);
                // 每轮朝锚点推进一个步长，但不越过最近距离 —— 再往里就是敌方
                // 基地核心了，而厂通常就在锚点附近（估算误差最差 26 格曼哈顿）。
                const int probeDis =
                    max(USR_PRIEST_SIEGE_PROBE_MIN,
                        curDis - USR_PRIEST_SIEGE_PROBE_STEP);
                if (curDis > probeDis)
                {
                    int goalDR = -1;
                    int goalUR = -1;
                    // 复用祭司驻留点那套取点：沿"祭司相对锚点"的方位取 probeDis
                    // 格处的可站格，所以走的是它自己那条射线，不会横穿基地。
                    if (PriestFrontPostBlock(anchorDR, anchorUR, priest->BlockDR,
                                             priest->BlockUR, probeDis, goalDR,
                                             goalUR) &&
                        ShouldReissuePriestMove(priest, goalDR, goalUR))
                    {
                        priestMoveOrderId = ai->HumanMove(
                            priest->SN,
                            (goalDR + 0.5) * double(BLOCKSIDELENGTH),
                            (goalUR + 0.5) * double(BLOCKSIDELENGTH));
                        priestEmergencyTarget = make_pair(goalDR, goalUR);
                        priestEmergencyTargetFrame = g_frame;
                        priestMoveFromDR = priest->BlockDR;
                        priestMoveFromUR = priest->BlockUR;
                        lastPriestOrderFrame = g_frame;
                    }
                    return;
                }
            }
        }
    }

    // 【跟随军队出征：在敌方基地外 USR_PRIEST_FRONT_POST_DISTANCE(40) 格驻留】
    //
    // 做成【双向带】而不是单向的"推出去"：
    //     距离 < 40 − BAND(6) → 朝外推（别闯进敌方基地）
    //     距离 > 40 + BAND     → 朝里拉（别缩在家里，要跟队）
    //     带内                 → 不动
    // 只做单向的话两头都够不着：原 keepout 只保证"不进去"，所以祭司会一直待在
    // 家里；只做"拉回来"又会一路冲进敌方基地。
    //
    // 【为什么放在这里】必须在「走向攻城厂」之后 —— 那一段才是获胜路径的最后
    // 一段（无敌兵时走到厂边），站桩带不能把它拦在 40 格外。也必须在下面的
    // keepout 与 priestPassive「守家」之前 —— 否则 26000 帧之后那条会让祭司
    // 什么都不做，跟队规则永远执行不到。
    // 同时它排在 keepout 之前，也就避免了两条规则方向相反时的来回跑
    // （keepout 是"朝我方中心退"，而中心在 100+ 格外的另一头）。
    //
    // 【只在军队出征时生效】判据是「已侦察到敌方基地 + 过了
    // USR_PRIEST_FRONT_POST_FRAME(35000)」。否则开局就把祭司往敌方基地拉，
    // 等于让它一个人横穿半张地图。
    //
    // 【这一帧为什么与农民护送同帧、而不是沿用军队总攻的 28000】需求是
    // 「农民和祭司一起上战场」：两者同帧动身才能同时到位（移速相近、路程相同）。
    // 祭司单独提前 5000 帧出发，就会在农民还在赶路的那段时间里独自站在
    // 敌方基地外的环上 —— 没有掩护，而它只有 100 血、防御为 0。
    if (enemyBaseDiscovered && g_frame >= USR_PRIEST_FRONT_POST_FRAME)
    {
        int anchorDR = 0;
        int anchorUR = 0;
        if (EstimateEnemySiegeAnchor(anchorDR, anchorUR))
        {
            const int dis2 = BlockDis2(priest->BlockDR, priest->BlockUR,
                                       anchorDR, anchorUR);
            const int nearRadius = USR_PRIEST_FRONT_POST_DISTANCE -
                                   USR_PRIEST_FRONT_POST_BAND;
            const int farRadius = USR_PRIEST_FRONT_POST_DISTANCE +
                                  USR_PRIEST_FRONT_POST_BAND;
            // 【朝外推必须避开「正在冲厂」的情形】
            //
            // 转换分支会让祭司往厂里走（厂比驻留带更近），而这条「太近就推出去」
            // 会把它拽回来 —— 两个分支每帧打架。实测表现：祭司卡在 34 格线上
            // 来回振荡，到厂的关系被反复打断（[SIEGE] 里 wo 在厂 SN 与 -1 之间跳，
            // [CONV] 每 40 帧重开一次），而它始终没能真正靠近厂。
            //
            // 所以只要 CanPriestApproachSiege()（四道门，见它的说明）+ 厂已被
            // 侦察到，就不再朝外推 —— 那正是允许它进厂的判据，这时候把它推出去
            // 等于自己掐断获胜路径。朝外推原本的作用只是「别闯进敌方基地送死」，
            // 而「会不会送死」已经由那些门把关了。
            const bool mayApproachSiege =
                CanPriestApproachSiege() &&
                FindEnemySiegeBuilding() != nullptr;
            if ((dis2 < nearRadius * nearRadius && !mayApproachSiege) ||
                dis2 > farRadius * farRadius)
            {
                int postDR = -1;
                int postUR = -1;
                if (PriestFrontPostBlock(anchorDR, anchorUR, priest->BlockDR,
                                         priest->BlockUR,
                                         USR_PRIEST_FRONT_POST_DISTANCE, postDR,
                                         postUR) &&
                    ShouldReissuePriestMove(priest, postDR, postUR))
                {
                    priestMoveOrderId = ai->HumanMove(
                        priest->SN, (postDR + 0.5) * double(BLOCKSIDELENGTH),
                        (postUR + 0.5) * double(BLOCKSIDELENGTH));
                    priestEmergencyTarget = make_pair(postDR, postUR);
                    priestEmergencyTargetFrame = g_frame;
                    priestMoveFromDR = priest->BlockDR;
                    priestMoveFromUR = priest->BlockUR;
                    lastPriestOrderFrame = g_frame;
                }
                return;
            }
        }
    }

    // 【不得进入敌方基地 USR_PRIEST_ENEMY_BASE_KEEPOUT 格以内】
    // —— 取代原来的「活动缰绳：离市镇中心超过 50 格就拉回来」。
    //
    // 参照点从【我方市镇中心】换成【敌方基地】：祭司在别处可以自由活动，
    // 唯一的硬边界是不许靠近敌方基地。豁免条件仍是 WorkObjectSN != -1 ——
    // 那表示它已经建立了对象关系（正在转换、或正赶往转换目标），属于获胜路径。
    // 注意用 WorkObjectSN 而不是 NowState：纯移动不会产生对象关系
    // （实测祭司带着移动目标时 wo 一直是 -1），所以这个判据只放过「有活干」的情况。
    //
    // 【为什么还要挂在 HasVisibleEnemyArmy() 上】转换攻城厂要求走到厂 10 格内，
    // 与 32 格红线直接矛盾。所以只在【有可见敌人】时才拦：敌人还在时远离敌方基地，
    // 敌人清空了才放它去转换。（真正去转换那条路在本函数开头，会先 return。）
    {
        int awayDR = 0;
        int awayUR = 0;
        if (HasVisibleEnemyArmy() && priest->WorkObjectSN == -1 &&
            PriestInEnemyBaseKeepout(*priest, awayDR, awayUR))
        {
            if (ShouldReissuePriestMove(priest, awayDR, awayUR))
            {
                priestMoveOrderId = ai->HumanMove(
                    priest->SN, (awayDR + 0.5) * double(BLOCKSIDELENGTH),
                    (awayUR + 0.5) * double(BLOCKSIDELENGTH));
                priestEmergencyTarget = make_pair(awayDR, awayUR);
                priestEmergencyTargetFrame = g_frame;
                priestMoveFromDR = priest->BlockDR;
                priestMoveFromUR = priest->BlockUR;
                lastPriestOrderFrame = g_frame;
            }
            return;
        }
    }

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
    //
    // 【为什么"正在转换"要单独判】原来这里只有一个「有没有关系」的判据（与目标
    // 隔 109 格也算在转换），于是「转换中不撤退」那条取舍被永久触发 —— 祭司在基地
    // 原地站着被 5 个贴上来的敌人从 98 血打到 0，全程一次撤退都没有。
    //
    // conversionBuildingAlive 只用于日志的 convB= 字段（区分"转建筑"与"转士兵"），
    // 不参与任何分支判断 —— 实测最近十局，7 局死亡里有 4 局在第一次大掉血时
    // convAlive=1，分清是哪种转换对回看很有用。
    bool conversionTargetAlive = false;
    bool conversionBuildingAlive = false;
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
                conversionBuildingAlive = true;
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
                     "minDis=%d convAlive=%d convB=%d",
                     g_frame, priest->Blood, priest->MaxBlood, priest->BlockDR,
                     priest->BlockUR, near10, targeting, minDis,
                     (int)conversionTargetAlive,
                     (int)conversionBuildingAlive);
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
    if (nearestEnemy != nullptr &&
        (nearestEnemy->WorkObjectSN == priest->SN ||
         nearestDis2 <=
             USR_PRIEST_DANGER_RADIUS * USR_PRIEST_DANGER_RADIUS)) {
        closeThreat = nearestEnemy;
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
                     "tSort=%d tRng=%d tRngE2=%d tManh=%d tE2=%d conv=%d convB=%d "
                     "mvF=%d mvTgt=(%d,%d) mvRet=%d mvRetF=%d wo=%d ns=%d",
                     g_frame, priest->Blood, closeThreat != nullptr,
                     targetingCount, tSort, tRng,
                     tRng * tRng, tManh, tE2, (int)conversionTargetAlive,
                     (int)conversionBuildingAlive,
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

    // 【转换不被打断 —— 按需求：不要让转换被任何事打断】
    //
    // 只要正在读条转换（conversionTargetAlive），这里就 return —— 撤退、撤离窗口、
    // 禁区推出、治疗、探路、回家，全都执行不到。转换一旦开始只有两种结束方式：
    // 目标死亡，或祭司自己死。
    //
    // 【为什么】转换是唯一的获胜路径。历史上那句注释留着理由：
    // 「曾有『遭到围攻或血量过低时放弃转换先保命』的中断判据，已按要求移除：
    //   它会让祭司在被打断后反复重来，转换永远完不成。」
    // 中途放手撤了就等于把这一次读条作废，重来又要重新走近、重新起手。
    //
    // 【代价，必须知道】祭司会在读条期间被围攻致死。实测最近十局：7 局死亡里有 4 局
    // 在第一次大掉血时 convAlive=1（正在读条），而祭司不能自愈、不可补充。
    // 这是「能不能赢」压过「能不能活」的取舍，按需求选前者。
    //
    // conversionBuildingAlive 现在只用于日志的 convB= 字段（区分"转建筑"与
    // "转士兵"），便于回看某次死亡的具体情形 —— 不参与任何分支判断。
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

    // 【总攻阶段（priestPassive = f >= USR_PRIEST_PASSIVE_FRAME，即 26000）
    //   之后，祭司守家，不追出去转换】
    //
    // 【为什么必须放在转换之前】原先「回基地待机」那段逻辑裹在下面的
    // `if (!enemyVisible)` 里面，而转换判断排在它【前面】—— 所以只要视野里出现
    // 敌人，祭司就会去追转换目标，回基地那段根本执行不到。实测它就是这样跑到
    // 离基地很远的地方、被 4 个敌人贴到 1 格围死的。
    //
    // 判据从「超出市镇中心 12 格就回家」改成「进入敌方基地 32 格以内就退开」
    // （即 USR_PRIEST_ENEMY_BASE_KEEPOUT）。原来那两条以我方中心为参照的守家/
    // 缰绳规则都已删除，理由见文件上方常量的说明。
    //
    // 与上面开头那段是一对，两边合起来才是完整的规则：
    //     没有敌人 → 去转换敌方攻城武器厂（唯一的获胜路径）
    //     有敌人   → 不许靠近敌方基地，不转换、不治疗、不追
    //
    // 【为什么「没进禁区」也要 return】原实现在圈内时会落到下面的转换逻辑 ——
    // 于是祭司会去追一个转换目标、跑出圈外，又被这段拉回来，形成
    // HumanAction ↔ HumanMove 的来回拉扯（游戏日志里表现为「设置工作目标为 X」
    // 和「移动至同一坐标」交替刷屏）。有敌人时直接停手，就不存在这个循环。
    if (priestPassive)
    {
        int awayDR = 0;
        int awayUR = 0;
        if (PriestInEnemyBaseKeepout(*priest, awayDR, awayUR) &&
            ShouldReissuePriestMove(priest, awayDR, awayUR))
        {
            priestMoveOrderId = ai->HumanMove(
                priest->SN, (awayDR + 0.5) * double(BLOCKSIDELENGTH),
                (awayUR + 0.5) * double(BLOCKSIDELENGTH));
            priestEmergencyTarget = make_pair(awayDR, awayUR);
            priestEmergencyTargetFrame = g_frame;
            priestMoveFromDR = priest->BlockDR;
            priestMoveFromUR = priest->BlockUR;
            lastPriestOrderFrame = g_frame;
        }
        // 有敌人 → 到此为止：不转换、不撤退、不治疗。
        // 没有敌人 → 落到下面（转换攻城厂那段已经在上面前面处理过了，
        // 这里放行是给「还没侦察到攻城厂」的情况留余地）。
        if (HasVisibleEnemyArmy())
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
        // 【转士兵仍然要求"视野里有敌兵"；转建筑不要求 —— 这是本次的关键改动】
        //
        // 原先整块都裹在 `if (anyEnemyVisible)` 里，于是和上面那段「走向攻城厂」
        // 形成死锁：
        //     走向攻城厂  要求 !HasVisibleEnemyArmy()（没敌人才出门）
        //     转换攻城厂  要求  anyEnemyVisible （有敌人才转换）
        // 两个判据互为反条件 —— 而敌方守军被打光时视野里【永远不会】再有敌兵，
        // 于是祭司走到了厂边上却永远不发起转换。实测日志：它停在 (15,77)、
        // mvTgt=(11,86)（那是厂的位置）、minDis=9999，直到被别的敌人打死。
        // 获胜路径要求"把敌人打光之后去把厂转掉"，所以这一条必须放开。
        // 【26000 帧后不再转换敌方士兵】总攻阶段（USR_PRIEST_PASSIVE_FRAME）起，
        // 祭司只做一件事：把敌方攻城武器厂转掉 —— 那是唯一的获胜条件。
        // 转换士兵会把它按在敌方基地里读条换血，而它只有 100 血、近战与远程
        // 防御都是 0、且不可补充；换来的一个士兵对胜负没有贡献。
        //
        // 【为什么用 USR_PRIEST_PASSIVE_FRAME 而不另立常量】那个常量本来就是
        // 「总攻阶段起始帧」的定义，而它自己的注释里写的就是「此后祭司进入只
        // 保存实力状态 —— 不再转换」。此前转士兵的分支在这条门槛之后仍然生效，
        // 注释与代码并不一致，这一条把行为对齐到注释。
        // 【目标锁定：一旦选定就转到完成 / 死亡 / 离开视野】
        //
        // 原先这里是每次调用都重选一遍（FindPriestConversionTarget 按兵种优先级），
        // 于是目标一变就重新 HumanAction —— 而每次重发都中止并重建关系、转换进度
        // 清零。实测最新一局到 f=25274 为止新开了 15 次转换指令、换了 10 个目标，
        // 其中有两次间隔只有 6~18 帧，一个都没转完。
        //
        // 现在改成：先看锁定的目标还在不在（FindLockedConversionTarget 的判据是
        // 「仍在 info.enemy_armies 里且活着」—— 死亡、走出视野、被转成我方的
        // 三种情况都会让它从列表里消失，自动落到重选分支）；不在才按优先级重选
        // 并改写锁定。
        const tagArmy *armyTarget = nullptr;
        if (anyEnemyVisible && g_frame < USR_PRIEST_PASSIVE_FRAME)
        {
            armyTarget = FindLockedConversionTarget();
            if (armyTarget == nullptr)
            {
                const int prevLock = priestConversionLockSN;
                armyTarget = FindPriestConversionTarget(*priest);
                priestConversionLockSN =
                    armyTarget != nullptr ? armyTarget->SN : -1;
                // 锁定发生变更（上一个目标死了 / 走出视野 / 被转成我方的）才记一行。
                // 加锁之后【这行的出现频率就是「祭司一共换了几次转换目标」】——
                // 加锁前那一局到 f=25274 换了 10 个目标，正是它要消掉的东西。
                if (armyTarget != nullptr && armyTarget->SN != prevLock)
                {
                    char lockbuf[160];
                    snprintf(lockbuf, sizeof(lockbuf),
                             "[CONVLOCK] f=%d lock %d -> %d sort=%d", g_frame,
                             prevLock, armyTarget->SN, armyTarget->Sort);
                    AiDebugLog(lockbuf);
                }
            }
        }
        else
        {
            // 视野里没有敌兵、或已进入总攻阶段（不再转士兵）→ 解除锁定。
            priestConversionLockSN = -1;
        }
        // 【转换分支同样要过进厂门控】原先这里只判「过了 26000 帧」，
        // 于是它可以绕过冲厂分支的那套门控直接对厂建立关系、把祭司带进塔的
        // 火力网 —— 实测致命一局就是这样：祭司在 convB=1（正在转换建筑）的
        // 状态下被三座 15/15/34 血的箭塔从 100 打到 13。
        const tagBuilding *buildingTarget = nullptr;
        if (!armyTarget && CanPriestApproachSiege())
            buildingTarget = FindEnemySiege(*priest);
        {
            const int targetSN = armyTarget
                                     ? armyTarget->SN
                                     : (buildingTarget ? buildingTarget->SN : -1);
            // 【诊断：为什么没发起转换】只在「有目标却没走到下发那一步」时记录。
            //
            // 【为什么必须加这一行】实测有过这样一局：祭司站在敌方锚点旁
            // （(14,7)，离锚点曼哈顿 6 格）、满血 100、视野里一个敌兵都没有，
            // 却整整 1500 帧没有发起过一次转换，[CONV] 一条都没有。
            // 日志里能看到的只有「成功发起转换」的那几次，看不见「为什么没发起」，
            // 于是只能靠猜（是没目标？冷却？还是有在途指令？）。
            // 这一行把那几扇门的值一次摊开，直接定位。
            if (targetSN == -1 || priest->ConvertCooldown > 0 ||
                priestMoveOrderId != -1 || priest->WorkObjectSN == targetSN ||
                closeThreat != nullptr)
            {
                static int lastConvBlockLogFrame = USR_INVALID_FRAME;
                if (lastConvBlockLogFrame == USR_INVALID_FRAME ||
                    g_frame - lastConvBlockLogFrame >= 500)
                {
                    lastConvBlockLogFrame = g_frame;
                    // siegeSeen 是【已侦察到的】攻城厂数量。它为 0 而祭司又停在
                    // 敌方基地旁边，就说明 FindEnemySiegeBuilding 拿不到厂
                    // （敌方基地侦察到了、厂本身没有），祭司永远不会有转换目标。
                    int siegeSeen = 0;
                    for (const tagBuilding &b : info.enemy_buildings)
                    {
                        if (b.Blood > 0 && b.Type == BUILDING_SIEGE)
                            ++siegeSeen;
                    }
                    char buf[288];
                    snprintf(buf, sizeof(buf),
                             "[CONVBLOCK] f=%d target=%d army=%d build=%d cd=%d "
                             "mvId=%d wo=%d threat=%d siegeSeen=%d enemyB=%d "
                             "priest=(%d,%d)",
                             g_frame, targetSN, armyTarget ? 1 : 0,
                             buildingTarget ? 1 : 0, priest->ConvertCooldown,
                             priestMoveOrderId, priest->WorkObjectSN,
                             closeThreat ? closeThreat->SN : -1, siegeSeen,
                             (int)info.enemy_buildings.size(), priest->BlockDR,
                             priest->BlockUR);
                    AiDebugLog(buf);
                }
            }
            // 同一目标已有关系时不重复下达 HumanAction；重复指令会中止原关系。
            //
            // 【正在被威胁时不【新开】转换】上面那道「转换不被打断」保护的是
            // 【已经开始读条】的转换；这里挡的是【还没开始】的 —— 否则一个残血祭司
            // 只要视野里有敌人就会不断起手，而起手之后就不能跑了。
            // 实测某局 f=14001：convAlive=1、lock=5、minDis=7，100 帧掉 71 血
            // （98→27），4000 帧后的波 3 把它补掉 —— 那一次读条就是"在被打的时候
            // 起的头"。已经读着的转换不受这一条影响（它由上面那道 return 保护）。
            if (targetSN != -1 && priest->ConvertCooldown <= 0 &&
                priestMoveOrderId == -1 && priest->WorkObjectSN != targetSN &&
                closeThreat == nullptr) {
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
        bool wasStuck = false;
        if (ShouldReissuePriestMove(priest, target.first, target.second,
                                    &wasStuck)) {
          // 【卡住 = 这个落点走不到 → 记进黑名单】不记的话目标不变、
          // GetPriestEmergencyPoint 每帧算出同一个点，于是每 150 帧重发一条同样的
          // 指令（每次都清空路径）—— 祭司原地站着不动。记下之后下一次选点会绕开它。
          if (wasStuck) {
            // 顺手清掉过期的条目，避免这张表随对局无限增长。
            for (map<pair<int, int>, int>::iterator it =
                     priestBadRetreatPoints.begin();
                 it != priestBadRetreatPoints.end();)
            {
              if (g_frame >= it->second)
                it = priestBadRetreatPoints.erase(it);
              else
                ++it;
            }
            priestBadRetreatPoints[target] =
                g_frame + USR_PRIEST_BAD_POINT_FRAMES;
          }
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
        // 开局探路：分层推进，把市镇中心 USR_PRIEST_EXPLORE_RADIUS(120) 格
        // 以内的区域探开为止 —— 半径涵盖整张图，目的就是找到敌方基地
        // （见该常量的说明）。走法与终止条件见 CapturePriestLap 与那里的说明。
        //
        // 放在「无敌人」分支里：有敌人时仍走威胁/转换逻辑，不会为了探路挨打。
        //
        // 【已取消】原先这里还有一个「视野里出现瞪羚就提前收工」的出口
        // （HasVisibleGazelle + FinishPriestExplore("gazelle")）。按需求去掉：
        // 探路的目的不再只是找食物，而是把外围探开 —— 瞪羚出现得早，
        // 按那个出口走的话祭司会刚出门就收工，外围根本没探。
        // 【时间截止】到帧就收工，与「半径内探干净」并列，谁先满足用谁。
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

        // 【探路收工 → 回家】
        //
        // 放在待机段最前面：这一段会 return，于是「回家」期间不会被后面的治疗抢走
        // 目标。而真正的威胁处理（撤退、转换）都排在本函数更前面，会先 return ——
        // 也就是说回家途中一旦遇敌，撤退照常接管；威胁过去后这里接着走回家。
        //
        // 【为什么由这里下发，而不是 FinishPriestExplore 里发一次】
        // 那里发的话，同一帧后面还会走到 TryPriestHeal —— 它一旦找到 12 格内的
        // 伤员就会 HumanAction 覆盖掉那条移动，而之后没有任何机制重发。
        if (priestGoingHome)
        {
            const tagBuilding *home = FindCenter();
            if (home == nullptr ||
                BlockDis(priest->BlockDR, priest->BlockUR, home->BlockDR,
                         home->BlockUR) <= 2)
            {
                priestGoingHome = false;  // 到家了（或找不到中心）→ 结束
            }
            else
            {
                if (ShouldReissuePriestMove(priest, home->BlockDR, home->BlockUR))
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
                return;  // 回家途中不做别的（治疗）
            }
        }

        // 【无敌人时的待机位置：不再是「回基地」，而是「别待在敌方基地附近」】
        //
        // 原来这里是「守家」：没有敌人时留在市镇中心 12 格内待机（理由是祭司
        // 只有 100 血、防御 0、不能自愈，待在箭塔圈里最安全）。按需求改成以
        // 敌方基地为参照的红线 —— 祭司在别处可以自由活动，只要不进
        // USR_PRIEST_ENEMY_BASE_KEEPOUT 圈就行。
        if (priestPassive || g_frame < USR_PRIEST_HOLD_HOME_UNTIL_FRAME)
        {
            int awayDR = 0;
            int awayUR = 0;
            if (PriestInEnemyBaseKeepout(*priest, awayDR, awayUR))
            {
                // 节流重发：每 USR_PRIEST_ORDER_INTERVAL 帧最多一次，
                // 否则每帧重发会不断 suspendRelation + 重新寻路，反而走不动。
                if (ShouldReissuePriestMove(priest, awayDR, awayUR))
                {
                    priestMoveOrderId = ai->HumanMove(
                        priest->SN, (awayDR + 0.5) * double(BLOCKSIDELENGTH),
                        (awayUR + 0.5) * double(BLOCKSIDELENGTH));
                    priestEmergencyTarget = make_pair(awayDR, awayUR);
                    priestEmergencyTargetFrame = g_frame;
                    priestMoveFromDR = priest->BlockDR;
                    priestMoveFromUR = priest->BlockUR;
                    lastPriestOrderFrame = g_frame;
                }
                return;
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
    // 而且原先挂在上面的 `g_frame >= 26000` 与 priestPassive 的门槛相同，
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

// 这个前沿点是不是在「朝敌方锚点的那一侧」。
//
// 【为什么用中垂线判据而不是角度】「对角线方向」本质上就是「离敌方基地比离
// 我方基地更近的那半张图」，而以中心↔锚点的中垂线来切，恰恰等价于「与
// 中心→锚点方向的夹角在 ±90° 内」。好处是全程只有平方距离比较，不必做三角函数，
// 也就没有整数溢出的风险（坐标最大 128，点积的平方项会顶到 int 上限）。
static bool IsTowardEnemyAnchor(int blockDR, int blockUR, int anchorDR,
                                int anchorUR, int centerDR, int centerUR)
{
    if (centerDR < 0)
        return true;   // 拿不到我方中心就不设限
    return BlockDis2(blockDR, blockUR, anchorDR, anchorUR) <
           BlockDis2(blockDR, blockUR, centerDR, centerUR);
}

// 【优先侦察对角线】
//
// 需求是「侦察骑兵优先侦察对角线」，而且是【硬性优先】：不把通往敌方基地的
// 那一侧探完，就不许去探别的方向。
//
// 四张图的敌方基地都在我方市镇中心的对角线上（实测偏移见 EstimateEnemySiegeAnchor
// 的注释：-66,+64 / +73,-65 / -57,-83 / +64,-58），所以朝那个方向探最有可能最早
// 发现敌方基地与攻城厂。
//
// 【为什么这件事是前置】攻城厂必须【已经进 info.enemy_buildings】，祭司才能转换
// 它 —— 因为转换指令要的是它的 SN，而 SN 只能从那份已侦察列表里拿。侦察兵不去，
// 厂就永远不出现在列表里，祭司整条获胜路径都无从执行（实测有一局祭司站在敌方
// 锚点旁满血待命 1500 帧、一次转换都没发起过）。
//
// 【为什么是两遍扫描而不是给方向加权】先前试过把「到锚点的距离」按约十分之一的
// 权重并进评分，效果是侦察兵仍然会在别的方向花掉大量时间 —— 加权只改变同分时的
// 偏好，压不过「就近」这一项。要做成「探完对角线才探别的」，就必须把它变成
// 筛选条件，而不是评分项：第一遍只在中垂线靠敌那侧找，一个都没有（那一侧真探完了）
// 才放开全部。
//
// 【锚点与中心为什么在循环外各算一次】EstimateEnemySiegeAnchor 内部要遍历敌方
// 建筑列表、FindCenter 也要遍历，放进 32×32 的双重循环里会白跑上千次。
static bool FindBestScoutFrontier(const tagArmy &scout, int &targetDR,
                                  int &targetUR)
{
    int anchorDR = 0;
    int anchorUR = 0;
    const bool haveAnchor = EstimateEnemySiegeAnchor(anchorDR, anchorUR);
    const tagBuilding *center = FindCenter();
    const int centerDR = center != nullptr ? center->BlockDR : -1;
    const int centerUR = center != nullptr ? center->BlockUR : -1;

    for (int pass = 0; pass < 2; ++pass)
    {
        // pass 0：只在中垂线靠敌那侧找。没有锚点就跳过这一遍（无从判断方向）。
        const bool enemySideOnly = (pass == 0 && haveAnchor);

        int bestScore = -2000000000;
        bool found = false;
        for (int dr = 2; dr < MAP_L - 2; ++dr)
        {
            for (int ur = 2; ur < MAP_U - 2; ++ur)
            {
                if (!IsScoutFrontierUsable(dr, ur))
                    continue;
                if (enemySideOnly &&
                    !IsTowardEnemyAnchor(dr, ur, anchorDR, anchorUR, centerDR,
                                         centerUR))
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
                // 同一遍之内仍然是「就近」（-dis2*10）为主，再按到锚点的远近
                // 做次序微调 —— 于是靠敌那一侧是从近到远一路推过去的。
                const int toAnchor =
                    haveAnchor ? BlockDis2(dr, ur, anchorDR, anchorUR) : 0;
                const int score = -distance * 10 - toAnchor - recentPenalty -
                                  occupiedPenalty;
                if (!found || score > bestScore)
                {
                    bestScore = score;
                    targetDR = dr;
                    targetUR = ur;
                    found = true;
                }
            }
        }
        if (found)
            return true;
        // pass 0 一个都没找到 → 靠敌那侧已经探完，落进 pass 1 放开全部。
    }
    return false;
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
// 保底巡逻点是否可用。巡逻点是一张写死的坐标表，可能落在海面、障碍或建筑上。
//
// 【为什么必须有这一条】落点不可达时 HumanMove 会被引擎按「目标格四正交邻居
// 全是障碍 → nullPath」取消（Core_List.cpp:2091），而巡逻的重发间隔是 60 帧 ——
// 表现为反复重试、侦察骑兵原地不动。
//
// 【为什么不复用 IsScoutFrontierUsable】它头一条是 IsExplorationFrontierBlock
// （已探明且邻接未探明），那是"前沿点"的定义；巡逻点是允许落在已探明区域里的，
// 套上去会把八个点全部否掉。这里只取它后半段真正通用的几条判据。
static bool IsScoutPatrolPointUsable(int blockDR, int blockUR)
{
    if (blockDR < 2 || blockUR < 2 || blockDR >= MAP_L - 2 ||
        blockUR >= MAP_U - 2)
        return false;
    // 四邻全被挡 → 坐标移动会被引擎取消。IsReachableAround 内部有按帧缓存。
    if (!IsReachableAround(blockDR, blockUR, 1))
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
        if (resource.Blood > 0 && abs(blockDR - resource.BlockDR) <= 1 &&
            abs(blockUR - resource.BlockUR) <= 1)
            return false;
    }
    return true;
}

static void DispatchScouts(UsrAI *ai)
{
    const int scoutOrderInterval = 60;
    const int scoutEmergencyOrderInterval = 20;
    const int scoutSafeRadius = 6;
    // 「勾引」只对这么近的敌人生效（格）。见下面 !isRecon 那一支的说明：
    // 原判据是"视野里有敌人"，而波次期间视野里几乎一直有敌人 —— 那会把保命型
    // 侦察兵永久钉在基地里，探索逻辑整段执行不到。
    const int scoutLureRadius = 20;
    const int scoutWaypointCount = 8;
    const int scoutMargin = 10;
    // 保底巡逻点。表本身是固定的八个坐标，"先用哪个"见下面的 wpOrder。
    //
    // 【原先注释写的是"避开左下角（敌方基地方向）"】—— 那是按未旋转的 map.njust
    // 写死的。四张图的敌方朝向并不相同（实测我方市镇中心 (77,22)/(20,76)/(81,89)/
    // (18,78)，敌方在对角），那张表只对 map.njust 成立，另外三张图反而会把侦察兵
    // 先送去敌方那一侧。现在改成按【实测的敌方方向】排序，见 wpOrder。
    static const int scoutWaypoints[][2] = {
        {MAP_L - scoutMargin - 1, scoutMargin},             // 右上角
        {MAP_L / 2, scoutMargin},                           // 上边中点
        {MAP_L / 2, MAP_U / 3},                             // 中上
        {MAP_L - scoutMargin - 1, MAP_U / 2},               // 右边中点
        {MAP_L / 2, MAP_U / 2},                             // 地图中心
        {MAP_L - scoutMargin - 1, MAP_U - scoutMargin - 1}, // 右下角
        {MAP_L / 2, MAP_U - scoutMargin - 1},               // 下边中点
        {MAP_L * 3 / 4, MAP_U / 3}};                        // 右上偏中

    // 【巡逻点的访问顺序：按到敌方基地锚点的距离【降序】】离敌方越远越先走，
    // 最后才轮到敌方那一侧 —— 这就是"避开敌方"的正确做法，且对四张图都成立。
    // 锚点走 EstimateEnemySiegeAnchor（侦察到用真坐标，否则用我方中心的地图对
    // 极点估算）；拿不到锚点时保持原下标顺序。
    // 插入排序，平局保持原下标顺序（严格小于才前移）—— 结果只取决于输入。
    int wpOrder[scoutWaypointCount];
    for (int i = 0; i < scoutWaypointCount; ++i)
        wpOrder[i] = i;
    {
        int anchorDR = 0;
        int anchorUR = 0;
        if (EstimateEnemySiegeAnchor(anchorDR, anchorUR))
        {
            int dis2[scoutWaypointCount];
            for (int i = 0; i < scoutWaypointCount; ++i)
                dis2[i] = BlockDis2(scoutWaypoints[i][0], scoutWaypoints[i][1],
                                    anchorDR, anchorUR);
            for (int i = 1; i < scoutWaypointCount; ++i)
            {
                const int key = wpOrder[i];
                int j = i - 1;
                while (j >= 0 && dis2[wpOrder[j]] < dis2[key])
                {
                    wpOrder[j + 1] = wpOrder[j];
                    --j;
                }
                wpOrder[j + 1] = key;
            }
        }
    }

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
        // 而 USR_SCOUT_RECON_FRAME(28000) 在那之后 —— 过了这个点，波次防御已经
        // 用不上它了，让它转去侦测敌方基地收益更大。
        //
        // 【按需求取消保命型】现在所有侦察兵一视同仁按侦测型行事，见
        // USR_SCOUT_KEEP_ALIVE_TYPE_ENABLED 的说明。用开关而不是直接写 true：
        // 那两段保命逻辑（勾引 + 双档撤离）仍保持可达，不会产生
        // -Wunused-function 警告，将来想恢复只改这一个常量。
        const bool isRecon =
            !USR_SCOUT_KEEP_ALIVE_TYPE_ENABLED ||
            (g_frame >= USR_SCOUT_RECON_FRAME);

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

            // 挑【scoutLureRadius 格以内】最近的敌人作为勾引对象。
            //
            // 【为什么必须有这个距离上限】原判据只是"视野里有敌人"，没有距离限制。
            // 而波次期间视野里几乎一直有敌人（基地守军、路上的兵都算），于是它每帧
            // 都进这一支、每次都被命令"回市镇中心" —— 而探索逻辑排在这一支后面、
            // 整段执行不到。实测表现就是侦察骑兵钉在中心不动。
            // 加上限之后：只有近处有敌人才勾引（那才是"把追兵引进火力圈"要管的
            // 事），远处有敌人不影响它继续探路。
            int lureSN = -1;
            int lureDis2 = scoutLureRadius * scoutLureRadius;
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
                //
                // 【已经在中心附近就不重发】HumanMove 到自己的坐标不会产生任何移动，
                // 只会每 scoutEmergencyOrderInterval(20) 帧把路径清一次 —— 也就是
                // "站着不动"的直接来源。已经在中心就只是待在中心（勾引就是要它待
                // 在那儿等追兵）。
                if (BlockDis(scout.BlockDR, scout.BlockUR, home->BlockDR,
                             home->BlockUR) > 3)
                {
                    const pair<int, int> homeBlock =
                        make_pair(home->BlockDR, home->BlockUR);
                    const map<int, pair<int, int>>::const_iterator targetIt =
                        scoutEmergencyTarget.find(scout.SN);
                    if (targetIt == scoutEmergencyTarget.end() ||
                        targetIt->second != homeBlock ||
                        g_frame - scoutDangerLastFrame[scout.SN] >=
                            scoutEmergencyOrderInterval) {
                        scoutEmergencyOrderId[scout.SN] = ai->HumanMove(
                            scout.SN,
                            (home->BlockDR + 0.5) * double(BLOCKSIDELENGTH),
                            (home->BlockUR + 0.5) * double(BLOCKSIDELENGTH));
                        scoutEmergencyTarget[scout.SN] = homeBlock;
                        scoutDangerLastFrame[scout.SN] = g_frame;
                    }
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
            //
            // 【逐个挑"没到过且可用"的点】原写法是「到了就 +1，然后无条件取下一个」
            // —— 下一个可能同样到过了、或落在海面/障碍里（那种落点会被引擎按
            // nullPath 取消指令，而这里 60 帧才重发一次），于是它在原地反复重发。
            // 现在把这类点直接跳过；整圈都不行时本帧不发指令，下一帧重算
            // （敌方方向与地形都可能已经变了）。
            int &waypoint = scoutWaypointIndex[scout.SN];
            if (waypoint < 0 || waypoint >= scoutWaypointCount)
                waypoint = 0;

            int pickDR = -1;
            int pickUR = -1;
            for (int attempt = 0; attempt < scoutWaypointCount; ++attempt)
            {
                const int idx = wpOrder[waypoint];   // 按"离敌方由远及近"排过序
                const int dr = scoutWaypoints[idx][0];
                const int ur = scoutWaypoints[idx][1];
                if (BlockDis2(scout.BlockDR, scout.BlockUR, dr, ur) <= 9 ||
                    !IsScoutPatrolPointUsable(dr, ur))
                {
                    waypoint = (waypoint + 1) % scoutWaypointCount;
                    continue;
                }
                pickDR = dr;
                pickUR = ur;
                break;
            }
            if (pickDR == -1)
                continue;   // 一圈巡逻点全到过 / 全不可用
            targetDR = pickDR;
            targetUR = pickUR;
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

    // 【侦察兵状态日志】这个机制此前完全没有日志 —— 侦察兵探得深不深只能从画面
    // 上看，日志里既查不到它走到了哪、也看不出它是不是一直在避险。
    //
    // 【这三个数怎么用来定位】pos 与 dest 一起看方向对不对；homeDis 是离我方
    // 市镇中心的曼哈顿距离 —— 它长期停在几十格就说明侦察兵根本没走出去
    // （而不是"走出去但绕错了方向"），那是另一类问题（避险过频 / 生产太晚）。
    {
        static int lastScoutLogFrame = USR_INVALID_FRAME;
        if (lastScoutLogFrame == USR_INVALID_FRAME ||
            g_frame - lastScoutLogFrame >= 500)
        {
            lastScoutLogFrame = g_frame;
            const tagBuilding *center = FindCenter();
            for (const tagArmy &army : info.armies)
            {
                if (army.Sort != AT_SCOUT || army.Blood <= 0)
                    continue;
                const int homeDis =
                    center != nullptr
                        ? BlockDis(army.BlockDR, army.BlockUR, center->BlockDR,
                                   center->BlockUR)
                        : -1;
                char buf[224];
                snprintf(buf, sizeof(buf),
                         "[SCOUT] f=%d sn=%d pos=(%d,%d) dest=(%d,%d) state=%d "
                         "homeDis=%d",
                         g_frame, army.SN, army.BlockDR, army.BlockUR,
                         int(army.DR0 / double(BLOCKSIDELENGTH)),
                         int(army.UR0 / double(BLOCKSIDELENGTH)), army.NowState,
                         homeDis);
                AiDebugLog(buf);
            }
        }
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
        // 【护送祭司的农民整个跳过自卫】理由有三条：
        //   ① 阶段 A 的位置（锚点外 46 格）在敌方守军的追击上限
        //      DEFENSE_CHASE_LIMIT(25) 之外，本来就不该挨打，自卫近乎死代码；
        //   ② 阶段 B 里他的工作目标就是箭塔，12 帧一次的自卫会把他从塔上拽下来；
        //   ③ 只留一个 Owner —— 自卫与护送都下发 HumanAction 的话，两个函数
        //      每帧互抢，而每次 HumanAction 都清一次路径（表现为原地抖）。
        if (IsFarmerEscorting(farmer.SN))
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

// ══════════════════════════════════════════════════════════════════════
// 农民护送祭司
//
// 祭司是唯一的获胜路径（MainWidget::isWin 只认被转换过来的 BUILDING_SIEGE），
// 但它只有 100 血、近战与远程防御都是 0、不可补充。它冲进敌方基地去转换攻城厂
// 的那段路，正好穿过敌方箭塔的射程 —— 本机制就是把这段路上的箭塔先拔掉。
//
// 两个阶段（都由 ManageFarmerEscort 驱动，状态见文件顶部的 farmerEscortStage）：
//   BAND  —— 全部农民撤出经济，随祭司集结在敌方锚点外 46/54 两圈待命；
//   TOWER —— 祭司开始冲厂时，整队转去拆所有已知的敌方箭塔；塔清空后回环上待命。
//
// 【为什么敌方箭塔打不到农民】enemyai.cpp:238-241 的 Defend 集合只收
// enemyInfo.enemy_armies（我方军队），农民与建筑那两行 push_back 都被注释掉了；
// enemyai.cpp:447-453 的塔目标就是 Defend.back()。所以拆塔是单方面输出，
// 塔不会还手 —— 这是这件事在后期做得成的前提。
// ══════════════════════════════════════════════════════════════════════

// 清理已阵亡的护送队员，把它们占着的落点、目标与黑名单条目一起放掉。
// 不清理的后果有两层：这些 map 会随对局无限增长；而且 SN 是跨局复用的，
// 新单位可能被当成老队员（新对局重置块处理跨局，这里是同一件事的对局内版本）。
static void CleanFarmerEscortRoster()
{
    for (map<int, int>::iterator it = farmerEscortDuty.begin();
         it != farmerEscortDuty.end();)
    {
        if (IsAliveFarmerSN(it->first))
        {
            ++it;
            continue;
        }
        const int sn = it->first;
        farmerEscortOrders.erase(sn);
        farmerEscortTowerTarget.erase(sn);
        farmerEscortLastOrderFrame.erase(sn);
        farmerEscortStuckTries.erase(sn);
        for (map<pair<int, int>, int>::iterator b =
                 farmerEscortTowerBadForFarmer.begin();
             b != farmerEscortTowerBadForFarmer.end();)
        {
            if (b->first.first == sn)
                b = farmerEscortTowerBadForFarmer.erase(b);
            else
                ++b;
        }
        it = farmerEscortDuty.erase(it);
    }
}

// 已知的敌方建筑里还有没有存活的箭塔（阶段 B 的时间兜底要用）。
static bool HasKnownEnemyArrowTower()
{
    for (const tagBuilding &b : info.enemy_buildings)
    {
        if (b.Blood > 0 && b.Type == BUILDING_ARROWTOWER)
            return true;
    }
    return false;
}

// 为某个护送农民挑一个集结落点。
//
// 【方位怎么排】以「祭司相对锚点的方位」为基准、向两侧交替展开的 16 个方位，
// 每圈 16 个、共两圈 —— 与 PriestFrontPostBlock 完全相同的枚举顺序，好处是
// 「铺不下时优先铺正面」（正对祭司的那几个方位先被选中）。
//
// 【为什么以祭司的方位为基准、而不是铺满一圈】铺满的话有一半落点在敌方基地的
// 另一侧：农民从我家出发要绕半张地图才能到位（多 100+ 格 = 1600+ 帧），
// 而且脱离大部队、单独撞上守军。以祭司的方位为基准，整队落在「祭司所在的那
// 一侧」，也就是我方来向。
//
// 【落点为什么必须互不相同】目标格四邻全被占时，引擎对纯坐标移动直接
// nullPath、停 50 帧后【整条指令被取消】（Core_List.cpp:2071-2093）——
// 不是原地待命，而是行动作废。所以这里靠「查已有订单是否占了同一格」去重，
// 而不是维护一张槽位占用表（少一张需要同步的表）。
static bool FindFarmerEscortSlot(const tagFarmer &farmer, int anchorDR,
                                 int anchorUR, int priestDR, int priestUR,
                                 int &outDR, int &outUR)
{
    // 16 方向单位向量（×100 整数表），与 PriestFrontPostBlock / StandoffRingOffset
    // 是同一套 —— 用整数表而不是 sin/cos，免去浮点取整带来的落点抖动。
    static const int kCos16[16] = {100, 92, 71, 38, 0, -38, -71, -92,
                                   -100, -92, -71, -38, 0, 38, 71, 92};
    static const int kSin16[16] = {0, 38, 71, 92, 100, 92, 71, 38,
                                   0, -38, -71, -92, -100, -92, -71, -38};

    // 祭司相对锚点的方位（16 分度，点积最大的那个）
    int base = 0;
    {
        const double dx = double(priestDR - anchorDR);
        const double dy = double(priestUR - anchorUR);
        const double len = sqrt(dx * dx + dy * dy);
        if (len >= 0.5)
        {
            double bestDot = -1e18;
            for (int i = 0; i < 16; ++i)
            {
                const double dot = double(kCos16[i]) * (dx / len) +
                                   double(kSin16[i]) * (dy / len);
                if (dot > bestDot)
                {
                    bestDot = dot;
                    base = i;
                }
            }
        }
        // 祭司压在锚点上时（len < 0.5）base 保持 0：只是把整队的方位固定成
        // 一个确定值，不影响任何判据。
    }

    int bestDR = -1;
    int bestUR = -1;
    int bestDis2 = 1000000000;
    for (int ring = 0; ring < 2; ++ring)
    {
        const int radius = (ring == 0) ? USR_FARMER_ESCORT_POST_DISTANCE
                                       : USR_FARMER_ESCORT_OUTER_RING;
        for (int step = 0; step < 16; ++step)
        {
            // 从 base 向两侧交替展开：0, +1, -1, +2, -2, ...
            const int offset = ((step + 1) / 2) * ((step % 2 == 1) ? -1 : 1);
            const int i = ((base + offset) % 16 + 16) % 16;
            int dr = anchorDR + kCos16[i] * radius / 100;
            int ur = anchorUR + kSin16[i] * radius / 100;
            // 夹进地图：朝敌那几个方位通常指向地图内部，越界是少数情况，
            // 夹取只是安全网 —— 等价于「那一侧能走多远走多远」。
            dr = max(1, min(MAP_L - 2, dr));
            ur = max(1, min(MAP_U - 2, ur));

            // ① 陆地、且没被自家建筑占住（复用祭司那套判据）
            if (!IsPriestPointUsable(dr, ur))
                continue;
            // ② 四邻不能全被挡（理由见函数头）。这一条【只能】用在纯坐标落点上，
            //    拆塔那种对象目标的指令不要加它。
            if (!IsReachableAround(dr, ur, 1))
                continue;
            // ③ 上一轮判定过「走不到」的落点，在拉黑期内跳过
            map<pair<int, int>, int>::const_iterator bad =
                farmerEscortBadPoints.find(make_pair(dr, ur));
            if (bad != farmerEscortBadPoints.end() && g_frame < bad->second)
                continue;
            // ④ 不能离祭司太远 —— 需求是「和祭司一起」。没有这一条时，
            //    外圈背面那半圈会把队伍拆成两处。
            if (BlockDis2(dr, ur, priestDR, priestUR) >
                USR_FARMER_ESCORT_LEASH * USR_FARMER_ESCORT_LEASH)
                continue;
            // ⑤ 已有别的护送农民占着这个落点 → 跳过
            bool taken = false;
            for (map<int, FarmerEscortOrder>::const_iterator o =
                     farmerEscortOrders.begin();
                 o != farmerEscortOrders.end(); ++o)
            {
                if (o->first == farmer.SN)
                    continue;
                if (o->second.targetDR == dr && o->second.targetUR == ur)
                {
                    taken = true;
                    break;
                }
            }
            if (taken)
                continue;

            const int dis2 = BlockDis2(farmer.BlockDR, farmer.BlockUR, dr, ur);
            if (dis2 < bestDis2)
            {
                bestDis2 = dis2;
                bestDR = dr;
                bestUR = ur;
            }
        }
    }

    if (bestDR < 0)
        return false;
    outDR = bestDR;
    outUR = bestUR;
    return true;
}

// 征召：把农民编入护送队（永久退出经济）。
// 只在指定帧之后、且已侦察到敌方基地时执行一次。
static void RecruitFarmersForEscort()
{
    // 与军队总攻同门槛（帧号 + 已侦察到敌方基地）。没有基地信息就编队，
    // 只会得到一支不知道往哪走的队伍。
    if (g_frame < USR_FARMER_ESCORT_FRAME)
        return;
    if (!enemyBaseDiscovered)
        return;
    // 只征召一次。用独立帧号而不是「名单非空」做判据：全体阵亡时后者会变空，
    // 于是又征召一轮新产出的农民，形成送人头循环。
    if (farmerEscortDutyFrame != USR_INVALID_FRAME)
        return;

    int anchorDR = 0;
    int anchorUR = 0;
    if (!EstimateEnemySiegeAnchor(anchorDR, anchorUR))
        return;

    // 按「离敌方锚点的距离」从近到远征召。
    // 【为什么按距离而不是按 SN】SN 顺序与战场位置无关；按距离征召能让已经
    // 在前线附近的农民先入列，队伍首尾不脱节。USR_FARMER_ESCORT_MAX 小于
    // 农民总数时，这一条同时保证被留下的是家里那些（离战场最远的）。
    vector<pair<int, int> > candidates;
    for (const tagFarmer &farmer : info.farmers)
    {
        if (farmer.Blood <= 0 || farmer.FarmerSort != FARMERTYPE_FARMER)
            continue;
        candidates.push_back(
            make_pair(BlockDis2(farmer.BlockDR, farmer.BlockUR, anchorDR,
                                anchorUR),
                      farmer.SN));
    }
    sort(candidates.begin(), candidates.end());

    int taken = 0;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (taken >= USR_FARMER_ESCORT_MAX)
            break;
        const int sn = candidates[i].second;
        farmerEscortDuty[sn] = g_frame;
        // 清掉还没结算的采集挂单；已经建立的采集关系由第一次 HumanMove 打断
        // （HumanMove → addRelation → suspendRelation 清路径）。
        CancelPendingGatherOrder(sn);
        ++taken;
    }

    farmerEscortDutyFrame = g_frame;
    farmerEscortStage = FARMER_ESCORT_STAGE_BAND;

    char buf[192];
    snprintf(buf, sizeof(buf),
             "[FARMESC] f=%d RECRUIT n=%d max=%d anchor=(%d,%d)", g_frame,
             taken, USR_FARMER_ESCORT_MAX, anchorDR, anchorUR);
    AiDebugLog(buf);
}

// 祭司是否已经「冲进敌方基地去转换攻城厂」—— 阶段 B 的触发判据。
//
// 【为什么不能只判 WorkObjectSN == 攻城厂SN】那个信号最早只出现在祭司距厂
// 12 格处（FindEnemySiege 的 dis2 < 12*12），而：
//   · 祭司走完剩下的 12 格只要约 211 帧（移速 2.03 px/帧 ÷ 35.78 px/格）；
//   · 转换【建筑】是瞬发的 —— Core_List.cpp:2379-2387 贴邻 + isConstructed
//     就直接 change_BuildingRepresent，没有读条；
//   · 农民从 46 格环跑到箭塔要 500~640 帧。
// 等这个信号再放人，农民到达时祭司早已获胜、或已被打死（祭司 100 血、
// 远程防御 0，一座箭塔 3 点/次 → 34 次命中就够）。
// 所以主判据取「祭司的目的地已经越过 40 格带、指向敌方基地」这个更早的真实
// 状态；WorkObjectSN 作为权威确认并入 —— 它成立时必然为真。
//
// outTriggerKind 只用于日志：事后要能区分「机制按预期提前放了人」与「兜底才放的」。
static bool IsPriestSiegeCommitted(const tagArmy *priest, int &outTriggerKind)
{
    if (priest == nullptr)
        return false;
    // 与 ManagePriest 那条冲厂分支同门槛：26000 帧之前祭司不主动出门。
    if (g_frame < USR_PRIEST_PASSIVE_FRAME)
        return false;
    if (!enemyBaseDiscovered)
        return false;

    // ① 权威：已建立到攻城厂的工作关系（正在走去 / 正在转换）。
    const tagBuilding *siege = FindEnemySiegeBuilding();
    if (siege != nullptr && priest->WorkObjectSN == siege->SN)
    {
        outTriggerKind = 1;
        return true;
    }

    // ② 有可见敌兵时祭司不会往里冲（冲厂分支要求 !HasVisibleEnemyArmy()），
    //    此时它的目的地即使指向前方，也只是站桩带的重算。
    //    这一条同时保证「放人」不会和祭司的 keepout 分支（把他往家里推）同时发生。
    if (HasVisibleEnemyArmy())
        return false;

    int anchorDR = 0;
    int anchorUR = 0;
    if (!EstimateEnemySiegeAnchor(anchorDR, anchorUR))
        return false;

    // ③ 祭司当前的目的地已经进入 40 格【带以内】。
    //    冲厂那条 HumanMove 的落点就是厂的正交相邻格，而厂≈锚点。
    //    站桩带那条 HumanMove 的落点半径恒为 40（PriestFrontPostBlock 传的是
    //    USR_PRIEST_FRONT_POST_DISTANCE），斜方向上是 36²+15²=1521，
    //    大于 (40−6)²=1156 —— 所以「带内落点」不会误触发这一条。
    const int destDR = int(priest->DR0 / double(BLOCKSIDELENGTH));
    const int destUR = int(priest->UR0 / double(BLOCKSIDELENGTH));
    const int nearR =
        USR_PRIEST_FRONT_POST_DISTANCE - USR_PRIEST_FRONT_POST_BAND;
    if (BlockDis2(destDR, destUR, anchorDR, anchorUR) <= nearR * nearR)
    {
        outTriggerKind = 2;
        return true;
    }

    // ④ 时间兜底：攻城厂整局没被侦察到（EstimateEnemySiegeAnchor 的注释记录过
    //    这个真实故障：`enemyB` 最多到 1）时，ManagePriest 那条冲厂分支因为
    //    FindEnemySiegeBuilding() == nullptr 永远不执行，①②③ 全部关死，
    //    护送队会在环上白站到底。到兜底帧之后放开：此时总攻已经打了很久，
    //    敌方建筑必然已进 info.enemy_buildings。
    if (g_frame >= USR_FARMER_TOWER_FALLBACK_FRAME && HasKnownEnemyArrowTower())
    {
        outTriggerKind = 3;
        return true;
    }
    return false;
}

// 阶段 A：把护送队铺到锚点外的环上。
static void ManageFarmerEscortBand(UsrAI *ai, const tagArmy &priest)
{
    int anchorDR = 0;
    int anchorUR = 0;
    if (!EstimateEnemySiegeAnchor(anchorDR, anchorUR))
        return;

    // 全局节流 60 帧（与站桩 / 侦察 / 祭司探路同一粒度）。不节流就会每帧重算
    // 落点，而 HumanMove 每次都会清空移动路径 —— 表现是整队原地抖。
    static int lastBandPassFrame = USR_INVALID_FRAME;
    if (lastBandPassFrame != USR_INVALID_FRAME &&
        g_frame - lastBandPassFrame < USR_FARMER_ESCORT_ORDER_INTERVAL)
        return;
    lastBandPassFrame = g_frame;

    for (const tagFarmer &farmer : info.farmers)
    {
        if (farmer.Blood <= 0 || farmer.FarmerSort != FARMERTYPE_FARMER)
            continue;
        if (!IsFarmerEscorting(farmer.SN))
            continue;

        map<int, FarmerEscortOrder>::iterator it =
            farmerEscortOrders.find(farmer.SN);
        const bool hasOrder = (it != farmerEscortOrders.end());

        // 已经就位 → 不再干预。注意【不能删记录】—— 落点可能本身就落在环上，
        // 删掉的话下一轮会被当成新队员重新分配落点，形成来回搬运
        // （SpreadChariotArchers 的同一处注释）。
        if (hasOrder &&
            BlockDis2(farmer.BlockDR, farmer.BlockUR, it->second.targetDR,
                      it->second.targetUR) <= USR_FARMER_ESCORT_ARRIVED_DIS2)
            continue;

        // 正在交战：给一段宽限让他脱身，超时才硬拉。
        // 【为什么必须设上限】没有它，农民会顺着追兵一路漂向敌方基地，
        // 既脱离大部队，又把自己送进守军的包围里。
        if (farmer.NowState == HUMAN_STATE_ATTACKING)
        {
            if (!hasOrder ||
                g_frame - it->second.orderFrame <
                    USR_FARMER_ESCORT_ENGAGED_FRAMES)
                continue;
        }

        // 正在正常行进（没卡住）→ 不打扰。
        // 其余情况（IDLE / WORKING / 卡住的 WALKING）都重新下令。
        if (hasOrder && farmer.NowState == HUMAN_STATE_WALKING &&
            !IsFarmerStuckWalking(farmer))
            continue;

        // 上一轮那个落点走不到 → 记黑名单，这一轮会换一个新的。
        //
        // 【必须限定在 WALKING 上】IsFarmerStuckWalking 的语义是「在同一格里
        // 待了 USR_FARMER_STUCK_FRAMES 帧没动」，【不是】「走不动」—— 站着
        // 待命的 IDLE 农民同样满足它。不加这个限定的话，一个已经走到落点附近、
        // 只是被挤开几格的农民会被判成「这个落点走不到」而把它拉黑，
        // 32 个落点会这样被逐个拉黑，最后整队在环上乱走。
        // 落点本身的可达性在选点时已由 IsReachableAround 保证，剩下的
        // 停住只是临时拥堵，重发一次指令即可，不该拉黑。
        if (hasOrder && farmer.NowState == HUMAN_STATE_WALKING &&
            IsFarmerStuckWalking(farmer))
            farmerEscortBadPoints[make_pair(it->second.targetDR,
                                            it->second.targetUR)] =
                g_frame + USR_FARMER_ESCORT_BAD_POINT_FRAMES;

        int dr = -1;
        int ur = -1;
        if (!FindFarmerEscortSlot(farmer, anchorDR, anchorUR, priest.BlockDR,
                                  priest.BlockUR, dr, ur))
            continue;   // 32 个槽位全不可用，这一轮先不动他

        FarmerEscortOrder order;
        order.targetDR = dr;
        order.targetUR = ur;
        order.orderFrame = g_frame;
        order.fromDR = farmer.BlockDR;
        order.fromUR = farmer.BlockUR;
        farmerEscortOrders[farmer.SN] = order;

        CancelPendingGatherOrder(farmer.SN);   // 双保险，防止漏进一条采集挂单
        ai->HumanMove(farmer.SN, (dr + 0.5) * double(BLOCKSIDELENGTH),
                      (ur + 0.5) * double(BLOCKSIDELENGTH));
    }
}

// 阶段 B：把护送队分配到各座敌方箭塔上并下发攻击。
// 收集存活的敌方箭塔，按距锚点（≈祭司必经之路）升序。
//
// 【为什么只认箭塔】需求是「防止祭司被箭塔攻击」，而箭塔是祭司进场期间唯一的
// 持续伤害源：敌方守军追到 DEFENSE_CHASE_LIMIT(25) 就折返，而祭司站在 40 格外；
// 打别的建筑对「保护祭司」没有作用，还会让农民白挨守军。
//
// 【为什么按距锚点排序】沿祭司的必经之路依次清，而不是各自就近 —— 靠前的塔先
// 被灌满人、先被打掉，祭司往里走时面对的塔是越来越少，而不是越走越多。
//
// 【注意：这里与军队那条路的顺序【相反】，尚未统一】本函数按距锚点【升序】
// （由内向外），而 FindOffensiveTargetSN() 的第 0 条现已按需求改为【最外围优先】
// （由外向内）。两条排序目前不冲突，因为本机制已由 USR_FARMER_ESCORT_ENABLED
// 停用、不在跑；若将来重新启用护送，应先决定用哪一种，别让两支力量一个从里
// 往外、一个从外往里。
//
// 农民与军队两条路共用这一个列表：两者必须看到同一份「还剩哪些塔」，
// 否则会出现「农民以为清完了、士兵还在打另一座」这种对不上的状态。
static void CollectEnemyArrowTowers(vector<const tagBuilding *> &towers,
                                    int &anchorDR, int &anchorUR)
{
    towers.clear();
    for (const tagBuilding &b : info.enemy_buildings)
    {
        if (b.Blood > 0 && b.Type == BUILDING_ARROWTOWER)
            towers.push_back(&b);
    }
    anchorDR = 0;
    anchorUR = 0;
    EstimateEnemySiegeAnchor(anchorDR, anchorUR);
    sort(towers.begin(), towers.end(),
         [anchorDR, anchorUR](const tagBuilding *a, const tagBuilding *b) {
             return BlockDis2(a->BlockDR, a->BlockUR, anchorDR, anchorUR) <
                    BlockDis2(b->BlockDR, b->BlockUR, anchorDR, anchorUR);
         });
}

// 阶段 B：把护送队分配到各座敌方箭塔上并下发攻击。
static void ManageFarmerEscortTowers(UsrAI *ai, const tagArmy &priest)
{
    vector<const tagBuilding *> towers;
    int anchorDR = 0;
    int anchorUR = 0;
    CollectEnemyArrowTowers(towers, anchorDR, anchorUR);

    // 没有塔可打（已清空 / 还没侦察到）→ 回集结带待命。
    // 注意阶段【不回退】：阶段是单向 latch，这里只是「没活干就待在祭司旁边」，
    // 而不是回经济。按需求，农民拆完塔就在原地等。
    if (towers.empty())
    {
        farmerEscortTowerTarget.clear();
        ManageFarmerEscortBand(ai, priest);
        return;
    }

    map<int, int> towerWorkers;   // 塔 SN -> 本轮已分配人数
    map<int, int> farmerToTower;  // 农民 SN -> 本轮的塔 SN
    map<int, pair<int, int> > towerPos;  // 塔 SN -> 坐标（判「是否已在塔跟前」）
    for (size_t t = 0; t < towers.size(); ++t)
        towerPos[towers[t]->SN] =
            make_pair(towers[t]->BlockDR, towers[t]->BlockUR);

    // ④ 先保留既有分配。对同一目标重复下达 HumanAction 会中止并重建关系、
    //    把已经打出去的伤害进度清零（AssignArrowTowerTargets 的同一处注释）——
    //    这是本机制能不能真拆掉一座塔的关键。
    for (map<int, int>::iterator it = farmerEscortTowerTarget.begin();
         it != farmerEscortTowerTarget.end(); ++it)
    {
        if (!IsAliveFarmerSN(it->first))
            continue;
        bool towerAlive = false;
        for (size_t t = 0; t < towers.size(); ++t)
        {
            if (towers[t]->SN == it->second)
                towerAlive = true;
        }
        if (!towerAlive)
            continue;   // 塔没了 → 本轮重新分配
        if (towerWorkers[it->second] >= USR_FARMER_TOWER_PER_TARGET_MAX)
            continue;
        map<pair<int, int>, int>::const_iterator bad =
            farmerEscortTowerBadForFarmer.find(
                make_pair(it->first, it->second));
        if (bad != farmerEscortTowerBadForFarmer.end() && g_frame < bad->second)
            continue;
        farmerToTower[it->first] = it->second;
        towerWorkers[it->second]++;
    }

    // 再给还没有归属的农民按「塔的威胁顺序」补位，每次挑离该塔最近的那个。
    for (size_t t = 0; t < towers.size(); ++t)
    {
        const int towerSN = towers[t]->SN;
        while (towerWorkers[towerSN] < USR_FARMER_TOWER_PER_TARGET_MAX)
        {
            int bestSN = -1;
            int bestDis2 = 1000000000;
            for (const tagFarmer &f : info.farmers)
            {
                if (f.Blood <= 0 || f.FarmerSort != FARMERTYPE_FARMER)
                    continue;
                if (!IsFarmerEscorting(f.SN))
                    continue;
                if (farmerToTower.find(f.SN) != farmerToTower.end())
                    continue;
                map<pair<int, int>, int>::const_iterator bad =
                    farmerEscortTowerBadForFarmer.find(
                        make_pair(f.SN, towerSN));
                if (bad != farmerEscortTowerBadForFarmer.end() &&
                    g_frame < bad->second)
                    continue;
                const int d2 =
                    BlockDis2(f.BlockDR, f.BlockUR, towers[t]->BlockDR,
                              towers[t]->BlockUR);
                if (d2 < bestDis2)
                {
                    bestDis2 = d2;
                    bestSN = f.SN;
                }
            }
            if (bestSN < 0)
                break;   // 这个塔附近没有可选农民了
            farmerToTower[bestSN] = towerSN;
            towerWorkers[towerSN]++;
        }
    }

    // ⑤ 超员兜底：有塔已满但仍有人没归属 → 全部压到威胁最大的那一座。
    //    宁可超员排队，也不能让农民站着不动（超员的那些会在外围等着，
    //    塔一死就补上）。
    for (const tagFarmer &f : info.farmers)
    {
        if (f.Blood <= 0 || f.FarmerSort != FARMERTYPE_FARMER)
            continue;
        if (!IsFarmerEscorting(f.SN))
            continue;
        if (farmerToTower.find(f.SN) != farmerToTower.end())
            continue;
        farmerToTower[f.SN] = towers[0]->SN;
    }

    // ⑥ 下发。
    for (const tagFarmer &farmer : info.farmers)
    {
        if (farmer.Blood <= 0 || farmer.FarmerSort != FARMERTYPE_FARMER)
            continue;
        if (!IsFarmerEscorting(farmer.SN))
            continue;
        map<int, int>::const_iterator pick = farmerToTower.find(farmer.SN);
        if (pick == farmerToTower.end())
            continue;
        const int targetSN = pick->second;

        // 卡住：走着但没进展，【且还没走到塔的跟前】。
        //   · 不能用 NowState == ATTACKING 判卡死 —— 那正是我们要的状态；
        //   · 必须排除「已经在塔跟前」：20 个农民打一座 2×2 的塔只有 8 个贴得上，
        //     其余在周围排队等位置，位置自然不动，那不是在卡死。没有这条排除，
        //     排队的会被逐个判成「这座塔走不到」而换塔，超员兜底又把它们压回来，
        //     形成来回换目标的抖动。判据见 USR_FARMER_TOWER_ENGAGE_DIS2。
        const pair<int, int> &tpos = towerPos[targetSN];
        const bool stuck =
            (farmer.NowState == HUMAN_STATE_WALKING &&
             IsFarmerStuckWalking(farmer) &&
             BlockDis2(farmer.BlockDR, farmer.BlockUR, tpos.first,
                       tpos.second) > USR_FARMER_TOWER_ENGAGE_DIS2);

        // 目标没变、且没卡住 → 什么都不做。这是「不打断攻击关系」的防线。
        // 注意卡住这一条必须【排在前面判】：「关系已建立、但走不过去」正是
        // 卡住的典型形态，先按 WorkObjectSN 去重的话这种农民永远走不到
        // 下面的换塔逻辑，会一直挂在原地。
        if (farmer.WorkObjectSN == targetSN && !stuck)
        {
            farmerEscortStuckTries[farmer.SN] = 0;
            farmerEscortTowerTarget[farmer.SN] = targetSN;
            continue;
        }

        // 节流。卡住的也要走同一个窗口 —— IsFarmerStuckWalking 一旦为真就
        // 之后每帧都为真，不节流的话下面的重试计数会在 3 帧内被加满。
        map<int, int>::const_iterator last =
            farmerEscortLastOrderFrame.find(farmer.SN);
        if (last != farmerEscortLastOrderFrame.end() &&
            g_frame - last->second < USR_FARMER_TOWER_ORDER_INTERVAL)
            continue;
        farmerEscortLastOrderFrame[farmer.SN] = g_frame;

        if (stuck)
        {
            int &tries = farmerEscortStuckTries[farmer.SN];
            ++tries;
            if (tries >= USR_FARMER_TOWER_STUCK_MAX_TRIES)
            {
                // 这座塔对这个农民走不到 → 拉黑一段时间，下一轮他会分到别的塔
                tries = 0;
                farmerEscortTowerBadForFarmer[make_pair(farmer.SN, targetSN)] =
                    g_frame + USR_FARMER_TOWER_BLACKLIST_FRAMES;
                farmerEscortTowerTarget.erase(farmer.SN);
                char buf[192];
                snprintf(buf, sizeof(buf),
                         "[FARMESC-BAD] f=%d farmer=%d tower=%d tries=%d "
                         "until=%d",
                         g_frame, farmer.SN, targetSN,
                         USR_FARMER_TOWER_STUCK_MAX_TRIES,
                         g_frame + USR_FARMER_TOWER_BLACKLIST_FRAMES);
                AiDebugLog(buf);
            }
            // 卡住时一律不重发同一条指令 —— 重发不会改变结果，
            // 只会把刚建立的攻击关系又清一次。
            continue;
        }

        farmerEscortStuckTries[farmer.SN] = 0;
        CancelPendingGatherOrder(farmer.SN);
        ai->HumanAction(farmer.SN, targetSN);
        farmerEscortTowerTarget[farmer.SN] = targetSN;
    }

    // 每座塔的进度日志（每 500 帧）。blood 的下降速率用来反推「单次命中周期」——
    // 那是本机制唯一无法从代码确定的量（动画帧数在 res.rcc 里），
    // USR_FARMER_TOWER_PER_TARGET_MAX 等工期常量要靠它校准。
    static int lastTowerLogFrame = USR_INVALID_FRAME;
    if (lastTowerLogFrame == USR_INVALID_FRAME ||
        g_frame - lastTowerLogFrame >= 500)
    {
        lastTowerLogFrame = g_frame;
        for (size_t t = 0; t < towers.size(); ++t)
        {
            char buf[224];
            snprintf(buf, sizeof(buf),
                     "[FARMESC-TOWER] f=%d sn=%d blood=%d/%d pos=(%d,%d) "
                     "attackers=%d disToPriest=%d",
                     g_frame, towers[t]->SN, towers[t]->Blood,
                     towers[t]->MaxBlood, towers[t]->BlockDR,
                     towers[t]->BlockUR, towerWorkers[towers[t]->SN],
                     BlockDis(towers[t]->BlockDR, towers[t]->BlockUR,
                              priest.BlockDR, priest.BlockUR));
            AiDebugLog(buf);
        }
    }
}

// 所有已知的敌方箭塔是不是都已经被牵制住了。
//
// 【这是祭司进场的前置条件】用户要求「确保每一个箭塔都被牵制住才可以让祭司
// 进行转化」。判据取「有我方单位正以这座塔为工作目标」—— 关系一旦建立（不论
// 是正在赶路还是正在攻击）就算数，因为：
//   · 对农民：那意味着已经有人在这座塔上投入劳动，塔正在被打掉；
//   · 对士兵：关系成立就意味着它已经在塔附近，会进塔的 Defend 集合并分走火力。
//
// 【为什么必须逐座查】只查「附近有没有人」是不够的：5 座塔可能分布在不同方向，
// 祭司冲进去的路上只经过其中两座 —— 剩下那三座照样能打到它。要保证的是
// 【每一座】都有人，而不是随便哪一座有人。
static bool AllEnemyArrowTowersEngaged()
{
    for (const tagBuilding &tower : info.enemy_buildings)
    {
        if (tower.Blood <= 0 || tower.Type != BUILDING_ARROWTOWER)
            continue;
        bool engaged = false;
        for (const tagFarmer &farmer : info.farmers)
        {
            if (farmer.Blood > 0 && farmer.WorkObjectSN == tower.SN)
            {
                engaged = true;
                break;
            }
        }
        if (!engaged)
        {
            for (const tagArmy &army : info.armies)
            {
                if (army.Blood > 0 && army.WorkObjectSN == tower.SN)
                {
                    engaged = true;
                    break;
                }
            }
        }
        if (!engaged)
            return false;
    }
    return true;
}

// 敌方箭塔是否已【全部被摧毁】（不只是被牵制，理由见
// USR_PRIEST_SIEGE_TOWER_CLEAR 的说明：被牵制的塔照样开火，而祭司
// 100 血、防御 0，扛不住几轮）。
//
// 【按需求改成「全部」】原先只查「攻城厂周围 USR_PRIEST_SIEGE_TOWER_CLEAR
// 格以内」的塔，现在改成任何一座活着的敌方箭塔都会拦住祭司 —— 它会一直等到
// 敌方塔被清光才去转化攻城厂。
//
// 因为敌方塔的位置不一定都被侦察到，「已知的全清光」才是能执行的判据；
// 这比只盯厂周围严得多，代价是如果有一座塔始终拆不掉（农民死光、或它在
// 够不到的地方），祭司就会一直不进厂 —— 从「送死」变成「僵持」。
//
// 已摧毁的塔 Blood<=0、根本不会进下面的循环，所以判据就是「没有活着的塔」。
static bool SiegeTowersCleared()
{
    for (const tagBuilding &b : info.enemy_buildings)
    {
        if (b.Blood > 0 && b.Type == BUILDING_ARROWTOWER)
            return false;   // 还有塔活着
    }
    return true;
}

// 祭司现在可不可以冲向攻城厂 —— 进厂的前置条件，由「冲厂」与「转换」两条
// 分支共用（两者原先各写各的，于是转换分支能绕过冲厂分支的门控直接建立关系，
// 实测祭司就是这样在塔还活着的时候冲进去转换、被打死的）。
//
// 四条同时成立才放行：
//   ① 过了 USR_PRIEST_PASSIVE_FRAME —— 那之前军队还没成型，押上祭司是亏的；
//   ② 视野里没有敌方军队 —— 有敌人时出门就是送（原设计）；
//   ③ 所有已知箭塔都被牵制 —— 每座塔都有人在打，它们才不会专心招呼祭司；
//   ④ 敌方箭塔【全部已被摧毁】—— ③ 只保证"有人在打它"，塔照样开火，而祭司
//      100 血、防御 0，扛不住几轮。按需求这里取「全部」而不是原来的「厂周围
//      若干格内」：祭司要等到敌方塔被清光才去转化攻城厂。
static bool CanPriestApproachSiege()
{
    if (g_frame < USR_PRIEST_PASSIVE_FRAME)
        return false;
    if (HasVisibleEnemyArmy())
        return false;
    if (!AllEnemyArrowTowersEngaged())
        return false;
    return SiegeTowersCleared();
}

// 农民护送祭司的唯一入口（在 processData 里每帧调用一次）。
static void ManageFarmerEscort(UsrAI *ai)
{
    // 先清理阵亡队员，下面的统计与日志才是准的。
    CleanFarmerEscortRoster();
    RecruitFarmersForEscort();

    if (farmerEscortDuty.empty())
        return;

    // 祭司是唯一的获胜路径；他没了这局基本就结束，护送队跟着撤编。
    const tagArmy *priest = FindPriest();
    if (priest == nullptr)
        return;

    if (farmerEscortStage == FARMER_ESCORT_STAGE_BAND)
    {
        int kind = 0;
        if (IsPriestSiegeCommitted(priest, kind))
        {
            farmerEscortStage = FARMER_ESCORT_STAGE_TOWER;   // 单向，永不回退
            // 清掉集结状态：残留的落点记录会干扰拆塔分配（落点去重会看到旧订单）。
            farmerEscortOrders.clear();
            farmerEscortTowerTarget.clear();
            farmerEscortStuckTries.clear();
            farmerEscortLastOrderFrame.clear();

            int anchorDR = 0;
            int anchorUR = 0;
            EstimateEnemySiegeAnchor(anchorDR, anchorUR);
            int towerCount = 0;
            for (const tagBuilding &b : info.enemy_buildings)
            {
                if (b.Blood > 0 && b.Type == BUILDING_ARROWTOWER)
                    ++towerCount;
            }
            // 【这行是本机制的验收口径】kind = 1 关系确认 / 2 目的地越过带 / 3 兜底。
            // 若实测全是 1，说明上面第 ③ 条没生效、农民必然迟到（阶段 B 的整个
            // 时序论证见 IsPriestSiegeCommitted 的函数头）。
            char buf[288];
            snprintf(buf, sizeof(buf),
                     "[FARMESC] f=%d STAGE=TOWER kind=%d priest=(%d,%d) hp=%d "
                     "wo=%d dest=(%d,%d) anchor=(%d,%d) towers=%d",
                     g_frame, kind, priest->BlockDR, priest->BlockUR,
                     priest->Blood, priest->WorkObjectSN,
                     int(priest->DR0 / double(BLOCKSIDELENGTH)),
                     int(priest->UR0 / double(BLOCKSIDELENGTH)), anchorDR,
                     anchorUR, towerCount);
            AiDebugLog(buf);
        }
    }

    if (farmerEscortStage == FARMER_ESCORT_STAGE_TOWER)
        ManageFarmerEscortTowers(ai, *priest);
    else
        ManageFarmerEscortBand(ai, *priest);

    // 状态日志（每 500 帧）。n 单调下降就是「农民在被消耗」—— 它是
    // 「农民在路上被吃光」这个风险唯一的早期信号；pd 用来确认「40 格以外」
    // 这条不变量（应当稳定在 34~46）。
    static int lastEscortLogFrame = USR_INVALID_FRAME;
    if (lastEscortLogFrame == USR_INVALID_FRAME ||
        g_frame - lastEscortLogFrame >= 500)
    {
        lastEscortLogFrame = g_frame;
        int alive = 0;
        int arrived = 0;
        int attacking = 0;
        for (const tagFarmer &farmer : info.farmers)
        {
            if (farmer.Blood <= 0 || farmer.FarmerSort != FARMERTYPE_FARMER)
                continue;
            if (!IsFarmerEscorting(farmer.SN))
                continue;
            ++alive;
            if (farmer.NowState == HUMAN_STATE_ATTACKING)
                ++attacking;
            else if (farmer.NowState == HUMAN_STATE_IDLE)
                ++arrived;
        }
        int pd = -1;
        int anchorDR = 0;
        int anchorUR = 0;
        if (EstimateEnemySiegeAnchor(anchorDR, anchorUR))
            pd = BlockDis(priest->BlockDR, priest->BlockUR, anchorDR, anchorUR);
        char buf[224];
        snprintf(buf, sizeof(buf),
                 "[FARMESC] f=%d stage=%d n=%d arrived=%d attacking=%d "
                 "priest=(%d,%d) pd=%d hp=%d",
                 g_frame, farmerEscortStage, alive, arrived, attacking,
                 priest->BlockDR, priest->BlockUR, pd, priest->Blood);
        AiDebugLog(buf);
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

// 每次后撤几格。
static const int USR_MELEE_KITE_BLOCKS = 2;
// 被近战贴上后撤到「离敌方锚点」多少格。取 30（比站桩环 USR_STANDOFF_RADIUS
// 的 32 近 2 格）。
//
// 【为什么是 30 而不是 32】敌方守军的追击上限是 25 格（DEFENSE_CHASE_LIMIT，
// 量到它们的攻城厂），退到 30 格拉开 5 格，仍然在守军够不到的位置；比站桩环
// 近一点的好处是「一被贴就退到最外圈」不会让推进彻底停下来 —— 部队愿意在
// 28~30 这一带跟守军拉锯，而不是每次都被顶回 32。
static const int USR_MELEE_KITE_RETREAT_DISTANCE = 30;
// 两次后撤之间至少间隔多少帧。每次后撤都发 HumanMove，而 HumanMove 会清空路径并
// 打断攻击关系 —— 不加节流会变成「每帧后退 → 每帧被清空 → 原地不动」。
// 取 40：速度 4.07 的战车弓兵走完两格约 20 帧，够它真的挪开。
static const int USR_MELEE_KITE_INTERVAL = 40;
// 「被近战打到」的贴身判定：两格（欧氏距离平方）。敌人有 WorkObjectSN 指向我方
// 单位只说明它锁了目标，还要真的贴上来才算打到。
static const int USR_MELEE_KITE_TRIGGER_DIS2 = 4;

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
// 【所以】把远程部队停在它 26~32 格处：守军冲出来到 25 格就折返，而那 1~7 格
// 正好落在我方战车弓兵射程（7）内 —— 白挨打，永远够不到我们。
// 这比文档描述的「突击者反复进退」更省事：站对距离，对方会自己完成拉扯。
//
// 【环怎么维持】圈内不再按位置阈值驱赶，改成「被打到就后退两格」—— 挨一次打
// 退一步，退到 32 格之外自然停手（见 ManageStandoff 圈内那档）。掉队的（> 44 格）
// 则被拉回来，中间 32~44 是死区。只做「退出」不做「拉回」，被截停在后方的部队
// 就无人接管；只做「拉回」不做「退出」，部队会一直贴在敌人身上并和自卫对撞。
//
// 【锚点为什么用攻城武器厂】因为敌方守军的判据就是量到它的。用市镇中心会算偏，
// 偏多少取决于两座建筑隔多远。
static const int USR_STANDOFF_RADIUS = 32;
// 「掉队靠拢」的触发宽度（格）：离锚点超过 USR_STANDOFF_RADIUS + 本值 的单位，
// 才被当成掉队、派去向环上靠拢；32 ~ 44 之间是死区，什么都不做。
//
// 【为什么需要靠拢这一档】原先只有「圈内推出，圈外不管」两档，那个设计默认了
// 「所有部队都已经在环附近」。但推进指令是全有或全无的 —— ManageOffensiveArmy
// 一见视野里有敌人就 `return`（见那里的说明），还在半路上的部队被当场截停；
// 而站桩只处理 32 格内的，圈外的一句都不发。两边同时忽略，掉队的部队
// （实测多在 40~70 格）拿到零指令、永久站着不动。
// 表现为「一部分兵在 50 格外站着不动」，而走在前面、已经进 32 格的那批正常。
//
// 【为什么中间必须留死区】靠拢与自卫是对撞的：靠拢把单位往环上搬，自卫把它
// 往视野（7 格）/ 协防（12 格）半径内的敌人那儿搬，而敌人并不总在环上
// （守军会追出到 25 格，也可能有人停在外圈）。两条规则的方向在 32 格线两侧
// 会翻转，没有死区的话单位就卡在线上被两边轮流推。
// 取 12 格：比自卫最大的非祭司触发半径（协防 12 格）不窄，保证「刚退到圈外」
// 和「刚靠拢到环上」都不会立刻触发对面那条规则。
static const int USR_STANDOFF_JOIN_BAND = 12;
// 「被打到就后退几格」—— 圈内单位撤离环内的唯一手段。与 USR_MELEE_KITE_BLOCKS
// 取同值（都是一次后撤的步长），但语义不同：这条背对【锚点】、那条背对【攻击者】，
// 要调就分别调。
static const int USR_STANDOFF_STEP_BLOCKS = 2;
// 后退步的封口窗口（帧）：只用来挡住 AssignFieldSelfDefense 在同帧稍后覆盖这条
// HumanMove。取 45 —— 走完两格约 24 帧、留一倍余量；且比 60 帧的站桩节流短，
// 保证窗口在下一次站桩之前自然过期，不会和靠拢那条 200 帧的封口逻辑互相干扰。
static const int USR_STANDOFF_STEP_SEAL_FRAMES = 45;
// 靠拢指令的封口窗口长度（帧）：单位被派去向环上槽位靠拢之后，这么多帧内
// 不接自卫指令、也不被站桩重发，让这次移动真正走完。
//
// 【为什么需要它】站桩每 60 帧才发一次「走向槽位」，而 AssignFieldDefense
// 每 12 帧就发一次「打最近的敌人」（USR_FIELD_SELF_DEFENSE_ORDER_INTERVAL），
// 且自卫在 processData 里【后执行】—— 频率差 5 倍 + 后执行，站桩的移动指令
// 每次都被覆盖，净效果是原地不动。
// 取 200 帧：从内圈走到环上最多约 12 格，单位约 1 格 / 12 帧，留一倍余量。
// 窗口内的解封条件见 ManageStandoff：走进死区（32~44）立刻解封、走到槽位解封、
// 或一个周期内没有朝目标前进也解封 —— 所以「不还手」的时间不会超过实际所需。
// 窗口到期时，同一次站桩遍历会立刻重新下发并续封，中间没有空档。
//
// 【为什么靠拢要 200 帧，而圈内的后退步只要 45 帧】靠拢是一段长距离行军
// （从 70 格走到 32 格约 38 格、约 456 帧，分几段走完），必须整段封住才不会被
// 自卫截走；圈内那档一次只退两格（约 24 帧），封 45 帧就够，而且封得短反而好 ——
// 它退完就能立刻回到自卫状态继续输出。
static const int USR_STANDOFF_RETREAT_FRAMES = 200;
// 【已删除】USR_STANDOFF_RETREAT_INNER = 20（「20~32 之间不动」的滞回带）。
// 它当年是和另一条规则配套的：那条规则说「进圈的一律推到 32 格环上」，
// 滞回带是为了让 20~32 之间的单位不被反复推来推去。
// 那条「按位置阈值推出去」的规则现在也删了 —— 圈内改成由挨打驱动的两格后退
// （见 USR_STANDOFF_STEP_BLOCKS）。阈值本身没有了，围绕它设的滞回带自然也不必要。
// 环上落点数。半径 32 的圆周长约 201 格，16 个点平均间隔约 12.5 格 ——
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
// 用固定的 16 方向表而不是 sin/cos：这个文件里其它环状布局（战车弓兵
// USR_SPREAD_RINGS）也都是整数表，保持一致且没有浮点取整问题。
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

// 敌方攻城武器厂的位置 —— 侦察到了用真的，没侦察到用估算。
//
// 【为什么要估算】FindEnemySiegeBuilding() 要求攻城厂已经进过
// info.enemy_buildings（也就是被侦察到）。实测它【整局都是 nullptr】（`enemyB`
// 最多到 1，那还是座箭塔），于是下面 ManageStandoff 第一行就 return false ——
// 「有敌人就退到 28 格外」一次都没生效过。
//
// 【依据】四张地图都满足同一条：敌方攻城厂永远在我方市镇中心的【对角落】。
//   相对我方中心的偏移：map (-66,+64) / map1 (+73,-65) / map2 (-57,-83) / map3 (+64,-58)
//   用「地图中心的对极点」预测的误差：map3 (1,-1) / map2 (6,-4) / map (11,9) / map1 (14,12)
// 最差曼哈顿 26 格 —— 站桩圈半径 28，够用；而且估错了也只是把站位圈摆偏一点，
// 不会有别的副作用（真实坐标一出现就优先用真的）。
//
// 【为什么对地图旋转免疫】「对极点」这个关系在任何绕地图中心的旋转下都成立，
// 所以不需要知道本局的 rotation：拿 FindCenter() 的真实坐标直接算。
//
// 【只用于「取位置」，不能用于「取目标】】转换攻城厂需要它的 SN，估算给不出 SN ——
// 所以 ManagePriest 里那一段仍然只认 FindEnemySiegeBuilding()。
static bool EstimateEnemySiegeAnchor(int &anchorDR, int &anchorUR)
{
    // ① 真攻城厂
    const tagBuilding *real = FindEnemySiegeBuilding();
    if (real != nullptr)
    {
        anchorDR = real->BlockDR;
        anchorUR = real->BlockUR;
        return true;
    }

    // ② 侦察到过的敌方建筑（enemyBaseDiscovered 记下的第一个）—— 真实数据，优于估算
    if (enemyBaseDiscovered && enemyBaseBlockDR >= 0 && enemyBaseBlockUR >= 0)
    {
        anchorDR = enemyBaseBlockDR;
        anchorUR = enemyBaseBlockUR;
        return true;
    }

    // ③ 都没侦察到 → 用我方中心关于地图中心的对极点估一个
    const tagBuilding *center = FindCenter();
    if (center == nullptr)
        return false;
    anchorDR = max(0, min(MAP_L - 1, MAP_L - 1 - center->BlockDR));
    anchorUR = max(0, min(MAP_U - 1, MAP_U - 1 - center->BlockUR));
    return true;
}

// 从 (fromDR,fromUR) 朝 (toDR,toUR) 走 step 格，发一条 HumanMove。
// 按单位向量取整，所以走的是斜向也没关系；距离不足 step 时直接走向目标。
static void MoveTowardBlock(UsrAI *ai, int sn, int fromDR, int fromUR, int toDR,
                            int toUR, int step)
{
    double dx = double(toDR - fromDR);
    double dy = double(toUR - fromUR);
    const double len = sqrt(dx * dx + dy * dy);
    if (len < 0.5)
        return;  // 已经重合，不必发指令
    const double go = (len < double(step)) ? len : double(step);
    const int tx =
        max(1, min(MAP_L - 2, fromDR + int(dx / len * go)));
    const int ty =
        max(1, min(MAP_U - 2, fromUR + int(dy / len * go)));
    ai->HumanMove(sn, (tx + 0.5) * double(BLOCKSIDELENGTH),
                  (ty + 0.5) * double(BLOCKSIDELENGTH));
}

// 突击者的驱动。只在已确认「视野里有敌人」时由 ManageStandoff 调用。
//
// 三个状态：没有突击者 → 站桩僵住足够久就选一个；有突击者 → 前进或后退；
// 回到起点 → 交还指挥权（下次僵住再选一个）。死亡的突击者也走这条路。
static void ManageStandoffAssault(UsrAI *ai, int anchorDR, int anchorUR)
{
    // ---- 没有突击者：站桩僵住够久了就选一个 ----
    if (assaultSN == -1)
    {
        if (standoffEngagedSince == USR_INVALID_FRAME ||
            g_frame - standoffEngagedSince < USR_ASSAULT_IDLE_FRAMES)
            return;  // 还没僵住，老实站桩

        int bestDis2 = 1000000000;
        const tagArmy *best = nullptr;
        for (const tagArmy &army : info.armies)
        {
            if (!IsOffensiveArmy(army) || army.NowState != HUMAN_STATE_IDLE)
                continue;  // 只要空闲的：正在交战的别抽调
            const int dis2 =
                BlockDis2(army.BlockDR, army.BlockUR, anchorDR, anchorUR);
            if (dis2 < bestDis2)
            {
                bestDis2 = dis2;
                best = &army;
            }
        }
        if (best == nullptr)
            return;
        assaultSN = best->SN;
        assaultStartDR = best->BlockDR;
        assaultStartUR = best->BlockUR;
        assaultRetreating = false;
        return;
    }

    // ---- 找到突击者本体 ----
    const tagArmy *assault = nullptr;
    for (const tagArmy &army : info.armies)
    {
        if (army.SN == assaultSN)
        {
            assault = &army;
            break;
        }
    }
    if (assault == nullptr)
    {
        // 死了（或回执里消失了）→ 取消，下次僵住时重新选一个
        assaultSN = -1;
        assaultRetreating = false;
        return;
    }
    // 只驱动【空闲】的突击者。正在打或正在走的都别插指令 ——
    // 否则 HumanMove 会经 suspendRelation 打断它刚建立的攻击关系，
    // 变成「前进 → 被打断 → 再前进」的死循环（这个坑站桩那边踩过一次）。
    if (assault->NowState != HUMAN_STATE_IDLE)
        return;

    // ---- 威胁判定：掉过血，或 8 格内有敌人 ----
    bool threat = assault->Blood < assault->MaxBlood;
    if (!threat)
    {
        for (const tagArmy &enemy : info.enemy_armies)
        {
            if (enemy.Blood <= 0)
                continue;
            if (BlockDis2(assault->BlockDR, assault->BlockUR, enemy.BlockDR,
                          enemy.BlockUR) <= USR_ASSAULT_TRIGGER_DIS2)
            {
                threat = true;
                break;
            }
        }
    }

    if (!threat)
    {
        // 安全 → 朝敌方基地前进，把守军勾出来
        MoveTowardBlock(ai, assault->SN, assault->BlockDR, assault->BlockUR,
                        anchorDR, anchorUR, USR_ASSAULT_STEP);
        return;
    }

    // 有威胁 → 退回起点，把追兵带到 32 格环上的火力网里
    assaultRetreating = true;
    const int backDis2 = BlockDis2(assault->BlockDR, assault->BlockUR,
                                   assaultStartDR, assaultStartUR);
    if (backDis2 <= 4)  // 回到起点 2 格内 → 收工
    {
        assaultSN = -1;
        assaultRetreating = false;
        return;
    }
    MoveTowardBlock(ai, assault->SN, assault->BlockDR, assault->BlockUR,
                    assaultStartDR, assaultStartUR, USR_ASSAULT_STEP);
}

// 这个单位是不是正在被攻击（站桩圈的「被打到」判据）。
//
// 复用 FindDirectThreatToArmySN：某敌人的 WorkObjectSN 指向它，或敌箭塔把它
// 设成 Project 且在 USR_FIELD_ASSIST_RADIUS 内。也就是【被锁定 / 被打】，
// 而不是严格的「这一帧血量掉了」。
//
// 【为什么不用掉血判据】tagArmy 快照里没有逐单位的上一帧血量，要另加一张
// map 才能做（参考文件对祭司用的就是这个办法）。而站桩这一步每 60 帧才跑
// 一趟，"被锁定"已经够用 —— 真正的上行边界是 32 格环：退出去就不再触发。
static bool IsArmyBeingAttacked(const tagArmy &army)
{
    return FindDirectThreatToArmySN(army.SN) != -1;
}

// 站桩阶段：返回 true 表示本帧已处理（部队正在圈上待机，不要再去打建筑）。
static bool ManageStandoff(UsrAI *ai)
{
    // 【锚点改成估算】原先是 `siege = FindEnemySiegeBuilding(); if (siege == nullptr)
    // return false;` —— 实测攻城厂整局没被侦察到，这条规则从来没生效过。
    // 站位只需要一个坐标，估算的够用（依据与误差见 EstimateEnemySiegeAnchor）。
    int anchorDR = 0;
    int anchorUR = 0;
    if (!EstimateEnemySiegeAnchor(anchorDR, anchorUR))
        return false; // 连市镇中心都没有（开局头几帧），退回原来的进攻逻辑

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
    {
        // 站桩阶段结束（视野里没敌人了）→ 复位僵住计时与突击者。
        // 【为什么必须复位】僵住计时是用来判「守军不出来」的，下一次站桩是全新的
        // 一轮，不该继承上一轮的时长；突击者也一样（它可能已经死在半路）。
        standoffEngagedSince = USR_INVALID_FRAME;
        assaultSN = -1;
        assaultRetreating = false;
        return false;
    }
    // 站桩开始计时（首次进入时记下，之后不动）。
    if (standoffEngagedSince == USR_INVALID_FRAME)
        standoffEngagedSince = g_frame;

    const int orderInterval = 60;
    if (g_frame - offensiveLastOrderFrame < orderInterval)
        return true; // 还在节流窗口里，但阶段判定已经做完
    offensiveLastOrderFrame = g_frame;

    // 【突击者】站桩僵住够久就派一个去勾守军出来（见 ManageStandoffAssault）。
    // 放在节流之后：它内部按每次走 3 格设计，不需要每帧驱动。
    ManageStandoffAssault(ai, anchorDR, anchorUR);

    int slot = 0;
    for (const tagArmy &army : info.armies)
    {
        if (!IsOffensiveArmy(army))
            continue;

        // 【突击者不参与环上站位】它现在归 ManageStandoffAssault 管 ——
        // 不跳过的话，站桩每 60 帧会把它拽回 32 格环，而突击者每 60 帧又要它
        // 前进 3 格，两条指令互相抵消，等于没派。
        if (army.SN == assaultSN)
            continue;

        // 清掉进攻锁：否则 AssignFieldDefense 的第 2 优先级会捧着旧锁
        // （可能是上一阶段留下的攻城武器厂），把部队带去冲建筑而不是守圈。
        // 清掉之后它会落到本地分支，只打视野/协防半径内的敌人 ——
        // 也就是「站圈只打送上门的」。
        ClearArmyTargetLock(army.SN);

        // 【有敌人就退到 32 格环上 —— 圈内的退出去、掉队的靠回来，交战中也搬】
        //
        // 【为什么现在连交战单位也搬】旧实现在这里有一句
        //     if (army.WorkObjectSN != -1) continue;    // 交战中不动它
        // 于是部队一旦开打就再也撤不出来。实测上一局：我军压到敌方基地外围，
        // 对面 32 个守军（[AI] f=40000 enemyA=32）扑上来，我方 23 个兵全程处于
        // 交战状态、每一帧都在这一行被跳过 —— 站桩规则形同不存在，2000 帧内
        // 被打光（carcher 16→0），家里同时被抄（farmers 9→0，pop 37→1）。
        //
        // 【那条老注释担心的死循环，真正的成因不是「打断了攻击」】
        // 它写的是「站桩 Move → 自卫重新 Attack → 站桩发现它不在圈上、又 Move」。
        // 但这个环能闭上，是因为旧实现还负责【把圈外的单位拉回环上】—— 同一次
        // Move 既是「退出」又是「进入」，自卫刚建好的关系立刻被打断。
        //
        // 【当时的修法是「只退不进」，但它错在两处】
        //   ① 它写道「推出去之后它就在圈外，下一帧不会再被这条规则碰到 ——
        //      环没有闭合点」。这个推理只约束了站桩自己，漏掉了自卫。自卫不是
        //      「只退不进」：敌方守军追到 DEFENSE_CHASE_LIMIT(25) 格处停下时，
        //      离环上的我们正好 7 格 —— 早就在自卫的主动索敌半径
        //      （USR_FIELD_ARMY_AGGRO_RADIUS）以内（7 格时相等，后来提到 12 更宽），
        //      几何上必然踩线，自卫每 12 帧就把部队拉回圈里一次，环照旧闭着。
        //      退出去的那一程也因此走不完（见下面进度判据的说明）。
        //   ② 圈外一律不管，等于放弃了「半路被截停」的部队。推进指令是全有或
        //      全无的（ManageOffensiveArmy 一见视野里有敌人就 return），
        //      那些兵卡在 40~70 格处拿到零指令、永久站着不动。
        // 【现在的规则是三档】圈内挨打后退 / 死区交还自卫 / 掉队靠拢。
        // ① 那个抖动最终是【把位置阈值换成挨打驱动】解决的 —— 没有「一进阈值
        // 就被推」这个每 60 帧的固定对撞，方向相反的两条规则就不再打架。
        // 详见下面圈内那档的说明。
        //
        // 【代价】撤退发的 HumanMove 会打断该单位当前的攻击关系（经
        // suspendRelation）。这是有意的：撤退就是脱离接触，松手之后由
        // AssignFieldDefense 重新选目标。所以要清锁（上面那句）。
        const int anchorDis2 = BlockDis2(army.BlockDR, army.BlockUR,
                                         anchorDR, anchorUR);
        const int ringDis2 = USR_STANDOFF_RADIUS * USR_STANDOFF_RADIUS;
        const int joinRadius = USR_STANDOFF_RADIUS + USR_STANDOFF_JOIN_BAND;
        const int joinDis2 = joinRadius * joinRadius;
        // 三档：圈内(≤32) 挨打后退两格；死区(32,44] 交还自卫权；掉队(>44) 向环靠拢。
        // 【为什么圈外那一档不能一律不管】见 USR_STANDOFF_JOIN_BAND 的说明：
        // 推进指令是全有或全无的，半路被截停的部队会卡在圈外，两边都不管。
        const bool joinRing = (anchorDis2 > joinDis2);
        if (!joinRing && anchorDis2 > ringDis2)
        {
            // 死区：不搬运，立刻解除封口、把自卫权还回去。
            // 这一步保证「不还手」的时间不会超过实际走出/走进圈所需的时长。
            standoffRetreatUntil.erase(army.SN);
            standoffRetreatFromDis2.erase(army.SN);
            continue;
        }

        if (!joinRing)
        {
            // ============ 圈内（离锚点 ≤ 32 格）：被打到就后退两格 ============
            //
            // 【规则】不再按位置阈值推出去，改成【由挨打驱动、每次只退两格】。
            //
            // 【为什么换掉原规则】原来这里写的是「只要在 32 格内就推到环上的
            // 槽位」。它和自卫是对撞的：那是一个硬位置阈值，单位一进 32 格就
            // 被推一次（每 60 帧），而自卫每 12 帧把它拉回来一次 —— 敌方守军
            // 追到 DEFENSE_CHASE_LIMIT(25) 格处停下时，离环上的我们正好 7 格，
            // 在自卫的主动索敌半径 USR_FIELD_ARMY_AGGRO_RADIUS 以内
            // （7 格时相等，后来提到 12 更宽），几何上必然踩线。
            // 方向相反 + 频率差 5 倍，部队就在 25~32 之间来回走，退不完。
            //
            // 【为什么挨打驱动就没有这个问题】没有位置阈值，就没有「一进阈值
            // 就被推」这个每 60 帧的固定对撞；而退到 32 格之外就自然停手
            // （本分支只在 ≤32 格内生效）—— 环仍然是终点，只是不再靠硬阈值
            // 驱赶，改成挨一次打退一步、逐步退出去。
            //
            // 【为什么这一档不需要像靠拢那样封 200 帧】它只在自己正被攻击时
            // 触发，而被攻击的单位通常也在还手（WorkObjectSN 指向那个敌人），
            // 自卫因此不会对它下发指令（看那里 `WorkObjectSN != targetSN` 的
            // 条件）—— 本就不会被覆盖。所以这里只封 45 帧当保险，且它比 60 帧
            // 的站桩节流短，不会和上面那套封口窗口的分析互相干扰。
            //
            // 【"被打到"的判据复用 FindDirectThreatToArmySN】它的定义是「某敌人
            // 的 WorkObjectSN 指向我」+「敌箭塔把我设成 Project 且在 12 格内」，
            // 也就是【被锁定/被打】。没有再加距离上限 —— 敌人在远处锁定我们时
            // 它正在冲我们来，此时后退是合理的；真正的上行边界由 32 格环提供。
            if (!IsArmyBeingAttacked(army))
                continue;   // 没被打 → 原地待命，接敌交给自卫

            int stepDR = 0;
            int stepUR = 0;
            if (army.BlockDR > anchorDR)
                stepDR = USR_STANDOFF_STEP_BLOCKS;
            else if (army.BlockDR < anchorDR)
                stepDR = -USR_STANDOFF_STEP_BLOCKS;
            if (army.BlockUR > anchorUR)
                stepUR = USR_STANDOFF_STEP_BLOCKS;
            else if (army.BlockUR < anchorUR)
                stepUR = -USR_STANDOFF_STEP_BLOCKS;
            if (stepDR == 0 && stepUR == 0)
                stepUR = USR_STANDOFF_STEP_BLOCKS;  // 与锚点重合，给一个确定方向

            int tx = max(1, min(MAP_L - 2, army.BlockDR + stepDR));
            int ty = max(1, min(MAP_U - 2, army.BlockUR + stepUR));
            if (tx == army.BlockDR && ty == army.BlockUR)
                continue;   // 被地图边界夹回原地，这一步退不动

            standoffRetreatUntil[army.SN] =
                g_frame + USR_STANDOFF_STEP_SEAL_FRAMES;
            // 清掉靠拢用的进度基准：这一档不用它，留着的话下次真去靠拢时
            // 第一次 stalled 比较会拿一个早已过期的基准，误判成"走不动"。
            standoffRetreatFromDis2.erase(army.SN);
            ai->HumanMove(army.SN, (tx + 0.5) * double(BLOCKSIDELENGTH),
                          (ty + 0.5) * double(BLOCKSIDELENGTH));
            continue;
        }

        // ============ 圈外掉队（离锚点 > 44 格）：向环上槽位靠拢 ============
        //
        // 【站桩优先于自卫：靠拢执行期间把这个单位封给自己】
        //
        // 站桩每 60 帧才发一次「走向槽位」，而 AssignFieldDefense 每 12 帧就发
        // 一次「打最近的敌人」（USR_FIELD_SELF_DEFENSE_ORDER_INTERVAL），且
        // 自卫在 processData 里【后执行】—— 频率差 5 倍 + 后执行，站桩的移动
        // 指令每次都被覆盖，净效果就是原地不动。
        //
        // 所以窗口内：(i) 不重发（避免反复清空路径）；(ii) 由 AssignFieldDefense
        // 主动跳过它。走进死区或到达槽位时上面/下面那句 erase 会解封。
        map<int, int>::iterator retreatIt = standoffRetreatUntil.find(army.SN);
        if (retreatIt != standoffRetreatUntil.end() &&
            g_frame < retreatIt->second)
        {
            // 【窗口内的三个出口】① 走进死区（32~44）→ 由上面那句 erase 解封；
            //                     ② 走到槽位 → 由下面那句解封；
            //                     ③ 走不动 → 在这里解封。
            //
            // 【为什么必须有 ③】解封条件原来只有「走出 32 格」。走不出去的情况
            // （槽位被别的单位占了 / 路径被挡 / 目的地在障碍里）就会变成
            //    封 200 帧 → 到期 → 进圈内又发一次撤退 → 再封 200 帧 → …
            // 单位全程既不动也不还手，实测表现为战车弓兵集体站着不动。
            //
            // 【判据】站桩循环每 60 帧才跑一次（orderInterval），所以这次比较
            // 跨度正好是 60 帧 —— 期间单位本该走 4~5 格。没在朝目标前进就说明
            // 它这一步根本走不动，再封下去只是继续站着。
            //
            // 【前进方向按档位取反】撤退是往环外走（离锚点越来越远），靠拢是往
            // 环上走（越来越近），两者的「前进」在 anchorDis2 上是相反的符号。
            // 基准值统一存「下发指令那一刻的 anchorDis2」（见下面赋值处）。
            //
            // 【原先是写死的 anchorDis2 >= 基准】那个比较对撤退来说是反的：
            // 撤退成功时 anchorDis2 变大，`>=` 成立 → 立刻解封 → 自卫马上把它
            // 拉回圈里，撤退等于只走了一程（约 60 帧）就被撤销。改成方向感知后，
            // 成功前进的单位会一直封到走出圈外，撤退才真正走得完。
            map<int, int>::const_iterator fromIt =
                standoffRetreatFromDis2.find(army.SN);
            // stalled：有基准、且这一程没有朝目标前进。
            // 没基准（第一次进这个窗口）不算 stalled，由下面那句建立基准。
            const bool stalled =
                fromIt != standoffRetreatFromDis2.end() &&
                (joinRing ? (anchorDis2 >= fromIt->second)
                          : (anchorDis2 <= fromIt->second));
            if (stalled)
            {
                standoffRetreatUntil.erase(retreatIt);
                standoffRetreatFromDis2.erase(army.SN);
            }
            else
            {
                standoffRetreatFromDis2[army.SN] = anchorDis2;  // 首次建立 / 刷新基准
            }
            continue;
        }

        // 【原先这里有一条「20~32 之间不动」的滞回带，已删除】
        //
        // 它当时的理由是：把 25 格内的单位也推出去，会出现「推出 32 → 自卫又把它
        // 追回 32 内 → 再推出去」的来回抖。但那个抖动现在由 USR_STANDOFF_RETREAT_FRAMES
        // 的封口窗口解决了（撤退期间该单位不接自卫指令），滞回带不再必要。
        //
        // 而它有害：按规则「有敌人就退回到 32 格」，20~32 之间的单位本来就该被推
        // 出去，留在带子里等于把那一段完全交给自卫逻辑，它们会一路被拉到敌方基地
        // 脚下 —— 实测部队就是这样被摁在基地旁边退不出来的。
        //
        // 【无谓的搬动由下面「已站在槽位 3 格内就不发」那条挡掉】它天然形成了一个
        // 3 格的死区，比按半径划线更贴合「已经在环上就别折腾」。

        // 目标槽位（半径正好 USR_STANDOFF_RADIUS）：圈内单位来这里「退出」，
        // 掉队单位也来这里「靠拢」—— 两者目标相同，都是站上环。
        // 每个单位一个槽位（按它在 info.armies 里的顺序对 USR_STANDOFF_SLOTS 取模），
        // 保证散开；错开方向避免所有人都挤在正对家门口的那一格。
        // 两档共用同一条游标：槽位只是环上的点，圈内与圈外的单位站位不冲突。
        int ddr = 0;
        int dur = 0;
        StandoffRingOffset(slot++, ddr, dur);
        int tx = anchorDR + ddr;
        int ty = anchorUR + dur;
        tx = max(1, min(MAP_L - 2, tx));
        ty = max(1, min(MAP_U - 2, ty));

        // 已经站在自己的槽位附近（3 格内）就不必重发 —— HumanMove 每次都会
        // 清空路径，反复下发会让部队原地打转，反而走不到。
        if (BlockDis2(army.BlockDR, army.BlockUR, tx, ty) <= 9)
        {
            // 靠拢任务完成 → 交还自卫权。不能等窗口自然到期：那会让归队的
            // 单位在环上白站最多 200 帧，连祭司被打都不会回救。
            // （走到这里必然是靠拢档，圈内那档在上面已经 continue 掉了。）
            standoffRetreatUntil.erase(army.SN);
            standoffRetreatFromDis2.erase(army.SN);
            continue;
        }
        // 发指令 + 开/续封口窗口：接下来 USR_STANDOFF_RETREAT_FRAMES 帧内，
        // 这个单位不接自卫指令、也不会被站桩重发 —— 让这次移动真正走完。
        // 窗口若在本帧已经过期，就会在这里被立刻续上，中间没有空档。
        standoffRetreatUntil[army.SN] = g_frame + USR_STANDOFF_RETREAT_FRAMES;
        standoffRetreatFromDis2[army.SN] = anchorDis2;   // 进度基准：出发时的距离
        ai->HumanMove(army.SN, (tx + 0.5) * double(BLOCKSIDELENGTH),
                      (ty + 0.5) * double(BLOCKSIDELENGTH));
    }
    return true;
}

static void UpdateEnemyBaseDiscovery()
{
    // 侦察骑兵探路后即可发现敌方基地；进攻时机由科技完成度控制。
    //
    // 【为什么不能取「列表里第一个活着的建筑」】info.enemy_buildings 每帧都被
    // GlobalVariate 的 WLHHunYao 洗牌（GlobalVariate.cpp:1086），「第一个」每帧
    // 都在变。本函数又每帧被 ManageOffensiveArmy 调用一次，enemyBaseSN 于是每帧
    // 被改写成一座【随机的】敌方建筑 —— 而 FindOffensiveTargetSN() 的第 ② 条正是
    // 拿它当进攻目标。实测表现：场上还有箭塔，全军却在几座塔之间每 60 帧换一次
    // 目标，每次换目标都重建攻击关系、把已打出的伤害进度清零
    // （见 :7976 的警告），于是哪座塔都拆不掉。
    //
    // 改成按【最小的 SN】取：SN 是稳定唯一键，与洗牌顺序无关，每帧结果一致。
    // 「任意存活敌方建筑都算发现敌方基地」这条口径不变，仍然不要求是市镇中心。
    int pickSN = -1;
    int pickDR = 0;
    int pickUR = 0;
    for (const tagBuilding &building : info.enemy_buildings)
    {
        if (building.Blood <= 0)
            continue;
        if (pickSN == -1 || building.SN < pickSN)
        {
            pickSN = building.SN;
            pickDR = building.BlockDR;
            pickUR = building.BlockUR;
        }
    }
    if (pickSN == -1)
        return;   // 视野里没有敌方建筑，保持上一次的记录
    enemyBaseDiscovered = true;
    enemyBaseSN = pickSN;
    enemyBaseBlockDR = pickDR;
    enemyBaseBlockUR = pickUR;
    enemyBaseLastSeenFrame = g_frame;
}

// 进攻目标
// SN：优先敌方箭塔（逐座集中拆除），其次是市镇中心；基地摧毁后转为攻城武器厂
// 附近的防守兵（护送祭司）。
static int FindOffensiveTargetSN() {
  // 0. 【最高优先级】敌方箭塔 —— 视野里没有敌兵时，全军集中拆【一座】，
  //    且优先拆【最外围】的那一座（由外向内逐座清）。
  //
  // 【为什么必须排在市镇中心之前】本 AI 的获胜路径是祭司转化敌方攻城武器厂，而
  // CanPriestApproachSiege() 的第 ④ 条要求「一座活着的敌方箭塔都没有」
  // （SiegeTowersCleared：被牵制的塔照样开火，祭司 100 血 0 防扛不住几轮）。
  // 只要还剩一座已知的活塔，祭司就永远不出门 —— 拆塔是获胜的必经之路。
  // 实测这局敌方【没有市镇中心】，只有箭塔与攻城武器厂，所以原先排在第 ① 条的
  // 「市镇中心」根本不成立，全军会落到第 ② 条 —— 而那条正是下面这个 bug 的来源。
  //
  // 【为什么是「最外围」而不是「最靠近基地」】按需求：先清最外围，由外向内推。
  // 反过来（先拆贴着基地的那座）要先穿过外围塔的火力再打，且外围塔会从背后继续
  // 打我们；从外往里推则每一步都在自己刚清干净的地面上。
  //
  // 【为什么不能靠列表顺序挑一座】info.enemy_buildings 每帧都被 GlobalVariate 的
  // WLHHunYao 洗牌（GlobalVariate.cpp:1086），「第一个匹配的」每帧都在变。全军
  // 目标一跳变就要 ClearArmyTargetLock + 重发 HumanAction，而重发会【重建攻击
  // 关系、把已经打出去的伤害进度清零】（见 :7976 与 :9256 两处警告）—— 表现就是
  // 「场上还有箭塔、军队却在几座塔之间来回走，一座都拆不掉」。
  //
  // 【判据：取「离地图边缘最远」的那座塔，不需要任何圆心/锚点估算】
  //
  // 依据是这四张图的地形：敌方基地都贴着我方对角的那条地图边（实测敌方基地离
  // 我方中心曼哈顿 122~140 格，见 EstimateEnemySiegeAnchor 的实测记录）。所以在
  // 敌方那一片塔里，「离最近的地图边最远」＝「离我方最近」＝朝我方的那座外围塔。
  //
  // 【为什么比之前两版都稳】
  //   · 不需要 EstimateEnemySiegeAnchor —— 它在真攻城厂尚未被侦察到时返回的是
  //     「SN 最小的那座敌建筑」（见 UpdateEnemyBaseDiscovery），那个位置本身可能
  //     就是一座【外围】箭塔，拿它当圆心会把「最外围」判到基地对面去；
  //   · 不需要先算敌方建筑的质心；
  //   · 只用到塔自己的坐标与地图尺寸，与 info.enemy_buildings 的遍历顺序无关 ——
  //     正合上面那条「不能依赖洗牌」（GlobalVariate.cpp:1086）的硬要求；
  //   · 没有任何「估算失败就退化」的路径。
  //
  // 【假设与它的边界】这条判据依赖「敌方基地区域比任何别处都更靠近同一条地图边」。
  // 若将来出现一张图把敌方基地放在靠近地图中心的位置，这里会取到【最靠里】的塔，
  // 顺序就反了 —— 那时改用「以已知敌方建筑的质心为圆心、取最远者」的写法。
  //
  // 【判据细节】到最近那条边的距离 = 四边距离取最小；离边缘越远者胜，同距取 SN
  // 小的 —— 与洗牌无关，确定。一座拆掉（Blood<=0）后自动落到下一座，天然形成
  // 「由外向内逐座集中拆除」。
  {
    int towerBestSN = -1;
    int towerBestEdgeDis = -1;
    for (const tagBuilding &building : info.enemy_buildings) {
      if (building.Blood <= 0 || building.Type != BUILDING_ARROWTOWER)
        continue;
      const int toLeft = building.BlockDR;
      const int toRight = MAP_L - 1 - building.BlockDR;
      const int toTop = building.BlockUR;
      const int toBottom = MAP_U - 1 - building.BlockUR;
      const int edgeDis = min(min(toLeft, toRight), min(toTop, toBottom));
      // 离边缘越远者胜（`>`）；同距取 SN 小的那个。
      if (towerBestSN == -1 || edgeDis > towerBestEdgeDis ||
          (edgeDis == towerBestEdgeDis && building.SN < towerBestSN)) {
        towerBestSN = building.SN;
        towerBestEdgeDis = edgeDis;
      }
    }
    if (towerBestSN != -1)
      return towerBestSN;
  }
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

// 远程兵种判定：config.json 里 DIS_* > 0 的那些 ——
//   SLINGER 4 / BOWMAN 5 / IMPROVED 7 / COMPOSITE_BOWMAN 7 /
//   CHARIOT_ARCHER 7 / STONE_THROWER 10。
// 本 AI 实际会产的是普通弓兵与战车弓兵。
static bool IsRangedUnitSort(int sort)
{
    return sort == AT_SLINGER || sort == AT_BOWMAN || sort == AT_IMPROVED ||
           sort == AT_COMPOSITE_BOWMAN || sort == AT_CHARIOT_ARCHER ||
           sort == AT_STONE_THROWER;
}

// 敌方近战兵判定：DIS_* == 0 的那些。SCOUT 也是 0，但它不参战，排除。
static bool IsMeleeAttackerSort(int sort)
{
    return sort == AT_CLUBMAN || sort == AT_SWORDSMAN || sort == AT_HOPLITE ||
           sort == AT_CHARIOT || sort == AT_CAVALRY ||
           sort == AT_BROADSWORDSMAN;
}

// 【攻打敌方基地时：有远程兵被近战贴上 → 全体远程兵一起后撤两格】
//
// 【为什么是「全体」而不是只动挨打的那个】远程兵的价值在于拉开距离集体输出。
// 只把一个单位往后拽，它会孤零零站在后面（离开其他人的火力掩护），而追它的
// 那个近战兵跟着它一个人跑，其余远程兵照样被贴。整条线一起退两格，才能让
// 追兵落在所有人射程之外、重新挨打。
//
// 【触发条件】「被近战打到」= 某个敌方近战兵的 WorkObjectSN 指向我方某个远程兵，
// 且它就在两格内（USR_MELEE_KITE_TRIGGER_DIS2）。有工作关系只说明它锁了目标，
// 还要真的贴上来才算打到。只看接触不看血量 —— 要趁接触的第一时间退，
// 不是等血掉下去。
//
// 【为什么必须排在 ManageStandoff 之前】站桩那段会把圈内的单位往 28 格环上搬，
// 而它一旦接管就直接 return。挨打撤退是更紧迫的动作，排在它后面就永远执行不到。
//
// 【没有「只在敌基地附近」的判断】调用点已经在 ManageOffensiveArmy 的几道门
// 之后（28000 帧 / 已升时代 / 车轮科技 / 已侦察到敌方建筑），
// 而那些门就是「正在攻打敌方基地」的定义 —— 不必在这里再写一遍。
// （原先这里还列了一条「兵力 ≥8」，该门控已按需求停用，见 ManageOffensiveArmy
//   里那段说明。）
static void KiteRangedBackFromMelee(UsrAI *ai)
{
    if (g_frame - lastKiteFrame < USR_MELEE_KITE_INTERVAL)
        return;

    // ① 找出「正在被近战兵打的远程兵」，顺便记下那个近战兵的位置当参照点。
    bool pressed = false;
    int refDR = 0;
    int refUR = 0;
    for (const tagArmy &unit : info.armies)
    {
        if (unit.Blood <= 0 || !IsRangedUnitSort(unit.Sort))
            continue;
        for (const tagArmy &enemy : info.enemy_armies)
        {
            if (enemy.Blood <= 0 || !IsMeleeAttackerSort(enemy.Sort))
                continue;
            if (enemy.WorkObjectSN != unit.SN)
                continue;
            if (BlockDis2(unit.BlockDR, unit.BlockUR, enemy.BlockDR,
                          enemy.BlockUR) > USR_MELEE_KITE_TRIGGER_DIS2)
                continue;
            pressed = true;
            refDR = enemy.BlockDR;
            refUR = enemy.BlockUR;
            break;
        }
        if (pressed)
            break;
    }
    if (!pressed)
        return;   // 没人被近战贴上 → 什么都不做

    // ② 全体远程兵各退两格。
    // 方向：(i) 优先背离【自己最近的那个近战兵】；(ii) 自己身边没有近战兵时，
    // 背离 ① 找到的参照点。这样左右两翼不会朝相反方向乱跑。
    lastKiteFrame = g_frame;
    for (const tagArmy &unit : info.armies)
    {
        if (unit.Blood <= 0 || !IsRangedUnitSort(unit.Sort))
            continue;

        int anchorDR = refDR;
        int anchorUR = refUR;
        int bestDis2 = USR_MELEE_KITE_TRIGGER_DIS2;
        for (const tagArmy &enemy : info.enemy_armies)
        {
            if (enemy.Blood <= 0 || !IsMeleeAttackerSort(enemy.Sort))
                continue;
            const int dis2 = BlockDis2(unit.BlockDR, unit.BlockUR,
                                       enemy.BlockDR, enemy.BlockUR);
            if (dis2 <= bestDis2)
            {
                bestDis2 = dis2;
                anchorDR = enemy.BlockDR;
                anchorUR = enemy.BlockUR;
            }
        }

        // 【退到站桩环上：离敌方锚点 USR_STANDOFF_RADIUS(32) 格】
        //
        // 原来只退 USR_MELEE_KITE_BLOCKS(2) 格 —— 而敌方骑兵 / 战车与战车弓兵
        // 的移速【完全相同】（config.json 里都是 4.0656）：退 2 格、它追 2 格，
        // 距离纹丝不动，只是每 40 帧白挨一轮打，最后被磨死。
        //
        // 站桩环是现成的安全位置：敌方守军的追击上限是 25 格（量到它们的
        // 攻城厂，DEFENSE_CHASE_LIMIT），退到 32 格至少拉开 7 格 —— 而 7 格
        // 正好是战车弓兵的射程，追兵停在 25 格线上时我们还够得着。
        int tx = -1;
        int ty = -1;
        {
            int siegeDR = 0;
            int siegeUR = 0;
            if (EstimateEnemySiegeAnchor(siegeDR, siegeUR))
            {
                // 沿「锚点 → 单位」这条射线取 32 格处的点：沿它自己那条射线
                // 往外退，不会横穿战场，左右两翼也不会朝相反方向跑。
                const double dx = double(unit.BlockDR - siegeDR);
                const double dy = double(unit.BlockUR - siegeUR);
                const double len = sqrt(dx * dx + dy * dy);
                if (len >= 0.5)
                {
                    tx = siegeDR +
                         int(dx / len * USR_MELEE_KITE_RETREAT_DISTANCE);
                    ty = siegeUR +
                         int(dy / len * USR_MELEE_KITE_RETREAT_DISTANCE);
                }
            }
        }
        if (tx < 0 || ty < 0)
        {
            // 拿不到敌方锚点（或单位正好压在锚点上、算不出方位）→ 退回原来的
            // 「背离最近的那个近战兵、每轴退 USR_MELEE_KITE_BLOCKS 格」。
            const int dx = unit.BlockDR - anchorDR;
            const int dy = unit.BlockUR - anchorUR;
            int sx = (dx > 0) ? USR_MELEE_KITE_BLOCKS
                              : ((dx < 0) ? -USR_MELEE_KITE_BLOCKS : 0);
            int sy = (dy > 0) ? USR_MELEE_KITE_BLOCKS
                              : ((dy < 0) ? -USR_MELEE_KITE_BLOCKS : 0);
            if (sx == 0 && sy == 0)
                sy = USR_MELEE_KITE_BLOCKS;   // 与参照点完全重合，给一个确定方向
            tx = unit.BlockDR + sx;
            ty = unit.BlockUR + sy;
        }
        tx = max(1, min(MAP_L - 2, tx));
        ty = max(1, min(MAP_U - 2, ty));
        // 清掉进攻锁：否则 AssignFieldDefense 会捧着旧锁把刚退开的单位又送回去。
        ClearArmyTargetLock(unit.SN);
        ai->HumanMove(unit.SN, (tx + 0.5) * double(BLOCKSIDELENGTH),
                      (ty + 0.5) * double(BLOCKSIDELENGTH));
    }
}

// 给一支部队挑一座敌方箭塔去牵制。
//
// 【为什么要「先认已有的锁」】重复对同一目标下 HumanAction 会中止并重建关系、
// 把已经打出去的伤害进度清零（AssignArrowTowerTargets 的同一处注释）。
// 而本函数每帧都会被调用（外层有 60 帧节流），如果每次都按「离谁近」重算，
// 单位一边移动一边换目标，伤害永远攒不起来。所以第一优先是沿用已有的攻击锁。
//
// 【为什么「都满员了还要挑最近的」】宁可超员排队，也不能让士兵因为
// 「塔都满了」而站着不动 —— 排队的那些会在外围等着，塔一没就补上。
static int PickEscortTowerForArmy(const tagArmy &army,
                                  const vector<const tagBuilding *> &towers,
                                  map<int, int> &towerWorkers, int perTargetMax)
{
    // ① 已经锁着一座还活着的塔、且它还没满 → 继续打它
    const int locked = GetLockedArmyTarget(army.SN);
    if (locked != -1)
    {
        for (size_t t = 0; t < towers.size(); ++t)
        {
            if (towers[t]->SN == locked &&
                towerWorkers[locked] < perTargetMax)
            {
                towerWorkers[locked]++;
                return locked;
            }
        }
    }

    // ② 挑离自己最近、还有名额的那座
    int bestSN = -1;
    int bestDis2 = 1000000000;
    for (size_t t = 0; t < towers.size(); ++t)
    {
        const int sn = towers[t]->SN;
        if (towerWorkers[sn] >= perTargetMax)
            continue;
        const int d2 = BlockDis2(army.BlockDR, army.BlockUR,
                                 towers[t]->BlockDR, towers[t]->BlockUR);
        if (d2 < bestDis2)
        {
            bestDis2 = d2;
            bestSN = sn;
        }
    }
    if (bestSN != -1)
    {
        towerWorkers[bestSN]++;
        return bestSN;
    }

    // ③ 全满员 → 仍然挑最近的那座（超员排队，不让人闲着）
    for (size_t t = 0; t < towers.size(); ++t)
    {
        const int d2 = BlockDis2(army.BlockDR, army.BlockUR,
                                 towers[t]->BlockDR, towers[t]->BlockUR);
        if (d2 < bestDis2)
        {
            bestDis2 = d2;
            bestSN = towers[t]->SN;
        }
    }
    if (bestSN != -1)
        towerWorkers[bestSN]++;
    return bestSN;
}

static void ManageOffensiveArmy(UsrAI *ai)
{
  // 【集结阶段：USR_ARMY_RALLY_FRAME(25000) 之后，先全军到地图中间收拢】
  //
  // 地图中心是双方基地之间的中点（四张图的敌方基地都在我方市镇中心的对角线上，
  // 见 EstimateEnemySiegeAnchor 的实测数据）。先在那儿把队伍收齐，等
  // USR_OFFENSIVE_FRAME(28000) 再整体压上去 —— 部队是零散生产出来的、平时散在
  // 基地各处，直接出发会拉成一条长线，先到的被守军逐个吃掉。
  //
  // 【为什么放在这里、不放进下面的进攻分支】下面第一行就是
  // `if (g_frame < USR_OFFENSIVE_FRAME) return;`，集结必须发生在它之前。
  // 同时也排在那些兵力/科技门控之前：集结只是把人往中间挪，不需要满足
  // 「车轮科技完成」「兵力 ≥ 8」这些出击条件 —— 早点开始走，到点正好能压上。
  if (g_frame >= USR_ARMY_RALLY_FRAME && g_frame < USR_OFFENSIVE_FRAME)
  {
    if (lastArmyRallyFrame == USR_INVALID_FRAME ||
        g_frame - lastArmyRallyFrame >= USR_ARMY_RALLY_ORDER_INTERVAL)
    {
      lastArmyRallyFrame = g_frame;
      const int cx = MAP_L / 2;
      const int cy = MAP_U / 2;
      for (const tagArmy &army : info.armies)
      {
        if (!IsOffensiveArmy(army))
          continue;   // 祭司与侦察兵各有自己的调度
        // 已经在中心附近 → 不动（重复下发 HumanMove 会清路径）
        if (BlockDis2(army.BlockDR, army.BlockUR, cx, cy) <=
            USR_ARMY_RALLY_ARRIVED_DIS2)
          continue;
        ai->HumanMove(army.SN, (cx + 0.5) * double(BLOCKSIDELENGTH),
                      (cy + 0.5) * double(BLOCKSIDELENGTH));
      }
    }
    return;
  }

  // 进攻时机：过了 USR_OFFENSIVE_FRAME（28000）之后。此前军队只做接敌自卫
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

  // 【已停用：兵力门控「不足 8 个不进攻」（2026-09-28 按需求停用）】
  //
  // 原意是「兵力不足时不进攻，避免送死」。停用后兵力再少也允许压上去。
  //
  // 【为什么连计数一起注释掉】只注释那个 if 的话，offensiveArmyCount 就成了
  // 「设了但没读」的局部变量，触发 -Wunused-but-set-variable —— 所以整段一起
  // 注释。它没有被任何其它地方引用（日志用的是 [AI] 行里的 carcher=/bow= 等）。
  //
  // 【原先这里恒不成立】原计数只数近战步兵与普通/复合弓兵，
  // 而本 AI 的兵力全是战车弓兵（AT_CHARIOT_ARCHER）与少量战车 —— 那几种兵
  // 一个都不产，于是计数恒为 5（只有 5 个普通弓兵），`< 8` 永远成立，
  // 整个进攻分支等于被永久关死。改成数实际会出击的兵种。
  //
  // 【恢复】把下面那五行解开即可；想换阈值就改 8 这个字面量。
  // const int offensiveArmyCount =
  //     CountArmyBySort(AT_CHARIOT_ARCHER) + CountArmyBySort(AT_CHARIOT) +
  //     CountArmyBySort(AT_BOWMAN) + CountArmyBySort(AT_CLUBMAN) +
  //     CountArmyBySort(AT_BROADSWORDSMAN) + CountArmyBySort(AT_COMPOSITE_BOWMAN) +
  //     CountArmyBySort(AT_HOPLITE);
  // if (offensiveArmyCount < 8)
  //   return;

  UpdateEnemyBaseDiscovery();
  if (!enemyBaseDiscovered)
    return;

  // 【远程兵被近战贴上 → 全体后撤两格】必须排在站桩之前：站桩一旦接管就直接
  // return，排在它后面这条永远执行不到。详见 KiteRangedBackFromMelee。
  KiteRangedBackFromMelee(ai);

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

  // 【农民拆塔阶段：军队改为分散去牵制敌方箭塔】
  //
  // 【为什么就放在这里、不另起一路】这段代码只有「视野里没有敌方士兵」时才走得到
  // —— 上面 `if (HasVisibleEnemyArmy()) { ...; return; }` 已经先返回了。
  // 所以视野里一出现敌兵，军队就自动丢下塔回去打敌人（那是 AssignFieldDefense
  // 的职责），不需要任何额外判据，也不会出现两套控制每帧互抢。
  //
  // 【为什么要把目标从 FindOffensiveTargetSN() 换掉】那个函数优先选敌方市镇中心，
  // 全军会一起压过去 —— 而围着攻城厂的那几座箭塔没人管，祭司的进门条件
  // （AllEnemyArrowTowersEngaged：每一座塔都要被牵制住）就永远不成立。
  // 改成按单位分散到各座塔，才能让每一座都有人。
  //
  // 【士兵在这里的价值不只是拆塔】它们的伤害效率和农民一样是 1 点/次
  // （战车弓兵 ATK 4 × 建筑 20% = 0.8 → 下限取 1；Development.cpp:47 那个
  // 「军队打建筑 2 倍」的加成只给剑士/骑兵/改进弓兵，战车弓兵不在名单里）。
  // 但它们【是唯一会被箭塔打的目标】—— 塔的目标取自 Defend 集合，
  // 而那个集合只收我方军队（enemyai.cpp:238-241）。士兵在塔边，
  // 塔的火力就不再全部落在只有 100 血的祭司身上。
  vector<const tagBuilding *> escortTowers;
  int escortAnchorDR = 0;
  int escortAnchorUR = 0;
  const bool towerAssault = (farmerEscortStage == FARMER_ESCORT_STAGE_TOWER);
  if (towerAssault)
    CollectEnemyArrowTowers(escortTowers, escortAnchorDR, escortAnchorUR);

  const int targetSN = FindOffensiveTargetSN();
  const int orderInterval = 60;
  if (g_frame - offensiveLastOrderFrame < orderInterval)
    return;
  offensiveLastOrderFrame = g_frame;

  map<int, int> towerWorkers;  // 塔 SN -> 本轮已认领的士兵数
  bool issuedAttack = false;
  for (const tagArmy &army : info.armies) {
    if (!IsOffensiveArmy(army))
      continue;

    int armyTarget = targetSN;
    if (USR_ARMY_TOWER_SPREAD_ENABLED && towerAssault && !escortTowers.empty())
      armyTarget = PickEscortTowerForArmy(army, escortTowers, towerWorkers,
                                          USR_ARMY_TOWER_PER_TARGET_MAX);

    if (armyTarget != -1) {
      if (GetLockedArmyTarget(army.SN) == armyTarget)
        continue;
      ClearArmyTargetLock(army.SN);
      currentTarget[army.SN] = armyTarget;
      // 【不限距离】这里是刻意不做距离判断的：只要目标建筑已被侦察到，
      // 全军就出发，哪怕它在地图另一头。引擎会自己寻路过去
      // （目标建筑是 goalOb，不受「目标格四邻全被占就 nullPath」那条影响）。
      // 不要把「超过 N 格就放弃」加进来 —— 那是防守逻辑的取舍，不是进攻的。
      ai->HumanAction(army.SN, armyTarget);
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
        // 护送祭司的农民不归打猎管（他的工作对象是箭塔，本来也进不了猎物群判断，
        // 这一句是把意图写明，免得日后改了 gazelleClusterOf 的口径时被误伤）。
        if (IsFarmerEscorting(farmer.SN))
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

        // 【正在执行站桩调动的单位不接自卫指令】
        // 站桩那边每 60 帧才发一次（撤退 / 掉队靠拢），这里每 12 帧发一次
        // 「打最近的敌人」，而且本函数后执行 —— 不给它让路的话每次都被覆盖。
        // 窗口与解除条件（走进死区 / 到达槽位 / 走不动）见 ManageStandoff 里
        // USR_STANDOFF_RETREAT_FRAMES 的说明。
        map<int, int>::const_iterator retreatIt = standoffRetreatUntil.find(army.SN);
        if (retreatIt != standoffRetreatUntil.end() &&
            g_frame < retreatIt->second)
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

  // 【触发条件：人口真的卡住了军队 —— 而不是"非护送农民超过 5 个"】
  //
  // 原来这里判的是「非护送农民 > 5」，那是个错误的代理指标：护送启动后
  // 非护送农民本来就少（RECRUIT 常常只征到 16 个，剩下 4 个 + 中心补产的），
  // 反而更容易越过阈值，于是把新农民逐个当多余的清掉 —— 实测一局杀了 17 个、
  // 农民从 20 掉到 2，之后没人拆敌塔、撑到帧数上限判负。
  //
  // 真正该问的是「人口满了（上面那道门已保证），而且还有军队要造」。
  // 「还有军队要造」的可观测证据：有靶场空闲着 —— 说明产能不是瓶颈，
  // 卡的是人口（或资源，但资源不够时自毁也解决不了，不该在这条路径上处理）。
  // 这样就把「人口确实卡住军队」和「本来就不需要造兵」分开了。
  if (CountAvailableRanges() == 0)
    return;

  // 【保留多少采集力：按【全部】农民算，不是按非护送的算】
  // 用总数才是"还剩多少人在干活"的正确口径。选人时再排除护送队 ——
  // 他们是 latched 的编制，自毁不可逆，不能碰。
  int farmerCount = 0;
  for (const tagFarmer &farmer : info.farmers) {
    if (farmer.Blood > 0 && farmer.FarmerSort == FARMERTYPE_FARMER)
      farmerCount++;
  }
  if (farmerCount <= USR_FARMER_KEEP_MIN) // 保留这么多农民维持采集
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
    if (IsFarmerEscorting(farmer.SN))
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

// 【开关一：农民护送祭司】撤出经济 → 集结到敌方基地外 → 拆敌塔。
//
// 【当前取值：关闭（按需求取消护送机制）】关掉之后农民全部留在采集队里 ——
// 不再撤出经济、也不再去拆敌塔。带「已征召」判据的那些入口补丁
// （CalculateFarmerTargets / FindBuilderFarmerSN / TryAssignIdleFarmer /
// TryResumeIncompleteBuilding / FindRepairFarmerSN / AssignFarmerSelfDefense /
// KeepHuntersOnLiveGazelles / SacrificeExcessFarmers 等 10 处）读的是
// IsFarmerEscorting()，而它查的 farmerEscortDuty 只由 RecruitFarmersForEscort
// 写入；本开关关掉后那张表恒为空，补丁自然全部失效，不需要跟着改。
//
// 【代价：拆塔的担子换人挑了】关掉之后农民不再拆塔，改由【军队】承担 ——
// 军队的进攻目标由 FindOffensiveTargetSN() 决定，而它的第 0 条正是「视野里
// 没有敌兵时，全军集中拆【最外围】的那一座敌方箭塔」，一座拆完自动换下一座。
// 所以获胜路径仍然成立（CanPriestApproachSiege 的第 ④ 条 SiegeTowersCleared
// 只要求「一座活着的敌方箭塔都没有」，军队逐座拆完即可满足），代价是两处：
//   · 节奏比农民那套慢，而且军队拆塔时会挨箭塔的反击 —— 塔的目标取自 Defend
//     集合，那个集合只收我方军队（enemyai.cpp:238-241）；农民拆塔是单方面输出，
//     不会挨打，军队不是；
//   · 少了一条兜底。原先 USR_FARMER_TOWER_FALLBACK_FRAME 那条「时间到了就让
//     农民去拆」是用来对付「某些塔没被侦察到、参谋永远凑不齐」的，现在没有替代。
// 因此若实测出现「祭司长期不进厂」，先查 SiegeTowersCleared 的返回（[SIEGE]
// 行的 towersCleared=），而不是先怀疑祭司的走位。
static const bool USR_FARMER_ESCORT_ENABLED = false;
// 【开关二：人口满时自毁农民腾人口】
//
// 【为什么和护送拆成两个开关】原来它们共用一个常量，于是「只要其中一个出
// 问题就得把另一个也关掉」。这两件事的风险完全不同：护送是可逆的调度，
// 自毁是不可逆的 —— 实测它在一局里杀掉了 17 个农民（农民 20 → 2）：它挑的是
// 「不在护送队里的」零散农民，本意是牺牲多余劳动力换人口，实际把市镇中心
// 补产的新农民逐个清掉了。
//
// 【现在的取值】自毁按需求关闭。判据虽然已经改成「人口真的卡住军队」
// （见 SacrificeExcessFarmers：要求有靶场空闲，说明卡的是人口而不是产能），
// 但连带的副作用（后期人口占满、军队补不上来）还没有实测验证过，先关着。
//
// 【为什么用常量开关而不是直接注释掉调用】函数仍然被「引用」，不会产生
// -Wunused-function 警告；启用只改这一个 true/false。
static const bool USR_FARMER_SACRIFICE_ENABLED = false;

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
    // 【农民护送祭司】必须排在 ManageEconomyAndProduction【之前】：征召名单
    // 要在同一帧被 TryAssignIdleFarmer 看到 —— 排在它后面的话，刚被拉走的农民
    // 会先领到一条采集指令，我们再花一条 HumanAction 去覆盖它，白走一次
    // addRelation → suspendRelation（清路径），而且那条 pendingGatherOrders
    // 会残留到下一帧（ProcessPendingGatherOrders 本帧已经跑过了）。
    // 必须排在 ManagePriest【之后】：它读祭司本帧的 WorkObjectSN / DR0 / UR0
    // 判阶段 B。排在 UpdateFarmerWatch【之后】：IsFarmerStuckWalking 依赖
    // 它刷新的位置记录，否则会把正常行进的农民误判成卡死。
    //
    // 【暂时停用，见 USR_FARMER_ESCORT_ENABLED】护送机制（农民撤出经济 →
    // 集结 → 拆敌塔）。上面那些排序理由仍然成立，恢复时把开关改成 true 即可。
    if (USR_FARMER_ESCORT_ENABLED)
        ManageFarmerEscort(this);
    // 【禁止农民进敌方基地 50 格】必须排在 ManageEconomyAndProduction【之前】——
    // 与上面护送那条同理：撤离会把农民写进 farmerThreatLastFrame，而
    // TryAssignIdleFarmer 正是靠这张表跳过被威胁的农民；排在后面的话，本帧他会
    // 先领到一条采集指令（可能又往禁区方向去），下一帧才被我们覆盖，白走一次
    // 清路径。也必须排在 UpdateFarmerWatch【之后】：本规则依赖的位置数据来自它。
    EvacuateFarmersFromEnemyKeepout(this);
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
    //
    // 【默认关闭，见 USR_FARMER_SACRIFICE_ENABLED】人口满时自毁农民腾人口。
    if (USR_FARMER_SACRIFICE_ENABLED)
        SacrificeExcessFarmers(this);
    // 农民自卫：30000 帧后启动（对应「第三波骚扰之后」，enemyai.cpp:45 的
    // TAT = 21000 已过）。此前农民遇袭一律靠撤离、不还手，避免早期被零星
    // 骚扰牵着走、把采集力从食物/木头上拽开；进入后期农民挨打时则就地反击。
    //
    // 【已停用，见 USR_FARMER_SELF_DEFENSE_ENABLED】农民不还手、也不救祭司，
    // 遇袭只撤离。帧号门槛保留：将来恢复时行为与停用前完全一致。
    if (USR_FARMER_SELF_DEFENSE_ENABLED &&
        g_frame >= USR_FARMER_SELF_DEFENSE_FRAME)
        AssignFarmerSelfDefense(this);
    DispatchScouts(this);
    AssignArrowTowerTargets(this);
}
