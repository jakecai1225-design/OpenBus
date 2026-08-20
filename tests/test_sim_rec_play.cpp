// ============================================================
//  test_sim_rec_play — B5 采集/录制/回放套件（doc/测试验收方案.md §四 B5）
//
//  覆盖用例（验收需求 → 断言）：
//  - SIM-01 模拟器生命周期与配置（start/stop 幂等、channel/interval）
//  - SIM-02 帧流合法性：ID ∈ 预定义池、DLC↔数据长度一致、时间戳单调
//  - SIM-03 流量构成：扩展帧与 CAN FD 帧按比例出现
//  - REC-01 录制 ASC 闭环：信号时序 + 文件可读回（reader 正确性由 B1 保证）
//  - REC-02 暂停/恢复语义与失败分支
//  - PLAY-01 加载元数据（totalFrames/totalTime/currentTime/speed）
//  - PLAY-02 加速回放：framePlayed 逐帧有序 + finished + 循环回绕
//  - PLAY-03 seek/pause/stop 控制
// ============================================================
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QSet>

#include "core/canframe.h"
#include "core/cansimulator.h"
#include "core/recorder.h"
#include "core/player.h"
#include "core/canfileio/canfileio_factory.h"

// 注：本套件所有 QSignalSpy 均用 SIGNAL() 字符串构造 —— 新式 PMF 构造
// （QMetaMethod::fromSignal）在"类定义于 openbus_data.dll、调用在测试
// exe"的 MinGW 跨模块场景下解析失败（"Null signal is not valid"，
// 2026-08-20 B 轮实测）；字符串查找走运行时元对象，可靠。

static CanFrame mkFrame(double ts, quint32 id, bool ext = false, bool fdFrame = false)
{
    CanFrame f;
    f.timestamp = ts;
    f.timestampNs = static_cast<quint64>(ts * 1e9);
    f.id = id;
    f.extended = ext;
    f.fd = fdFrame;
    const int len = fdFrame ? 64 : 8;
    f.dlc = CanFrame::lengthToDlc(len);
    f.data = QByteArray(len, 0x5A);
    f.channel = 1;
    return f;
}

class TestSimRecPlay : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // QSignalSpy 记录自定义类型参数需要运行期元类型注册
        qRegisterMetaType<CanFrame>("CanFrame");
    }

    // ---- SIM-01：生命周期与配置 ----
    void simulatorLifecycle()
    {
        CanSimulator sim;
        QCOMPARE(sim.channel(), quint8(1));
        sim.setChannel(2);
        QCOMPARE(sim.channel(), quint8(2));
        sim.setIntervalMs(1);
        QCOMPARE(sim.intervalMs(), 1);

        QVERIFY(!sim.isRunning());
        sim.start();
        QVERIFY(sim.isRunning());
        sim.start(); // 重复启动幂等
        QVERIFY(sim.isRunning());
        sim.stop();
        QVERIFY(!sim.isRunning());
    }

    // ---- SIM-02/03：帧流合法性与构成 ----
    void simulatorFrameStream()
    {
        // 预定义 ID 池（cansimulator.h：8 标准 + 3 扩展）
        static const QSet<quint32> idPool = {
            0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x700, 0x7FF,
            0x18FEF100, 0x18FFA827, 0x18EAFFFE,
        };

        CanSimulator sim;
        sim.setIntervalMs(1); // 加速生成
        QSignalSpy spy(&sim, SIGNAL(frameGenerated(CanFrame)));
        sim.start();

        // 120 帧 @1ms ≈ 0.2s（宽限 10s）
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 120, 10000);
        sim.stop();

        bool sawExtended = false;
        bool sawFd = false;
        double lastTs = 0.0;
        for (const auto &args : spy) {
            const CanFrame f = args.at(0).value<CanFrame>();
            QVERIFY2(idPool.contains(f.id),
                     qPrintable(QString("ID 0x%1 不在预定义池").arg(f.id, 0, 16)));
            QCOMPARE(f.length(), CanFrame::dlcToLength(f.dlc));
            QCOMPARE(f.channel, quint8(1));
            QVERIFY(f.timestamp >= lastTs); // 时间戳单调不减
            lastTs = f.timestamp;
            sawExtended |= f.extended;
            sawFd |= f.fd;
        }
        // 生成比例：扩展 15% / FD 20%，120 帧内缺席概率 < 1e-8
        QVERIFY2(sawExtended, "120 帧内未出现扩展帧（生成比例异常）");
        QVERIFY2(sawFd, "120 帧内未出现 CAN FD 帧（生成比例异常）");
    }

    // ---- REC-01：录制 ASC 闭环 ----
    void recorderRoundtrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("rec.asc");

        Recorder rec;
        QSignalSpy started(&rec, SIGNAL(recordingStarted(QString)));
        QSignalSpy recorded(&rec, SIGNAL(frameRecorded(int)));
        QSignalSpy stopped(&rec, SIGNAL(recordingStopped(QString,int)));

        QVERIFY(!rec.isRecording());
        QVERIFY(rec.start(path));
        QVERIFY(rec.isRecording());
        QCOMPARE(started.count(), 1);
        QCOMPARE(started.first().first().toString(), path);

        QVector<CanFrame> frames;
        frames << mkFrame(0.000, 0x100)
               << mkFrame(0.001, 0x101)
               << mkFrame(0.002, 0x18FF0000, true) // 扩展帧
               << mkFrame(0.003, 0x102)
               << mkFrame(0.004, 0x103);
        for (const auto &f : frames)
            rec.recordFrame(f);

        QCOMPARE(rec.frameCount(), 5);
        QCOMPARE(recorded.count(), 5);
        QCOMPARE(recorded.at(4).first().toInt(), 5); // totalFrames 递增到最后帧

        rec.stop();
        QVERIFY(!rec.isRecording());
        QCOMPARE(stopped.count(), 1);
        QCOMPARE(stopped.first().first().toString(), path);
        QCOMPARE(stopped.first().at(1).toInt(), 5);
        QVERIFY(QFileInfo::exists(path));

        // 读回验证（reader 正确性已由 B1 套件保证）
        auto reader = CanFileIOFactory::createReader(path);
        QVERIFY(reader);
        QVERIFY(reader->open(path));
        QVector<CanFrame> loaded;
        QCOMPARE(reader->readAll(loaded), 5);
        reader->close();
        for (int i = 0; i < frames.size(); ++i) {
            QCOMPARE(loaded.at(i).id, frames.at(i).id);
            QCOMPARE(loaded.at(i).extended, frames.at(i).extended);
            QCOMPARE(loaded.at(i).dlc, frames.at(i).dlc);
            QCOMPARE(loaded.at(i).data, frames.at(i).data);
        }
    }

    // ---- REC-02：暂停/恢复与失败分支 ----
    void recorderPauseAndFailures()
    {
        Recorder rec;

        // 父目录不存在 → open 失败
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(!rec.start(dir.filePath("no/such/dir/x.asc")));
        QVERIFY(!rec.isRecording());

        // 未知扩展名 → 无 writer
        QVERIFY(!rec.start("x.unsupported"));
        QVERIFY(!rec.isRecording());

        const QString path = dir.filePath("rec2.asc");
        QVERIFY(rec.start(path));

        rec.recordFrame(mkFrame(0.0, 0x200));
        rec.recordFrame(mkFrame(0.001, 0x201));
        QCOMPARE(rec.frameCount(), 2);

        QSignalSpy recorded(&rec, SIGNAL(frameRecorded(int)));
        rec.pause();
        QVERIFY(rec.isPaused());
        rec.recordFrame(mkFrame(0.002, 0x202)); // 暂停中丢弃
        QCOMPARE(rec.frameCount(), 2);
        QCOMPARE(recorded.count(), 0);
        QVERIFY(rec.isRecording()); // 暂停 ≠ 停止

        rec.resume();
        QVERIFY(!rec.isPaused());
        rec.recordFrame(mkFrame(0.003, 0x203));
        QCOMPARE(rec.frameCount(), 3);
        QCOMPARE(recorded.count(), 1);

        rec.stop();
        QVERIFY(QFileInfo::exists(path));
    }

    // ---- PLAY-01：加载元数据 ----
    void playerMetadata()
    {
        Player p;
        QVERIFY(!p.isLoaded());
        QCOMPARE(p.totalFrames(), 0);
        QCOMPARE(p.totalTime(), 0.0);
        QCOMPARE(p.speed(), 1.0);

        p.setLoop(true);
        QVERIFY(p.loop());
        p.setLoop(false);
        QVERIFY(!p.loop());

        QVector<CanFrame> frames;
        for (int i = 0; i < 10; ++i)
            frames << mkFrame(i * 0.1, 0x100 + i);
        p.loadFrames(frames);

        QVERIFY(p.isLoaded());
        QCOMPARE(p.totalFrames(), 10);
        QCOMPARE(p.totalTime(), 0.9); // = 最后一帧时间戳
        QCOMPARE(p.currentTime(), 0.0);
        QCOMPARE(p.currentFrameIndex(), 0);

        p.setSpeed(4.0);
        QCOMPARE(p.speed(), 4.0);
        p.setSpeed(0); // 非法速度被拒绝
        QCOMPARE(p.speed(), 4.0);

        p.unload();
        QVERIFY(!p.isLoaded());
        QCOMPARE(p.totalFrames(), 0);
    }

    // ---- PLAY-02：加速回放逐帧有序 + finished + 循环 ----
    void playerPlayback()
    {
        Player p;
        QVector<CanFrame> frames;
        for (int i = 0; i < 10; ++i)
            frames << mkFrame(i * 0.1, 0x100 + i);
        p.loadFrames(frames);
        p.setSpeed(8.0); // 0.9s 序列 ≈ 115ms 播完

        QSignalSpy played(&p, SIGNAL(framePlayed(CanFrame)));
        QSignalSpy finishedSpy(&p, SIGNAL(finished()));
        QSignalSpy states(&p, SIGNAL(stateChanged(bool)));

        p.play();
        QVERIFY(p.isPlaying());
        QTRY_COMPARE(played.count(), 10);
        QVERIFY(!p.isPlaying());
        QCOMPARE(finishedSpy.count(), 1);

        // 逐帧有序：0x100..0x109
        for (int i = 0; i < 10; ++i)
            QCOMPARE(played.at(i).first().value<CanFrame>().id, quint32(0x100 + i));

        // stateChanged：start=true → 播完=false
        QCOMPARE(states.count(), 2);
        QCOMPARE(states.first().first().toBool(), true);
        QCOMPARE(states.last().first().toBool(), false);

        // 播完后再 play 从头开始
        played.clear();
        p.play();
        QTRY_COMPARE(played.count(), 10);
        p.stop();

        // 循环模式：播完回绕继续
        p.setLoop(true);
        played.clear();
        p.play();
        QTRY_VERIFY_WITH_TIMEOUT(played.count() >= 13, 5000);
        p.stop();
        QCOMPARE(played.at(9).first().value<CanFrame>().id, quint32(0x109));
        QCOMPARE(played.at(10).first().value<CanFrame>().id, quint32(0x100)); // 回绕首帧
    }

    // ---- PLAY-03：seek/pause/stop 控制 ----
    void playerSeekControl()
    {
        Player p;
        QVector<CanFrame> frames;
        for (int i = 0; i < 10; ++i)
            frames << mkFrame(i * 0.1, 0x100 + i);
        p.loadFrames(frames);

        // seek 定位到第一个 timestamp >= 0.45 的帧（0.5s，index 5）
        p.seekTo(0.45);
        QCOMPARE(p.currentFrameIndex(), 5);
        QCOMPARE(p.currentTime(), 0.5);

        p.seekTo(0.0);
        QCOMPARE(p.currentFrameIndex(), 0);

        p.seekTo(999.0); // 超界钳制到最后一帧
        QCOMPARE(p.currentFrameIndex(), 9);
        QCOMPARE(p.currentTime(), 0.9);

        // 从中间起播 + 暂停续播（1 倍速，帧间隔 0.1s）
        p.seekTo(0.3); // index 3
        QSignalSpy played(&p, SIGNAL(framePlayed(CanFrame)));
        p.play();
        QTRY_VERIFY(played.count() >= 1);
        p.pause();
        QVERIFY(!p.isPlaying());
        const int idxAfterPause = p.currentFrameIndex();
        QVERIFY(idxAfterPause >= 3);

        p.play(); // 从暂停位置续播至结束
        QTRY_VERIFY_WITH_TIMEOUT(!p.isPlaying(), 3000);
        QCOMPARE(played.count(), 10 - 3);

        p.play();
        p.stop();
        QVERIFY(!p.isPlaying());
        QCOMPARE(p.currentFrameIndex(), 0);
    }
};

QTEST_GUILESS_MAIN(TestSimRecPlay)
#include "test_sim_rec_play.moc"
