// ============================================================
//  test_canfileio — B1 文件 I/O 套件（doc/测试验收方案.md §四 B1）
//
//  覆盖用例（验收需求 → 断言）：
//  - TC-01 读小 BLF：帧数>0 + 字段自洽性不变量
//  - TC-02 读 ASC：同上
//  - TC-05/ST-01 大 BLF 加载计时（宽松上限，输出基线）
//  - TC-09/10/12 导出-重导入闭环（ASC/CSV/BLF）：自构造已知帧，
//    写→读→逐帧精确比对（帧数/ID/DLC/数据/时间戳单调）
//  - TC-16 异常文件容错（截断 BLF）：不崩溃、行为确定
//  - TC-17 不存在文件：友好失败
//  - 格式推断/工厂后缀支持
//
//  断言原则：真实样本文件（info.blf 等）帧内容未知 → 只断言自洽性
//  不变量；闭环测试用自构造数据做精确断言（需求语义，非实现常量）。
// ============================================================
#include <QtTest>
#include <QTemporaryDir>
#include <QElapsedTimer>

#include "core/canframe.h"
#include "core/canfileio/canfileio.h"
#include "core/canfileio/canfileio_factory.h"

using namespace CanFileIO;

static QString testData(const QString &name)
{
    // 样本统一位于 test/resources/（info.blf/info.asc/CD701 大文件等）
    return QString::fromLatin1(SIN_SOURCE_DIR) + QStringLiteral("/test/resources/") + name;
}

/// 构造已知帧：数据为 firstByte 起的递增字节序列
static CanFrame makeFrame(double tsS, quint32 id, int len, int firstByte,
                          bool ext = false, bool fd = false, bool tx = false)
{
    CanFrame f;
    f.timestamp = tsS;
    f.timestampNs = static_cast<quint64>(tsS * 1e9);
    f.id = id;
    f.extended = ext;
    f.fd = fd;
    f.direction = tx ? CanFrame::Tx : CanFrame::Rx;
    f.dlc = CanFrame::lengthToDlc(len);
    QByteArray d(len, Qt::Uninitialized);
    for (int i = 0; i < len; ++i)
        d[i] = static_cast<char>((firstByte + i) & 0xFF);
    f.data = d;
    f.channel = 1;
    return f;
}

/// 真实样本的自洽性不变量（帧内容未知，只验证结构合法）
static void verifyFrameInvariants(const CanFrame &f)
{
    QVERIFY2(f.dlc <= 15, qPrintable(QStringLiteral("dlc=%1 超界").arg(f.dlc)));
    QVERIFY2(f.data.size() == CanFrame::dlcToLength(f.dlc),
             qPrintable(QStringLiteral("dlc=%1 与 data.size()=%2 不一致")
                            .arg(f.dlc).arg(f.data.size())));
    const quint32 maxId = f.extended ? 0x1FFFFFFFu : 0x7FFu;
    QVERIFY2((f.id & ~maxId) == 0 || f.isErrorFrame(),
             qPrintable(QStringLiteral("id=0x%1 超出 %2 位范围")
                            .arg(f.id, 0, 16).arg(f.extended ? 29 : 11)));
}

/// 闭环精确比对：读回帧须与写入帧逐字段一致
static void verifyRoundtrip(const QVector<CanFrame> &written,
                            const QVector<CanFrame> &readBack)
{
    QCOMPARE(readBack.size(), written.size());
    for (int i = 0; i < written.size(); ++i) {
        const CanFrame &w = written[i];
        const CanFrame &r = readBack[i];
        QCOMPARE(r.id, w.id);
        QCOMPARE(r.data, w.data);
        QCOMPARE(r.data.size(), w.data.size());
        QCOMPARE(r.extended, w.extended);
        QCOMPARE(r.direction, w.direction);
    }
    // 时间戳单调不减（写入即单调）
    for (int i = 1; i < readBack.size(); ++i)
        QVERIFY2(readBack[i].timestamp >= readBack[i - 1].timestamp,
                 "读回时间戳非单调");
}

class TestCanFileIO : public QObject
{
    Q_OBJECT

private slots:
    // ---- 格式推断（TC-01 前置） ----
    void formatDetection()
    {
        QCOMPARE(formatFromSuffix(QStringLiteral("blf")), Format::BLF);
        QCOMPARE(formatFromSuffix(QStringLiteral("asc")), Format::ASC);
        QCOMPARE(formatFromSuffix(QStringLiteral("csv")), Format::CSV);
        QCOMPARE(formatFromSuffix(QStringLiteral("pcap")), Format::PCAP);
        QCOMPARE(formatFromSuffix(QStringLiteral("trc")), Format::TRC);
        QCOMPARE(formatFromSuffix(QStringLiteral("xyz")), Format::Unknown);
    }

    // ---- 工厂后缀支持（MK/TC 前置） ----
    void factorySuffixSupport()
    {
        QVERIFY(CanFileIOFactory::canRead(QStringLiteral("blf")));
        QVERIFY(CanFileIOFactory::canRead(QStringLiteral("asc")));
        QVERIFY(CanFileIOFactory::canRead(QStringLiteral("csv")));
        QVERIFY(CanFileIOFactory::canWrite(QStringLiteral("asc")));
        QVERIFY(CanFileIOFactory::canWrite(QStringLiteral("csv")));
        // 未知后缀返回 nullptr 读取器
        QVERIFY(!CanFileIOFactory::createReader(QStringLiteral("x.xyz")));
    }

    // ---- TC-01：读小 BLF ----
    void readSmallBlf()
    {
        auto reader = CanFileIOFactory::createReader(testData(QStringLiteral("info.blf")));
        QVERIFY2(reader, "blf 读取器创建失败");
        QVERIFY2(reader->open(testData(QStringLiteral("info.blf"))), "打开 info.blf 失败");
        QVector<CanFrame> frames;
        const int n = reader->readAll(frames);
        reader->close();
        QVERIFY2(n > 0, qPrintable(QStringLiteral("读出帧数 %1").arg(n)));
        qDebug() << "info.blf 帧数:" << n;
        for (const CanFrame &f : frames)
            verifyFrameInvariants(f);
    }

    // ---- TC-02：读 ASC ----
    void readAsc()
    {
        const QString path = testData(QStringLiteral("info.asc"));
        auto reader = CanFileIOFactory::createReader(path);
        QVERIFY2(reader, "asc 读取器创建失败");
        QVERIFY2(reader->open(path), "打开 info.asc 失败");
        QVector<CanFrame> frames;
        const int n = reader->readAll(frames);
        reader->close();
        QVERIFY2(n > 0, qPrintable(QStringLiteral("读出帧数 %1").arg(n)));
        qDebug() << "info.asc 帧数:" << n;
        for (const CanFrame &f : frames)
            verifyFrameInvariants(f);
    }

    // ---- TC-02 扩展：第三方 ASC（CANoe 7.0 导出）----
    // CANFD 关键字 + ID 前置格式B + flags 列 + dlc码/dataLen 分离
    void readAscV7ThirdParty()
    {
        const QString path = testData(QStringLiteral("can_20250526210818.asc"));
        if (!QFile::exists(path))
            QSKIP("样本文件不存在");
        auto reader = CanFileIOFactory::createReader(path);
        QVERIFY2(reader, "asc 读取器创建失败");
        QVERIFY2(reader->open(path), "打开样本失败");
        QVector<CanFrame> frames;
        const int n = reader->readAll(frames);
        reader->close();
        QVERIFY2(n > 0, qPrintable(QStringLiteral("读出帧数 %1").arg(n)));
        qDebug() << "CANoe 7.0 ASC 帧数:" << n;
        bool anyFd = false;
        for (const CanFrame &f : frames) {
            verifyFrameInvariants(f);
            anyFd |= f.fd;
        }
        QVERIFY2(anyFd, "样本含 CANFD 帧但未解析出任何 FD 帧");
    }

    // ---- TC-02 扩展：第三方 ASC（CANoe 15.7 导出）----
    // 头部含 internal events logged / Begin TriggerBlock，行尾附加列
    void readAscV15ThirdParty()
    {
        const QString path = testData(QStringLiteral("test_L035.asc"));
        if (!QFile::exists(path))
            QSKIP("样本文件不存在");
        auto reader = CanFileIOFactory::createReader(path);
        QVERIFY2(reader, "asc 读取器创建失败");
        QVERIFY2(reader->open(path), "打开样本失败");
        QVector<CanFrame> frames;
        const int n = reader->readAll(frames);
        reader->close();
        QVERIFY2(n > 0, qPrintable(QStringLiteral("读出帧数 %1").arg(n)));
        qDebug() << "CANoe 15.7 ASC 帧数:" << n;
        bool anyFd = false;
        for (const CanFrame &f : frames) {
            verifyFrameInvariants(f);
            anyFd |= f.fd;
        }
        QVERIFY2(anyFd, "样本含 CANFD 帧但未解析出任何 FD 帧");
    }

    // ---- TC-05/ST-01：大 BLF 加载计时（基线记录） ----
    void readLargeBlfTiming()
    {
        const QString path = testData(
            QStringLiteral("CD701_LS6C3E1E2RA960027_2023-12-20_17-12-16.blf"));
        if (!QFile::exists(path))
            QSKIP("大样本文件不存在");
        auto reader = CanFileIOFactory::createReader(path);
        QVERIFY(reader && reader->open(path));
        QVector<CanFrame> frames;
        QElapsedTimer timer;
        timer.start();
        const int n = reader->readAll(frames);
        const qint64 ms = timer.elapsed();
        reader->close();
        QVERIFY2(n > 0, qPrintable(QStringLiteral("读出帧数 %1").arg(n)));
        qDebug() << "大 BLF: 帧数=" << n << "耗时=" << ms << "ms"
                 << "速率=" << (n / qMax<qint64>(ms, 1) * 1000.0 / 1.0) / 1000.0 << "k帧/s";
        // 宽松上限：120s 内必须完成（防回归恶化）
        QVERIFY2(ms < 120000, qPrintable(QStringLiteral("加载耗时 %1 ms 超上限").arg(ms)));
    }

    // ---- TC-16：截断 BLF 容错（不崩溃 + 行为确定） ----
    void readTruncatedBlf()
    {
        const QString path = testData(QStringLiteral("truncated.blf"));
        auto reader = CanFileIOFactory::createReader(path);
        QVERIFY(reader);
        // open 可能失败（文件头不完整）或成功（头在 1KB 内）；两者都合法，
        // 关键断言：全程不崩溃、readAll 不返回除 -1/非负外的异常值
        const bool opened = reader->open(path);
        qDebug() << "截断 BLF open:" << opened;
        if (opened) {
            QVector<CanFrame> frames;
            const int n = reader->readAll(frames);
            qDebug() << "截断 BLF readAll:" << n;
            QVERIFY2(n >= -1, "readAll 返回异常值");
            for (const CanFrame &f : frames)
                verifyFrameInvariants(f);
            reader->close();
        }
        // 执行到此即未崩溃 —— PASS
    }

    // ---- TC-17：不存在文件 ----
    void readNonExistent()
    {
        auto reader = CanFileIOFactory::createReader(
            testData(QStringLiteral("no_such_file.blf")));
        QVERIFY(reader);
        QVERIFY2(!reader->open(testData(QStringLiteral("no_such_file.blf"))),
                 "不存在文件 open 应失败");
    }

    // ---- TC-09/TC-12：ASC 导出-重导入闭环 ----
    void writeReadAscRoundtrip()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.path() + QStringLiteral("/rt.asc");

        QVector<CanFrame> written;
        // 覆盖：标准帧/扩展帧/CAN FD 64B/Tx 方向/递增时间戳
        written << makeFrame(0.000100, 0x123, 8, 0x10);
        written << makeFrame(0.010200, 0x1ABCDEF, 8, 0x20, /*ext*/ true);
        written << makeFrame(0.020300, 0x456, 64, 0x30, /*ext*/ false, /*fd*/ true);
        written << makeFrame(0.030400, 0x078, 3, 0x40, false, false, /*tx*/ true);

        auto writer = CanFileIOFactory::createWriter(path);
        QVERIFY2(writer, "asc 写入器创建失败");
        QVERIFY2(writer->open(path), "asc open 失败");
        for (const CanFrame &f : written)
            writer->writeFrame(f);
        writer->close();
        QCOMPARE(writer->frameCount(), written.size());

        auto reader = CanFileIOFactory::createReader(path);
        QVERIFY(reader && reader->open(path));
        QVector<CanFrame> readBack;
        const int n = reader->readAll(readBack);
        reader->close();
        QVERIFY2(n == readBack.size() && n > 0, "读回帧数异常");
        verifyRoundtrip(written, readBack);
    }

    // ---- TC-10/TC-12：CSV 导出-重导入闭环 ----
    void writeReadCsvRoundtrip()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.path() + QStringLiteral("/rt.csv");

        QVector<CanFrame> written;
        written << makeFrame(0.000100, 0x123, 8, 0x50);
        written << makeFrame(0.010200, 0x0FF, 2, 0x60, false, false, true);
        written << makeFrame(0.020300, 0x7FF, 8, 0x70);

        auto writer = CanFileIOFactory::createWriter(path);
        QVERIFY2(writer, "csv 写入器创建失败");
        QVERIFY2(writer->open(path), "csv open 失败");
        for (const CanFrame &f : written)
            writer->writeFrame(f);
        writer->close();

        auto reader = CanFileIOFactory::createReader(path);
        QVERIFY(reader && reader->open(path));
        QVector<CanFrame> readBack;
        const int n = reader->readAll(readBack);
        reader->close();
        QVERIFY2(n == readBack.size() && n > 0, "读回帧数异常");
        verifyRoundtrip(written, readBack);
    }

    // ---- TC-12 扩展：BLF 闭环（vector_blf 写入） ----
    void writeReadBlfRoundtrip()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.path() + QStringLiteral("/rt.blf");

        auto probe = CanFileIOFactory::createWriter(path);
        if (!probe)
            QSKIP("BLF 写入未启用（vector_blf 未链接）");

        QVector<CanFrame> written;
        written << makeFrame(0.000100, 0x123, 8, 0x80);
        written << makeFrame(0.010200, 0x234, 8, 0x90);

        auto writer = std::move(probe);
        QVERIFY2(writer->open(path), "blf open 失败");
        for (const CanFrame &f : written)
            writer->writeFrame(f);
        writer->close();

        auto reader = CanFileIOFactory::createReader(path);
        QVERIFY(reader && reader->open(path));
        QVector<CanFrame> readBack;
        const int n = reader->readAll(readBack);
        reader->close();
        QVERIFY2(n == readBack.size() && n > 0, "读回帧数异常");
        verifyRoundtrip(written, readBack);
    }

};

QTEST_GUILESS_MAIN(TestCanFileIO)
#include "test_canfileio.moc"
