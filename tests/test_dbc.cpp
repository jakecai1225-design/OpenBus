// ============================================================
//  test_dbc — B4 DBC 解析解码套件（doc/测试验收方案.md §四 B4）
//
//  覆盖用例（验收需求 → 断言）：
//  - DB-01 加载真实 DBC：V5.4.0 样本 → 报文/信号结构自洽
//  - DB-02 信号解码：自构造 DBC（已知 factor/offset/字节序/值表）
//    → decodeFrame/decodeSignal 精确物理值断言（需求语义）
//  - DB-03 编码闭环：encode 物理值 → decode 读回一致
//  - DB-05 卸载 DBC：卸载后 findMessage 失效
//  - DB-16 异常容错：乱码 DBC 不崩溃、返回失败
//  - 元数据完整性：factor/offset/unit/Intel 字节序/值表描述
//
//  断言原则：真实 DBC 样本只断言结构自洽（内容未知）；
//  语义断言全部基于自构造 DBC（可精确计算期望值）。
// ============================================================
#include <QtTest>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include "core/dbcmanager.h"

static QString testData(const QString &name)
{
    return QString::fromLatin1(SIN_SOURCE_DIR) + QStringLiteral("/test/resources/") + name;
}

/// 自构造最小 DBC：TestMsg (id=291=0x123, 8B) 三个信号
///   EngineSpeed : bit0|16 Intel 无符号 factor=0.125 offset=0 unit=rpm
///   Temperature : bit16|8 Intel 无符号 factor=1 offset=-40 unit=degC
///   MotorActive : bit24|1 Intel 无符号 factor=1 + 值表 1=Active 0=Inactive
static QString writeSyntheticDbc(const QString &dir)
{
    static const char *dbcText = R"DBC(VERSION "synthetic-test"

NS_ :
    CM_
    BA_DEF_
    BA_
    VAL_

BS_:

BU_: TEST_ECU RECEIVER_ECU

BO_ 291 TestMsg: 8 TEST_ECU
 SG_ EngineSpeed : 0|16@1+ (0.125,0) [0|8191.875] "rpm" RECEIVER_ECU
 SG_ Temperature : 16|8@1+ (1,-40) [-40|215] "degC" RECEIVER_ECU
 SG_ MotorActive : 24|1@1+ (1,0) [0|1] "" RECEIVER_ECU

VAL_ 291 MotorActive 1 "Active" 0 "Inactive" ;
)DBC";
    const QString path = dir + QStringLiteral("/synthetic.dbc");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString();
    f.write(dbcText);
    f.close();
    return path;
}

/// TestMsg 的已知数据帧：
///   data[0..1] = 0x40 0x1F → EngineSpeed raw=0x1F40=8000 → 8000*0.125 = 1000 rpm
///   data[2]    = 0x28      → Temperature raw=40 → 40-40 = 0 degC
///   data[3]    = 0x01      → MotorActive raw=1 → 1（值表 "Active"）
static QByteArray knownTestMsgData()
{
    return QByteArray::fromHex("401f280100000000");
}

class TestDbc : public QObject
{
    Q_OBJECT

private slots:
    // ---- DB-01：加载真实 DBC（结构自洽）----
    void loadRealDbc()
    {
        const QString path = testData(QStringLiteral("V5.4.0_20260605_INFO_CAN.dbc"));
        if (!QFile::exists(path))
            QSKIP("真实 DBC 样本不存在");

        DbcManager mgr;
        QVERIFY2(mgr.loadDbc(path), "加载 V5.4.0 DBC 失败");
        QVERIFY(!mgr.files().isEmpty());

        const auto msgs = mgr.allMessages();
        QVERIFY2(msgs.size() > 10, qPrintable(QStringLiteral("报文数 %1").arg(msgs.size())));
        qDebug() << "真实 DBC 报文数:" << msgs.size();

        // 任取一个报文验证索引一致性 + 结构自洽
        const DbcMessage *m = msgs.first();
        QVERIFY(m);
        QCOMPARE(mgr.findMessage(m->id), m);          // O(1) 索引命中同一对象
        QVERIFY2(!m->name.isEmpty(), "报文名非空");
        QVERIFY2(m->dlc > 0 && m->dlc <= 8, "经典 CAN DLC 范围");
        for (const DbcSignal &s : m->signalList) {
            QVERIFY(!s.name.isEmpty());
            QVERIFY2(s.bitLength > 0 && s.bitLength <= 64, "信号位长范围");
            QVERIFY2(s.factor != 0.0, "factor 非零");
        }
    }

    // ---- DB-02 前置：自构造 DBC 元数据解析 ----
    void syntheticDbcMetadata()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeSyntheticDbc(dir.path());
        QVERIFY2(!path.isEmpty(), "写自构造 DBC 失败");

        DbcManager mgr;
        QVERIFY2(mgr.loadDbc(path), "加载自构造 DBC 失败");

        const DbcMessage *m = mgr.findMessage(291);
        QVERIFY2(m, "找不到 TestMsg (291)");
        QCOMPARE(m->name, QStringLiteral("TestMsg"));
        QCOMPARE(m->dlc, 8);
        QCOMPARE(m->signalList.size(), 3);

        const DbcSignal *spd = mgr.findSignal(291, QStringLiteral("EngineSpeed"));
        QVERIFY(spd);
        QCOMPARE(spd->startBit, 0);
        QCOMPARE(spd->bitLength, 16);
        QVERIFY(spd->littleEndian);                    // @1 = Intel
        QVERIFY(!spd->isSigned);
        QCOMPARE(spd->factor, 0.125);
        QCOMPARE(spd->offset, 0.0);
        QCOMPARE(spd->unit, QStringLiteral("rpm"));

        const DbcSignal *tmp = mgr.findSignal(291, QStringLiteral("Temperature"));
        QVERIFY(tmp);
        QCOMPARE(tmp->factor, 1.0);
        QCOMPARE(tmp->offset, -40.0);
        QCOMPARE(tmp->unit, QStringLiteral("degC"));

        const DbcSignal *act = mgr.findSignal(291, QStringLiteral("MotorActive"));
        QVERIFY(act);
        QCOMPARE(act->lookupValueDesc(1), QStringLiteral("Active"));
        QCOMPARE(act->lookupValueDesc(0), QStringLiteral("Inactive"));
    }

    // ---- DB-02：已知帧解码精确断言 ----
    void decodeKnownValues()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeSyntheticDbc(dir.path());
        DbcManager mgr;
        QVERIFY(mgr.loadDbc(path));

        const QByteArray data = knownTestMsgData();

        // 批量解码：信号名 → 物理值
        const auto decoded = mgr.decodeFrame(291, data);
        QCOMPARE(decoded.size(), 3);
        QHash<QString, double> byName;
        QHash<QString, QString> byDesc;
        for (const auto &d : decoded) {
            byName[d.name] = d.physValue;
            byDesc[d.name] = d.valueDesc;
        }
        QCOMPARE(byName[QStringLiteral("EngineSpeed")], 1000.0);  // 8000*0.125
        QCOMPARE(byName[QStringLiteral("Temperature")], 0.0);     // 40-40
        QCOMPARE(byName[QStringLiteral("MotorActive")], 1.0);
        QCOMPARE(byDesc[QStringLiteral("MotorActive")], QStringLiteral("Active"));

        // 单信号解码接口
        double v = 0;
        QVERIFY2(mgr.decodeSignal(291, QStringLiteral("EngineSpeed"), data, v),
                 "decodeSignal 返回失败");
        QCOMPARE(v, 1000.0);

        // 未定义报文 → decodeSignal false
        QVERIFY(!mgr.decodeSignal(0x7FF, QStringLiteral("x"), data, v));
    }

    // ---- DB-03：编码-解码闭环 ----
    void encodeDecodeRoundtrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeSyntheticDbc(dir.path());
        DbcManager mgr;
        QVERIFY(mgr.loadDbc(path));

        const DbcSignal *sig = mgr.findSignal(291, QStringLiteral("EngineSpeed"));
        QVERIFY(sig);

        // 物理值 2000 rpm → raw = 2000/0.125 = 16000 = 0x3E80 → data[0]=0x80 data[1]=0x3E
        QByteArray data(8, 0);
        sig->encode(data, 2000.0);
        QCOMPARE(static_cast<quint8>(data[0]), quint8(0x80));
        QCOMPARE(static_cast<quint8>(data[1]), quint8(0x3E));

        // 读回一致
        QCOMPARE(sig->decode(data), 2000.0);
    }

    // ---- DB-05：卸载 ----
    void unloadDbc()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeSyntheticDbc(dir.path());
        DbcManager mgr;
        QVERIFY(mgr.loadDbc(path));
        QVERIFY(mgr.findMessage(291));

        mgr.unloadDbc(path);
        QVERIFY(!mgr.findMessage(291));
        QVERIFY(mgr.allMessages().isEmpty());
    }

    // ---- DB-16：乱码 DBC 容错 ----
    void badDbcTolerated()
    {
        const QString path = testData(QStringLiteral("bad.dbc"));
        if (!QFile::exists(path))
            QSKIP("bad.dbc 样本不存在");

        DbcManager mgr;
        const bool ok = mgr.loadDbc(path); // 乱码内容 → 失败（或空）且不崩溃
        if (ok) {
            // 若容错解析返回 true，则必须无报文（不可产生垃圾报文）
            QVERIFY2(mgr.allMessages().isEmpty(), "乱码 DBC 不应产出报文");
        }
        QVERIFY(!mgr.findMessage(291));
    }
};

QTEST_GUILESS_MAIN(TestDbc)
#include "test_dbc.moc"
