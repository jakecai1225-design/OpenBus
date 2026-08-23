// E2E 插件链路校验（t7：数据链路 + 控制链路 + 崩溃自愈；不入 CMake）
// 前置：build/bin/plugins/frame-link-probe/（fixture 探针，勿入市场）
// 手动编译（在 sin 根目录）：
//   powershell -File scripts/build_e2e.ps1 -Name e2e_plugin_link
// 运行（PATH 含 build/bin 与 Qt bin；Python 需可用）：
//   build\bin\e2e_plugin_link.exe [dbcPath]
//
// 验证内容（对应 doc/插件系统方案.md「链路审计与修复」）：
//  数据链路：subscribeFrames 订阅制推送（清单无 onFrame 声明也能收到帧，
//           A1 回归）、100ms 批量 flush、frames.getRecent 往返（B2）
//  控制链路：signals.decode/encode（B1）、workspace.getProjectDir/
//           getDbcFiles/getSetting（B1）、frames.getSelected、
//           output.clear（B3）、sendFrame、未知方法 -32601 错误响应（B5）、
//           dbc.* 会话 API（dbc-tool 同链路）、files.* 异步转换
//           （blf-converter 同链路，progress/finished 通知回报）
//  生命周期：宿主被杀 → 1s 重启 → onHostStarted 重激活（C1）→ 数据链路
//           自动恢复；shutdown 后无僵尸重启（C1）
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QStringList>
#include <cstdio>
#include <functional>

#include "core/appconfig.h"
#include "core/canframe.h"
#include "core/dbcmanager.h"
#include "core/plugin/pluginmanager.h"

// 跨 DLL 信号（DEF-08）：PMF 连接静默失败，用 SIGNAL()/SLOT() 字符串连接，
// 因此接收端必须是 Q_OBJECT（文件末 #include moc 产物）
class ProbeSink : public QObject
{
    Q_OBJECT
public:
    explicit ProbeSink(PluginManager *pm, QObject *parent = nullptr)
        : QObject(parent), m_pm(pm) {}

    QStringList messages;         // output.append 收集（含探针全部证据行）
    QList<CanFrame> fedFrames;    // 主程序侧喂入的帧（get_recent 应答数据源）
    quint32 lastSentFrameId = 0;  // sendFrameRequested 收到的帧 ID
    int clearCount = 0;           // outputClearRequested 次数

    int count(const QString &sub) const
    {
        int n = 0;
        for (const auto &m : messages)
            if (m.contains(sub))
                ++n;
        return n;
    }

    bool contains(const QString &sub) const { return count(sub) > 0; }

public slots:
    void onOutput(const QString &text)
    {
        messages << text;
        std::printf("  [output] %s\n", text.toUtf8().constData());
    }
    void onSendFrame(const CanFrame &frame) { lastSentFrameId = frame.id; }
    void onClearOutput() { ++clearCount; }
    void onRequestRecentFrames(const QJsonValue &requestId, int count_)
    {
        // 模拟 MainWindow：从已喂入帧尾部取 count 帧回复
        const int from = qMax(0, fedFrames.size() - count_);
        m_pm->provideRecentFrames(requestId, fedFrames.mid(from));
    }
    void onRequestSelectedFrames(const QJsonValue &requestId)
    {
        // 模拟 MainWindow：回复 2 帧选中数据
        QList<CanFrame> frames;
        CanFrame f;
        f.id = 0x201;
        f.dlc = 8;
        f.data = QByteArray(8, 0);
        frames << f;
        f.id = 0x202;
        frames << f;
        m_pm->provideSelectedFrames(requestId, frames);
    }

private:
    PluginManager *m_pm;
};

static bool pollFor(const std::function<bool()> &pred, int timeoutMs)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!pred()) {
        if (elapsed.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return true;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("openbus");
    app.setOrganizationName("openbus");

    // DBC：默认 test/resources（VIU_FR_50 = 0x50，含 SrsCrashOutpSts 等信号）
    const QString dbcPath = argc > 1
        ? QString::fromUtf8(argv[1])
        : QDir(QCoreApplication::applicationDirPath())
              .filePath(QStringLiteral("../../test/resources/V5.4.0_20260605_INFO_CAN.dbc"));
    // dbc.*/files.* 深度验证素材：探针经环境变量读取（宿主进程继承）
    const QString ascPath = QDir(QCoreApplication::applicationDirPath())
              .filePath(QStringLiteral("../../test/resources/info.asc"));
    qputenv("SIN_E2E_DBC", dbcPath.toUtf8());
    qputenv("SIN_E2E_ASC", ascPath.toUtf8());
    int failures = 0;
    auto check = [&failures](bool ok, const char *what) {
        std::printf("%-28s %s\n", what, ok ? "PASS" : "FAIL");
        if (!ok)
            failures++;
    };

    // ① 控制链路前置：注入 DBC 管理器 + 预写设置项
    AppConfig::instance()->load();
    AppConfig::instance()->set(QStringLiteral("e2e.probeSetting"),
                               QStringLiteral("ok42"));
    AppConfig::instance()->save();

    DbcManager dbc;
    check(dbc.loadDbc(dbcPath) && dbc.findMessage(0x50), "dbc_loaded");

    PluginManager *pm = PluginManager::instance();
    pm->setDbcManager(&dbc);
    pm->initialize();

    ProbeSink sink(pm);
    QObject::connect(pm, SIGNAL(outputMessage(QString)),
                     &sink, SLOT(onOutput(QString)));
    QObject::connect(pm, SIGNAL(sendFrameRequested(CanFrame)),
                     &sink, SLOT(onSendFrame(CanFrame)));
    QObject::connect(pm, SIGNAL(outputClearRequested()),
                     &sink, SLOT(onClearOutput()));
    QObject::connect(pm, SIGNAL(requestSelectedFrames(QJsonValue)),
                     &sink, SLOT(onRequestSelectedFrames(QJsonValue)));
    QObject::connect(pm, SIGNAL(requestRecentFrames(QJsonValue,int)),
                     &sink, SLOT(onRequestRecentFrames(QJsonValue,int)));

    std::printf("python=%s\n", pm->pythonExecutable().toUtf8().constData());
    check(pollFor([pm] { return pm->isHostRunning(); }, 20000), "host_started");

    // ② 激活探针 → 控制链路全方法探测（探针 activate 内完成）
    pm->activatePlugin(QStringLiteral("frame-link-probe"));
    check(pollFor([&sink] { return sink.contains(QStringLiteral("PROBE ACTIVATED")); },
                  25000), "probe_activated");
    check(pollFor([&sink] { return sink.contains(QStringLiteral("PROBE READY")); },
                  20000), "ctl_checks_completed");

    check(sink.contains(QStringLiteral("SrsCrashOutpSts=2")), "ctl_signals_decode");
    check(sink.contains(QStringLiteral("CTL encode=2100000000000000")),
          "ctl_signals_encode");
    check(sink.contains(QStringLiteral("CTL project_dir=")), "ctl_workspace_project_dir");
    check(sink.contains(QStringLiteral("CTL dbc_files=1")), "ctl_workspace_dbc_files");
    check(sink.contains(QStringLiteral("CTL setting=ok42")), "ctl_workspace_get_setting");
    check(sink.contains(QStringLiteral("CTL selected=2")), "ctl_frames_get_selected");
    check(sink.contains(QStringLiteral("CTL unknown=-32601")), "ctl_unknown_method_error");
    check(sink.contains(QStringLiteral("CTL dbc msgs="))
          && !sink.contains(QStringLiteral("CTL dbc FAIL")), "ctl_dbc_session");
    check(sink.lastSentFrameId == 0x1AB, "send_frame_requested");
    check(sink.clearCount >= 1, "output_clear_requested");

    // ③ 数据链路：喂 5 帧 → 100ms 批量 flush → 探针 FRAME 证据
    for (int i = 0; i < 5; ++i) {
        CanFrame f;
        f.id = 0x50;
        f.dlc = 8;
        f.data = QByteArray::fromHex("2100000000000000");
        f.channel = 1;
        sink.fedFrames.append(f);
        pm->onFrameReceived(f);
    }
    check(pollFor([&sink] { return sink.count(QStringLiteral("FRAME id=0x50")) >= 5; },
                  15000), "frames_delivered_batch");
    check(sink.contains(QStringLiteral("CTL recent="))
          && sink.contains(QStringLiteral("last=0x50")), "ctl_frames_get_recent");

    // files.* 异步转换完成回调（convertFinished 通知 → 插件回调 → output）
    check(pollFor([&sink] {
              return sink.contains(QStringLiteral("CTL files ok=True"));
          }, 90000), "ctl_files_convert");

    // ④ 崩溃自愈：taskkill 宿主 → 1s 重启 → 重激活 → 数据链路恢复
    const qint64 pidBefore = pm->hostProcessId();
    QProcess::execute(QStringLiteral("taskkill"),
                      {QStringLiteral("/F"), QStringLiteral("/PID"),
                       QString::number(pidBefore)});
    check(pollFor([pm, pidBefore] {
              return pm->isHostRunning() && pm->hostProcessId() != pidBefore;
          }, 20000), "host_restarted");
    check(pollFor([&sink] {
              return sink.count(QStringLiteral("PROBE ACTIVATED")) >= 2;
          }, 25000), "probe_reactivated");

    CanFrame f2;
    f2.id = 0x50;
    f2.dlc = 8;
    f2.data = QByteArray::fromHex("2200000000000000");
    sink.fedFrames.append(f2);
    pm->onFrameReceived(f2);
    check(pollFor([&sink] {
              return sink.count(QStringLiteral("FRAME id=0x50")) >= 6;
          }, 15000), "data_link_restored");

    // ⑤ 停用 + 关闭（含僵尸重启回归：stop 后 2.5s 不得再拉起宿主）
    pm->deactivatePlugin(QStringLiteral("frame-link-probe"));
    check(pollFor([&sink] {
              return sink.contains(QStringLiteral("PROBE DEACTIVATED"));
          }, 10000), "probe_deactivated");

    pm->shutdown();
    check(!pm->isHostRunning(), "host_stopped");
    QElapsedTimer zombie;
    zombie.start();
    while (zombie.elapsed() < 2500)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    check(!pm->isHostRunning(), "host_no_zombie_restart");

    std::printf(failures == 0 ? "E2E_PLUGIN_LINK PASS\n"
                              : "E2E_PLUGIN_LINK FAIL(%d)\n", failures);
    return failures == 0 ? 0 : 4;
}

#include "e2e_plugin_link.moc"
