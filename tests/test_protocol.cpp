// ============================================================
//  test_protocol — M2 多协议数据层预埋套件（doc/flow.md §13.3）
//
//  覆盖用例（验收需求 → 断言）：
//  - PT-01 报文转换：CanFrame ⇄ BusMessage 往返无损
//    （id/flags/通道/方向/payload/时间戳）
//  - PT-02 适配器注册表：内置 CAN 懒注册、枚举、按 id 查找、重复注册拒绝
//  - PT-03 解析器注册表：内置 DBC 懒注册、按 id/扩展名查找
//  - PT-04 DbcParser 直通：BusDefinitionSet 与 DbcManager 解析结果一致
//    （报文数/id/名称/DLC/信号字段逐一比对）
//  - PT-05 CanProtocolAdapter：traceColumns 12 列、decode 复用 DbcManager
//  - PT-06 BusDefinitionStore：定义集增删查、同文件替换语义
//  - PT-07 M3 端到端管道：ParserRegistry → DbcParser → DbcManager
//    直通加载 → 比对（doc/flow.md §13.4 验收 ②）
//
//  断言原则：全部基于自构造 DBC（可精确计算期望值），与 test_dbc 同源风格。
// ============================================================
#include <QtTest>
#include <QTemporaryDir>

#include "core/busmessage.h"
#include "core/dbcmanager.h"
#include "core/protocol/busdefinitionstore.h"
#include "core/protocol/canprotocoladapter.h"
#include "core/protocol/dbcparser.h"
#include "core/protocol/parserregistry.h"
#include "core/protocol/protocolregistry.h"
#include "utils/canutils.h"

/// 自构造最小 DBC（与 test_dbc 同源）：TestMsg (id=291, 8B) 三信号
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

class TestProtocol : public QObject
{
    Q_OBJECT

private slots:
    // ---- PT-01：CanFrame ⇄ BusMessage 往返无损 ----
    void busMessageRoundTrip()
    {
        CanFrame f;
        f.timestampNs = 1234567890ULL;
        f.id = 0x1ABCDEF;      // 扩展帧 ID
        f.extended = true;
        f.fd = true;
        f.bitrateSwitch = true;
        f.errorState = false;
        f.dlc = 9;             // FD 12 字节
        f.data = QByteArray::fromHex("0102030405060708090a0b0c");
        f.channel = 3;
        f.direction = CanFrame::Tx;

        const BusMessage m = toBusMessage(f);
        QCOMPARE(m.bus, BusType::Can);
        QCOMPARE(m.timestampNs, f.timestampNs);
        QCOMPARE(m.id, f.id);
        QCOMPARE(m.channel, f.channel);
        QCOMPARE(m.direction, quint8(1));
        QVERIFY(m.flags & BusMessageFlags::CanExtended);
        QVERIFY(m.flags & BusMessageFlags::CanFd);
        QVERIFY(m.flags & BusMessageFlags::CanBrs);
        QVERIFY(!(m.flags & BusMessageFlags::CanEsi));
        QVERIFY(m.flags & BusMessageFlags::DirectionTx);
        QCOMPARE(m.payload, f.data);

        const CanFrame back = toCanFrame(m);
        QCOMPARE(back.timestampNs, f.timestampNs);
        QCOMPARE(back.id, f.id);
        QCOMPARE(back.extended, f.extended);
        QCOMPARE(back.fd, f.fd);
        QCOMPARE(back.bitrateSwitch, f.bitrateSwitch);
        QCOMPARE(back.errorState, f.errorState);
        QCOMPARE(back.channel, f.channel);
        QCOMPARE(back.direction, f.direction);
        QCOMPARE(back.data, f.data);
        QCOMPARE(back.dlc, f.dlc);
        QCOMPARE(back.timestamp, f.timestampNs / 1e9);

        // Rx 方向位
        f.direction = CanFrame::Rx;
        const BusMessage m2 = toBusMessage(f);
        QCOMPARE(m2.direction, quint8(0));
        QVERIFY(!(m2.flags & BusMessageFlags::DirectionTx));
    }

    // ---- PT-02：ProtocolRegistry 内置 CAN 枚举 ----
    void protocolRegistryEnumeratesCan()
    {
        ProtocolRegistry *reg = ProtocolRegistry::instance();
        QVERIFY(reg);
        QVERIFY(reg->count() >= 1);
        QVERIFY(reg->findAdapter(QStringLiteral("can")) != nullptr);
        QVERIFY(reg->findAdapter(QStringLiteral("no-such-protocol")) == nullptr);

        // 重复注册拒绝（同 protocolId）
        CanProtocolAdapter dup;
        QVERIFY(!reg->registerAdapter(&dup));

        auto *can = reg->findAdapter(QStringLiteral("can"));
        QCOMPARE(can->displayName(), QStringLiteral("CAN Flow"));
        QCOMPARE(can->maxChannels(), 16);
        QCOMPARE(can->acceptedParsers(), QStringList{ QStringLiteral("dbc") });
        QVERIFY(can->supportedSources().contains(QStringLiteral("hardware")));
        QVERIFY(can->supportedSources().contains(QStringLiteral("file")));
        QVERIFY(can->supportedSources().contains(QStringLiteral("simulator")));
    }

    // ---- PT-03：ParserRegistry 内置 DBC 查找 ----
    void parserRegistryFindsDbc()
    {
        ParserRegistry *reg = ParserRegistry::instance();
        QVERIFY(reg);
        QVERIFY(reg->findParser(QStringLiteral("dbc")) != nullptr);
        QCOMPARE(reg->findParser(QStringLiteral("dbc"))->displayName(),
                 QStringLiteral("DBC 数据库"));
        QVERIFY(reg->findParser(QStringLiteral("no-such-parser")) == nullptr);

        // 扩展名查找（大小写不敏感）
        QVERIFY(reg->findParserForExtension(QStringLiteral("dbc")) != nullptr);
        QVERIFY(reg->findParserForExtension(QStringLiteral(".DBC")) != nullptr);
        QVERIFY(reg->findParserForExtension(QStringLiteral("xyz")) == nullptr);

        // 重复注册拒绝
        DbcParser dup;
        QVERIFY(!reg->registerParser(&dup));
    }

    // ---- PT-04：DbcParser 直通 — 与 DbcManager 解析结果一致 ----
    void dbcParserMatchesDbcManager()
    {
        QTemporaryDir dir;
        const QString path = writeSyntheticDbc(dir.path());
        QVERIFY(!path.isEmpty());

        DbcParser parser;
        QString error;
        const BusDefinitionSet set = parser.parse(path, &error);
        QVERIFY(!set.isEmpty());
        QCOMPARE(set.parserId, QStringLiteral("dbc"));
        QCOMPARE(set.filePath, path);
        QCOMPARE(set.messages.size(), 1);

        // 与 DbcManager 直连解析逐一比对（§13.3 验收 ②）
        DbcManager mgr;
        QVERIFY(mgr.loadDbc(path));
        QCOMPARE(mgr.files().size(), 1);
        const auto &msgs = mgr.files().first().messages;
        QCOMPARE(set.messages.size(), msgs.size());
        QCOMPARE(set.messages.first().id, msgs.first().id);
        QCOMPARE(set.messages.first().name, msgs.first().name);
        QCOMPARE(set.messages.first().length, msgs.first().dlc);
        QCOMPARE(set.messages.first().signalList.size(), msgs.first().signalList.size());

        const BusSignalDef &sig = set.messages.first().signalList.first();
        const DbcSignal &ref = msgs.first().signalList.first();
        QCOMPARE(sig.name, ref.name);
        QCOMPARE(sig.startBit, ref.startBit);
        QCOMPARE(sig.bitLength, ref.bitLength);
        QCOMPARE(sig.littleEndian, ref.littleEndian);
        QCOMPARE(sig.factor, ref.factor);
        QCOMPARE(sig.offset, ref.offset);
        QCOMPARE(sig.unit, ref.unit);

        // 集内查找
        QVERIFY(set.findMessage(291) != nullptr);
        QVERIFY(set.findMessage(999) == nullptr);

        // 失败路径：不存在文件 → 空集 + error
        QString err2;
        const BusDefinitionSet bad = parser.parse(
            dir.path() + QStringLiteral("/no-such.dbc"), &err2);
        QVERIFY(bad.isEmpty());
        QVERIFY(!err2.isEmpty());
    }

    // ---- PT-05：CanProtocolAdapter traceColumns / decode ----
    void canAdapterColumnsAndDecode()
    {
        auto *can = ProtocolRegistry::instance()->findAdapter(QStringLiteral("can"));
        QVERIFY(can);

        // traceColumns：与 CanTraceModel 现有 12 列一致
        const auto cols = can->traceColumns();
        QCOMPARE(cols.size(), 12);
        QCOMPARE(cols.first().key, QStringLiteral("no"));
        QCOMPARE(cols.first().title, QStringLiteral("No."));
        QCOMPARE(cols.last().key, QStringLiteral("signal"));
        QCOMPARE(cols.last().title, QStringLiteral("Signals"));

        // decode：无 DbcManager 引用时返回空（M2 不接线）
        BusMessage msg;
        msg.bus = BusType::Can;
        msg.id = 291;
        msg.payload = QByteArray::fromHex("401f280100000000");
        QVERIFY(can->decode(msg).isEmpty());

        // decode：注入 DbcManager 后复用其解码路径——用本地实例验证
        // （注册表持有的实例与消费者只经接口交互，具体类型注入是 F1 壳的接线点）
        QTemporaryDir dir;
        const QString path = writeSyntheticDbc(dir.path());
        DbcManager mgr;
        QVERIFY(mgr.loadDbc(path));

        CanProtocolAdapter canAdapter;
        canAdapter.setDbcManager(&mgr);

        const auto decoded = canAdapter.decode(msg);
        QCOMPARE(decoded.size(), 3);
        // EngineSpeed raw=8000 → 1000 rpm
        QCOMPARE(decoded.at(0).name, QStringLiteral("EngineSpeed"));
        QCOMPARE(decoded.at(0).value, 1000.0);
        QCOMPARE(decoded.at(0).unit, QStringLiteral("rpm"));
        // 非 CAN 总线报文不解码
        msg.bus = BusType::General;
        QVERIFY(canAdapter.decode(msg).isEmpty());

        // formatField：ID/数据格式化与 CanUtils 一致
        msg.bus = BusType::Can;
        msg.id = 0x123;
        msg.flags = 0;
        QCOMPARE(can->formatField(msg, QStringLiteral("id")),
                 CanUtils::formatId(0x123, false));   // "0X123"
        QCOMPARE(can->formatField(msg, QStringLiteral("dir")), QStringLiteral("Rx"));
        QCOMPARE(can->formatField(msg, QStringLiteral("no-such-key")), QString());
    }

    // ---- PT-06：BusDefinitionStore 定义集增删查 ----
    void definitionStoreCrud()
    {
        QTemporaryDir dir;
        const QString path = writeSyntheticDbc(dir.path());
        DbcParser parser;
        const BusDefinitionSet set = parser.parse(path, nullptr);
        QVERIFY(!set.isEmpty());

        BusDefinitionStore *store = BusDefinitionStore::instance();
        store->addDefinitionSet(set);
        QCOMPARE(store->definitionSets().size(), 1);
        QVERIFY(store->findMessage(QStringLiteral("can"), 291) != nullptr);
        QCOMPARE(store->findMessage(QStringLiteral("can"), 291)->name,
                 QStringLiteral("TestMsg"));
        QVERIFY(store->findSignal(QStringLiteral("can"), 291,
                                  QStringLiteral("EngineSpeed")) != nullptr);
        QVERIFY(store->findSignal(QStringLiteral("can"), 291,
                                  QStringLiteral("Nope")) == nullptr);
        QCOMPARE(store->messageCount(QStringLiteral("can")), 1);

        // 同文件重复加入 = 替换（不产生重复定义集）
        store->addDefinitionSet(set);
        QCOMPARE(store->definitionSets().size(), 1);

        // 移除后查找失效
        QVERIFY(store->removeDefinitionSet(path));
        QVERIFY(store->findMessage(QStringLiteral("can"), 291) == nullptr);
        QVERIFY(!store->removeDefinitionSet(path));   // 再移除返回 false
    }

    // ---- PT-07：M3 端到端管道 — 注册表加载 → 直通 DbcManager → 比对 ----
    void registryPipelineEndToEnd()
    {
        QTemporaryDir dir;
        const QString path = writeSyntheticDbc(dir.path());

        // M3 管道（doc/flow.md §13.4）：扩展名 → 解析器 → 定义集 → store
        ParserRegistry *parsers = ParserRegistry::instance();
        IBusParser *p = parsers->findParserForExtension(
            QFileInfo(path).suffix());
        QVERIFY(p != nullptr);
        QCOMPARE(p->parserId(), QStringLiteral("dbc"));

        QString error;
        const BusDefinitionSet set = p->parse(path, &error);
        QVERIFY(!set.isEmpty());
        BusDefinitionStore::instance()->addDefinitionSet(set);

        // 直通 DbcManager（外部行为不变的双入口）
        DbcManager mgr;
        QVERIFY(mgr.loadDbc(path));

        // 比对：store 查询结果与 DbcManager 完全一致
        const DbcMessage *dm = mgr.findMessage(291);
        QVERIFY(dm != nullptr);
        const BusMessageDef *bm = BusDefinitionStore::instance()->findMessage(
            QStringLiteral("can"), 291);
        QVERIFY(bm != nullptr);
        QCOMPARE(bm->name, dm->name);
        QCOMPARE(bm->length, dm->dlc);
        QCOMPARE(bm->signalList.size(), dm->signalList.size());
        for (int i = 0; i < bm->signalList.size(); ++i) {
            QCOMPARE(bm->signalList.at(i).name, dm->signalList.at(i).name);
            QCOMPARE(bm->signalList.at(i).factor, dm->signalList.at(i).factor);
            QCOMPARE(bm->signalList.at(i).offset, dm->signalList.at(i).offset);
        }

        BusDefinitionStore::instance()->removeDefinitionSet(path);
    }
};

QTEST_GUILESS_MAIN(TestProtocol)
#include "test_protocol.moc"
