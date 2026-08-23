// E2E UDS 诊断插件全链路校验 + 市场舰队推广验证（不入 CMake）
// 手动编译（在 sin 根目录）：
//   powershell -File scripts/build_e2e.ps1 -Name e2e_plugin_uds
// 运行（PATH 含 build/bin 与 Qt bin；Python 需带 PyQt6）：
//   build\bin\e2e_plugin_uds.exe [marketUrl]
//
// 以 uds-diagnostic（真实市场插件，ISO 14229 客户端）为例验证全链路：
//  安装链  ：market.url → 索引 → .opk 下载 → sha256 → plugin_tool install
//  控制链路：activate/deactivate、registerCommand、executeCommand 分发、
//           output.append 证据（「已加载/已停用」约定行）
//  UI 链路 ：sin.ui.create_window（PyQt6 窗口在宿主进程内创建）
//  数据链路：subscribeFrames 订阅 → 100ms 批量 frameReceived → 插件
//           on_frame → ISO-TP 层收到 FF(0x7E8) 自动回 FC(0x7E0) →
//           sin.frames.send → sendFrameRequested —— C++→宿主→插件→
//           ISO-TP→插件→宿主→C++ 完整闭环（真实协议行为，非探针）
//  舰队推广：blf-converter / bus-statistics / dbc-tool 逐一
//           安装→激活→命令注册→停用→卸载；canopen-explorer（用户已装）
//           仅激活/命令/停用
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <cstdio>
#include <functional>

#include "core/appconfig.h"
#include "core/canframe.h"
#include "core/driver/marketindex.h"
#include "core/plugin/pluginmanager.h"

// 跨 DLL 信号（DEF-08）：SIGNAL()/SLOT() 字符串连接，接收端必须 Q_OBJECT
// （文件末 #include moc 产物）
class UdsSink : public QObject
{
    Q_OBJECT
public:
    explicit UdsSink(QObject *parent = nullptr) : QObject(parent) {}

    struct SentFrame {
        quint32 id = 0;
        QString dataHex;
    };

    QStringList messages;      // output.append 收集（生命周期证据行）
    QList<SentFrame> sentFrames;  // sendFrameRequested 收集（FC 应答观察点）
    QStringList commandIds;    // registerCommand 收集
    QStringList errorLogs;     // log level>0（命令分发失败等）

    bool contains(const QString &sub) const
    {
        for (const auto &m : messages)
            if (m.contains(sub))
                return true;
        return false;
    }

    bool sentFlowControl() const
    {
        // ISO-TP FC：0x7E0 上 0x30 | BS | STmin
        for (const auto &sf : sentFrames)
            if (sf.id == 0x7E0 && sf.dataHex.startsWith(QStringLiteral("30")))
                return true;
        return false;
    }

    bool commandNotFound(const QString &cmdId) const
    {
        for (const auto &l : errorLogs)
            if (l.contains(QStringLiteral("未找到命令")) && l.contains(cmdId))
                return true;
        return false;
    }

    void resetCycle() { messages.clear(); }   // 每个插件周期独立断言

public slots:
    void onOutput(const QString &text)
    {
        messages << text;
        std::printf("  [output] %s\n", text.toUtf8().constData());
    }
    void onSendFrame(const CanFrame &frame)
    {
        SentFrame sf;
        sf.id = frame.id;
        sf.dataHex = QString::fromUtf8(frame.data.toHex().toUpper());
        sentFrames << sf;
        std::printf("  [sendFrame] 0x%X %s\n", frame.id,
                    sf.dataHex.toUtf8().constData());
    }
    void onCommandRegistered(const QString &id, const QString &title)
    {
        commandIds << id;
        std::printf("  [command] %s (%s)\n", id.toUtf8().constData(),
                    title.toUtf8().constData());
    }
    void onPluginLog(int level, const QString &message)
    {
        if (level > 0)
            errorLogs << message;
        std::printf("  [log%d] %s\n", level, message.toUtf8().constData());
    }
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

    const QString marketUrl = argc > 1 ? QString::fromUtf8(argv[1])
        : QStringLiteral("http://sin.org.cn/market/market.json");
    int failures = 0;
    auto check = [&failures](bool ok, const QString &what) {
        std::printf("%-30s %s\n", what.toUtf8().constData(), ok ? "PASS" : "FAIL");
        if (!ok)
            failures++;
    };

    // ① 市场源 + 索引
    AppConfig::instance()->load();
    AppConfig::instance()->set(QStringLiteral("market.url"), marketUrl);
    AppConfig::instance()->save();

    MarketIndex *mi = MarketIndex::instance();
    std::printf("resolved_url=%s\n", mi->marketUrl().toString().toUtf8().constData());
    mi->refresh();
    if (!pollFor([mi] { return mi->isLoaded(); }, 20000)) {
        std::printf("market_index err=%s\n", mi->lastError().toUtf8().constData());
        return 3;
    }

    // ② 插件系统 + 信号汇集
    PluginManager *pm = PluginManager::instance();
    pm->initialize();
    UdsSink sink;
    QObject::connect(pm, SIGNAL(outputMessage(QString)),
                     &sink, SLOT(onOutput(QString)));
    QObject::connect(pm, SIGNAL(sendFrameRequested(CanFrame)),
                     &sink, SLOT(onSendFrame(CanFrame)));
    QObject::connect(pm, SIGNAL(commandRegistered(QString,QString)),
                     &sink, SLOT(onCommandRegistered(QString,QString)));
    QObject::connect(pm, SIGNAL(pluginLog(int,QString)),
                     &sink, SLOT(onPluginLog(int,QString)));

    std::printf("python=%s\n", pm->pythonExecutable().toUtf8().constData());
    check(pollFor([pm] { return pm->isHostRunning(); }, 20000), "host_started");

    // 下载 + 安装市场插件（返回 false 时已打印原因）
    QNetworkAccessManager nam;
    auto installFromMarket = [&](const QString &pluginId) -> bool {
        const auto entry = mi->pluginById(pluginId);
        if (entry.id.isEmpty()) {
            std::printf("plugin '%s' not in market\n", pluginId.toUtf8().constData());
            return false;
        }
        QNetworkReply *reply = nam.get(QNetworkRequest(mi->resolveUrl(entry.package)));
        if (!pollFor([reply] { return reply->isFinished(); }, 120000)) {
            std::printf("download_timeout %s\n", pluginId.toUtf8().constData());
            return false;
        }
        const QByteArray body = reply->error() == QNetworkReply::NoError
                                    ? reply->readAll() : QByteArray();
        reply->deleteLater();
        if (body.size() != entry.size) {
            std::printf("size_mismatch %s: %d != %d\n", pluginId.toUtf8().constData(),
                        body.size(), entry.size);
            return false;
        }
        const QString sha = QString::fromLatin1(QCryptographicHash::hash(
            body, QCryptographicHash::Sha256).toHex());
        if (sha != entry.sha256) {
            std::printf("sha256_mismatch %s\n", pluginId.toUtf8().constData());
            return false;
        }
        const QString opkPath = QFileInfo(QDir::temp().absoluteFilePath(
            QStringLiteral("e2e_uds_%1_%2.opk").arg(entry.id, entry.version)))
            .absoluteFilePath();
        {
            QFile f(opkPath);
            f.open(QIODevice::WriteOnly);
            f.write(body);
            f.close();
        }
        const QString installErr = pm->installPackage(opkPath);
        if (!installErr.isEmpty())
            std::printf("install_err=%s\n", installErr.toUtf8().constData());
        return installErr.isEmpty();
    };

    // 残留清理：上次运行可能残留 uds-diagnostic / 舰队插件
    for (const QString &leftover : {QStringLiteral("uds-diagnostic"),
                                    QStringLiteral("blf-converter"),
                                    QStringLiteral("bus-statistics"),
                                    QStringLiteral("dbc-tool")}) {
        for (const auto &p : pm->discoveredPlugins())
            if (p.name == leftover)
                pm->uninstallPlugin(leftover);
    }

    // ③ UDS：安装 → 激活（activate + output.append + PyQt6 UI 窗口）
    check(installFromMarket(QStringLiteral("uds-diagnostic")), "uds_install_package");
    bool udsFound = false;
    for (const auto &p : pm->discoveredPlugins())
        if (p.name == QStringLiteral("uds-diagnostic"))
            udsFound = true;
    check(udsFound, "uds_discovered");

    pm->activatePlugin(QStringLiteral("uds-diagnostic"));
    check(pollFor([&sink] { return sink.contains(QStringLiteral("已加载")); }, 30000),
          "uds_activated_evidence");
    check(pollFor([&sink] {
        return sink.commandIds.contains(QStringLiteral("udsDiagnostic.open"));
    }, 10000), "uds_command_registered");

    // ④ 数据链路完整闭环：喂 FF(0x7E8) → 插件 ISO-TP → FC(0x7E0) → 主程序
    CanFrame ff;
    ff.id = 0x7E8;
    ff.dlc = 8;
    ff.channel = 1;
    ff.data = QByteArray::fromHex("100E5003003201F4");  // FF total=14
    pm->onFrameReceived(ff);
    check(pollFor([&sink] { return sink.sentFlowControl(); }, 15000),
          "uds_data_link_ff_fc_roundtrip");

    // ⑤ 命令分发（executeCommand 链路；失败时宿主回「未找到命令」错误日志）
    pm->executeCommand(QStringLiteral("udsDiagnostic.open"));
    QElapsedTimer cmdWait;
    cmdWait.start();
    while (cmdWait.elapsed() < 1500)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    check(!sink.commandNotFound(QStringLiteral("udsDiagnostic.open")),
          "uds_command_dispatch");

    // ⑥ 停用 + 卸载（恢复环境）
    pm->deactivatePlugin(QStringLiteral("uds-diagnostic"));
    check(pollFor([&sink] { return sink.contains(QStringLiteral("已停用")); }, 10000),
          "uds_deactivated_evidence");
    check(pm->uninstallPlugin(QStringLiteral("uds-diagnostic")).isEmpty(),
          "uds_uninstalled");

    // ⑦ 舰队推广：其余市场插件同一套链路清单验证
    //    （安装→激活→命令注册→停用→卸载；数据链路机制与 UDS 同源，
    //     已由 link/uds 两级 harness 覆盖）
    struct FleetEntry {
        const char *id;
        const char *cmdId;
    };
    const FleetEntry fleet[] = {
        {"blf-converter", "blfConverter.open"},
        {"bus-statistics", "busStatistics.open"},
        {"dbc-tool", "dbcTool.open"},
    };
    for (const auto &fe : fleet) {
        const QString id = QString::fromUtf8(fe.id);
        const QString cmdId = QString::fromUtf8(fe.cmdId);
        std::printf("---- fleet: %s ----\n", id.toUtf8().constData());
        sink.resetCycle();

        check(installFromMarket(id), id + "_install");
        pm->activatePlugin(id);
        check(pollFor([&sink] { return sink.contains(QStringLiteral("已加载")); }, 30000),
              id + "_activated_evidence");
        check(pollFor([&sink, cmdId] { return sink.commandIds.contains(cmdId); }, 10000),
              id + "_command_registered");
        pm->deactivatePlugin(id);
        check(pollFor([&sink] { return sink.contains(QStringLiteral("已停用")); }, 10000),
              id + "_deactivated_evidence");
        check(pm->uninstallPlugin(id).isEmpty(), id + "_uninstalled");
    }

    // ⑧ canopen-explorer：用户已装 → 仅激活/命令/停用（不安装卸载）
    bool canopenInstalled = false;
    for (const auto &p : pm->discoveredPlugins())
        if (p.name == QStringLiteral("canopen-explorer"))
            canopenInstalled = true;
    if (canopenInstalled) {
        sink.resetCycle();
        pm->activatePlugin(QStringLiteral("canopen-explorer"));
        check(pollFor([&sink] { return sink.contains(QStringLiteral("已加载")); }, 30000),
              "canopen_activated_evidence");
        check(pollFor([&sink] {
            return sink.commandIds.contains(QStringLiteral("canopenExplorer.open"));
        }, 10000), "canopen_command_registered");
        pm->deactivatePlugin(QStringLiteral("canopen-explorer"));
        check(pollFor([&sink] { return sink.contains(QStringLiteral("已停用")); }, 10000),
              "canopen_deactivated_evidence");
    } else {
        std::printf("canopen-explorer not installed, skipped\n");
    }

    // ⑨ 关闭
    pm->shutdown();
    check(!pm->isHostRunning(), "host_stopped");

    std::printf(failures == 0 ? "E2E_PLUGIN_UDS PASS\n"
                              : "E2E_PLUGIN_UDS FAIL(%d)\n", failures);
    return failures == 0 ? 0 : 4;
}

#include "e2e_plugin_uds.moc"
