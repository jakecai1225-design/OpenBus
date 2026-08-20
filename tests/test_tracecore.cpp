// ============================================================
//  test_tracecore — B3 Trace 模型套件（doc/测试验收方案.md §四 B3）
//
//  覆盖用例（验收需求 → 断言）：
//  - TC-11 实时追加（pending 队列 + flushPending 提交）
//  - TC-12 批量加载（appendFrames 直接写入）
//  - PB-01 容量限制：setMaxFrames 环形缓冲淘汰最老帧
//  - TF-15 覆盖模式：同 ID 帧替换
//  - TF-07/11/12 标记/标签/行颜色 API
//  - 帧序列号永不回退（seqCounter）
//  - frameCountForId 按 ID 统计
//  - FrameRole 数据访问
// ============================================================
#include <QtTest>

#include "core/canframe.h"
#include "models/cantracemodel.h"

static CanFrame mkFrame(double ts, quint32 id, int len = 8)
{
    CanFrame f;
    f.timestamp = ts;
    f.timestampNs = static_cast<quint64>(ts * 1e9);
    f.id = id;
    f.dlc = CanFrame::lengthToDlc(len);
    f.data = QByteArray(len, 0x55);
    f.channel = 1;
    return f;
}

class TestTraceCore : public QObject
{
    Q_OBJECT

private slots:
    // ---- TC-12：批量加载 ----
    void appendFramesBatch()
    {
        CanTraceModel m;
        QVector<CanFrame> frames;
        for (int i = 0; i < 100; ++i)
            frames << mkFrame(i * 0.001, 0x100 + (i % 8));
        m.appendFrames(frames);

        QCOMPARE(m.rowCount(), 100);
        QCOMPARE(m.frameCount(), 100);
        QCOMPARE(m.frameAt(0).id, quint32(0x100));
        QCOMPARE(m.frameAt(99).id, quint32(0x100 + (99 % 8))); // 0x103

        // FrameRole 返回完整帧
        const QModelIndex idx = m.index(50, 0);
        const CanFrame f = idx.data(CanTraceModel::FrameRole).value<CanFrame>();
        QCOMPARE(f.id, m.frameAt(50).id);

        m.clear();
        QCOMPARE(m.rowCount(), 0);
    }

    // ---- TC-11：实时追加 + 手动 flush（暂停刷新率下）----
    void appendFrameFlushPending()
    {
        CanTraceModel m;
        m.setRefreshRate(CanTraceModel::Paused); // 不自动 flush，测试可控

        m.appendFrame(mkFrame(0.0, 0x123));
        m.appendFrame(mkFrame(0.001, 0x124));
        m.flushPending();

        QCOMPARE(m.frameCount(), 2);
        QCOMPARE(m.frameAt(1).id, quint32(0x124));
    }

    // ---- PB-01：容量限制（环形缓冲淘汰最老帧）----
    void ringBufferCapacity()
    {
        CanTraceModel m;
        m.setMaxFrames(50);
        QCOMPARE(m.maxFrames(), 50);

        QVector<CanFrame> frames;
        for (int i = 0; i < 80; ++i)
            frames << mkFrame(i * 0.001, 0x100 + i);
        m.appendFrames(frames);

        QCOMPARE(m.rowCount(), 50);            // 容量封顶
        QCOMPARE(int(m.seqCounter()), 80);     // 序列号含被淘汰帧，永不回退
        // 最老 30 帧被淘汰，首帧应为第 31 帧（id=0x100+30）
        QCOMPARE(m.frameAt(0).id, quint32(0x100 + 30));
        QCOMPARE(m.frameAt(49).id, quint32(0x100 + 79));
    }

    // ---- TF-15：覆盖模式（同 ID 替换）----
    void overwriteMode()
    {
        CanTraceModel m;
        m.setOverwriteMode(true);
        QVERIFY(m.isOverwriteMode());

        QVector<CanFrame> frames;
        frames << mkFrame(0.0, 0x100) << mkFrame(0.001, 0x200) << mkFrame(0.002, 0x100);
        m.appendFrames(frames);

        // 同 ID 0x100 第二帧覆盖第一帧：总行数 2，第 0 行为最新 0x100
        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(m.frameAt(0).id, quint32(0x100));
        QCOMPARE(m.frameAt(1).id, quint32(0x200));
        // 覆盖模式语义：同 ID 替换不占新序号（seq = 有效帧提交数）
        QCOMPARE(int(m.seqCounter()), 2);
    }

    // ---- TF-07/11：行标记 ----
    void rowMarks()
    {
        CanTraceModel m;
        QVector<CanFrame> frames;
        for (int i = 0; i < 10; ++i)
            frames << mkFrame(i * 0.001, 0x100 + i);
        m.appendFrames(frames);

        QVERIFY(!m.isMarked(3));
        m.toggleMark(3);
        QVERIFY(m.isMarked(3));
        m.toggleMark(5);
        const QList<int> expectedMarks{3, 5};
        QCOMPARE(m.markedRows(), expectedMarks);

        // FrameRole 同行读取标记态
        QVERIFY(m.index(3, 0).data(CanTraceModel::MarkedRole).toBool());

        m.setMarked(3, false);
        QVERIFY(!m.isMarked(3));
        m.clearMarks();
        QVERIFY(m.markedRows().isEmpty());
    }

    // ---- TF-12：行颜色 ----
    void rowColors()
    {
        CanTraceModel m;
        QVector<CanFrame> frames;
        for (int i = 0; i < 5; ++i)
            frames << mkFrame(i * 0.001, 0x100 + i);
        m.appendFrames(frames);

        const QColor red(Qt::red);
        m.setRowColor(2, red);
        QCOMPARE(m.rowColor(2), red);
        QVERIFY(!m.rowColor(1).isValid());
        m.clearColors();
        QVERIFY(!m.rowColor(2).isValid());
    }

    // ---- 行标签（书签）----
    void rowLabels()
    {
        CanTraceModel m;
        QVector<CanFrame> frames;
        for (int i = 0; i < 5; ++i)
            frames << mkFrame(i * 0.001, 0x100 + i);
        m.appendFrames(frames);

        m.setRowLabel(1, QStringLiteral("异常点"));
        m.toggleMark(1);
        QCOMPARE(m.rowLabel(1), QStringLiteral("异常点"));

        const auto marks = m.labeledMarks();
        QCOMPARE(marks.size(), 1);
        QCOMPARE(marks.first().first, 1);
        QCOMPARE(marks.first().second, QStringLiteral("异常点"));
    }

    // ---- 按 ID 统计 ----
    void frameCountForId()
    {
        CanTraceModel m;
        QVector<CanFrame> frames;
        frames << mkFrame(0.0, 0x100) << mkFrame(0.001, 0x100)
               << mkFrame(0.002, 0x200);
        m.appendFrames(frames);

        QCOMPARE(m.frameCountForId(0x100), 2);
        QCOMPARE(m.frameCountForId(0x200), 1);
        QCOMPARE(m.frameCountForId(0x300), 0);
    }

    // ---- 时间参考点 ----
    void timeReference()
    {
        CanTraceModel m;
        QVector<CanFrame> frames;
        for (int i = 0; i < 5; ++i)
            frames << mkFrame(10.0 + i * 0.5, 0x100); // 10.0s 起
        m.appendFrames(frames);

        QVERIFY(!m.hasTimeReference());
        m.setTimeReference(2);
        QVERIFY(m.hasTimeReference());
        QCOMPARE(m.timeReferenceRow(), 2);
        QCOMPARE(m.timeReferenceTimestamp(), 11.0);

        m.clearTimeReference();
        QVERIFY(!m.hasTimeReference());
    }
};

QTEST_GUILESS_MAIN(TestTraceCore)
#include "test_tracecore.moc"
