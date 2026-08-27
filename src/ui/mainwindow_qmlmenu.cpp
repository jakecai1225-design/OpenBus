// =============================================================================
//  mainwindow_qmlmenu.cpp - QML MenuBar Integration
// =============================================================================
//  B6 瘦身后按职责拆分（同一类，见 doc/拆分应用实施方案.md §4.5）
//  此文件负责 MainWindow 的 QML MenuBar 集成
// =============================================================================

#include "mainwindow.h"
#include <QtQml/qqml.h>              // ✅ Fixed: qmlRegisterType is a global function (not QQmlEngine::xxx)
#include <QQmlContext>
#include <QQmlEngine>                // ✅ Added: Required for engine->rootContext()
#include <QVBoxLayout>
#include <QTimer>                    // ✅ Added: For delayed menuController setup
#include "core/qmlmenulibrary.h"

/**
 * @brief 初始化 QML 菜单条
 * 
 * 创建一个 QQuickWidget 容器，加载 QML 界面，并连接 MenuController 后端
 * 将 QML MenuBar 集成到 QMainWindow 菜单栏区域
 */
void MainWindow::createQmlMenuBar()
{
    // ✅ 创建 MenuController 后端
    m_menuController = new MenuController(this);
    
    // ✅ 连接信号槽 - MenuController 信号 → MainWindow 槽函数
    connect(m_menuController, &MenuController::openFileRequested,
            this, &MainWindow::onOpenFile);
    connect(m_menuController, &MenuController::openProjectRequested,
            this, &MainWindow::onOpenProject);        // TODO: implement if needed
    connect(m_menuController, &MenuController::saveProjectRequested,
            this, &MainWindow::onSaveProject);
    connect(m_menuController, &MenuController::importLogFileRequested,
            this, &MainWindow::onImportLog);
    connect(m_menuController, &MenuController::quitApplication,
            qApp, &QApplication::quit);
    
    connect(m_menuController, &MenuController::toggleLeftDock,
            this, &MainWindow::toggleLeftDock);
    connect(m_menuController, &MenuController::toggleBottomDock,
            this, &MainWindow::toggleBottomDock);
    connect(m_menuController, &MenuController::toggleRightDock,
            this, &MainWindow::toggleRightDock);
    connect(m_menuController, &MenuController::resetLayoutRequested,
            this, &MainWindow::resetLayout);
    
    connect(m_menuController, &MenuController::dataWindowRequested,
            this, &MainWindow::onOpenDataWindow);
    connect(m_menuController, &MenuController::ioGraphRequested,
            this, &MainWindow::onOpenIOGraph);
    connect(m_menuController, &MenuController::watcherRequested,
            this, &MainWindow::onOpenWatcher);
    connect(m_menuController, &MenuController::colorRuleEditorRequested,
            this, &MainWindow::onOpenColorRuleEditor);
    
    connect(m_menuController, &MenuController::aboutRequested,
            this, &MainWindow::showAboutDialog);
    connect(m_menuController, &MenuController::docsRequested,
            this, &MainWindow::showReleaseNotes);         // TODO: docs
    connect(m_menuController, &MenuController::shortcutsRequested,
            this, &MainWindow::showShortcuts);
    connect(m_menuController, &MenuController::licenseRequested,
            this, &MainWindow::showLicenseDialog);
    
    // ✅ 创建 QQuickWidget 容器
    m_qmlMenuBar = new QQuickWidget(this);
    m_qmlMenuBar->resize(1024, 30);  // 固定尺寸，由 layout 自动调整宽度
    m_qmlMenuBar->setResizeMode(QQuickWidget::SizeRootObjectToView);  // ✅ 正确枚举值
    m_qmlMenuBar->setSource(QUrl("qrc:/qml/menubar/Main.qml"));
    
    // ⚠️ NOTE: QML 文件路径需要在 resources.qrc 中声明
    if (!m_qmlMenuBar->rootObject()) {
        qWarning() << "QML MenuBar file not found: qrc:/qml/menubar/Main.qml";
        qWarning() << "Please ensure this file is included in resources.qrc";
        return;
    }
    
    // ✅ 将 QML MenuBar 集成到 QMainWindow
    auto *menuWrapper = new QWidget(this);
    auto *menuLayout = new QVBoxLayout(menuWrapper);
    menuLayout->setContentsMargins(0, 0, 0, 0);
    menuLayout->addWidget(m_qmlMenuBar);
    setMenuWidget(menuWrapper);
    
    // ✅ 关键修复：使用 QMetaObject::invokeMethod + Qt::AutoConnection
    // 确保在事件循环运行时才设置 contextProperty!
    QTimer::singleShot(0, this, [this]() {
        if (m_qmlMenuBar && m_qmlMenuBar->rootObject()) {
            auto engine = m_qmlMenuBar->engine();
            if (engine) {
                qInfo() << "Setting menuController context property to QML";
                engine->rootContext()->setContextProperty("menuController", m_menuController);
            } else {
                qCritical() << "QML Engine is null after event loop!";
            }
        } else {
            qCritical() << "QML root object is null after event loop!";
        }
    });
}

/**
 * @brief 注册 QML 类型到引擎
 * 
 * 将 MenuController 注册为 QML 单例对象，供 QML 访问
 * ✅ Fixed: Use qmlRegisterType (global function) instead of QQmlEngine::qmlRegisterType
 */
void MainWindow::registerQmlTypes()
{
    // ✅ 注册 C++ 类为 QML 类型 (通过 URI)
    qmlRegisterType<MenuController>("OpenBUS.Menu", 1, 0, "MenuController");
}
