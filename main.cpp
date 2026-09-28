#include "GlobalVariate.h"
#include "MainWidget.h"
#include <QApplication>
#include <QMap>
#include "Logger.h"
#include"EventFilter.h"
int main(int argc, char* argv[])
{

    // 【窗口属性必须在 QApplication 构造之前设置】
    // Qt 在 QApplication 建立时就定下了渲染后端，构造之后再 setAttribute 是
    // 无效的 —— 下面那句 QApplication::setAttribute(Qt::AA_UseDesktopOpenGL)
    // 写在构造【之后】，所以它从加进来那天起就没生效过。
    //
    // 【--softgl：强制软件渲染，给并行跑多个实例用】
    // 多个游戏实例并行时，几个 OpenGL 窗口争抢同一个 GPU 上下文，被遮挡的
    // 那些会停止重绘、整块变黑（实测单实例正常，只要并行就黑 —— 2 个和 4 个
    // 都一样）。软件渲染不走 GPU，各画各的。代价是渲染改由 CPU 承担、会慢些。
    //
    // 这里不能用 QCommandLineParser：它要求先有 QApplication，而这些属性
    // 必须在 QApplication 之前设置。所以手工扫一遍 argv。
    // 不加这个参数时行为与改动前完全一致，手动玩不受影响。
    bool softGL = false;
    for (int i = 1; i < argc; ++i)
    {
        if (qstrcmp(argv[i], "--softgl") == 0)
        {
            softGL = true;
            break;
        }
    }
    if (softGL)
        QApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);

    //
    QApplication app(argc, argv);
    Logger::init(Logger::LogLevel::Debug);
    //解析参数
    ParseArguments(app);
    //开启GPU加速（注意：设在 QApplication 构造之后，按 Qt 的规则无效）
    QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    //创建网络插件
    NetworkManager=new NetworkPlugin(&app);
    NetworkManager->start();
    //安装全局事件器
    eventFilter=new EventFilter();
    app.installEventFilter(eventFilter);
    // 添加排除文件，这些文件不会被Logger写进日志文件（改为直接 fprintf 到 stdout）
    Logger::addExcludedFile("EnemyAI.cpp");
    Logger::addExcludedFile("UsrAI.cpp");
    // 【为什么把 Qt 渲染模块也排除掉：--offscreen 下它们会刷爆日志】
    // 无画面（offscreen）模式下渲染设备与位图资源是空的，于是每一次绘制都报一次
    //     QPixmap::scaled: Pixmap is a null pixmap          （qpixmap.cpp）
    //     QPainter::begin: Paint device returned engine == 0 （qpainter.cpp）
    // 实测【约每帧 12 条】，一局 26695 帧就是 32 万条；而 Logger 每行都
    // write()+flush()，于是帧循环被 I/O 拖住（74 帧/秒，有画面时约 190），
    // 一次 16 局评测里多局撞满超时，日志文件也涨到 34~58 MB/局。
    // 排除后这些消息改走 stdout（无 flush 的缓冲写），评测脚本再把 stdout 丢掉，
    // 洪流的代价就归零了；日志文件里留下的仍然是有诊断价值的 AI / 引擎信息。
    Logger::addExcludedFile("qpixmap.cpp");
    Logger::addExcludedFile("qpainter.cpp");
    Logger::addExcludedFile("qimage.cpp");
    //运行窗口
    MainWidget w;
    w.show();
    return app.exec();
}

