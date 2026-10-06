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
#include "core/file_import/file_importer.h"
#include "core/plugin/pluginmanager.h"
#include "core/plugin/plugininfo.h"
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
#include <QCoreApplication>
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
#include <QRegularExpression>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

// ============================================================
//  MainWindow 帮助对话框与插件集成（B6 拆分自 mainwindow.cpp）
//  About/许可/发行说明/快捷键/检查更新/商务合作 + 插件系统槽
// ============================================================

// ============================================================
//  帮助菜单对话框
// ============================================================

void MainWindow::showAboutDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("About openbus"));
    dlg.setFixedWidth(380);
    auto *layout = new QVBoxLayout(&dlg);

    auto *title = new QLabel("<b style='font-size:24px;color:#4a90d9'>openbus</b>", &dlg);
    auto *desc = new QLabel(tr("CAN / CAN FD bus analysis workbench"), &dlg);
    auto *ver = new QLabel(
        tr("Version: %1").arg(QCoreApplication::applicationVersion()), &dlg);
    auto *author = new QLabel(tr("Author: Jake.cai (蔡可杰)"), &dlg);
    auto *github = new QLabel(
        QStringLiteral("Gitee: <a href='https://gitee.com/jake_cai/sin'>https://gitee.com/jake_cai/sin</a>"),
        &dlg);
    github->setTextInteractionFlags(Qt::TextBrowserInteraction);
    github->setOpenExternalLinks(true);
    auto *email = new QLabel(tr("Email: 929168503@qq.com"), &dlg);
    auto *copyright = new QLabel(tr("Built with Qt 6. © 2026"), &dlg);
    copyright->setObjectName("DimLabel");

    for (auto *l : {title, desc, ver, author, github, email, copyright}) {
        layout->addWidget(l);
    }
    layout->addStretch();

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    layout->addWidget(btns);

    dlg.exec();
}

void MainWindow::showLicenseDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("License"));
    dlg.resize(500, 400);
    auto *layout = new QVBoxLayout(&dlg);

    auto *browser = new QTextBrowser(&dlg);
    browser->setPlainText(
        "MIT License\n\n"
        "Copyright (c) 2026 Jake.cai (蔡可杰)\n\n"
        "Permission is hereby granted, free of charge, to any person obtaining a copy "
        "of this software and associated documentation files (the \"Software\"), to deal "
        "in the Software without restriction, including without limitation the rights "
        "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell "
        "copies of the Software, and to permit persons to whom the Software is "
        "furnished to do so, subject to the following conditions:\n\n"
        "The above copyright notice and this permission notice shall be included in all "
        "copies or substantial portions of the Software.\n\n"
        "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR "
        "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
        "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE "
        "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER "
        "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, "
        "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE "
        "SOFTWARE.");
    layout->addWidget(browser);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    layout->addWidget(btns);

    dlg.exec();
}

void MainWindow::showReleaseNotes()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("http://sin.org.cn/updates")));
}

void MainWindow::showShortcuts()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Keyboard Shortcuts"));
    dlg.resize(400, 350);
    auto *layout = new QVBoxLayout(&dlg);

    auto *browser = new QTextBrowser(&dlg);
    browser->setPlainText(ShortcutsPage::shortcutsText());
    browser->setObjectName("TerminalOutput");
    layout->addWidget(browser);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    layout->addWidget(btns);

    dlg.exec();
}

void MainWindow::showCheckUpdate()
{
    const QString ver = QCoreApplication::applicationVersion();
    const int r = QMessageBox::information(
        this,
        tr("Check for Updates"),
        tr("Installed version: %1\n\nRelease notes and installers are published on the website.")
            .arg(ver),
        QMessageBox::Open | QMessageBox::Ok);
    if (r == QMessageBox::Open)
        QDesktopServices::openUrl(QUrl(QStringLiteral("http://sin.org.cn/updates")));
}

void MainWindow::showBusinessCoop()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Business"));
    dlg.setFixedWidth(380);
    auto *layout = new QVBoxLayout(&dlg);

    auto *title = new QLabel(
        QStringLiteral("<b style='color:#4a90d9'>%1</b>")
            .arg(tr("For commercial licensing, custom development, or support")),
        &dlg);
    title->setWordWrap(true);
    auto *author = new QLabel(tr("Author: Jake.cai (蔡可杰)"), &dlg);
    auto *email = new QLabel(tr("Email: 929168503@qq.com"), &dlg);
    auto *github = new QLabel(
        QStringLiteral("Gitee: <a href='https://gitee.com/jake_cai/sin'>https://gitee.com/jake_cai/sin</a>"),
        &dlg);
    github->setTextInteractionFlags(Qt::TextBrowserInteraction);
    github->setOpenExternalLinks(true);
    auto *note = new QLabel(
        tr("openbus is released under the MIT License. Contact the author for commercial arrangements."),
        &dlg);
    note->setObjectName("DimLabel");
    note->setWordWrap(true);

    for (auto *l : {title, author, email, github, note}) {
        layout->addWidget(l);
    }
    layout->addStretch();

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    layout->addWidget(btns);

    dlg.exec();
}


// ============================================================
//  插件系统集成（侧边栏迷你市场自刷，无需集中推送列表）
// ============================================================

void MainWindow::onPluginOutput(const QString &text)
{
    m_bottomPanel->appendPluginOutput(text);
}

void MainWindow::onPluginCommandRegistered(const QString &id, const QString &title)
{
    // 添加到侧边栏插件市场面板的命令列表
    if (m_sideBar && m_sideBar->extensionsPanel())
        m_sideBar->extensionsPanel()->addCommand(id, title);
}

void MainWindow::onPluginSendFrame(const CanFrame &frame)
{
    if (!m_deviceManager) {
        m_bottomPanel->appendOutput(
            QStringLiteral("Plugin send failed: device manager not initialized"));
        return;
    }

    CanFrame echo;
    if (m_deviceManager->sendFrame(frame, &echo)) {
        // Tx loopback (CANoe-style): same hub as bus Rx → CaptureLog / SampleStore /
        // Flow / PluginManager PUB. Measurement gate applies in onFramesReceived.
        onFrameReceived(echo);
        if (!m_measurementRunning) {
            m_bottomPanel->appendOutput(
                QStringLiteral("Plugin Tx sent on bus (ID=0x%1) but measurement is "
                               "stopped — Trace / UDS Rx gated. Start measurement on Flow.")
                    .arg(frame.id & 0x1FFFFFFF, 0, 16).toUpper());
        }
        return;
    }

    QString reason;
    if (!m_deviceManager->isRunning())
        reason = QStringLiteral("device not running — connect PCAN/simulator first");
    else if (!m_deviceManager->isRealDevice())
        reason = QStringLiteral("simulator not running");
    else
        reason = QStringLiteral("device send failed");
    m_bottomPanel->appendOutput(
        QStringLiteral("Plugin send failed (%1): ID=0x%2")
            .arg(reason)
            .arg(frame.id & 0x1FFFFFFF, 0, 16).toUpper());
}

void MainWindow::onPluginRequestSelectedFrames(const QJsonValue &requestId)
{
    QList<CanFrame> frames;

    // 获取当前活跃 Trace 页选中的帧（View → ViewportProxy → CanFilterProxy →
    // CanTraceModel 的代理链映射在 TraceModule 内完成，跨 DLL 以 QVariant 传帧）
    QWidget *active = m_editorArea->currentWidget();
    if (traceQuery(QStringLiteral("isTrace"), QVariant::fromValue(active)).toBool()) {
        const QVariantList vars = traceQuery(QStringLiteral("selectedFrames"),
                                             QVariant::fromValue(active)).toList();
        for (const QVariant &v : vars)
            frames.append(v.value<CanFrame>());
    }

    if (m_pluginManager)
        m_pluginManager->provideSelectedFrames(requestId, frames);
}

void MainWindow::onPluginRequestRecentFrames(const QJsonValue &requestId, int count)
{
    QList<CanFrame> frames;

    // 当前活跃 Trace 页最近 N 帧（时间正序，最新在后）。
    // arg 为 [TraceTab*, count]，见 TraceModule::query("recentFrames")
    QWidget *active = m_editorArea->currentWidget();
    if (traceQuery(QStringLiteral("isTrace"), QVariant::fromValue(active)).toBool()) {
        QVariantList args;
        args << QVariant::fromValue(active) << count;
        const QVariantList vars = traceQuery(QStringLiteral("recentFrames"),
                                             QVariant(args)).toList();
        for (const QVariant &v : vars)
            frames.append(v.value<CanFrame>());
    }

    if (m_pluginManager)
        m_pluginManager->provideRecentFrames(requestId, frames);
}

