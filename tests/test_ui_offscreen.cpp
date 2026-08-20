// ============================================================
//  test_ui_offscreen — C L2 offscreen UI 驱动套件
//  （doc/测试验收方案.md v2.0 三层金字塔 L2）
//
//  策略：QT_QPA_PLATFORM=offscreen（CMake 测试 ENVIRONMENT 注入，
//  先于 QApplication 构造生效），完整复刻 main.cpp 初始化链
//  （模块注册 → 主题 → 配置/会话 → MainWindow），在无窗口环境下
//  驱动真实主窗口并断言其行为。
//
//  覆盖用例：
//  - UI-01 主窗口构造：3 组 QDockWidget + 菜单栏 + 状态栏齐备
//  - UI-02 默认实例：Trace1 / Graphic1 启动即存在
//  - UI-03 数据驱动：onMeasurementToggled 门控 + onFrameReceived
//    帧流入 → Trace 模型行数增长（私有槽经元对象调用）
//  - UI-04 主题切换：Dark/Light 往返不破坏窗口
// ============================================================
#include <QtTest>
#include <QApplication>
#include <QDockWidget>
#include <QFile>
#include <QLabel>
#include <QStatusBar>
#include <QAbstractItemModel>
#include <QAbstractItemView>

#include "core/canframe.h"
#include "core/driver/driverregistry.h"
#include "core/appconfig.h"
#include "core/sessionmanager.h"
#include "core/module/moduleregistry.h"
#include "ui/mainwindow.h"
#include "ui/traceview.h"
#include "ui/thememanager.h"

// 业务模块唯一导出的 C 工厂（同 src/main.cpp；壳不 include 模块头）
extern "C" IBusinessModule *openbus_createMarketModule();       // openbus_market.dll
extern "C" IBusinessModule *openbus_createTransceiveModule();   // openbus_transceive.dll
extern "C" IBusinessModule *openbus_createDbcModule();          // openbus_dbc.dll
extern "C" IBusinessModule *openbus_createFlowModule();         // openbus_flow.dll

static CanFrame mkUiFrame(int seq)
{
    CanFrame f;
    f.timestamp = seq * 0.001;
    f.timestampNs = static_cast<quint64>(seq) * 1000000;
    f.id = 0x100 + (seq % 8);
    f.dlc = 8;
    f.data = QByteArray(8, 0x33);
    f.channel = 1;
    return f;
}

class TestUiOffscreen : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void windowConstructs();
    void defaultInstances();
    void frameFlowDrivesTrace();
    void themeSwitchSurvives();

private:
    MainWindow *m_win = nullptr;

    // drivers/disabled.json 现场备份（禁用 zlg 的副作用还原，见 initTestCase）
    bool m_disabledJsonExisted = false;
    QByteArray m_disabledJsonBackup;

    /// 按 metaObject 类名查找子孙部件（避免依赖具体部件头文件）
    QWidget *findFirstInstance(const char *className) const;
};

void TestUiOffscreen::initTestCase()
{
    // 环境自检：offscreen 平台必须生效（否则测试失去无头意义）
    QCOMPARE(qApp->platformName(), QStringLiteral("offscreen"));

    qRegisterMetaType<CanFrame>("CanFrame");
    QApplication::setApplicationName("openbus");
    QApplication::setOrganizationName("openbus");
    QApplication::setApplicationVersion("0.1.0");

    // 复刻 main.cpp 初始化链
    ModuleRegistry::instance()->registerModule(
        QStringLiteral("market"), &openbus_createMarketModule);
    ModuleRegistry::instance()->registerModule(
        QStringLiteral("transceive"), &openbus_createTransceiveModule);
    ModuleRegistry::instance()->registerModule(
        QStringLiteral("dbc"), &openbus_createDbcModule);
    ModuleRegistry::instance()->registerModule(
        QStringLiteral("flow"), &openbus_createFlowModule);
    ThemeManager::instance()->applyTheme("Light");
    AppConfig::instance()->load();
    SessionManager::instance()->load();

    // 禁用外置 ZLG 驱动（DEF-06 规避）：zlg 插件 + 真实设备在场的枚举链
    // 存在间歇性堆损坏崩溃（待产品修复，见测试报告）；禁用后 UI 套件
    // 不依赖硬件环境，可稳定重复。disabled.json 会落盘，cleanupTestCase
    // 还原现场。
    {
        QFile f(DriverRegistry::driversRootDir()
                + QStringLiteral("/disabled.json"));
        m_disabledJsonExisted = f.exists();
        if (m_disabledJsonExisted && f.open(QIODevice::ReadOnly))
            m_disabledJsonBackup = f.readAll();
        DriverRegistry::instance()->initialize();
        DriverRegistry::instance()->setDriverEnabled(
            QStringLiteral("zlg"), false);
    }

    m_win = new MainWindow();
    m_win->show();
}

void TestUiOffscreen::cleanupTestCase()
{
    delete m_win;
    m_win = nullptr;

    // 还原 drivers/disabled.json 现场（清除禁用 zlg 的落盘副作用）
    {
        QFile f(DriverRegistry::driversRootDir()
                + QStringLiteral("/disabled.json"));
        if (m_disabledJsonExisted) {
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write(m_disabledJsonBackup);
        } else if (f.exists()) {
            f.remove();
        }
    }
}

QWidget *TestUiOffscreen::findFirstInstance(const char *className) const
{
    const auto widgets = m_win->findChildren<QWidget*>();
    for (QWidget *w : widgets) {
        if (QLatin1String(w->metaObject()->className()) == QLatin1String(className))
            return w;
    }
    return nullptr;
}

// ---- UI-01：主窗口构造 ----
void TestUiOffscreen::windowConstructs()
{
    QVERIFY(m_win->isVisible());

    // 布局：左/右/底 三组 QDockWidget
    QCOMPARE(m_win->findChildren<QDockWidget*>().size(), 3);

    QVERIFY(m_win->menuBar() != nullptr);
    QVERIFY(m_win->statusBar() != nullptr);

    // 状态栏挂载统计标签（帧数/时间/连接等）
    QVERIFY(m_win->statusBar()->findChildren<QLabel*>().size() >= 5);
}

// ---- UI-02：默认 Trace1 / Graphic1 实例 ----
void TestUiOffscreen::defaultInstances()
{
    QVERIFY2(findFirstInstance("TraceTab"),
             "默认 Trace 实例未创建");
    QVERIFY2(findFirstInstance("GraphicView"),
             "默认 Graphic 实例未创建");
}

// ---- UI-03：帧流入驱动 Trace 模型 ----
void TestUiOffscreen::frameFlowDrivesTrace()
{
    // 前置 1：创建 Flow 测量配置页——onMeasurementToggled 的数据源判定
    // （currentSource/isBlockEnabled）依赖该页实例；未创建时误判离线模式
    // 弹文件对话框（offscreen 下死等，DEF-10）
    QVERIFY(QMetaObject::invokeMethod(m_win, "onOpenMeasurementSetup"));
    QTest::qWait(50);

    // TraceTab 子树内的视图 → 模型
    QWidget *traceTabWidget = findFirstInstance("TraceTab");
    QVERIFY(traceTabWidget);
    auto *traceTab = static_cast<TraceTab *>(traceTabWidget);

    // 精确定位 TraceView（TraceTab 内还有 T8 详情树等 QAbstractItemView，
    // findChildren 顺序不能保证 TraceView 在首位；其模型为
    // ViewportProxyModel，反映 CanTraceModel 提交后的真实行数）
    QAbstractItemView *traceView = nullptr;
    for (auto *v : traceTab->findChildren<QAbstractItemView*>()) {
        if (QLatin1String(v->metaObject()->className())
                == QLatin1String("TraceView")) {
            traceView = v;
            break;
        }
    }
    QVERIFY2(traceView, "TraceView 未找到");
    QAbstractItemModel *model = traceView->model();
    QVERIFY(model != nullptr);
    const int before = model->rowCount();

    // 打开测量门控（私有槽经元对象调用，同真实数据流路径；
    // 总门 m_measurementRunning + simulator 启动）
    QVERIFY(QMetaObject::invokeMethod(m_win, "onMeasurementToggled",
                                      Q_ARG(bool, true)));
    qWarning("[step] measurement toggled on");
    // Trace 实例门控：flow 块启用链（isBlockEnabled）对懒创建场景不稳定，
    // 直接置运行态（TraceTab 与测试同模块链接，直调可靠且幂等）
    traceTab->setRunning(true);
    qWarning("[step] trace running set, injecting frames");
    // 帧流入
    for (int i = 0; i < 20; ++i) {
        QVERIFY(QMetaObject::invokeMethod(m_win, "onFrameReceived",
                                          Q_ARG(CanFrame, mkUiFrame(i))));
    }
    QTest::qWait(150);
    QVERIFY2(model->rowCount() > before,
             qPrintable(QStringLiteral("20 帧流入后 Trace 行数未增长 (%1 → %2)")
                            .arg(before).arg(model->rowCount())));

    // 关闭门控后帧不再进入（onMeasurementToggled(false) 同时
    // simulator->stop()；先等刷新定时器把关闭前 pending 帧全部落盘，
    // 再取基线，避免在途帧干扰断言）
    QVERIFY(QMetaObject::invokeMethod(m_win, "onMeasurementToggled",
                                      Q_ARG(bool, false)));
    QTest::qWait(150);
    const int after = model->rowCount();
    for (int i = 0; i < 5; ++i) {
        QVERIFY(QMetaObject::invokeMethod(m_win, "onFrameReceived",
                                          Q_ARG(CanFrame, mkUiFrame(100 + i))));
    }
    QTest::qWait(150);
    QCOMPARE(model->rowCount(), after);
}

// ---- UI-04：主题切换 ----
void TestUiOffscreen::themeSwitchSurvives()
{
    ThemeManager::instance()->applyTheme("Dark");
    ThemeManager::instance()->applyTheme("Light");
    QVERIFY(m_win->isVisible());
    QVERIFY(findFirstInstance("TraceTab")); // 实例不被重建破坏
}

QTEST_MAIN(TestUiOffscreen)
#include "test_ui_offscreen.moc"
