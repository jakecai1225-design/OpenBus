// ============================================================
//  test_market_driver — B6 市场驱动套件（doc/测试验收方案.md §四 B6）
//
//  覆盖用例（验收需求 → 断言）：
//  - MKT-01 defaultMarketUrl 定位本地市场（开发布局 build/bin → ../market）
//  - MKT-02 refresh 异步加载 schema 2（loaded 信号 + drivers/plugins 非空）
//  - MKT-03 driverById / pluginById 命中与未命中
//  - MKT-04 DriverInfo 字段完整性（下载描述可消费）
//  - MKT-05 matchWords 多词 AND 过滤语义（列表过滤工具）
//  - MKT-06 resolveUrl 相对路径拼接 / 绝对 URL 直通
// ============================================================
#include <QtTest>
#include <QSignalSpy>
#include <QFileInfo>

#include "core/driver/marketindex.h"

class TestMarketDriver : public QObject
{
    Q_OBJECT

private slots:
    void defaultUrlResolvesLocal();
    void refreshLoadsIndex();
    void lookupById();
    void driverInfoFields();
    void matchWordsSemantics();
    void resolveUrlComposition();

private:
    /// 确保索引已加载（单例可能已被此前用例刷新过）
    MarketIndex *loadedIndex();
};

MarketIndex *TestMarketDriver::loadedIndex()
{
    // 注 1：返回非 void 的辅助函数不能用 QVERIFY/QTRY_*（失败分支展开为
    // "return;"），用 qWaitFor + qFatal 代替
    // 注 2：QSignalSpy 用 SIGNAL() 字符串构造（跨 DLL 信号 PMF 解析失败）
    MarketIndex *idx = MarketIndex::instance();
    if (!idx->isLoaded()) {
        QSignalSpy spy(idx, SIGNAL(loaded(bool,QString)));
        idx->refresh();
        QTest::qWaitFor([&spy]() { return spy.count() >= 1; }, 5000);
        if (spy.count() < 1 || !spy.first().first().toBool())
            qFatal("market.json 加载失败: %s", qPrintable(idx->lastError()));
    }
    return idx;
}

// ---- MKT-01：默认市场源定位 ----
void TestMarketDriver::defaultUrlResolvesLocal()
{
    // 测试 exe 位于 build/bin → 开发布局命中 ../market/market.json
    const QUrl url = MarketIndex::defaultMarketUrl();
    QVERIFY(url.isLocalFile());
    const QString local = url.toLocalFile();
    QVERIFY2(QFileInfo::exists(local),
             qPrintable(QStringLiteral("market.json 不存在: ") + local));
}

// ---- MKT-02：异步加载 ----
void TestMarketDriver::refreshLoadsIndex()
{
    MarketIndex *idx = MarketIndex::instance();
    QSignalSpy spy(idx, SIGNAL(loaded(bool,QString)));
    idx->refresh();
    QTRY_COMPARE(spy.count(), 1);

    QVERIFY(spy.first().first().toBool());
    QVERIFY(idx->isLoaded());
    QVERIFY(idx->lastError().isEmpty());
    QVERIFY(!idx->updated().isEmpty());
    QVERIFY(idx->drivers().size() >= 1);  // zlg / kvaser（make_market.py 生成）
    QVERIFY(idx->plugins().size() >= 1);  // blf-converter 等
}

// ---- MKT-03：按 ID 检索 ----
void TestMarketDriver::lookupById()
{
    MarketIndex *idx = loadedIndex();

    const auto &first = idx->drivers().first();
    const auto hit = idx->driverById(first.id);
    QCOMPARE(hit.id, first.id);
    QCOMPARE(hit.name, first.name);
    QVERIFY(idx->driverById(QStringLiteral("no-such-driver")).id.isEmpty());

    const auto &fp = idx->plugins().first();
    QCOMPARE(idx->pluginById(fp.id).id, fp.id);
    QVERIFY(idx->pluginById(QStringLiteral("no-such-plugin")).id.isEmpty());
}

// ---- MKT-04：条目字段完整性 ----
void TestMarketDriver::driverInfoFields()
{
    MarketIndex *idx = loadedIndex();
    const auto &d = idx->drivers().first();
    QVERIFY(!d.id.isEmpty());
    QVERIFY(!d.name.isEmpty());
    QVERIFY(!d.vendor.isEmpty());
    QVERIFY(!d.version.isEmpty());
    QVERIFY(!d.package.isEmpty()); // 加载过滤条件（id + package 非空）
    QVERIFY(!d.sha256.isEmpty());
    QVERIFY(d.size > 0);
    QVERIFY(!d.devices.isEmpty()); // v2 聚合设备简表
}

// ---- MKT-05：多词 AND 过滤 ----
void TestMarketDriver::matchWordsSemantics()
{
    using MI = MarketIndex;

    // 空搜索文本 → 不过滤
    QVERIFY(MI::matchWords(QString(), {"abc"}));
    QVERIFY(MI::matchWords("   ", {"abc"}));

    // 单词：大小写不敏感、字段内 contains
    QVERIFY(MI::matchWords("Hello", {"say hello world", "zzz"}));
    QVERIFY(!MI::matchWords("Hello", {"zzz", "yyy"}));

    // 多词 AND：每个词命中任一字段即可
    QVERIFY(MI::matchWords("kvaser zlg", {"ZLG 致远电子", "Kvaser AB"}));
    QVERIFY(!MI::matchWords("zlg peak", {"ZLG 致远电子", "Kvaser AB"}));

    // 有搜索词但无字段可命中
    QVERIFY(!MI::matchWords("abc", {}));
}

// ---- MKT-06：URL 组合 ----
void TestMarketDriver::resolveUrlComposition()
{
    MarketIndex *idx = loadedIndex();

    // 绝对 URL 直通
    const QString abs = QStringLiteral("https://example.com/pkg.odp");
    QCOMPARE(idx->resolveUrl(abs).toString(), abs);

    // 相对路径 → market.json 所在目录下的本地 file URL
    const auto &d = idx->drivers().first();
    const QUrl resolved = idx->resolveUrl(d.package);
    QVERIFY(resolved.isLocalFile());
    QVERIFY(resolved.toLocalFile().endsWith(d.package, Qt::CaseInsensitive));
    QVERIFY(resolved.toLocalFile().contains(QStringLiteral("market"),
                                            Qt::CaseInsensitive));
}

QTEST_GUILESS_MAIN(TestMarketDriver)
#include "test_market_driver.moc"
