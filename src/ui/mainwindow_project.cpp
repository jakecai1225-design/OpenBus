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
#include "ui/watcherview.h"

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
//  MainWindow 工程与会话生命周期（B6 拆分自 mainwindow.cpp）
//  工程打开/保存/切换/新建 + 状态捕获/应用（capture/applyProjectState）
//  + 文件预览 + closeEvent
// ============================================================

// ============================================================
//  关闭
// ============================================================

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_recording) {
        if (QMessageBox::question(this, "退出", "正在录制，确定退出？") != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_recorder->stop();
    }
    m_simulator->stop();
    m_deviceManager->stop();
    m_player->stop();

    // 关闭插件系统
    if (m_pluginManager)
        m_pluginManager->shutdown();

    // 自动保存工程
    if (AppConfig::instance()->getBool("project.autoSaveOnClose", true)) {
        captureProjectState();
        if (!ProjectManager::instance()->currentFilePath().isEmpty()) {
            ProjectManager::instance()->saveProject();
        }
    }

    event->accept();
}


// ============================================================
//  工程管理
// ============================================================

void MainWindow::onOpenProject()
{
    QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开工程"), {},
        QStringLiteral("openbus 工程文件 (*.openbusproj);;所有文件 (*.*)"));
    if (path.isEmpty()) return;

    // 先保存当前工程状态
    captureProjectState();
    if (!ProjectManager::instance()->currentFilePath().isEmpty())
        ProjectManager::instance()->saveProject();

    // 加载新工程
    if (ProjectManager::instance()->loadProject(path)) {
        applyProjectState();
        m_bottomPanel->appendOutput(QStringLiteral("工程已加载: ") +
                                    ProjectManager::instance()->currentProjectName());
    }
}

void MainWindow::onSaveProject()
{
    captureProjectState();

    QString path = ProjectManager::instance()->currentFilePath();
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存工程"),
            ProjectManager::instance()->currentProjectName() + ".openbusproj",
            QStringLiteral("openbus 工程文件 (*.openbusproj);;所有文件 (*.*)"));
        if (path.isEmpty()) return;
    }

    if (ProjectManager::instance()->saveProject(path)) {
        m_bottomPanel->appendOutput(QStringLiteral("工程已保存: ") + path);
    }
}

void MainWindow::onProjectSwitched(int index)
{
    auto &projects = m_sideBar->projectPanel()->projectsRef();
    if (index < 0 || index >= projects.size()) return;

    // 1. 保存当前工程状态
    int curIdx = m_sideBar->projectPanel()->currentIndex();
    captureProjectState();
    if (curIdx >= 0 && curIdx < projects.size()) {
        // 将状态快照保存到 ProjectContext，未保存的工程切换回来时可恢复
        projects[curIdx].stateJson = ProjectManager::instance()->toJsonString();
    }
    if (!ProjectManager::instance()->currentFilePath().isEmpty())
        ProjectManager::instance()->saveProject();

    // 2. 恢复目标工程状态
    const auto &target = projects.at(index);
    if (!target.filePath.isEmpty() && QFile::exists(target.filePath)) {
        // 已保存到文件的工程：从文件加载
        if (ProjectManager::instance()->loadProject(target.filePath)) {
            applyProjectState();
            m_bottomPanel->appendOutput(QStringLiteral("已切换到工程: ") +
                                        ProjectManager::instance()->currentProjectName());
        }
    } else if (!target.stateJson.isEmpty()) {
        // 未保存但有状态快照：从快照恢复
        ProjectManager::instance()->fromJsonString(target.stateJson);
        applyProjectState();
        m_bottomPanel->appendOutput(QStringLiteral("已切换到工程: ") +
                                    ProjectManager::instance()->currentProjectName());
    } else {
        // 全新工程：创建空状态
        ProjectManager::instance()->newProject(target.name);
        applyProjectState();
    }

    // 3. Refresh tree: active marker + section title + file children
    projects[index].name = ProjectManager::instance()->currentProjectName().isEmpty()
                               ? projects[index].name
                               : ProjectManager::instance()->currentProjectName();
    projects[index].stateJson = ProjectManager::instance()->toJsonString();
    m_sideBar->projectPanel()->refreshList();
}

void MainWindow::onProjectCreated(const QString &name)
{
    // 1. 保存当前工程状态到状态快照
    auto &projects = m_sideBar->projectPanel()->projectsRef();
    int curIdx = m_sideBar->projectPanel()->currentIndex();
    captureProjectState();
    if (curIdx >= 0 && curIdx < projects.size())
        projects[curIdx].stateJson = ProjectManager::instance()->toJsonString();
    if (!ProjectManager::instance()->currentFilePath().isEmpty())
        ProjectManager::instance()->saveProject();

    // 2. 创建新工程
    ProjectManager::instance()->newProject(name);
    applyProjectState();
    m_bottomPanel->appendOutput(QStringLiteral("已创建新工程: ") + name);

    // 3. 刷新树形列表
    projects[curIdx].stateJson = ProjectManager::instance()->toJsonString();
    m_sideBar->projectPanel()->refreshList();
}

void MainWindow::captureProjectState()
{
    auto &st = ProjectManager::instance()->currentStateRef();

    // 数据源（Flow 页经 flow 模块查询，拆分方案 B4）
    const QVariantMap flowState = flowQuery(QStringLiteral("projectState")).toMap();
    if (!flowState.isEmpty()) {
        st.sourceMode = flowState.value(QStringLiteral("sourceMode")).toInt();
        st.filePath = flowState.value(QStringLiteral("filePath")).toString();
    }

    // 波特率 / 通道
    if (m_simulator) {
        st.baudrate = m_simulator->baudrate();
        st.channel = m_simulator->channel();
    }

    // Device config (CAN FD / baud / full device identity via Flow module)
    const QVariantMap devCfg = flowQuery(QStringLiteral("deviceConfig")).toMap();
    if (!devCfg.isEmpty()) {
        st.deviceConfig.fd = devCfg.value(QStringLiteral("canFd")).toBool();
        st.deviceConfig.fdBaudrate = devCfg.value(QStringLiteral("fdBaudrate")).toInt();
        st.deviceConfig.kind = devCfg.value(QStringLiteral("deviceKind")).toInt();
        st.deviceConfig.index = devCfg.value(QStringLiteral("deviceIndex")).toInt();
        st.deviceConfig.subType = devCfg.value(QStringLiteral("deviceSubType")).toInt();
        st.deviceConfig.name = devCfg.value(QStringLiteral("deviceName")).toString();
        st.deviceConfig.type = QStringLiteral("devKind%1").arg(st.deviceConfig.kind);
        if (devCfg.value(QStringLiteral("baudrate")).toInt() > 0)
            st.baudrate = devCfg.value(QStringLiteral("baudrate")).toInt();
        if (devCfg.value(QStringLiteral("channel")).toInt() > 0)
            st.channel = devCfg.value(QStringLiteral("channel")).toInt();
    }

    // Flow block enables
    st.flowBlockEnabled.clear();
    {
        const QVariantMap en = flowQuery(QStringLiteral("blockEnabledMap")).toMap();
        for (auto it = en.constBegin(); it != en.constEnd(); ++it)
            st.flowBlockEnabled.insert(it.key(), it.value().toBool());
    }

    // Flow filter rule summaries
    st.flowFilterRules = flowQuery(QStringLiteral("filterRules")).toStringList();

    // DBC files
    st.dbcFiles.clear();
    if (m_dbcManager) {
        for (const auto &f : m_dbcManager->files())
            st.dbcFiles << f.filePath;
    }

    // Trace 实例 — 遍历全部实例（过滤表达式经 trace 模块查询，B5）
    st.traces.clear();
    {
        QStringList ids = m_traceInstances.keys();
        std::sort(ids.begin(), ids.end(),
                  [](const QString &a, const QString &b) {
                      return a.mid(5).toInt() < b.mid(5).toInt();
                  });
        for (const auto &id : ids) {
            ProjectTraceInstance ti;
            ti.id = id;
            ti.title = QString("帧列表%1").arg(id.mid(5).toInt());
            ti.filterExpression = traceQuery(QStringLiteral("filterExpression"),
                                             id).toString();
            ti.colorRules = traceQuery(QStringLiteral("colorRules"), id).toList();
            ti.hasColorRules = true;
            st.traces.append(ti);
        }
    }

    // Graphic 实例 — 遍历全部实例（信号配置经 graphic 模块查询，B5）
    st.graphics.clear();
    {
        QStringList ids = m_graphicInstances.keys();
        std::sort(ids.begin(), ids.end(),
                  [](const QString &a, const QString &b) {
                      return a.mid(7).toInt() < b.mid(7).toInt();
                  });
        for (const auto &id : ids) {
            QWidget *gv = m_graphicInstances.value(id);
            if (!gv)
                continue;
            ProjectGraphicInstance gi;
            gi.id = id;
            gi.title = QString("时序波形%1").arg(id.mid(7).toInt());
            const auto sigList = graphicQuery(QStringLiteral("signalConfigs"),
                                              QVariant::fromValue(gv)).toList();
            for (const auto &sigVar : sigList) {
                const auto sig = sigVar.toMap();
                ProjectSigCfg sc;
                sc.canId = sig.value(QStringLiteral("canId")).toUInt();
                sc.name = sig.value(QStringLiteral("name")).toString();
                sc.extended = sig.value(QStringLiteral("extended")).toBool();
                sc.color = sig.value(QStringLiteral("color")).toString();
                sc.displayMode = sig.value(QStringLiteral("displayMode"), 1).toInt();
                gi.sigList.append(sc);
            }
            st.graphics.append(gi);
        }
    }

    // Watcher variables
    st.watchers.clear();
    if (m_watcherView) {
        for (const auto &e : m_watcherView->watchEntries()) {
            ProjectWatcherEntry we;
            we.canId = e.canId;
            we.name = e.name;
            we.messageName = e.messageName;
            we.extended = e.extended;
            st.watchers.append(we);
        }
    }

    // Offline analysis file list (via transceive module)
    st.offlineFiles = transceiveQuery(QStringLiteral("offlineFiles")).toStringList();
    // Seed offline from Flow playback path when file-mode and list empty
    if (st.sourceMode == 1 && !st.filePath.isEmpty()
        && QFile::exists(st.filePath)
        && !st.offlineFiles.contains(st.filePath))
        st.offlineFiles.prepend(st.filePath);

    // Record tab UI + send list (empty if tabs never opened)
    st.recordConfig = transceiveQuery(QStringLiteral("recordConfig")).toMap();
    st.sendEntries = transceiveQuery(QStringLiteral("sendEntries")).toList();
    st.playbackConfig = transceiveQuery(QStringLiteral("playbackConfig")).toMap();

    // 标签页顺序
    st.openTabs.clear();
    st.activeTab.clear();
    if (m_editorArea) {
        const auto allTabs = m_editorArea->allTabWidgets();
        for (auto *tw : allTabs) {
            for (int i = 0; i < tw->count(); ++i)
                st.openTabs << tw->tabText(i).trimmed();
            int idx = tw->currentIndex();
            if (idx >= 0 && idx < tw->count())
                st.activeTab = tw->tabText(idx).trimmed();
            break;
        }
    }

    ProjectManager::instance()->currentStateRef() = st;
}

void MainWindow::applyProjectState()
{
    const auto &st = ProjectManager::instance()->currentState();

    // 1. 停止测量
    m_simulator->stop();
    m_player->stop();

    // 2. 关闭当前所有 Trace/Graphic 标签页及收发四页（回放/离线分析/录制/
    //    发送，切换工程时旧页指向旧工程数据）。
    //    使用 delete 而非 deleteLater — 必须在 DBC 卸载前销毁 widget，
    //    防止旧 TraceTab/GraphicView 在 DBC 卸载后访问已释放的 DBC 数据
    if (m_editorArea) {
        const auto allTabs = m_editorArea->allTabWidgets();
        for (auto *tw : allTabs) {
            for (int i = tw->count() - 1; i >= 0; --i) {
                QString text = tw->tabText(i);
                if (isTraceTabText(text) || isGraphicTabText(text) ||
                    text.contains(QStringLiteral("回放")) ||
                    text.contains(QStringLiteral("离线分析")) ||
                    text.contains(QStringLiteral("录制")) ||
                    text.contains(QStringLiteral("发送")) ||
                    text.contains(QStringLiteral("Watcher"))) {
                    QWidget *w = tw->widget(i);
                    tw->removeTab(i);
                    delete w;  // immediate destroy — avoid use-after-free
                }
            }
        }
    }
    m_traceInstances.clear();
    m_graphicInstances.clear();
    m_traceCount = 0;
    m_graphicCount = 0;
    // 关页触发的 Queued removeModuleInstance 立即派发，防止其在
    // 下文重建实例完成后误删新注册的 flow 画布块
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);

    // 3. Unload all DBC and reload
    auto dbcFiles = m_dbcManager->files();
    for (const auto &f : dbcFiles)
        m_dbcManager->unloadDbc(f.filePath);
    for (const auto &path : st.dbcFiles) {
        if (QFile::exists(path)) {
            m_dbcManager->loadDbc(path);
        } else {
            m_bottomPanel->appendOutput(QStringLiteral("DBC 文件不存在: ") + path);
        }
    }

    // Ensure Flow setup page exists before source/device restore (do not rely on openTabs)
    onOpenMeasurementSetup();

    // 4. Data source (via flow module)
    flowInvoke(QStringLiteral("setSource"), st.sourceMode);
    flowInvoke(QStringLiteral("setFilePath"), st.filePath);

    // 5. Baud / channel / full device identity (via Flow module)
    m_simulator->setChannel(static_cast<quint8>(st.channel));
    m_simulator->setBaudrate(st.baudrate);
    QVariantMap devCfg;
    devCfg.insert(QStringLiteral("baudrate"), st.baudrate);
    devCfg.insert(QStringLiteral("channel"), st.channel);
    devCfg.insert(QStringLiteral("canFd"), st.deviceConfig.fd);
    devCfg.insert(QStringLiteral("fdBaudrate"), st.deviceConfig.fdBaudrate);
    devCfg.insert(QStringLiteral("deviceKind"), st.deviceConfig.kind);
    devCfg.insert(QStringLiteral("deviceIndex"), st.deviceConfig.index);
    devCfg.insert(QStringLiteral("deviceSubType"), st.deviceConfig.subType);
    devCfg.insert(QStringLiteral("deviceName"),
                  st.deviceConfig.name.isEmpty()
                      ? QStringLiteral("Device")
                      : st.deviceConfig.name);
    flowInvoke(QStringLiteral("setDeviceConfig"), devCfg);

    // 6. 按保存顺序重建标签页（Trace/Graphic 先仅创建，配置在步骤 7/8
    //    回填；DBC 详情/预览等复杂页面暂不重建，后续版本支持）
    for (const QString &tabName : st.openTabs) {
        if (tabName.contains(QStringLiteral("Flow"), Qt::CaseInsensitive)) {
            // Already ensured above — skip second open (avoids extra default tabs)
            continue;
        } else if (isTraceTabText(tabName)) {
            QString numPart = tabName;
            numPart.remove(QStringLiteral("帧列表"))
                   .remove(QStringLiteral("Trace"), Qt::CaseInsensitive);
            createTraceInstance(QString("trace%1").arg(numPart.toInt()));
        } else if (isGraphicTabText(tabName)) {
            QString numPart = tabName;
            numPart.remove(QStringLiteral("时序波形"))
                   .remove(QStringLiteral("Graphic"), Qt::CaseInsensitive);
            createGraphicInstance(QString("graphic%1").arg(numPart.toInt()));
        } else if (tabName.contains(QStringLiteral("发送"))) {
            onOpenSendTab();
        } else if (tabName.contains(QStringLiteral("回放"))) {
            onOpenPlaybackTab();
        } else if (tabName.contains(QStringLiteral("离线分析"))) {
            onOpenOfflineAnalysisTab();
        } else if (tabName.contains(QStringLiteral("录制"))) {
            onOpenRecordTab();
        } else if (tabName.contains(QStringLiteral("Watcher"))) {
            onOpenWatcher();
        }
    }

    // 7. 创建 Trace 实例（补建 openTabs 之外的实例）+ 过滤表达式回填
    //   （经 trace 模块创建+装配，拆分方案 B5）
    for (const auto &t : st.traces) {
        QWidget *tab = m_traceInstances.contains(t.id)
                           ? m_traceInstances.value(t.id)
                           : createTraceInstance(t.id);
        if (tab && !t.filterExpression.isEmpty())
            traceInvoke(QStringLiteral("setFilterExpression"),
                        QVariantList{ QVariant::fromValue(tab), t.filterExpression });
        if (tab && t.hasColorRules)
            traceInvoke(QStringLiteral("setColorRules"),
                        QVariantList{ QVariant::fromValue(tab), t.colorRules });
    }
    // 若保存状态中没有 Trace 实例，则创建默认的
    if (m_traceInstances.isEmpty())
        createTraceInstance(QStringLiteral("trace1"));

    // 8. 创建 Graphic 实例（补建 openTabs 之外的实例）+ 信号配置回填
    //   （经 graphic 模块创建，拆分方案 B5）
    for (const auto &g : st.graphics) {
        QWidget *gv = m_graphicInstances.contains(g.id)
                          ? m_graphicInstances.value(g.id)
                          : createGraphicInstance(g.id);
        if (!gv)
            continue;
        // 重建信号配置（dbcSig 从当前已加载的 DBC 查补完整定义，未找到用默认值）
        QVariantList sigMaps;
        for (const auto &s : g.sigList) {
            const DbcMessage *msg = m_dbcManager->findMessage(s.canId);
            const DbcSignal *ds = msg ? msg->findSignal(s.name) : nullptr;
            QVariantMap sm = buildSignalMap(s.canId, s.extended, s.name,
                                           ds ? *ds : DbcSignal());
            if (!s.color.isEmpty())
                sm.insert(QStringLiteral("color"), s.color);
            sm.insert(QStringLiteral("displayMode"), s.displayMode);
            sigMaps.append(sm);
        }
        if (!sigMaps.isEmpty())
            graphicInvoke(QStringLiteral("loadSignalConfigs"),
                          QVariantList{ QVariant::fromValue(gv), sigMaps });
    }
    // 若保存状态中没有 Graphic 实例，则创建默认的
    if (m_graphicInstances.isEmpty())
        createGraphicInstance(QStringLiteral("graphic1"));

    // 9. Offline analysis files + seed from Flow playback path
    {
        QStringList offline = st.offlineFiles;
        if (st.sourceMode == 1 && !st.filePath.isEmpty()
            && QFile::exists(st.filePath)
            && !offline.contains(st.filePath))
            offline.prepend(st.filePath);
        if (!offline.isEmpty()) {
            onOpenOfflineAnalysisTab();  // ensure tab exists — addOfflineFiles no-ops otherwise
            transceiveInvoke(QStringLiteral("addOfflineFiles"), offline);
        }
    }

    // 9a. Record tab UI config
    if (!st.recordConfig.isEmpty()) {
        onOpenRecordTab();
        transceiveInvoke(QStringLiteral("loadRecordConfig"), st.recordConfig);
    }

    // 9a2. Send list entries
    if (!st.sendEntries.isEmpty()) {
        onOpenSendTab();
        transceiveInvoke(QStringLiteral("loadSendEntries"), st.sendEntries);
    }

    // 9a3. Playback tab UI config
    if (!st.playbackConfig.isEmpty()) {
        onOpenPlaybackTab();
        transceiveInvoke(QStringLiteral("loadPlaybackConfig"), st.playbackConfig);
    }

    // 9b. Watcher signal list (open tab if entries exist)
    if (!st.watchers.isEmpty()) {
        if (!m_watcherView)
            onOpenWatcher();
        if (m_watcherView) {
            QVector<WatcherView::WatchEntry> entries;
            entries.reserve(st.watchers.size());
            for (const auto &w : st.watchers) {
                WatcherView::WatchEntry e;
                e.canId = w.canId;
                e.name = w.name;
                e.messageName = w.messageName;
                e.extended = w.extended;
                entries.append(e);
            }
            m_watcherView->loadWatchEntries(entries);
        }
    }

    // 10. Update Flow canvas (via flow module)
    flowInvoke(QStringLiteral("clearTraceGraphicInstances"), {});
    for (const auto &t : st.traces)
        flowInvoke(QStringLiteral("addModuleInstance"),
                   QVariantList{ QStringLiteral("trace"), t.id, t.title });
    for (const auto &g : st.graphics)
        flowInvoke(QStringLiteral("addModuleInstance"),
                   QVariantList{ QStringLiteral("graphic"), g.id, g.title });
    flowInvoke(QStringLiteral("rebuildScene"), {});

    // 10b. Restore Flow block enable flags (after rebuild)
    if (!st.flowBlockEnabled.isEmpty()) {
        QVariantMap en;
        for (auto it = st.flowBlockEnabled.constBegin();
             it != st.flowBlockEnabled.constEnd(); ++it)
            en.insert(it.key(), it.value());
        flowInvoke(QStringLiteral("setBlockEnabledMap"), en);
    }

    // 10c. Filter rules (always clear then set — avoid cross-project leak)
    flowInvoke(QStringLiteral("setFilterRules"), st.flowFilterRules);

    // 11. Always land on Flow after project activate/restore (do not restore
    //    last activeTab — user expects a consistent entry point).
    onOpenMeasurementSetup();

    // 12. Window title
    setWindowTitle(QStringLiteral("openbus - %1").arg(st.name));

    m_bottomPanel->appendOutput(QStringLiteral("工程现场已恢复: %1").arg(st.name));
}

// ============================================================
//  工程文件文本预览标签页
// ============================================================
void MainWindow::onFilePreviewRequested(const QString &filePath)
{
    QFileInfo fi(filePath);
    QString label = QStringLiteral("[预览] %1").arg(fi.fileName());

    // 检查是否已存在同名标签页
    auto *tabs = m_editorArea->activeTabWidget();
    if (tabs) {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabText(i) == label) {
                tabs->setCurrentIndex(i);
                if (m_tabLabel) m_tabLabel->setText(label);
                return;
            }
        }
    }

    // 创建文本预览部件
    auto *preview = new QPlainTextEdit(this);
    preview->setReadOnly(true);
    preview->setFont(QFont(QStringLiteral("Consolas"), 10));

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        preview->setPlainText(QStringLiteral("无法打开文件:\n%1").arg(filePath));
    } else {
        QByteArray data = file.readAll();
        file.close();

        // 检测二进制内容（包含 NULL 字节）
        if (data.contains('\0') && data.size() > 0) {
            preview->setPlainText(
                QStringLiteral("二进制文件，无法以文本方式预览:\n%1\n\n文件大小: %2 bytes")
                    .arg(filePath)
                    .arg(data.size()));
        } else {
            QString ext = fi.suffix().toLower();
            if (ext == "openbusproj" || ext == "json") {
                // JSON 文件 — 格式化输出
                try {
                    auto j = nlohmann::json::parse(data.toStdString());
                    preview->setPlainText(QString::fromStdString(j.dump(2)));
                } catch (...) {
                    preview->setPlainText(QString::fromUtf8(data));
                }
            } else {
                preview->setPlainText(QString::fromUtf8(data));
            }
        }
    }

    openTab(preview, label);
}
