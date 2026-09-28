#include "ai.h"

std::vector<QString> AI::AIName= { "UsrAI", "EnemyAI" };

AI::~AI() {
    stopThread = true;
    condition.wakeAll();
    wait();
}
int AI::ActionCancel(int SN){
    return AI::AddToIns(instruction(INS_CANCEL, SN,0));
}
int AI::HumanMove(int SN, double DR0, double UR0){
    return AI::AddToIns(instruction(INS_HUMANMOVE,SN,Double::FromDouble(DR0),Double::FromDouble(UR0)));
}

int AI::HumanAction(int SN,int obSN){
    return AI::AddToIns(instruction(INS_HUMANACTION,SN,obSN , true));
}

int AI::HumanBuild(int SN, int BuildingNum, int BlockDR, int BlockUR){
    return AI::AddToIns(instruction(INS_HUMANBUILD,SN,BlockDR,BlockUR,BuildingNum));
}

int AI::BuildingAction(int SN,int Action){
    return AI::AddToIns(instruction(INS_BUILDINGACTION,SN,Action));
}

int AI::PinPointStrike(int SN, double DR0, double UR0)
{
    return AI::AddToIns(instruction(INS_PINPOINT_STRIKE,SN,Double::FromDouble(DR0),Double::FromDouble(UR0)));
}

void AI::cheatAction() {
    if(!IsExamining){
        is_cheatAction = true;
    }
}

void AI::startProcessing() {
    // 【阻塞取锁，而不是 tryLock —— 这是「AI 每帧必被处理一次」的唯一保证】
    //
    // 【原实现的问题】`if (!mutex.tryLock()) return;` —— AI 还在处理上一帧时，
    // 这次唤醒被【静默丢弃】：不排队、不补跑、不计数。再加上 run() 在
    // processData() + CommitInstruction() 全程持有 mutex，实际效果是
    //     AI 的决策频率 = min(游戏帧率, AI 处理能力)
    // 多出来的帧被直接跳过。
    //
    // 【为什么这件事很严重】AI 里所有节流与时间戳都是按 g_frame 写的
    // （60/100/120/500/2000 帧……），所以「跳过了哪些帧」直接决定它的行为；
    // 而跳帧比例取决于机器负载与构建优化 —— -O2 的 Release 帧循环快得多，
    // 更容易把帧率顶到 AI 处理能力之上，Debug 则相反。于是同一份代码在两个
    // 构建下打法差异巨大，而且同一版本两次跑的结果也不可复现（跳帧模式本身就
    // 是时序噪声的函数）。这就是 --freq MAX 下结果随机的根源。
    //
    // 【改法】改成阻塞取锁：帧循环必须等 AI 把上一帧处理完，才能推进到下一帧。
    // AI 于是每帧恰好被处理一次，行为只取决于帧号与局势，不再取决于机器快慢。
    //
    // 【代价，必须知道】
    //   · AI 的处理耗时进入关键路径 —— 帧率被压在 AI 的能力上限上，
    //     `--freq MAX` 的语义变成「AI 能想多快就跑多快」（原来它只是「定时器
    //     间隔设成 0」，实际帧率是机器快慢的副产物）。整局墙钟时间可能变长。
    //   · processData 若真的卡住（死循环/等锁），整个游戏会跟着卡住 ——
    //     原实现只会表现为 AI 退化、画面照常。这个代价是有意接受的：
    //     宁可在测试里立刻看见卡死，也不要一个静默降级的 AI。
    //
    // 【锁序安全性】调用链是 FrameUpdate → gameDataUpdate()（MainWidget.cpp:2672，
    // 每帧无条件一次，且在 g_frame = gameframe 之后）→ emit startAI()。emit 之前
    // tagEnemyGame / tagUsrGame 都已 release（见 gameDataUpdate 里那三行），
    // 所以本线程在等 AI 时【不持有任何锁】；而 AI 侧在这段时间里也只会去拿
    // UsrIns.lock（CommitInstruction），那把锁此刻只可能被 AI 自己或
    // manageOrder(0) 持有 —— 前者会释放，后者此时根本没在跑。不构成锁环。
    QMutexLocker locker(&mutex);
    stopThread = false;
    condition.wakeAll();
}

void AI::stopProcessing() {
    QMutexLocker locker(&mutex);
    stopThread = true;
    condition.wakeAll();
}

void AI::run() {
    while (true) {
        QMutexLocker locker(&mutex);
        if (stopThread)
            return;
        if (g_frame > 10) {
            ProcessDataWork = 1;
            if(!GameReplay){
                //非回放模式才会执行数据处理
                processData();
            }
            ProcessDataWork = 0;
        }
        //将所有命令放入Ins结构体
        if(!GameReplay){//虽然回放模式不会产生指令，但保险起见还是加一下
            CommitInstruction();
        }
        //
        condition.wait(&mutex);
    }
}

bool AI::trylock() {
    return aiLock.tryLock();
}

void AI::unlock() {
    aiLock.unlock();
}

int AI::AddToIns(instruction ins)
{
    ins.id=InsID++;
    InsPerFrame.push_back(ins);
    return ins.id;
}

ins &AI::GetInsStruct()
{
    extern ins UsrIns;
    return UsrIns;
}

void AI::CommitInstruction()
{
    ins&Ins=GetInsStruct();
    //
    Ins.lock.lock();
    for(auto&val:InsPerFrame)
    {
        Ins.instructions.push(val);
    }
    Ins.lock.unlock();
    //
    InsPerFrame.clear();
}

bool AI::isHuman(int SN) {
    int type = SN / 10000;
    return g_Object[SN] && (type == SORT_ARMY || type == SORT_FARMER);
}

bool AI::isBuilding(int SN) {
    int sort = SN / 10000;
    return g_Object[SN] && (sort == SORT_BUILDING || sort == SORT_Building_Resource);
}

double AI::calDistance(double DR1, double UR1, double DR2, double UR2)
{
    return pow(pow(DR1 - DR2, 2) + pow(UR1 - UR2, 2), 0.5);
}

void AI::DebugText(string debugStr)
{
    call_debugText("black", " " + AIName[id] + "打印：" + QString::fromStdString(debugStr), id);
}

void AI::DebugText(QString debugStr)
{
    call_debugText("black", " " + AIName[id] + "打印：" + debugStr, id);
}

void AI::DebugText(char *debugStr)
{
    call_debugText("black", " " + AIName[id] + "打印：" + QString::fromUtf8(debugStr), id);
}

void AI::DebugText(int debugInt)
{
    call_debugText("black", " " + AIName[id] + "打印：" + QString::number(debugInt), id);
}

void AI::DebugText(double debugdouble)
{
    call_debugText("black", " " + AIName[id] + "打印：" + QString::number(debugdouble), id);
}

