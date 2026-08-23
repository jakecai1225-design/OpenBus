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
    dlg.setWindowTitle("关于 openbus");
    dlg.setFixedWidth(380);
    auto *layout = new QVBoxLayout(&dlg);

    auto *title = new QLabel("<b style='font-size:24px;color:#4a90d9'>openbus</b>", &dlg);
    auto *desc = new QLabel("CAN/CAN FD 报文分析工具", &dlg);
    auto *ver = new QLabel("版本: 1.0.0", &dlg);
    auto *author = new QLabel("作者: 蔡可杰 (Jake.cai)", &dlg);
    auto *github = new QLabel("Gitee: <a href='https://gitee.com/jake_cai/openbus'>https://gitee.com/jake_cai/openbus</a>", &dlg);
    github->setTextInteractionFlags(Qt::TextBrowserInteraction);
    github->setOpenExternalLinks(true);
    auto *email = new QLabel("邮箱: 929168503@qq.com", &dlg);
    auto *wechat = new QLabel("微信: 13368295840", &dlg);
    auto *biz = new QLabel("商业合作: 929168503@qq.com / 微信 13368295840", &dlg);
    biz->setObjectName("DimLabel");
    auto *copyright = new QLabel("基于 Qt6 构建 © 2026", &dlg);
    copyright->setObjectName("DimLabel");

    for (auto *l : {title, desc, ver, author, github, email, wechat, biz, copyright}) {
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
    dlg.setWindowTitle("许可证");
    dlg.resize(500, 400);
    auto *layout = new QVBoxLayout(&dlg);

    auto *browser = new QTextBrowser(&dlg);
    browser->setPlainText(
        "MIT License\n\n"
        "Copyright (c) 2026 蔡可杰 (Jake.cai)\n\n"
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
    QDialog dlg(this);
    dlg.setWindowTitle("发版记录");
    dlg.resize(500, 400);
    auto *layout = new QVBoxLayout(&dlg);

    auto *browser = new QTextBrowser(&dlg);
    browser->setPlainText(
        "v1.0.0 (2026-07-27)\n"
        "  首个正式版本\n"
        "  - CAN/CAN FD 报文实时采集与离线回放\n"
        "  - DBC 文件加载与信号级解析\n"
        "  - Wireshark 风格三栏 Trace 视图\n"
        "  - 多 Graphic 信号波形图（多纵轴）\n"
        "  - VS Code 风格可拆分标签页布局\n"
        "  - AI 对话助手集成\n\n"
        "v0.9.0 (2026-07-20)\n"
        "  Beta 预览版\n"
        "  - 无边框窗口 + 菜单栏拖拽\n"
        "  - ActivityBar + SideBar 多面板\n"
        "  - 基础报文录制与回放\n");
    layout->addWidget(browser);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    layout->addWidget(btns);

    dlg.exec();
}

void MainWindow::showShortcuts()
{
    // 帮助菜单仍以对话框呈现；侧栏设置面板"快捷键"条目走标签页
    // （ShortcutsPage），文案共用 ShortcutsPage::shortcutsText()
    QDialog dlg(this);
    dlg.setWindowTitle("快捷键");
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
    QMessageBox::information(this, "检查更新",
        "当前版本: 1.0.0\n"
        "最新版本: 1.0.0 (已是最新)\n\n"
        "如有更新，请前往 Gitee Releases 页面下载最新版本。");
}

void MainWindow::showBusinessCoop()
{
    QDialog dlg(this);
    dlg.setWindowTitle("商业合作");
    dlg.setFixedWidth(380);
    auto *layout = new QVBoxLayout(&dlg);

    auto *title = new QLabel("<b style='color:#4a90d9'>如需商业授权、定制开发、技术支持或业务合作</b>", &dlg);
    title->setWordWrap(true);
    auto *author = new QLabel("作者: 蔡可杰 (Jake.cai)", &dlg);
    auto *email = new QLabel("邮箱: 929168503@qq.com", &dlg);
    auto *wechat = new QLabel("微信: 13368295840", &dlg);
    auto *github = new QLabel("Gitee: <a href='https://gitee.com/jake_cai/openbus'>https://gitee.com/jake_cai/openbus</a>", &dlg);
    github->setTextInteractionFlags(Qt::TextBrowserInteraction);
    github->setOpenExternalLinks(true);
    auto *note = new QLabel("本项目基于 MIT License 开源，商业使用请联系作者获取授权。", &dlg);
    note->setObjectName("DimLabel");
    note->setWordWrap(true);

    for (auto *l : {title, author, email, wechat, github, note}) {
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
    if (m_deviceManager)
        m_deviceManager->sendFrame(frame);
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

