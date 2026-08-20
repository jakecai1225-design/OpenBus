// ============================================================
//  test_filterengine — B2 过滤引擎套件（doc/测试验收方案.md §四 B2）
//
//  覆盖用例（验收需求 → 断言）：
//  - TF-01 表达式过滤：id==0x123 精确过滤（含预设保存/恢复闭环）
//  - TF-02 逻辑组合：id>0x100 && dlc>4、!rx / not (...) or (...) 等
//  - TF-03 语法糖：裸 0x123、id in 0x100,0x200 等价展开
//  - TF-04 非法表达式：语法错误 → isValid=false + errorString 非空，
//    evaluate 保持透传（true），不影响正常收帧
//  - TF-13 表达式语义基础：变量全集（id/dlc/ch/time/fd/ext/rx/tx/std/error）
//    与 data contains 字节序列匹配
//
//  断言原则：全部自构造已知帧做精确断言（表达式语义为需求约定）。
// ============================================================
#include <QtTest>
#include <QTemporaryDir>

#include "core/canframe.h"
#include "core/filter_engine.h"
#include "core/filterpresetmanager.h"

/// QCOMPARE 带上下文消息（Qt Test 原生宏无 message 参数）；
/// 失败时在默认差异输出后追加表达式上下文，便于定位用例
#define QCOMPARE2(actual, expected, msg)                                        \
    do {                                                                        \
        if (!QTest::qCompare(actual, expected, #actual, #expected,              \
                             __FILE__, __LINE__)) {                             \
            QTest::qFail(qPrintable(msg), __FILE__, __LINE__);                  \
            return;                                                             \
        }                                                                       \
    } while (false)

/// 构造测试帧：数据为 firstByte 起的递增字节序列
static CanFrame mkFrame(quint32 id, int len, int firstByte,
                        bool ext = false, bool fd = false, bool tx = false)
{
    CanFrame f;
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

class TestFilterEngine : public QObject
{
    Q_OBJECT

private slots:
    // ---- TF-01：基础表达式过滤 ----
    void compileAndEval()
    {
        FilterEngine fe;
        QVERIFY2(fe.compile(QStringLiteral("id == 0x123")), qPrintable(fe.errorString()));
        QVERIFY(fe.isValid());
        QVERIFY(!fe.isEmpty());
        QCOMPARE(fe.evaluate(mkFrame(0x123, 8, 0)), true);
        QCOMPARE(fe.evaluate(mkFrame(0x124, 8, 0)), false);
    }

    // ---- 比较运算符全集 ----
    void comparisonOps()
    {
        const CanFrame f8 = mkFrame(0x100, 8, 0);   // dlc=8
        const CanFrame f4 = mkFrame(0x200, 4, 0);   // dlc=4

        struct Case { const char *expr; const CanFrame &f; bool want; };
        const Case cases[] = {
            { "dlc > 4",  f8, true  },
            { "dlc > 4",  f4, false },
            { "dlc >= 8", f8, true  },
            { "dlc < 8",  f8, false },
            { "dlc <= 8", f8, true  },
            { "id != 0x100", f8, false },
            { "id != 0x100", f4, true  },
            { "ch == 1",  f8, true  },
            { "time >= 0", f8, true  },
        };
        for (const auto &c : cases) {
            FilterEngine fe;
            QVERIFY2(fe.compile(QString::fromLatin1(c.expr)),
                     qPrintable(QStringLiteral("%1: %2").arg(c.expr, fe.errorString())));
            QCOMPARE2(fe.evaluate(c.f), c.want, QStringLiteral("expr=%1").arg(c.expr));
        }
    }

    // ---- TF-02：逻辑组合 ----
    void logicOps()
    {
        const CanFrame std8 = mkFrame(0x150, 8, 0);                  // id>0x100, dlc=8, rx, std
        const CanFrame extTx = mkFrame(0x1ABCDEF, 8, 0, true, false, true);
        const CanFrame fd64 = mkFrame(0x80, 64, 0, false, true);

        struct Case { const char *expr; const CanFrame &f; bool want; };
        const Case cases[] = {
            { "id > 0x100 && dlc > 4", std8,  true  },
            { "id > 0x100 && dlc > 4", fd64,  false }, // id=0x80 不满足
            { "!rx",                    extTx, true  }, // tx 帧
            { "!rx",                    std8,  false },
            { "not (id == 0x100 or id == 0x200)", std8, true },
            { "id < 0x100 || ext",      extTx, true  },
            { "id < 0x100 || ext",      std8,  false },
            { "fd && dlc > 8",          fd64,  true  },
        };
        for (const auto &c : cases) {
            FilterEngine fe;
            QVERIFY2(fe.compile(QString::fromLatin1(c.expr)),
                     qPrintable(QStringLiteral("%1: %2").arg(c.expr, fe.errorString())));
            QCOMPARE2(fe.evaluate(c.f), c.want, QStringLiteral("expr=%1").arg(c.expr));
        }
    }

    // ---- 变量全集语义 ----
    void variables()
    {
        const CanFrame stdRx = mkFrame(0x123, 8, 0);                          // std rx classic
        const CanFrame extTx = mkFrame(0x1ABCDEF, 8, 0, true, false, true);   // ext tx
        const CanFrame fd    = mkFrame(0x456, 64, 0, false, true);            // fd
        CanFrame err = mkFrame(0x123, 8, 0);
        err.id |= 0x20000000;                                                 // error frame

        struct Case { const char *expr; const CanFrame &f; bool want; };
        const Case cases[] = {
            { "std", stdRx, true  },
            { "std", extTx, false },
            { "ext", extTx, true  },
            { "ext", stdRx, false },
            { "tx",  extTx, true  },
            { "rx",  stdRx, true  },
            { "fd",  fd,    true  },
            { "fd",  stdRx, false },
            { "error", err, true  },
            { "error", stdRx, false },
        };
        for (const auto &c : cases) {
            FilterEngine fe;
            QVERIFY2(fe.compile(QString::fromLatin1(c.expr)),
                     qPrintable(QStringLiteral("%1: %2").arg(c.expr, fe.errorString())));
            QCOMPARE2(fe.evaluate(c.f), c.want, QStringLiteral("expr=%1").arg(c.expr));
        }
    }

    // ---- TF-03：语法糖（裸 hex / in 列表）----
    void syntaxSugar()
    {
        FilterEngine bare;
        QVERIFY2(bare.compile(QStringLiteral("0x123")), qPrintable(bare.errorString()));
        QCOMPARE(bare.evaluate(mkFrame(0x123, 8, 0)), true);
        QCOMPARE(bare.evaluate(mkFrame(0x456, 8, 0)), false);

        FilterEngine inList;
        QVERIFY2(inList.compile(QStringLiteral("id in 0x100,0x200")),
                 qPrintable(inList.errorString()));
        QCOMPARE(inList.evaluate(mkFrame(0x100, 8, 0)), true);
        QCOMPARE(inList.evaluate(mkFrame(0x200, 8, 0)), true);
        QCOMPARE(inList.evaluate(mkFrame(0x300, 8, 0)), false);
    }

    // ---- data contains 字节序列 ----
    void dataContains()
    {
        // 数据 A0 A1 A2 A3 ...
        const CanFrame f = mkFrame(0x123, 8, 0xA0);

        FilterEngine fe;
        QVERIFY2(fe.compile(QStringLiteral("data contains 01 02")), qPrintable(fe.errorString()));
        QCOMPARE(fe.evaluate(f), false);

        FilterEngine fe2;
        QVERIFY2(fe2.compile(QStringLiteral("data contains A0 A1 A2")), qPrintable(fe2.errorString()));
        QCOMPARE(fe2.evaluate(f), true);

        // 与其他条件组合
        FilterEngine fe3;
        QVERIFY2(fe3.compile(QStringLiteral("data contains a0 a1 and ch == 1")),
                 qPrintable(fe3.errorString()));
        QCOMPARE(fe3.evaluate(f), true);

        // 回归：错误帧不得因 data contains 求值通道误匹配
        // （曾因 __data_N__ 占位 makeVar(9) 与 error 变量同槽而恒 true）
        FilterEngine fe4;
        QVERIFY2(fe4.compile(QStringLiteral("data contains A0 A1")),
                 qPrintable(fe4.errorString()));
        CanFrame err = mkFrame(0x123, 8, 0x10); // 数据 10 11 12 ...，无 A0 A1
        err.id |= 0x20000000;                    // 错误帧
        QCOMPARE(fe4.evaluate(err), false);
    }

    // ---- TF-04：非法表达式 ----
    void invalidExpr()
    {
        const QStringList bad = {
            QStringLiteral("id >>"),        // 缺右操作数
            QStringLiteral("id =="),        // 缺右值
            QStringLiteral("((id == 1)"),   // 括号不配对
            QStringLiteral("id = 0x123"),   // 非法运算符（单 =）
        };
        for (const QString &expr : bad) {
            FilterEngine fe;
            const bool ok = fe.compile(expr);
            QVERIFY2(!ok, qPrintable(QStringLiteral("应判定非法: %1").arg(expr)));
            QVERIFY2(!fe.isValid(), qPrintable(expr));
            QVERIFY2(!fe.errorString().isEmpty(),
                     qPrintable(QStringLiteral("应给出错误信息: %1").arg(expr)));
            // 编译失败时 evaluate 透传（不影响正常收帧）
            QCOMPARE(fe.evaluate(mkFrame(0x123, 8, 0)), true);
        }
    }

    // ---- 空表达式 = 无过滤 ----
    void emptyExpr()
    {
        FilterEngine fe;
        QVERIFY(fe.isEmpty());
        QVERIFY(fe.evaluate(mkFrame(0x123, 8, 0))); // 透传

        QVERIFY(fe.compile(QString()));
        QVERIFY(fe.isEmpty());
        QVERIFY(fe.evaluate(mkFrame(0x456, 8, 0)));
    }

    // ---- TF-01 预设部分：预设管理 + .sfilter 闭环 ----
    void presetRoundtrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("t.sfilter"));

        // 构造时加载默认预设（内置项或用户保存过的默认文件），
        // 只断言新增/读写行为，不依赖初始列表内容
        FilterPresetManager pm;
        pm.addPreset(QStringLiteral("发动机"), QStringLiteral("id == 0x123"));
        pm.addPreset(QStringLiteral("FD 大帧"), QStringLiteral("fd && dlc > 8"));
        const QStringList before = pm.presetNames();
        QVERIFY(before.contains(QStringLiteral("发动机")));
        QVERIFY(before.contains(QStringLiteral("FD 大帧")));
        QCOMPARE(pm.findExpr(QStringLiteral("发动机")), QStringLiteral("id == 0x123"));
        QCOMPARE(pm.findExpr(QStringLiteral("不存在")), QString());

        // 保存 → 新实例加载 → 内容一致
        QVERIFY(pm.saveToFile(path));
        FilterPresetManager pm2;
        QVERIFY(pm2.loadFromFile(path));
        QCOMPARE(pm2.presetNames(), before);
        QCOMPARE(pm2.findExpr(QStringLiteral("FD 大帧")), QStringLiteral("fd && dlc > 8"));

        // 删除（按名称定位，不依赖内置顺序）
        const int fdIdx = pm2.presetNames().indexOf(QStringLiteral("FD 大帧"));
        QVERIFY(fdIdx >= 0);
        pm2.removePreset(fdIdx);
        QVERIFY(!pm2.presetNames().contains(QStringLiteral("FD 大帧")));

        // 加载不存在的文件 → 失败且原有内容不被破坏
        FilterPresetManager pm3;
        const QStringList before3 = pm3.presetNames();
        QVERIFY(!pm3.loadFromFile(dir.filePath(QStringLiteral("no.sfilter"))));
        QCOMPARE(pm3.presetNames(), before3);
    }
};

QTEST_GUILESS_MAIN(TestFilterEngine)
#include "test_filterengine.moc"
