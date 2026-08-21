// ============================================================
//  test_project — B7 工程配置套件（doc/测试验收方案.md §四 B7）
//
//  覆盖用例（验收需求 → 断言）：
//  - PRJ-01 newProject 状态与信号（meta 时间戳、modified 置位）
//  - PRJ-02 全字段 JSON 闭环（toJsonString → fromJsonString）
//  - PRJ-03 saveAs → loadProject 文件闭环（v2 resources 相对路径无损还原）
//  - PRJ-04 样本工程 EngineAnalysis.openbusproj 加载
//  - PRJ-05 非法输入容错（损坏 JSON / 文件不存在，状态不被破坏）
//  - PRJ-06 最近工程列表（addRecentProject / clearRecent，测试后还原现场）
// ============================================================
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QFile>

#include "core/projectmanager.h"

// 注：QSignalSpy 用 SIGNAL() 字符串构造（跨 DLL 信号 PMF 解析失败，
// 见 test_sim_rec_play.cpp 头部说明）

static QString testData(const QString &name)
{
    return QStringLiteral(SIN_SOURCE_DIR) + QStringLiteral("/test/resources/") + name;
}

class TestProject : public QObject
{
    Q_OBJECT

private slots:
    void newProjectState();
    void jsonRoundtrip();
    void saveLoadFile();
    void loadSampleProject();
    void invalidInputs();
    void recentProjectsList();

private:
    /// 填充全部非路径字段（路径字段由 saveLoadFile 验证，需要真实 resolver 基）
    void fillState(ProjectState &st);
};

void TestProject::fillState(ProjectState &st)
{
    st.sourceMode = 1;
    st.filePath = QStringLiteral("D:/logs/playback.asc");
    st.baudrate = 250000;
    st.channel = 2;

    ProjectTraceInstance t1;
    t1.id = QStringLiteral("trace-1");
    t1.title = QStringLiteral("动力网");
    t1.filterExpression = QStringLiteral("id in (0x100 0x200) fd");
    st.traces << t1;

    ProjectGraphicInstance g1;
    g1.id = QStringLiteral("graphic-1");
    g1.title = QStringLiteral("发动机");
    ProjectSigCfg s1{0x100, QStringLiteral("EngineSpeed"), false};
    ProjectSigCfg s2{0x18FF0000, QStringLiteral("BatteryV"), true};
    g1.sigList << s1 << s2;
    st.graphics << g1;

    st.openTabs << QStringLiteral("trace-1") << QStringLiteral("graphic-1");
    st.activeTab = QStringLiteral("graphic-1");

    st.offlineFiles << QStringLiteral("D:/logs/trace1.asc")
                    << QStringLiteral("D:/logs/trace2.blf");

    st.meta.author = QStringLiteral("测试");
    st.meta.tags << QStringLiteral("验收") << QStringLiteral("demo");
    st.meta.notes = QStringLiteral("B7 roundtrip");

    st.deviceConfig.type = QStringLiteral("USBCANFD_200U");
    st.deviceConfig.fd = true;
    st.deviceConfig.fdBaudrate = 5000000;
}

// ---- PRJ-01：新建工程 ----
void TestProject::newProjectState()
{
    ProjectManager *pm = ProjectManager::instance();
    QSignalSpy loaded(pm, SIGNAL(projectLoaded(QString)));

    pm->newProject(QStringLiteral("demo"));
    QCOMPARE(pm->currentProjectName(), QStringLiteral("demo"));
    QCOMPARE(loaded.count(), 1);
    QCOMPARE(loaded.first().first().toString(), QStringLiteral("demo"));

    QVERIFY(pm->isModified());            // 新建即脏（待保存）
    QVERIFY(pm->currentFilePath().isEmpty());
    QVERIFY(!pm->currentState().meta.created.isEmpty()); // meta 时间戳填充
    QVERIFY(!pm->currentState().meta.modified.isEmpty());
    QVERIFY(pm->currentState().traces.isEmpty());
}

// ---- PRJ-02：JSON 全字段闭环 ----
void TestProject::jsonRoundtrip()
{
    ProjectManager *pm = ProjectManager::instance();
    pm->newProject(QStringLiteral("roundtrip"));
    fillState(pm->currentStateRef());

    const QString json = pm->toJsonString();
    QVERIFY(json.contains(QStringLiteral("roundtrip")));

    // 重置后恢复
    pm->newProject(QStringLiteral("blank"));
    QCOMPARE(pm->currentProjectName(), QStringLiteral("blank"));

    QVERIFY(pm->fromJsonString(json));
    const ProjectState &st = pm->currentState();
    QCOMPARE(st.name, QStringLiteral("roundtrip"));
    QCOMPARE(st.sourceMode, 1);
    QCOMPARE(st.filePath, QStringLiteral("D:/logs/playback.asc"));
    QCOMPARE(st.baudrate, 250000);
    QCOMPARE(st.channel, 2);

    QCOMPARE(st.traces.size(), 1);
    QCOMPARE(st.traces.first().id, QStringLiteral("trace-1"));
    QCOMPARE(st.traces.first().filterExpression,
             QStringLiteral("id in (0x100 0x200) fd"));

    QCOMPARE(st.graphics.size(), 1);
    QCOMPARE(st.graphics.first().sigList.size(), 2);
    QCOMPARE(st.graphics.first().sigList.at(0).canId, quint32(0x100));
    QCOMPARE(st.graphics.first().sigList.at(0).name, QStringLiteral("EngineSpeed"));
    QCOMPARE(st.graphics.first().sigList.at(1).canId, quint32(0x18FF0000));
    QVERIFY(st.graphics.first().sigList.at(1).extended);

    QCOMPARE(st.openTabs,
             QStringList({QStringLiteral("trace-1"), QStringLiteral("graphic-1")}));
    QCOMPARE(st.activeTab, QStringLiteral("graphic-1"));

    QCOMPARE(st.offlineFiles,
             QStringList({QStringLiteral("D:/logs/trace1.asc"),
                          QStringLiteral("D:/logs/trace2.blf")}));

    QCOMPARE(st.meta.author, QStringLiteral("测试"));
    QCOMPARE(st.meta.tags,
             QStringList({QStringLiteral("验收"), QStringLiteral("demo")}));
    QCOMPARE(st.meta.notes, QStringLiteral("B7 roundtrip"));

    QCOMPARE(st.deviceConfig.type, QStringLiteral("USBCANFD_200U"));
    QVERIFY(st.deviceConfig.fd);
    QCOMPARE(st.deviceConfig.fdBaudrate, 5000000);

    QVERIFY(pm->isModified()); // fromJsonString 置脏
}

// ---- PRJ-03：文件保存/加载闭环 ----
void TestProject::saveLoadFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 资源与工程文件同盘（QTemporaryDir），相对化后可无损还原
    const QString dbc = dir.filePath(QStringLiteral("a.dbc"));
    const QString rec = dir.filePath(QStringLiteral("r.blf"));
    const QString offline = dir.filePath(QStringLiteral("offline.asc"));
    QFile f1(dbc);
    QVERIFY(f1.open(QIODevice::WriteOnly));
    f1.close();
    QFile f2(rec);
    QVERIFY(f2.open(QIODevice::WriteOnly));
    f2.close();
    QFile f3(offline);
    QVERIFY(f3.open(QIODevice::WriteOnly));
    f3.close();
    const QString path = dir.filePath(QStringLiteral("p.openbusproj"));

    ProjectManager *pm = ProjectManager::instance();
    pm->newProject(QStringLiteral("files"));
    fillState(pm->currentStateRef());
    pm->currentStateRef().dbcFiles << dbc;
    pm->currentStateRef().recordFiles << rec;
    // 清掉 fillState 填入的跨盘路径（无法相对化），聚焦同目录相对化往返
    pm->currentStateRef().offlineFiles.clear();
    pm->currentStateRef().offlineFiles << offline;

    QSignalSpy saved(pm, SIGNAL(projectSaved(QString)));
    QVERIFY(pm->saveAs(path));
    QCOMPARE(saved.count(), 1);
    QVERIFY(QFileInfo::exists(path));
    QVERIFY(!pm->isModified()); // 保存后清脏
    QCOMPARE(pm->currentFilePath(), path);

    // v2 resources 节点：资源以相对路径写入
    QFile rf(path);
    QVERIFY(rf.open(QIODevice::ReadOnly));
    const QString raw = QString::fromUtf8(rf.readAll());
    rf.close();
    QVERIFY(raw.contains(QStringLiteral("\"resources\"")));
    QVERIFY(raw.contains(QStringLiteral("a.dbc")));

    // 重置后加载还原
    pm->newProject(QStringLiteral("other"));
    QSignalSpy loaded(pm, SIGNAL(projectLoaded(QString)));
    QVERIFY(pm->loadProject(path));
    QCOMPARE(loaded.count(), 1);

    const ProjectState &st = pm->currentState();
    QCOMPARE(st.name, QStringLiteral("files"));
    QCOMPARE(st.dbcFiles, QStringList({dbc})); // 相对化 → resolve 无损还原
    QCOMPARE(st.recordFiles, QStringList({rec}));
    QCOMPARE(st.offlineFiles, QStringList({offline}));
    QCOMPARE(st.deviceConfig.fdBaudrate, 5000000);
    QCOMPARE(st.traces.first().filterExpression,
             QStringLiteral("id in (0x100 0x200) fd"));
    QCOMPARE(pm->currentFilePath(), path);
    QVERIFY(!pm->isModified());
}

// ---- PRJ-04：样本工程加载 ----
void TestProject::loadSampleProject()
{
    const QString path = testData(QStringLiteral("EngineAnalysis.openbusproj"));
    if (!QFileInfo::exists(path))
        QSKIP("样本工程 EngineAnalysis.openbusproj 不在 test/resources，待补充后启用");

    ProjectManager *pm = ProjectManager::instance();
    QVERIFY(pm->loadProject(path));
    QVERIFY(!pm->currentProjectName().isEmpty());
    QVERIFY(!pm->isModified());
}

// ---- PRJ-05：非法输入容错 ----
void TestProject::invalidInputs()
{
    ProjectManager *pm = ProjectManager::instance();
    pm->newProject(QStringLiteral("keep"));

    // 不存在的文件
    QVERIFY(!pm->loadProject(testData(QStringLiteral("no_such_project.openbusproj"))));
    QCOMPARE(pm->currentProjectName(), QStringLiteral("keep"));

    // 损坏 JSON 文件
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString bad = dir.filePath(QStringLiteral("bad.openbusproj"));
    QFile bf(bad);
    QVERIFY(bf.open(QIODevice::WriteOnly));
    bf.write("{ not valid json");
    bf.close();
    QVERIFY(!pm->loadProject(bad));
    QCOMPARE(pm->currentProjectName(), QStringLiteral("keep"));

    // 损坏 JSON 字符串
    QVERIFY(!pm->fromJsonString(QStringLiteral("{ broken")));
    QCOMPARE(pm->currentProjectName(), QStringLiteral("keep"));

    // 空路径保存失败
    pm->newProject(QStringLiteral("unsaved"));
    QVERIFY(!pm->saveProject());
}

// ---- PRJ-06：最近工程列表 ----
void TestProject::recentProjectsList()
{
    ProjectManager *pm = ProjectManager::instance();

    // 备份当前列表（SessionManager 持久化，测试后还原现场）
    const QStringList backup = pm->recentProjects();

    pm->addRecentProject(QStringLiteral("D:/fake/one.openbusproj"));
    pm->addRecentProject(QStringLiteral("D:/fake/two.openbusproj"));
    const QStringList recent = pm->recentProjects();
    QVERIFY(recent.contains(QStringLiteral("D:/fake/one.openbusproj")));
    QVERIFY(recent.contains(QStringLiteral("D:/fake/two.openbusproj")));

    pm->clearRecent();
    QVERIFY(pm->recentProjects().isEmpty());

    // 还原现场（倒序回填恢复原排序）
    for (int i = backup.size() - 1; i >= 0; --i)
        pm->addRecentProject(backup.at(i));
    QCOMPARE(pm->recentProjects().size(), backup.size());
}

QTEST_GUILESS_MAIN(TestProject)
#include "test_project.moc"
