// E2E 校验（插件系统方案 §六.6，不入 CMake）——手动编译（在 sin/build 目录下）：
//   g++ -std=c++17 -DUNICODE -D_UNICODE -DQT_CORE_LIB -DQT_NETWORK_LIB \
//     -Ibuild/src/openbus_data_autogen/include -Isrc -Ithird_party/nlohmann_json \
//     -Ithird_party/spdlog/include \
//     -isystem C:/Qt/6.8.3/mingw_64/include \
//     -isystem C:/Qt/6.8.3/mingw_64/include/QtCore \
//     -isystem C:/Qt/6.8.3/mingw_64/include/QtNetwork \
//     -isystem C:/Qt/6.8.3/mingw_64/mkspecs/win32-g++ \
//     ../scripts/e2e_market_check.cpp -Lbuild/src -lopenbus_data \
//     C:/Qt/6.8.3/mingw_64/lib/libQt6Core.a \
//     C:/Qt/6.8.3/mingw_64/lib/libQt6Network.a -lole32 -luuid -lws2_32 \
//     -o build/bin/e2e_market_check.exe
// 运行（需 dev server 已启动；PATH 含 build/bin 与 Qt bin）：
//   e2e_market_check.exe           # 写入开发源并验证 HTTP 拉取
//   e2e_market_check.exe default   # 恢复 market.url 为空（自动定位）
//
// 验证内容：
// 1. 模拟 MarketTab「市场源 → 开发源」：写 settings.json 的 market.url
// 2. MarketIndex 构造应读到该设置并用 HTTP 拉取 openbus_appstore dev server
// 3. 校验索引解析结果（drivers/plugins 数量 + updated 时间戳）
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <cstdio>

#include "core/appconfig.h"
#include "core/driver/marketindex.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("openbus");
    app.setOrganizationName("openbus");

    // ① 载入既有配置 → ② 设置市场源（argv[1] 传 "default" 则恢复空）
    AppConfig::instance()->load();
    const bool useDefault = argc > 1 && qstrcmp(argv[1], "default") == 0;
    AppConfig::instance()->set(QStringLiteral("market.url"), useDefault
        ? QString()
        : QStringLiteral("http://127.0.0.1:5173/market/market.json"));
    AppConfig::instance()->save();
    const bool saved = QFileInfo::exists(AppConfig::instance()->configPath());
    std::printf("settings_path=%s saved=%d\n",
                AppConfig::instance()->configPath().toUtf8().constData(), int(saved));

    if (useDefault) {
        std::printf("market.url cleared\n");
        return 0;
    }

    // ④ MarketIndex 单例：构造函数应优先采用 market.url 设置
    MarketIndex *mi = MarketIndex::instance();
    std::printf("resolved_url=%s\n", mi->marketUrl().toString().toUtf8().constData());

    // ⑤ 异步拉取 → 轮询 isLoaded()（跨 DLL 信号 PMF 解析失败，SIGNAL()
    // 字符串又无 functor 重载，故用 processEvents 轮询 —— 等价验证）
    mi->refresh();
    QElapsedTimer elapsed;
    elapsed.start();
    while (!mi->isLoaded() && elapsed.elapsed() < 20000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    if (mi->isLoaded()) {
        std::printf("loaded_ok=1 err=\n");
        std::printf("updated=%s drivers=%d plugins=%d\n",
                    mi->updated().toUtf8().constData(),
                    int(mi->drivers().size()), int(mi->plugins().size()));
        const auto &p0 = mi->plugins().first();
        std::printf("first_plugin=%s ver=%s pkg=%s sha256=%.16s...\n",
                    p0.id.toUtf8().constData(), p0.version.toUtf8().constData(),
                    p0.package.toUtf8().constData(),
                    p0.sha256.toUtf8().constData());
        return 0;
    }
    std::printf("loaded_ok=0 err=%s\n", mi->lastError().toUtf8().constData());
    return 3;
}
