// E2E 插件全链路校验（t3：云端下载 → 安装 → 启动 → 关闭；不入 CMake）
// 手动编译（在 sin 根目录）：
//   1) moc.exe -o scripts/e2e_plugin_flow.moc scripts/e2e_plugin_flow.cpp
//   2) g++ -std=c++17 -DUNICODE -D_UNICODE -DQT_CORE_LIB -DQT_NETWORK_LIB \
//        -Ibuild/src/openbus_data_autogen/include -Isrc -Ithird_party/nlohmann_json \
//        -Ithird_party/spdlog/include \
//        -isystem C:/Qt/6.8.3/mingw_64/include \
//        -isystem C:/Qt/6.8.3/mingw_64/include/QtCore \
//        -isystem C:/Qt/6.8.3/mingw_64/include/QtNetwork \
//        -isystem C:/Qt/6.8.3/mingw_64/mkspecs/win32-g++ \
//        scripts/e2e_plugin_flow.cpp -Lbuild/src -lopenbus_data \
//        C:/Qt/6.8.3/mingw_64/lib/libQt6Core.a \
//        C:/Qt/6.8.3/mingw_64/lib/libQt6Network.a -lole32 -luuid -lws2_32 \
//        -o build/bin/e2e_plugin_flow.exe
// 运行（PATH 含 build/bin 与 Qt bin；Python 需带 PyQt6）：
//   set SIN_PYTHON=C:\Python313\python.exe
//   e2e_plugin_flow.exe http://sin.org.cn/market/market.json bus-statistics
//
// 验证内容（模拟 MarketTab 下载安装 + 插件面板启动/停止）：
// 1. market.url 指向云端 → MarketIndex 拉取索引
// 2. 下载指定插件 .opk → sha256 与索引一致
// 3. PluginManager::installPackage 安装（全新状态，安装前 0 插件）
// 4. activatePlugin → 插件 outputMessage 报告已加载（真实宿主证据，非乐观标记）
// 5. deactivatePlugin → 报告已停用；shutdown 后宿主退出
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

#include "core/appconfig.h"
#include "core/driver/marketindex.h"
#include "core/plugin/pluginmanager.h"

// 跨 DLL 信号（DEF-08）：PMF 连接静默失败，用 SIGNAL()/SLOT() 字符串连接，
// 因此接收端必须是 Q_OBJECT（文件末 #include moc 产物）
class OutputSink : public QObject
{
    Q_OBJECT
public:
    explicit OutputSink(QObject *parent = nullptr) : QObject(parent) {}
    QStringList messages;
public slots:
    void onOutput(const QString &text)
    {
        messages << text;
        std::printf("  [output] %s\n", text.toUtf8().constData());
    }
public:
    bool contains(const QString &sub) const
    {
        for (const auto &m : messages)
            if (m.contains(sub))
                return true;
        return false;
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

    const QString sourceUrl = argc > 1 ? QString::fromUtf8(argv[1])
                                       : QStringLiteral("http://sin.org.cn/market/market.json");
    const QString pluginId = argc > 2 ? QString::fromUtf8(argv[2])
                                      : QStringLiteral("bus-statistics");
    int failures = 0;
    auto check = [&failures](bool ok, const char *what) {
        std::printf("%-34s %s\n", what, ok ? "PASS" : "FAIL");
        if (!ok)
            failures++;
    };

    // ① 市场源 = 云端
    AppConfig::instance()->load();
    AppConfig::instance()->set(QStringLiteral("market.url"), sourceUrl);
    AppConfig::instance()->save();

    // ② 拉取索引
    MarketIndex *mi = MarketIndex::instance();
    std::printf("resolved_url=%s\n", mi->marketUrl().toString().toUtf8().constData());
    mi->refresh();
    if (!pollFor([mi] { return mi->isLoaded(); }, 20000)) {
        std::printf("market_index err=%s\n", mi->lastError().toUtf8().constData());
        return 3;
    }
    const auto entry = mi->pluginById(pluginId);
    if (entry.id.isEmpty()) {
        std::printf("plugin '%s' not in market\n", pluginId.toUtf8().constData());
        return 3;
    }
    std::printf("market_entry=%s ver=%s pkg=%s sha=%.16s...\n",
                entry.id.toUtf8().constData(), entry.version.toUtf8().constData(),
                entry.package.toUtf8().constData(),
                entry.sha256.toUtf8().constData());

    // ③ 下载 .opk + sha256 校验
    QNetworkAccessManager nam;
    QNetworkReply *reply = nam.get(QNetworkRequest(mi->resolveUrl(entry.package)));
    if (!pollFor([reply] { return reply->isFinished(); }, 120000)) {
        std::printf("download_timeout\n");
        return 3;
    }
    const int httpStatus = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->error() == QNetworkReply::NoError
                                ? reply->readAll() : QByteArray();
    reply->deleteLater();
    check(httpStatus == 200 && body.size() == entry.size, "download_opk");
    const QString actualSha = QString::fromLatin1(QCryptographicHash::hash(
        body, QCryptographicHash::Sha256).toHex());
    check(actualSha == entry.sha256, "sha256_match");

    const QString opkPath = QFileInfo(QDir::temp().absoluteFilePath(
        QStringLiteral("e2e_%1_%2.opk").arg(pluginId, entry.version))).absoluteFilePath();
    {
        QFile f(opkPath);
        f.open(QIODevice::WriteOnly);
        f.write(body);
        f.close();
    }

    // ④ 全新状态初始化：0 插件、宿主未启动
    PluginManager *pm = PluginManager::instance();
    pm->initialize();
    OutputSink sink;
    QObject::connect(pm, SIGNAL(outputMessage(QString)),
                     &sink, SLOT(onOutput(QString)));
    check(pm->discoveredPlugins().isEmpty(), "fresh_zero_plugins");
    check(!pm->isHostRunning(), "fresh_host_not_running");
    std::printf("python=%s\n", pm->pythonExecutable().toUtf8().constData());

    // ⑤ 安装（plugin_tool.py install → 重扫描 → 补启宿主）
    const QString installErr = pm->installPackage(opkPath);
    if (!installErr.isEmpty())
        std::printf("install_err=%s\n", installErr.toUtf8().constData());
    check(installErr.isEmpty(), "install_package");

    bool found = false;
    for (const auto &p : pm->discoveredPlugins())
        if (p.name == pluginId)
            found = true;
    check(found, "discovered_after_install");
    check(pollFor([pm] { return pm->isHostRunning(); }, 20000), "host_started");

    // ⑥ 启动插件：等宿主转发插件 output（真实激活证据）
    pm->activatePlugin(pluginId);
    check(pm->isPluginActivated(pluginId), "activate_requested");
    check(pollFor([&sink] { return sink.contains(QStringLiteral("已加载")); }, 25000),
          "plugin_loaded_evidence");

    // ⑦ 关闭插件
    pm->deactivatePlugin(pluginId);
    check(pollFor([&sink] { return sink.contains(QStringLiteral("已停用")); }, 10000),
          "plugin_stopped_evidence");
    check(!pm->isPluginActivated(pluginId), "deactivated_state");

    // ⑧ 宿主关闭（aboutToQuit 也会触发，shutdown 幂等：m_host 置空）
    pm->shutdown();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    check(!pm->isHostRunning(), "host_stopped");

    std::printf("plugins_dir=%s\n",
                QDir(QCoreApplication::applicationDirPath() + "/plugins")
                    .absolutePath().toUtf8().constData());
    std::printf(failures == 0 ? "E2E_PLUGIN_FLOW PASS\n" : "E2E_PLUGIN_FLOW FAIL(%d)\n",
                failures);
    return failures == 0 ? 0 : 4;
}

#include "e2e_plugin_flow.moc"
