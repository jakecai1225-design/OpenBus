#include "mainwindow.h"
#include <QTimer>
#include "core/canframe.h"
#include "core/recorder.h"
#include "core/player.h"
#include "core/cansimulator.h"
#include "core/candevicemanager.h"
#include "core/dbcmanager.h"
#include "core/dbcdata.h"
#include "core/canfileio/canfileio.h"
#include "core/canfileio/canfileio_factory.h"
#include "models/cantracemodel.h"
#include "models/cantraceproxymodel.h"
#include "ui/activitybar.h"
#include "ui/panels/sidebarpanels.h"
#include "ui/thememanager.h"
#include "ui/bottompanel.h"
#include "ui/rightpanel.h"
#include "ui/spliteditorarea.h"
// ui/measurementsetupview.h / ui/deviceconnectiontab.h 已移除 —
// Flow/设备连接页经 ModuleRegistry "flow" 模块创建（拆分方案 B4）
#include "core/driver/driverregistry.h"
#include "core/module/moduleregistry.h"
#include "core/module/imodule.h"
#include "core/signalrelay.h"   // DEF-08：字符串信号 → lambda 桥接
#include "core/marketmodel.h"   // MarketItem（ExtensionsPanel 信号类型，经 QVariant 传给市场模块；B5-5 迁 data 层）
// ui/udsview.h, ui/canopenview.h 已移除 — UDS/CANopen 由插件 uds-diagnostic/canopen-explorer 提供
// ui/markettab.h 已移除 — 插件市场页经 ModuleRegistry "market" 模块创建（拆分方案 B0）
// ui/signalsendtab.h / playbacktab.h / offlineanalysistab.h / recordtab.h 已移除 —
// 收发四页经 ModuleRegistry "transceive" 模块创建（拆分方案 B2）
// ui/dbcdetailtab.h / ui/tools/dbcsignallistview.h 已移除 —
// DBC 页经 ModuleRegistry "dbc" 模块创建（拆分方案 B3）
// ui/traceview.h / ui/graphicview.h / ui/datawindow.h / ui/filterbar.h /
// ui/colorruleeditor.h 已移除 — Trace/Graphic/DataWindow/着色规则经
// ModuleRegistry "trace"/"graphic" 模块创建与操控（拆分方案 B5）
#include "ui/tools/iographview.h"
#include "core/busstatistics.h"
// core/filterpresetmanager.h 已移除 — 过滤预设随 Trace 页迁入 TraceModule（B5）
#include "core/bookmarkmanager.h"
// core/triggerrecorder.h 已移除 — 触发录制随录制页迁入 transceive 模块（拆分方案 B2）
#include "utils/canutils.h"
#include "core/appconfig.h"
#include "core/projectmanager.h"
#include "utils/svg_icon.h"
#include "ui/settingspage.h"
#include "ui/shortcutspage.h"
#include "ui/commandcenter.h"
#include "ui/commandpalette.h"
#include "core/file_import/file_importer.h"
#include "core/plugin/pluginmanager.h"
#include "core/plugin/plugininfo.h"
#include "core/sessionmanager.h"
#include "models/viewportproxy.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QDockWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QStatusBar>
#include <QApplication>
#include <QFileInfo>
#include <QDir>
#include <QPlainTextEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTextBrowser>
#include <QToolButton>
#include <QMouseEvent>
#include <QWindow>
#include <QDesktopServices>
#include <QUrl>
#include <QLineEdit>
#include <QSlider>
#include <QComboBox>
#include <QProgressDialog>
#include <QPointer>
#include <QRegularExpression>
#include <functional>
#include <QRegularExpression>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

// ============================================================
//  MainWindow 窗口骨架（B6 拆分自 mainwindow.cpp）
//  菜单栏 / 自绘窗口按钮 / 主布局 / 状态栏 / ActivityBar 同步 /
//  dock 切换 / eventFilter / nativeEvent（无边框拖拽）
// ============================================================

// ============================================================
//  菜单栏
// ============================================================

void MainWindow::createMenuBar()
{
    // ---- 文件 ----
    auto *fileMenu = menuBar()->addMenu("文件(&F)");

    m_openAction = new QAction("打开文件...", this);
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setToolTip("打开报文文件 (BLF/ASC/CSV/PCAP/TRC) 或 DBC 文件");
    fileMenu->addAction(m_openAction);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::onOpenFile);

    auto *openProj = new QAction("打开工程...", this);
    openProj->setShortcut(QKeySequence("Ctrl+Shift+O"));
    fileMenu->addAction(openProj);
    connect(openProj, &QAction::triggered, this, &MainWindow::onOpenProject);

    auto *saveProj = new QAction("保存工程", this);
    saveProj->setShortcut(QKeySequence("Ctrl+Shift+S"));
    fileMenu->addAction(saveProj);
    connect(saveProj, &QAction::triggered, this, &MainWindow::onSaveProject);

    fileMenu->addSeparator();

    m_importAction = new QAction("导入日志文件...", this);
    m_importAction->setShortcut(QKeySequence("Ctrl+I"));
    m_importAction->setToolTip("导入 BLF/ASC/CSV 日志文件到 Trace");
    fileMenu->addAction(m_importAction);
    connect(m_importAction, &QAction::triggered, this, &MainWindow::onImportLog);

    fileMenu->addSeparator();
    fileMenu->addAction("退出(&Q)", QKeySequence("Alt+F4"), this, &QApplication::quit);

    // ---- 视图 ----
    auto *viewMenu = menuBar()->addMenu("视图(&V)");

    auto *toggleLeft = new QAction("左侧栏", this);
    toggleLeft->setCheckable(true);
    toggleLeft->setChecked(true);
    viewMenu->addAction(toggleLeft);
    connect(toggleLeft, &QAction::triggered, this, &MainWindow::toggleLeftDock);

    auto *toggleBottom = new QAction("底部栏", this);
    toggleBottom->setCheckable(true);
    toggleBottom->setChecked(false);
    viewMenu->addAction(toggleBottom);
    connect(toggleBottom, &QAction::triggered, this, &MainWindow::toggleBottomDock);

    auto *toggleRight = new QAction("右侧栏", this);
    toggleRight->setCheckable(true);
    toggleRight->setChecked(false);
    viewMenu->addAction(toggleRight);
    connect(toggleRight, &QAction::triggered, this, &MainWindow::toggleRightDock);

    viewMenu->addSeparator();
    viewMenu->addAction(QStringLiteral("Welcome"), this, &MainWindow::onOpenWelcomeTab);
    viewMenu->addAction("重置布局", this, &MainWindow::resetLayout);

    // ---- 工具 ----
    // 原“工具集/协议”侧边栏功能已插件化（blf-converter / dbc-tool / bus-statistics /
    // uds-diagnostic / canopen-explorer），在「插件市场」安装使用；内置工具保留在此菜单
    auto *toolsMenu = menuBar()->addMenu("工具(&T)");
    toolsMenu->addAction("Data Window", QKeySequence("Ctrl+Shift+D"),
                         this, &MainWindow::onOpenDataWindow);
    toolsMenu->addAction("I/O Graph", QKeySequence("Ctrl+Shift+G"),
                         this, &MainWindow::onOpenIOGraph);
    toolsMenu->addAction("Watcher 观测", QKeySequence("Ctrl+Shift+W"),
                         this, &MainWindow::onOpenWatcher);
    toolsMenu->addSeparator();
    toolsMenu->addAction("着色规则编辑器...", this, &MainWindow::onOpenColorRuleEditor);

    // ---- 工具操作 (不创建菜单, QAction 挂到主窗口, 快捷键仍然生效) ----
    m_recordAction = new QAction("录制", this);
    m_recordAction->setCheckable(true);
    m_recordAction->setShortcut(QKeySequence("Ctrl+R"));
    addAction(m_recordAction);
    connect(m_recordAction, &QAction::triggered, this, &MainWindow::onRecord);

    m_playAction = new QAction("播放", this);
    m_playAction->setShortcut(QKeySequence(Qt::Key_Space));
    addAction(m_playAction);
    connect(m_playAction, &QAction::triggered, this, &MainWindow::onPlay);

    m_pauseAction = new QAction("暂停", this);
    addAction(m_pauseAction);
    connect(m_pauseAction, &QAction::triggered, this, &MainWindow::onPause);

    m_stopAction = new QAction("停止", this);
    addAction(m_stopAction);
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::onStop);

    m_clearAction = new QAction("清空 Trace", this);
    addAction(m_clearAction);
    connect(m_clearAction, &QAction::triggered, this, &MainWindow::onClear);

    m_autoScrollAction = new QAction("自动滚动", this);
    m_autoScrollAction->setCheckable(true);
    m_autoScrollAction->setChecked(true);
    addAction(m_autoScrollAction);
    connect(m_autoScrollAction, &QAction::toggled, this, &MainWindow::onAutoScrollToggled);

    m_simAction = new QAction("模拟器开关", this);
    m_simAction->setCheckable(true);
    addAction(m_simAction);
    connect(m_simAction, &QAction::toggled, this, [this](bool on) {
        if (on) m_simulator->start();
        else    m_simulator->stop();
    });

    // ---- 插件入口已统一至插件市场（侧边栏迷你市场 + 插件市场页，方案 §13.10） ----

    // ---- 帮助 ----
    auto *helpMenu = menuBar()->addMenu("帮助(&H)");

    helpMenu->addAction(QStringLiteral("Welcome"), this, &MainWindow::onOpenWelcomeTab);
    helpMenu->addSeparator();
    helpMenu->addAction("关于 openbus", this, &MainWindow::showAboutDialog);
    helpMenu->addSeparator();
    helpMenu->addAction("文档", this, []() {
        QDesktopServices::openUrl(QUrl("https://gitee.com/jake_cai/openbus"));
    });
    helpMenu->addAction("官方网站", this, []() {
        QDesktopServices::openUrl(QUrl("https://gitee.com/jake_cai/openbus"));
    });
    helpMenu->addAction("Gitee 仓库", this, []() {
        QDesktopServices::openUrl(QUrl("https://gitee.com/jake_cai/openbus"));
    });
    helpMenu->addSeparator();
    helpMenu->addAction("报告问题", this, []() {
        QDesktopServices::openUrl(QUrl("https://gitee.com/jake_cai/openbus/issues"));
    });
    helpMenu->addAction("检查更新", this, &MainWindow::showCheckUpdate);
    helpMenu->addAction("发版记录", this, &MainWindow::showReleaseNotes);
    helpMenu->addSeparator();
    helpMenu->addAction("快捷键", this, &MainWindow::showShortcuts);
    helpMenu->addAction("许可证", this, &MainWindow::showLicenseDialog);
    helpMenu->addSeparator();
    helpMenu->addAction("商业合作", this, &MainWindow::showBusinessCoop);
}

// ============================================================
//  窗口控制按钮
// ============================================================

void MainWindow::createWindowButtons()
{
    auto *brand = new QWidget(this);
    brand->setObjectName(QStringLiteral("BrandMark"));
    auto *brandLay = new QHBoxLayout(brand);
    brandLay->setContentsMargins(10, 0, 4, 0);
    brandLay->setSpacing(0);
    brand->setFixedHeight(32);
    m_brandMark = new QLabel(brand);
    m_brandMark->setFixedSize(20, 20);
    m_brandMark->setAlignment(Qt::AlignCenter);
    m_brandMark->setToolTip(QStringLiteral("openbus"));
    brandLay->addWidget(m_brandMark, 0, Qt::AlignVCenter);
    menuBar()->setCornerWidget(brand, Qt::TopLeftCorner);

    auto *container = new QWidget(this);
    container->setObjectName("WindowButtons");
    container->setFixedHeight(30);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // VS Code-style layout toggles (left sidebar / bottom panel / right sidebar)
    auto makeLayoutBtn = [container](const QString &tip) {
        auto *b = new QToolButton(container);
        b->setObjectName("LayoutToggleBtn");
        b->setIconSize(QSize(16, 16));
        b->setFixedSize(36, 30);
        b->setAutoRaise(true);
        b->setCheckable(true);
        b->setToolTip(tip);
        return b;
    };
    m_layoutLeftBtn = makeLayoutBtn(QStringLiteral("Toggle Primary Side Bar (Ctrl+B)"));
    m_layoutBottomBtn = makeLayoutBtn(QStringLiteral("Toggle Panel (Ctrl+J)"));
    m_layoutRightBtn = makeLayoutBtn(QStringLiteral("Toggle Secondary Side Bar"));
    layout->addWidget(m_layoutLeftBtn);
    layout->addWidget(m_layoutBottomBtn);
    layout->addWidget(m_layoutRightBtn);

    // Spacer strip between layout toggles and window controls
    auto *sep = new QWidget(container);
    sep->setFixedWidth(8);
    layout->addWidget(sep);

    // Window controls: SVG icons (minimize / maximize / close)
    m_minBtn = new QToolButton(container);
    m_minBtn->setObjectName("WinMinBtn");
    m_minBtn->setIconSize(QSize(10, 10));
    m_minBtn->setFixedSize(46, 30);
    m_minBtn->setAutoRaise(true);
    m_minBtn->setToolTip(QStringLiteral("Minimize"));

    m_maxBtn = new QToolButton(container);
    m_maxBtn->setObjectName("WinMaxBtn");
    m_maxBtn->setIconSize(QSize(10, 10));
    m_maxBtn->setFixedSize(46, 30);
    m_maxBtn->setAutoRaise(true);
    m_maxBtn->setToolTip(QStringLiteral("Maximize"));

    m_closeBtn = new QToolButton(container);
    m_closeBtn->setObjectName("WinCloseBtn");
    m_closeBtn->setIconSize(QSize(10, 10));
    m_closeBtn->setFixedSize(46, 30);
    m_closeBtn->setAutoRaise(true);
    m_closeBtn->setToolTip(QStringLiteral("Close"));

    layout->addWidget(m_minBtn);
    layout->addWidget(m_maxBtn);
    layout->addWidget(m_closeBtn);

    menuBar()->setCornerWidget(container, Qt::TopRightCorner);

    connect(m_layoutLeftBtn, &QToolButton::clicked, this, &MainWindow::toggleLeftDock);
    connect(m_layoutBottomBtn, &QToolButton::clicked, this, &MainWindow::toggleBottomDock);
    connect(m_layoutRightBtn, &QToolButton::clicked, this, &MainWindow::toggleRightDock);

    connect(m_minBtn, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(m_maxBtn, &QToolButton::clicked, this, [this]() {
        if (isMaximized()) showNormal();
        else showMaximized();
    });
    connect(m_closeBtn, &QToolButton::clicked, this, &QWidget::close);

    refreshWindowButtonIcons();
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            this, SLOT(refreshWindowButtonIcons()));
}

// ============================================================
//  VS Code Command Center (menu-bar search pill)
// ============================================================

void MainWindow::createCommandCenter()
{
    m_commandCenter = new CommandCenter(menuBar());
    m_commandCenter->setPlaceholder(QStringLiteral("Search openbus"));
    m_commandCenter->show();

    m_commandPalette = new CommandPalette(this);

    connect(m_commandCenter, &CommandCenter::activated, this, [this]() {
        showCommandPalette();
    });
    connect(m_commandCenter, &CommandCenter::navigateBack, this, [this]() {
        if (!m_editorArea) return;
        if (QTabWidget *tw = m_editorArea->activeTabWidget()) {
            const int n = tw->count();
            if (n <= 0) return;
            int i = tw->currentIndex();
            tw->setCurrentIndex(i > 0 ? i - 1 : n - 1);
        }
    });
    connect(m_commandCenter, &CommandCenter::navigateForward, this, [this]() {
        if (!m_editorArea) return;
        if (QTabWidget *tw = m_editorArea->activeTabWidget()) {
            const int n = tw->count();
            if (n <= 0) return;
            int i = tw->currentIndex();
            tw->setCurrentIndex(i + 1 < n ? i + 1 : 0);
        }
    });

    auto *actQuick = new QAction(QStringLiteral("Command Center"), this);
    actQuick->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    addAction(actQuick);
    connect(actQuick, &QAction::triggered, this, [this]() { showCommandPalette(); });

    auto *actCmd = new QAction(QStringLiteral("Command Palette"), this);
    actCmd->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
    addAction(actCmd);
    connect(actCmd, &QAction::triggered, this, [this]() {
        showCommandPalette(QStringLiteral("> "));
    });

    for (QAction *a : menuBar()->actions()) {
        if (a->menu() && a->text().contains(QStringLiteral("视图"))) {
            a->menu()->addSeparator();
            a->menu()->addAction(actQuick);
            a->menu()->addAction(actCmd);
            break;
        }
    }

    QTimer::singleShot(0, this, &MainWindow::repositionCommandCenter);
}

void MainWindow::repositionCommandCenter()
{
    if (!m_commandCenter || !menuBar())
        return;

    QMenuBar *mb = menuBar();
    int menusRight = 8;
    for (QAction *a : mb->actions()) {
        const QRect r = mb->actionGeometry(a);
        if (r.isValid())
            menusRight = qMax(menusRight, r.right());
    }

    const int rightReserve = 200;
    const int avail = mb->width() - menusRight - rightReserve - 24;
    int w = qBound(240, 420, avail);
    if (w < 200) {
        m_commandCenter->hide();
        return;
    }
    m_commandCenter->show();
    m_commandCenter->setFixedWidth(w);

    int x = menusRight + 16;
    const int ideal = (mb->width() - w) / 2;
    if (ideal > menusRight + 12 && ideal + w < mb->width() - rightReserve)
        x = ideal;

    const int y = qMax(2, (mb->height() - m_commandCenter->height()) / 2);
    m_commandCenter->move(x, y);
    m_commandCenter->raise();
}

void MainWindow::showCommandPalette(const QString &initialQuery)
{
    if (!m_commandPalette)
        m_commandPalette = new CommandPalette(this);

    using Item = CommandPalette::Item;
    using Kind = CommandPalette::Kind;
    QVector<Item> items;

    auto addView = [&](const QString &label, const QString &detail,
                       const std::function<void()> &fn) {
        Item it;
        it.kind = Kind::View;
        it.label = label;
        it.detail = detail;
        it.run = fn;
        items.push_back(it);
    };
    auto addCmd = [&](const QString &label, const QString &detail,
                      const std::function<void()> &fn) {
        Item it;
        it.kind = Kind::Command;
        it.label = label;
        it.detail = detail;
        it.run = fn;
        items.push_back(it);
    };

    std::function<void(QMenu *, const QString &)> walkMenu;
    walkMenu = [&](QMenu *menu, const QString &prefix) {
        if (!menu) return;
        for (QAction *act : menu->actions()) {
            if (act->isSeparator()) continue;
            if (act->menu()) {
                const QString next = prefix.isEmpty()
                    ? act->text()
                    : prefix + QStringLiteral(" / ") + act->text();
                walkMenu(act->menu(), next);
                continue;
            }
            if (act->text().isEmpty()) continue;
            QString label = act->text();
            label.remove(QLatin1Char('&'));
            Item it;
            it.kind = Kind::Command;
            it.label = label;
            it.detail = prefix;
            if (!act->shortcut().isEmpty()) {
                const QString sc = act->shortcut().toString(QKeySequence::NativeText);
                it.detail = it.detail.isEmpty() ? sc
                                                : (it.detail + QStringLiteral(" · ") + sc);
            }
            QPointer<QAction> guard(act);
            it.run = [guard]() {
                if (guard) guard->trigger();
            };
            items.push_back(it);
        }
    };
    for (QAction *top : menuBar()->actions()) {
        if (top->menu())
            walkMenu(top->menu(), QString());
    }

    addView(QStringLiteral("Toggle Primary Side Bar"), QStringLiteral("View"),
            [this]() { toggleLeftDock(); });
    addView(QStringLiteral("Toggle Panel"), QStringLiteral("View"),
            [this]() { toggleBottomDock(); });
    addView(QStringLiteral("Toggle Secondary Side Bar"), QStringLiteral("View"),
            [this]() { toggleRightDock(); });
    addView(QStringLiteral("Welcome"), QStringLiteral("Start page"),
            [this]() { onOpenWelcomeTab(); });
    addView(QStringLiteral("Extensions Marketplace"), QStringLiteral("Plugins"),
            [this]() { onOpenMarketTab(); });
    addView(QStringLiteral("Open Settings"), QStringLiteral("Preferences"),
            [this]() { onSettingsRequested(QStringLiteral("General")); });
    addView(QStringLiteral("Keyboard Shortcuts"), QStringLiteral("Help"),
            [this]() { showShortcuts(); });

    addCmd(QStringLiteral("Open File…"), QStringLiteral("Ctrl+O"),
           [this]() { onOpenFile(); });
    addCmd(QStringLiteral("Open Project…"), QStringLiteral("Ctrl+Shift+O"),
           [this]() { onOpenProject(); });
    addCmd(QStringLiteral("Save Project"), QStringLiteral("Ctrl+Shift+S"),
           [this]() { onSaveProject(); });

    for (const QString &path : SessionManager::instance()->recentPaths()) {
        Item it;
        it.kind = Kind::File;
        it.label = QFileInfo(path).fileName();
        it.detail = path;
        it.filterText = (it.label + QLatin1Char(' ') + path).toLower();
        it.run = [this, path]() {
            QFileInfo fi(path);
            if (fi.suffix().compare(QStringLiteral("dbc"), Qt::CaseInsensitive) == 0) {
                if (m_dbcManager)
                    m_dbcManager->loadDbc(path);
            } else {
                m_bottomPanel->appendOutput(
                    QStringLiteral("Open from Command Center: %1").arg(path));
            }
        };
        items.push_back(it);
    }

    if (m_dbcManager) {
        for (const DbcFile &db : m_dbcManager->files()) {
            Item it;
            it.kind = Kind::File;
            it.label = db.fileName.isEmpty() ? QStringLiteral("(dbc)") : db.fileName;
            it.detail = QStringLiteral("Loaded DBC · %1 messages").arg(db.messages.size());
            it.filterText = (QStringLiteral("dbc ") + it.label).toLower();
            items.push_back(it);
        }
    }

    if (m_pluginManager) {
        for (const PluginInfo &pi : m_pluginManager->discoveredPlugins()) {
            Item it;
            it.kind = Kind::Plugin;
            it.label = pi.title();
            it.detail = pi.description;
            it.filterText = (QStringLiteral("plugin ") + pi.title() + QLatin1Char(' ')
                             + pi.name + QLatin1Char(' ')
                             + pi.description).toLower();
            const QString name = pi.name;
            it.run = [this, name]() {
                if (m_pluginManager)
                    m_pluginManager->reactivatePlugin(name);
            };
            items.push_back(it);
        }
    }

    struct SettingHit { const char *key; const char *label; const char *cat; };
    static const SettingHit kSettings[] = {
        {"font.family", "Font family", "General"},
        {"font.size", "Font size", "General"},
        {"window.rememberGeometry", "Remember window size", "General"},
        {"trace.maxFrames", "Max frames (local ring)", "Trace"},
        {"trace.overwriteMode", "Overwrite mode", "Trace"},
        {"trace.autoScroll", "Auto-scroll", "Trace"},
        {"graphic.timeWindow", "Time window (s)", "Graphic"},
        {"graphic.fps", "Refresh rate (FPS)", "Graphic"},
        {"graphic.maxSamples", "Max samples per signal", "Graphic"},
    };
    for (const SettingHit &s : kSettings) {
        Item it;
        it.kind = Kind::Setting;
        it.label = QString::fromUtf8(s.label);
        it.detail = QStringLiteral("%1 · %2")
                        .arg(QString::fromUtf8(s.cat), QString::fromUtf8(s.key));
        it.filterText = (it.label + QLatin1Char(' ') + it.detail).toLower();
        const QString cat = QString::fromUtf8(s.cat);
        it.run = [this, cat]() { onSettingsRequested(cat); };
        items.push_back(it);
    }

    m_commandPalette->setItems(items);

    if (m_commandCenter && m_commandCenter->isVisible()) {
        m_commandPalette->openBelow(
            QRect(m_commandCenter->mapToGlobal(QPoint(0, 0)), m_commandCenter->size()));
    } else {
        m_commandPalette->openCentered(this);
    }

    if (!initialQuery.isEmpty()) {
        if (auto *edit = m_commandPalette->findChild<QLineEdit *>(
                QStringLiteral("CommandPaletteInput"))) {
            edit->setText(initialQuery);
            edit->setCursorPosition(initialQuery.size());
        }
    }
}

void MainWindow::refreshWindowButtonIcons()
{
    const QString c = ThemeManager::instance()->currentTheme().barFg;
    const QPixmap mark = renderSvgPixmap(QStringLiteral(":/icons/spider-logo.svg"), c, 20);
    if (m_brandMark)
        m_brandMark->setPixmap(mark);
    QIcon brand;
    brand.addPixmap(renderSvgPixmap(QStringLiteral(":/icons/spider-logo.svg"), c, 16));
    brand.addPixmap(renderSvgPixmap(QStringLiteral(":/icons/spider-logo.svg"), c, 32));
    brand.addPixmap(renderSvgPixmap(QStringLiteral(":/icons/spider-logo.svg"), c, 64));
    setWindowIcon(brand);
    qApp->setWindowIcon(brand);
    if (m_layoutLeftBtn)
        m_layoutLeftBtn->setIcon(svgIcon(":/icons/layout-sidebar-left.svg", c, 16));
    if (m_layoutBottomBtn)
        m_layoutBottomBtn->setIcon(svgIcon(":/icons/layout-panel.svg", c, 16));
    if (m_layoutRightBtn)
        m_layoutRightBtn->setIcon(svgIcon(":/icons/layout-sidebar-right.svg", c, 16));
    m_minBtn->setIcon(svgIcon(":/icons/win-minimize.svg", c, 10));
    m_maxBtn->setIcon(svgIcon(isMaximized() ? ":/icons/win-restore.svg"
                                             : ":/icons/win-maximize.svg", c, 10));
    m_closeBtn->setIcon(svgIcon(":/icons/close.svg", c, 10));
}

void MainWindow::syncLayoutToggleButtons()
{
    if (m_layoutLeftBtn && m_leftDock)
        m_layoutLeftBtn->setChecked(m_leftDock->isVisible());
    if (m_layoutBottomBtn && m_bottomDock)
        m_layoutBottomBtn->setChecked(m_bottomDock->isVisible());
    if (m_layoutRightBtn && m_rightDock)
        m_layoutRightBtn->setChecked(m_rightDock->isVisible());
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    // Aero Snap / 双击标题栏等途径也会切换最大化状态 → 同步最大化/还原图标
    if (event->type() == QEvent::WindowStateChange && m_maxBtn)
        refreshWindowButtonIcons();
}

// ============================================================
//  停靠面板布局
// ============================================================

void MainWindow::createLayout()
{
    // ---- 左侧 Dock ----
    auto *leftContainer = new QWidget(this);
    leftContainer->setObjectName("LeftContainer");
    leftContainer->setAttribute(Qt::WA_StyledBackground, true);
    auto *leftLayout = new QHBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    m_activityBar = new ActivityBar(leftContainer);
    m_sideBar = new SideBar(leftContainer);

    leftLayout->addWidget(m_activityBar);
    leftLayout->addWidget(m_sideBar, 1);

    m_leftDock = new QDockWidget("Sidebar", this);
    m_leftDock->setObjectName("LeftDock");
    m_leftDock->setAttribute(Qt::WA_StyledBackground, true);
    m_leftDock->setWidget(leftContainer);
    m_leftDock->setFeatures(QDockWidget::DockWidgetMovable |
                            QDockWidget::DockWidgetClosable |
                            QDockWidget::DockWidgetFloatable);
    m_leftDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    {
        auto *titleBar = new QWidget(m_leftDock);
        titleBar->setFixedHeight(0);
        m_leftDock->setTitleBarWidget(titleBar);
    }
    m_leftDock->setMinimumWidth(0);
    addDockWidget(Qt::LeftDockWidgetArea, m_leftDock);

    // ---- 中央: 可拆分编辑器区域 ----
    m_editorArea = new SplitEditorArea(this);
    setCentralWidget(m_editorArea);

    // 默认标签页：Flow + 设备连接（其他不打开）
    onOpenMeasurementSetup();

    // 设备连接页经 flow 模块创建（拆分方案 B4；单实例缓存在模块内）
    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("flow"))) {
        ShellContext ctx = makeShellContext();
        if (QWidget *page = mod->createPage(QStringLiteral("device"), ctx))
            openTab(page, QStringLiteral("设备连接"));
    }

    // ---- 右侧 Dock ----
    m_rightPanel = new RightPanel(this);
    m_rightPanel->setAttribute(Qt::WA_StyledBackground, true);
    m_rightDock = new QDockWidget("Right", this);
    m_rightDock->setObjectName("RightDock");
    m_rightDock->setAttribute(Qt::WA_StyledBackground, true);
    m_rightDock->setWidget(m_rightPanel);
    m_rightDock->setFeatures(QDockWidget::DockWidgetMovable |
                             QDockWidget::DockWidgetClosable |
                             QDockWidget::DockWidgetFloatable);
    m_rightDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    {
        auto *titleBar = new QWidget(m_rightDock);
        titleBar->setFixedHeight(0);
        m_rightDock->setTitleBarWidget(titleBar);
    }
    addDockWidget(Qt::RightDockWidgetArea, m_rightDock);

    // ---- 底部 Dock ----
    m_bottomPanel = new BottomPanel(this);
    m_bottomPanel->setAttribute(Qt::WA_StyledBackground, true);
    m_bottomDock = new QDockWidget("Output", this);
    m_bottomDock->setObjectName("BottomDock");
    m_bottomDock->setAttribute(Qt::WA_StyledBackground, true);
    m_bottomDock->setWidget(m_bottomPanel);
    m_bottomDock->setFeatures(QDockWidget::DockWidgetMovable |
                              QDockWidget::DockWidgetClosable |
                              QDockWidget::DockWidgetFloatable);
    m_bottomDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    {
        auto *titleBar = new QWidget(m_bottomDock);
        titleBar->setFixedHeight(0);
        m_bottomDock->setTitleBarWidget(titleBar);
    }
    addDockWidget(Qt::BottomDockWidgetArea, m_bottomDock);

    resizeDocks({m_leftDock}, {280}, Qt::Horizontal);
    resizeDocks({m_rightDock}, {260}, Qt::Horizontal);
    resizeDocks({m_bottomDock}, {200}, Qt::Vertical);

    // Default: hide right and bottom panels (VS Code-like focus on editor)
    m_rightDock->setVisible(false);
    m_bottomDock->setVisible(false);

    connect(m_leftDock, &QDockWidget::visibilityChanged, this,
            [this](bool) { syncLayoutToggleButtons(); });
    connect(m_rightDock, &QDockWidget::visibilityChanged, this,
            [this](bool) { syncLayoutToggleButtons(); });
    connect(m_bottomDock, &QDockWidget::visibilityChanged, this,
            [this](bool) { syncLayoutToggleButtons(); });
    connect(m_bottomPanel, &BottomPanel::closeRequested, this, [this]() {
        m_bottomDock->setVisible(false);
        syncLayoutToggleButtons();
    });
    syncLayoutToggleButtons();
}

// ============================================================
//  状态栏
// ============================================================

void MainWindow::createStatusBar()
{
    m_statusLabel = new QLabel(QStringLiteral("Ready"), this);
    m_connLabel = new QLabel(QStringLiteral("Disconnected"), this);
    m_errorLabel = new QLabel(QString(), this);
    m_tabLabel = new QLabel(QStringLiteral("Trace"), this);
    // Slim permanent strip: tab | frames (disp/capt) | selection | time
    // Dropped duplicate row-count and filter-ratio labels (Trace reports via traceStatus).
    m_frameCountLabel = new QLabel(QStringLiteral("0 frames"), this);
    m_selectedLabel = new QLabel(QStringLiteral("Sel: 0"), this);
    m_timeLabel = new QLabel(QStringLiteral("0.000s"), this);
    // Legacy labels kept for older call sites; hidden to reclaim space
    m_rowCountLabel = new QLabel(this);
    m_rowCountLabel->hide();
    m_filterLabel = new QLabel(this);
    m_filterLabel->hide();

    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addWidget(m_connLabel);
    statusBar()->addWidget(m_errorLabel);
    statusBar()->addPermanentWidget(m_tabLabel);
    statusBar()->addPermanentWidget(m_frameCountLabel);
    statusBar()->addPermanentWidget(m_selectedLabel);
    statusBar()->addPermanentWidget(m_timeLabel);
}

// ============================================================
//  ActivityBar → 侧边栏 + 主标签页联动
// ============================================================

void MainWindow::onActivityChanged(int activity)
{
    m_sideBar->showPanel(activity);
    if (!m_sideBarVisible) {
        // 从收起状态展开：恢复 dock 宽度
        m_sideBar->setVisible(true);
        m_leftDock->setMinimumWidth(0);
        m_leftDock->setMaximumWidth(QWIDGETSIZE_MAX);
        m_leftDock->resize(m_savedDockWidth, m_leftDock->height());
        m_sideBarVisible = true;
    }

    // 联动主标签页
    if (activity == ActivityBar::Trace) {
        // 在所有拆分组中查找 Trace 标签页
        const auto allTabs = m_editorArea->allTabWidgets();
        bool found = false;
        for (auto *tw : allTabs) {
            for (int i = tw->count() - 1; i >= 0; --i) {
                if (isTraceTabText(tw->tabText(i))) {
                    tw->setCurrentIndex(i);
                    m_tabLabel->setText(tw->tabText(i));
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found)
            onOpenTraceTab();
    } else if (activity == ActivityBar::Graphic) {
        // 在所有拆分组中查找 Graphic 标签页
        const auto allTabs = m_editorArea->allTabWidgets();
        bool found = false;
        for (auto *tw : allTabs) {
            for (int i = tw->count() - 1; i >= 0; --i) {
                if (isGraphicTabText(tw->tabText(i))) {
                    tw->setCurrentIndex(i);
                    m_tabLabel->setText(tw->tabText(i));
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found)
            onNewGraphicRequested();
    } else if (activity == ActivityBar::Transceive) {
        // 收发面板：只切换侧边栏显示，不自动打开标签页
        // 用户点击侧边栏内的按钮才打开对应标签页
    } else if (activity == ActivityBar::Device) {
        // 切换到已存在的设备连接标签页
        const auto allTabs = m_editorArea->allTabWidgets();
        bool found = false;
        for (auto *tw : allTabs) {
            for (int i = tw->count() - 1; i >= 0; --i) {
                if (tw->tabText(i).contains("设备连接")) {
                    tw->setCurrentIndex(i);
                    m_tabLabel->setText(tw->tabText(i));
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found) {
            // 设备页存在但不在任何标签组 → 重新挂回（经 flow 模块查询，拆分方案 B4）
            if (QWidget *page = flowQuery(QStringLiteral("devicePage")).value<QWidget *>())
                m_editorArea->addTab(page, QStringLiteral("设备连接"));
        }
    } else if (activity == ActivityBar::Analysis) {
        onOpenMeasurementSetup();
    } else if (activity == ActivityBar::Extensions) {
        // 插件市场：打开统一插件市场标签页（v2，方案 §13.5）
        const auto allTabs = m_editorArea->allTabWidgets();
        bool found = false;
        for (auto *tw : allTabs) {
            for (int i = tw->count() - 1; i >= 0; --i) {
                if (tw->tabText(i).contains(QStringLiteral("插件市场"))) {
                    tw->setCurrentIndex(i);
                    m_tabLabel->setText(tw->tabText(i));
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (found) {
            if (m_marketWidget)
                marketInvoke(QStringLiteral("refreshInstalled"));
        } else {
            onOpenMarketTab();
        }
    }
}

void MainWindow::onActivityToggled(int)
{
    if (m_sideBarVisible) {
        // 收起：保存当前宽度，将 dock 缩小到仅 ActivityBar 宽度
        m_savedDockWidth = m_leftDock->width();
        m_sideBar->setVisible(false);
        m_leftDock->setFixedWidth(m_activityBar->width());
    } else {
        // 展开：恢复保存的宽度
        m_sideBar->setVisible(true);
        m_leftDock->setMinimumWidth(0);
        m_leftDock->setMaximumWidth(QWIDGETSIZE_MAX);
        m_leftDock->resize(m_savedDockWidth, m_leftDock->height());
    }
    m_sideBarVisible = !m_sideBarVisible;
}


// ============================================================
//  视图菜单
// ============================================================

void MainWindow::toggleLeftDock()
{
    m_leftDock->setVisible(!m_leftDock->isVisible());
    syncLayoutToggleButtons();
}

void MainWindow::toggleRightDock()
{
    m_rightDock->setVisible(!m_rightDock->isVisible());
    syncLayoutToggleButtons();
}

void MainWindow::toggleBottomDock()
{
    m_bottomDock->setVisible(!m_bottomDock->isVisible());
    syncLayoutToggleButtons();
}

void MainWindow::resetLayout()
{
    m_leftDock->setVisible(true);
    m_rightDock->setVisible(true);
    m_bottomDock->setVisible(true);
    resizeDocks({m_leftDock}, {280}, Qt::Horizontal);
    resizeDocks({m_rightDock}, {260}, Qt::Horizontal);
    resizeDocks({m_bottomDock}, {200}, Qt::Vertical);
    syncLayoutToggleButtons();
}


// ============================================================
//  事件过滤器
// ============================================================

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == menuBar()) {
        if (event->type() == QEvent::Resize) {
            repositionCommandCenter();
        } else if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                if (m_commandCenter && m_commandCenter->isVisible()
                    && m_commandCenter->geometry().contains(me->pos())) {
                    return false; // let CommandCenter handle click
                }
                QAction *act = menuBar()->actionAt(me->pos());
                if (!act) {
                    if (windowHandle())
                        windowHandle()->startSystemMove();
                }
            }
        } else if (event->type() == QEvent::MouseButtonDblClick) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (m_commandCenter && m_commandCenter->isVisible()
                && m_commandCenter->geometry().contains(me->pos())) {
                return true;
            }
            QAction *act = menuBar()->actionAt(me->pos());
            if (!act) {
                if (isMaximized())
                    showNormal();
                else
                    showMaximized();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

// ============================================================
//  Windows 原生事件
// ============================================================

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_NCHITTEST) {
            const int borderWidth = 5;
            RECT winrect;
            GetWindowRect(msg->hwnd, &winrect);
            long x = GET_X_LPARAM(msg->lParam);
            long y = GET_Y_LPARAM(msg->lParam);

            bool left   = x >= winrect.left && x < winrect.left + borderWidth;
            bool right  = x < winrect.right && x >= winrect.right - borderWidth;
            bool top    = y >= winrect.top && y < winrect.top + borderWidth;
            bool bottom = y < winrect.bottom && y >= winrect.bottom - borderWidth;

            if (top && left)     { *result = HTTOPLEFT;     return true; }
            if (top && right)    { *result = HTTOPRIGHT;    return true; }
            if (bottom && left)  { *result = HTBOTTOMLEFT;  return true; }
            if (bottom && right) { *result = HTBOTTOMRIGHT; return true; }
            if (left)            { *result = HTLEFT;         return true; }
            if (right)           { *result = HTRIGHT;        return true; }
            if (top)             { *result = HTTOP;          return true; }
            if (bottom)          { *result = HTBOTTOM;       return true; }
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

