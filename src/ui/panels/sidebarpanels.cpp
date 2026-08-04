#include "sidebarpanels.h"
#include "core/dbcmanager.h"
#include "core/cansimulator.h"
#include "core/candevicemanager.h"
#include "core/appconfig.h"
#include "ui/graphicview.h"
#include "ui/thememanager.h"

#include <nlohmann/json.hpp>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTreeWidget>
#include <QListWidget>
#include <QListWidgetItem>
#include <QComboBox>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QStyle>
#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QLinearGradient>
#include <QFile>
#include <QTextStream>

// ============================================================
//  SidePanel 基类
// ============================================================

SidePanel::SidePanel(const QString &title, QWidget *parent)
    : QWidget(parent), m_contentLayout(nullptr)
{
    setupTitle(title);
}

void SidePanel::setupTitle(const QString &title)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *titleBar = new QLabel(title, this);
    titleBar->setObjectName("SidePanelTitle");
    titleBar->setContentsMargins(8, 6, 8, 6);
    layout->addWidget(titleBar);

    auto *contentWidget = new QWidget(this);
    m_contentLayout = new QVBoxLayout(contentWidget);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);
    layout->addWidget(contentWidget, 1);
}

// ============================================================
//  ProjectPanel
// ============================================================

ProjectPanel::ProjectPanel(QWidget *parent)
    : SidePanel("工程列表", parent)
{
    auto *cl = contentLayout();

    m_projectList = new QListWidget(this);
    m_projectList->setObjectName("ProjectList");
    cl->addWidget(m_projectList, 1);

    // 最近工程列表
    auto *recentLabel = new QLabel("最近打开", this);
    recentLabel->setStyleSheet("font-weight: bold; padding: 2px; color: #888;");
    cl->addWidget(recentLabel);
    m_recentList = new QListWidget(this);
    m_recentList->setMaximumHeight(100);
    cl->addWidget(m_recentList);

    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(4, 4, 4, 4);
    auto *newBtn = new QPushButton("新建", this);
    auto *openBtn = new QPushButton("打开", this);
    auto *saveBtn = new QPushButton("保存", this);
    auto *delBtn = new QPushButton("删除", this);
    btnBar->addWidget(newBtn);
    btnBar->addWidget(openBtn);
    btnBar->addWidget(saveBtn);
    btnBar->addWidget(delBtn);
    cl->addLayout(btnBar);

    ProjectContext defaultProj;
    defaultProj.name = "默认工程";
    m_projects.append(defaultProj);
    m_currentIndex = 0;
    refreshList();
    refreshRecentList();

    connect(newBtn, &QPushButton::clicked, this, &ProjectPanel::onNewProject);
    connect(openBtn, &QPushButton::clicked, this, &ProjectPanel::onOpenProject);
    connect(saveBtn, &QPushButton::clicked, this, &ProjectPanel::onSaveProject);
    connect(delBtn, &QPushButton::clicked, this, &ProjectPanel::onDeleteProject);
    connect(m_projectList, &QListWidget::currentRowChanged,
            this, &ProjectPanel::onProjectSelected);
    connect(m_recentList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *item) {
        QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty())
            emit openProjectRequested(path);
    });
}

void ProjectPanel::refreshList()
{
    m_projectList->clear();
    for (int i = 0; i < m_projects.size(); ++i) {
        auto *item = new QListWidgetItem(m_projects[i].name);
        if (i == m_currentIndex) {
            item->setText(m_projects[i].name + "  (当前)");
        }
        m_projectList->addItem(item);
    }
    if (m_currentIndex >= 0 && m_currentIndex < m_projectList->count())
        m_projectList->setCurrentRow(m_currentIndex);
}

void ProjectPanel::onNewProject()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, "新建工程",
        "工程名称:", QLineEdit::Normal,
        QString("工程 %1").arg(m_projects.size() + 1), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    ProjectContext proj;
    proj.name = name.trimmed();
    m_projects.append(proj);
    m_currentIndex = m_projects.size() - 1;
    refreshList();
    emit projectCreated(proj.name);
    emit projectSwitched(m_currentIndex);
}

void ProjectPanel::onSaveProject()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_projects.size()) return;

    QString path = m_projects[m_currentIndex].filePath;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, "保存工程", m_projects[m_currentIndex].name + ".sinproj",
            "sin 工程文件 (*.sinproj);;所有文件 (*.*)");
        if (path.isEmpty()) return;
    }

    m_projects[m_currentIndex].filePath = path;
    emit saveProjectRequested(path);
    refreshRecentList();
}

void ProjectPanel::onOpenProject()
{
    QString path = QFileDialog::getOpenFileName(
        this, "打开工程", {},
        "sin 工程文件 (*.sinproj);;所有文件 (*.*)");
    if (path.isEmpty()) return;

    // 添加到工程列表
    QFileInfo fi(path);
    ProjectContext proj;
    proj.name = fi.baseName();
    proj.filePath = path;

    // 检查是否已存在
    for (int i = 0; i < m_projects.size(); ++i) {
        if (m_projects[i].filePath == path) {
            m_currentIndex = i;
            refreshList();
            emit projectSwitched(m_currentIndex);
            return;
        }
    }

    m_projects.append(proj);
    m_currentIndex = m_projects.size() - 1;
    refreshList();
    emit openProjectRequested(path);
}

void ProjectPanel::onOpenRecent()
{
    // 由 m_recentList 的 itemDoubleClicked 直接处理
}

void ProjectPanel::refreshRecentList()
{
    if (!m_recentList) return;
    m_recentList->clear();

    // 从 AppConfig 读取最近工程列表
    QString raw = AppConfig::instance()->getString("project.recent", "");
    if (raw.isEmpty()) return;

    try {
        auto j = nlohmann::json::parse(raw.toStdString());
        if (j.is_array()) {
            for (const auto &item : j) {
                if (item.is_string()) {
                    QString p = QString::fromStdString(item.get<std::string>());
                    QFileInfo fi(p);
                    auto *listItem = new QListWidgetItem(fi.fileName());
                    listItem->setToolTip(p);
                    listItem->setData(Qt::UserRole, p);
                    m_recentList->addItem(listItem);
                }
            }
        }
    } catch (...) {}
}

void ProjectPanel::onDeleteProject()
{
    if (m_projects.size() <= 1) {
        QMessageBox::information(this, "删除工程", "至少保留一个工程");
        return;
    }
    if (m_currentIndex < 0) return;

    auto reply = QMessageBox::question(this, "删除工程",
        QString("确定删除工程 \"%1\"?").arg(m_projects[m_currentIndex].name));
    if (reply != QMessageBox::Yes) return;

    m_projects.removeAt(m_currentIndex);
    m_currentIndex = qMax(0, m_currentIndex - 1);
    refreshList();
    emit projectSwitched(m_currentIndex);
}

void ProjectPanel::onProjectSelected(int row)
{
    if (row < 0 || row >= m_projects.size()) return;
    m_currentIndex = row;
    emit projectSwitched(m_currentIndex);
}

// ============================================================
//  DbcPanel
// ============================================================

DbcPanel::DbcPanel(QWidget *parent)
    : SidePanel("DBC 文件列表", parent)
{
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(0);          // 扁平列表，无缩进
    m_tree->setColumnCount(1);
    m_tree->setRootIsDecorated(false);  // 不显示展开箭头

    auto *cl = contentLayout();
    cl->addWidget(m_tree);

    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(4, 4, 4, 4);
    auto *importBtn = new QPushButton("+ 加载 DBC", this);
    btnBar->addWidget(importBtn);
    btnBar->addStretch();
    cl->addLayout(btnBar);

    connect(importBtn, &QPushButton::clicked, this, &DbcPanel::onImportDbc);
    connect(m_tree, &QTreeWidget::itemClicked,
            this, &DbcPanel::onItemClicked);

    auto *hint = new QTreeWidgetItem(m_tree, {"（点击加载 DBC 文件）"});
    hint->setFlags(Qt::NoItemFlags);
}

void DbcPanel::setDbcManager(DbcManager *mgr)
{
    m_dbcMgr = mgr;
    if (m_dbcMgr) {
        connect(m_dbcMgr, &DbcManager::dbcLoaded, this, [this](const QString &) { refreshTree(); });
        connect(m_dbcMgr, &DbcManager::dbcUnloaded, this, [this](const QString &) { refreshTree(); });
    }
    refreshTree();
}

void DbcPanel::onImportDbc()
{
    QString path = QFileDialog::getOpenFileName(
        this, "导入 DBC 文件", {}, "DBC 文件 (*.dbc);;所有文件 (*.*)");
    if (path.isEmpty() || !m_dbcMgr)
        return;

    if (!m_dbcMgr->loadDbc(path))
        m_tree->addTopLevelItem(new QTreeWidgetItem({QString("加载失败: %1").arg(path)}));
}

void DbcPanel::refreshTree()
{
    m_tree->clear();
    if (!m_dbcMgr) return;

    // 仅显示 DBC 文件名，不展开内部结构
    for (const auto &file : m_dbcMgr->files()) {
        auto *fileItem = new QTreeWidgetItem(m_tree, {file.fileName});
        fileItem->setIcon(0, style()->standardIcon(QStyle::SP_FileDialogListView));
    }

    if (m_tree->topLevelItemCount() == 0) {
        auto *hint = new QTreeWidgetItem(m_tree, {"（点击加载 DBC 文件）"});
        hint->setFlags(Qt::NoItemFlags);
    }
}

void DbcPanel::onItemClicked(QTreeWidgetItem *item, int)
{
    // 点击文件项 → 发出 dbcFileClicked 信号，在右侧标签页展开
    if (item && (item->flags() != Qt::NoItemFlags))
        emit dbcFileClicked(item->text(0));
}

// ============================================================
//  TracePanel — Trace 标签页列表 + 新建按钮
// ============================================================

TracePanel::TracePanel(QWidget *parent)
    : SidePanel("Trace", parent)
{
    auto *cl = contentLayout();

    m_traceList = new QListWidget(this);
    cl->addWidget(m_traceList, 1);

    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(4, 4, 4, 4);
    auto *newBtn = new QPushButton("+ 新建 Trace", this);
    btnBar->addWidget(newBtn);
    btnBar->addStretch();
    cl->addLayout(btnBar);

    connect(newBtn, &QPushButton::clicked, this, &TracePanel::onTraceClicked);
    connect(m_traceList, &QListWidget::currentRowChanged,
            this, &TracePanel::onPageSelected);
}

void TracePanel::refreshList(const QStringList &names)
{
    m_traceList->blockSignals(true);
    int prevRow = m_traceList->currentRow();
    m_traceList->clear();
    for (const auto &n : names)
        m_traceList->addItem(n);
    if (prevRow >= 0 && prevRow < m_traceList->count())
        m_traceList->setCurrentRow(prevRow);
    m_traceList->blockSignals(false);
}

void TracePanel::onTraceClicked()
{
    emit openTraceRequested();
}

void TracePanel::onPageSelected(int row)
{
    if (row >= 0)
        emit tracePageSelected(row);
}

// ============================================================
//  GraphicConfigPanel — Graphic 页面列表
// ============================================================

GraphicConfigPanel::GraphicConfigPanel(QWidget *parent)
    : SidePanel("Graphic 页面列表", parent)
{
    auto *cl = contentLayout();

    m_pageList = new QListWidget(this);
    cl->addWidget(m_pageList, 1);

    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(4, 4, 4, 4);
    auto *newBtn = new QPushButton("+ 新建 Graphic", this);
    btnBar->addWidget(newBtn);
    btnBar->addStretch();
    cl->addLayout(btnBar);

    connect(newBtn, &QPushButton::clicked, this, &GraphicConfigPanel::onNewGraphic);
    connect(m_pageList, &QListWidget::currentRowChanged,
            this, &GraphicConfigPanel::onPageSelected);
}

void GraphicConfigPanel::setGraphicView(GraphicView *view)
{
    m_graphicView = view;
}

void GraphicConfigPanel::onNewGraphic()
{
    emit newGraphicRequested();
}

void GraphicConfigPanel::onPageSelected(int row)
{
    if (row >= 0)
        emit graphicPageSelected(row);
}

void GraphicConfigPanel::refreshList(const QStringList &names)
{
    m_pageList->blockSignals(true);
    int prevRow = m_pageList->currentRow();
    m_pageList->clear();
    for (const auto &n : names)
        m_pageList->addItem(n);
    if (prevRow >= 0 && prevRow < m_pageList->count())
        m_pageList->setCurrentRow(prevRow);
    m_pageList->blockSignals(false);
}

// ============================================================
//  DevicePanel — 仅设备连接
// ============================================================

DevicePanel::DevicePanel(QWidget *parent)
    : SidePanel(QStringLiteral("设备连接"), parent)
{
    auto *cl = contentLayout();

    m_deviceTree = new QTreeWidget(this);
    m_deviceTree->setHeaderHidden(true);
    m_deviceTree->setIndentation(12);
    m_deviceTree->setExpandsOnDoubleClick(false);
    cl->addWidget(m_deviceTree);

    cl->addStretch();

    connect(m_deviceTree, &QTreeWidget::itemClicked,
            this, &DevicePanel::onItemClicked);
    connect(m_deviceTree, &QTreeWidget::itemDoubleClicked,
            this, &DevicePanel::onItemDoubleClicked);

    populateTree();
}

void DevicePanel::setSimulator(CanSimulator *sim)
{
    m_simulator = sim;
}

void DevicePanel::setDeviceManager(CanDeviceManager *mgr)
{
    m_deviceMgr = mgr;
    refreshDevices();
}

void DevicePanel::refreshDevices()
{
    populateTree();
}

void DevicePanel::populateTree()
{
    m_deviceTree->clear();

    // 模拟器（内置）
    auto *simItem = new QTreeWidgetItem(m_deviceTree);
    simItem->setText(0, QStringLiteral("模拟器 (内置)"));
    simItem->setData(0, Qt::UserRole, 0);       // deviceKind = 0
    simItem->setData(0, Qt::UserRole + 1, 0);   // devIndex = 0

    // ZLG 设备系列
    auto *zlgItem = new QTreeWidgetItem(m_deviceTree);
    zlgItem->setText(0, QStringLiteral("ZLG 致远电子"));

    QStringList zlgDevices;
    if (m_deviceMgr) {
        auto devices = CanDeviceManager::enumerateDevices();
        for (int i = 1; i < devices.size(); ++i)
            zlgDevices << devices[i];
    }

    if (zlgDevices.isEmpty()) {
        auto *emptyItem = new QTreeWidgetItem(zlgItem);
        emptyItem->setText(0, QStringLiteral("  ZLG USBCANFD (未检测到硬件)"));
        emptyItem->setData(0, Qt::UserRole, 1);       // deviceKind = 1 (ZLG)
        emptyItem->setData(0, Qt::UserRole + 1, 0);   // devIndex = 0
    } else {
        for (int i = 0; i < zlgDevices.size(); ++i) {
            auto *devItem = new QTreeWidgetItem(zlgItem);
            devItem->setText(0, QStringLiteral("  ") + zlgDevices[i]);
            devItem->setData(0, Qt::UserRole, 1);       // deviceKind = 1 (ZLG)
            devItem->setData(0, Qt::UserRole + 1, i);   // devIndex
        }
    }

    // PEAK (占位)
    auto *peakItem = new QTreeWidgetItem(m_deviceTree);
    peakItem->setText(0, QStringLiteral("PEAK PCAN"));
    auto *peakEmpty = new QTreeWidgetItem(peakItem);
    peakEmpty->setText(0, QStringLiteral("  PCAN-USB (待实现)"));
    peakEmpty->setData(0, Qt::UserRole, 2);       // deviceKind = 2 (PEAK)
    peakEmpty->setData(0, Qt::UserRole + 1, 0);

    // Kvaser (占位)
    auto *kvaserItem = new QTreeWidgetItem(m_deviceTree);
    kvaserItem->setText(0, QStringLiteral("Kvaser"));
    auto *kvaserEmpty = new QTreeWidgetItem(kvaserItem);
    kvaserEmpty->setText(0, QStringLiteral("  Kvaser USBcan (待实现)"));
    kvaserEmpty->setData(0, Qt::UserRole, 3);     // deviceKind = 3 (Kvaser)
    kvaserEmpty->setData(0, Qt::UserRole + 1, 0);

    // 开源 USB-CAN (占位)
    auto *candleItem = new QTreeWidgetItem(m_deviceTree);
    candleItem->setText(0, QStringLiteral("开源 USB-CAN (CandleLight)"));
    auto *candleEmpty = new QTreeWidgetItem(candleItem);
    candleEmpty->setText(0, QStringLiteral("  CandleLight (待实现)"));
    candleEmpty->setData(0, Qt::UserRole, 4);    // deviceKind = 4 (CandleLight)
    candleEmpty->setData(0, Qt::UserRole + 1, 0);

    zlgItem->setExpanded(true);
    peakItem->setExpanded(true);
}

void DevicePanel::onItemClicked(QTreeWidgetItem *item, int /*column*/)
{
    // 父节点 → 展开/折叠
    if (item->childCount() > 0) {
        item->setExpanded(!item->isExpanded());
        return;
    }
    // 叶子节点 → 发出打开请求
    int deviceKind = item->data(0, Qt::UserRole).toInt();
    int devIndex = item->data(0, Qt::UserRole + 1).toInt();
    emit deviceOpenRequested(deviceKind, devIndex, item->text(0).trimmed());
}

void DevicePanel::onItemDoubleClicked(QTreeWidgetItem *item, int /*column*/)
{
    if (item->childCount() > 0)
        return;
    int deviceKind = item->data(0, Qt::UserRole).toInt();
    int devIndex = item->data(0, Qt::UserRole + 1).toInt();
    emit deviceOpenRequested(deviceKind, devIndex, item->text(0).trimmed());
}

// ============================================================
//  SendPanel — 仅入口
// ============================================================

SendPanel::SendPanel(QWidget *parent)
    : SidePanel("发送", parent)
{
    auto *cl = contentLayout();

    auto *sendBtn = new QPushButton("📡 发送  →  点击打开发送标签页", this);
    sendBtn->setStyleSheet("text-align: left; padding: 8px;");
    cl->addWidget(sendBtn);

    auto *playbackBtn = new QPushButton("▶ 回放  →  点击打开回放标签页", this);
    playbackBtn->setStyleSheet("text-align: left; padding: 8px;");
    cl->addWidget(playbackBtn);

    cl->addStretch();

    connect(sendBtn, &QPushButton::clicked, this, &SendPanel::onSendClicked);
    connect(playbackBtn, &QPushButton::clicked, this, &SendPanel::onPlaybackClicked);
}

void SendPanel::onSendClicked()
{
    emit openSendRequested();
}

void SendPanel::onPlaybackClicked()
{
    emit openPlaybackRequested();
}

// ============================================================
//  RecordPanel — 仅入口
// ============================================================

RecordPanel::RecordPanel(QWidget *parent)
    : SidePanel("录制", parent)
{
    auto *cl = contentLayout();

    auto *btn = new QPushButton("● 录制  →  点击打开录制标签页", this);
    btn->setStyleSheet("text-align: left; padding: 8px;");
    cl->addWidget(btn);
    cl->addStretch();

    connect(btn, &QPushButton::clicked, this, &RecordPanel::onRecordClicked);
}

void RecordPanel::onRecordClicked()
{
    emit openRecordRequested();
}

// ============================================================
//  SettingsPanel — 设置入口
// ============================================================

SettingsPanel::SettingsPanel(QWidget *parent)
    : SidePanel("设置", parent)
{
    auto *cl = contentLayout();

    m_list = new QListWidget(this);
    m_list->addItem("通用设置");
    m_list->addItem("快捷键");
    cl->addWidget(m_list);

    // 颜色主题
    auto *themeLabel = new QLabel("颜色主题", this);
    themeLabel->setObjectName("SidePanelTitle");
    themeLabel->setContentsMargins(8, 6, 8, 6);
    cl->addWidget(themeLabel);

    m_themeList = new QListWidget(this);
    for (const auto &name : ThemeManager::instance()->themeNames())
        m_themeList->addItem(name);
    // 默认选中当前主题
    QString cur = ThemeManager::instance()->currentThemeName();
    for (int i = 0; i < m_themeList->count(); ++i) {
        if (m_themeList->item(i)->text() == cur) {
            m_themeList->setCurrentRow(i);
            break;
        }
    }
    cl->addWidget(m_themeList, 1);

    connect(m_list, &QListWidget::itemClicked,
            this, &SettingsPanel::onItemClicked);
    connect(m_themeList, &QListWidget::itemClicked,
            this, &SettingsPanel::onThemeItemClicked);
}

void SettingsPanel::onItemClicked(QListWidgetItem *item)
{
    if (item)
        emit settingsRequested(item->text());
}

void SettingsPanel::onThemeItemClicked(QListWidgetItem *item)
{
    if (item)
        emit themeChanged(item->text());
}

// ============================================================
//  MeasurementSetupPanel — 侧边栏入口面板
// ============================================================

MeasurementSetupPanel::MeasurementSetupPanel(QWidget *parent)
    : SidePanel("分析配置", parent)
{
    auto *cl = contentLayout();

    m_list = new QListWidget(this);
    m_list->addItem(new QListWidgetItem("\xF0\x9F\x93\x8A flow"));
    cl->addWidget(m_list);

    auto *hint = new QLabel("\n"
                           "\xE2\x80\xA2 点击“flow”打开画布\n"
                           "\xE2\x80\xA2 点击模块块可启用/禁用\n"
                           "\xE2\x80\xA2 双击模块块可打开对应标签页", this);
    hint->setWordWrap(true);
    hint->setStyleSheet("padding: 8px; color: #888; font-size: 11px;");
    cl->addWidget(hint);

    connect(m_list, &QListWidget::itemClicked,
            this, &MeasurementSetupPanel::onItemClicked);
}

void MeasurementSetupPanel::onItemClicked(QListWidgetItem *item)
{
    if (!item) return;
    QString text = item->text();
    if (text.contains("flow"))
        emit openMeasurementSetupRequested();
}

// ============================================================
//  工具集面板 — 总线分析工具列表
// ============================================================

ToolsPanel::ToolsPanel(QWidget *parent)
    : SidePanel("工具集", parent)
{
    auto *cl = contentLayout();

    m_list = new QListWidget(this);
    m_list->setObjectName("ToolsList");

    // 文件格式转换类
    auto *convItem = new QListWidgetItem("\xF0\x9F\x9B\x80 BLF \xE2\x86\x94 ASC \xE2\x86\x94 CSV 转换", m_list);
    convItem->setData(Qt::UserRole, "blf_converter");
    convItem->setToolTip("报文日志文件格式互转：BLF / ASC / CSV 之间转换");

    // DBC 工具类（合并：查看编辑 + 信号清单导出）
    auto *dbcItem = new QListWidgetItem("\xF0\x9F\x93\x9D DBC 工具", m_list);
    dbcItem->setData(Qt::UserRole, "dbc_tool");
    dbcItem->setToolTip("DBC 查看/编辑 + 信号清单导出");

    // 总线统计分析类（合并：报文统计 + ID 频率/周期 + 总线负载率）
    auto *statItem = new QListWidgetItem("\xF0\x9F\x93\x8A 总线统计分析", m_list);
    statItem->setData(Qt::UserRole, "bus_analysis");
    statItem->setToolTip("报文统计 / ID 频率周期 / 总线负载率");

    cl->addWidget(m_list);

    auto *hint = new QLabel("\n"
                           "\xE2\x80\xA2 点击工具名打开对应标签页\n"
                           "\xE2\x80\xA2 工具独立运行，不影响当前工程\n"
                           "\xE2\x80\xA2 后续将持续集成更多总线分析工具", this);
    hint->setWordWrap(true);
    hint->setStyleSheet("padding: 8px; color: #888; font-size: 11px;");
    cl->addWidget(hint);

    connect(m_list, &QListWidget::itemClicked,
            this, &ToolsPanel::onItemClicked);
}

void ToolsPanel::onItemClicked(QListWidgetItem *item)
{
    if (!item) return;
    QString key = item->data(Qt::UserRole).toString();
    if (key.isEmpty()) return;
    qDebug() << "[ToolsPanel] item clicked, key:" << key;
    emit toolOpened(key);
}

// ============================================================
//  SideBar — 11 个面板，索引与 ActivityBar 一致
//  0=Project  1=Trace  2=Graphic  3=DBC
//  4=Send     5=Record 6=Device   7=Protocol  8=Analysis
//  9=Tools   10=Settings
// ============================================================

SideBar::SideBar(QWidget *parent)
    : QStackedWidget(parent)
{
    m_project      = new ProjectPanel(this);
    m_trace        = new TracePanel(this);
    m_graphicConfig = new GraphicConfigPanel(this);
    m_dbc          = new DbcPanel(this);
    m_send         = new SendPanel(this);
    m_record       = new RecordPanel(this);
    m_device       = new DevicePanel(this);
    m_protocol     = new ProtocolPanel(this);
    m_analysis     = new MeasurementSetupPanel(this);
    m_tools        = new ToolsPanel(this);
    m_settings     = new SettingsPanel(this);

    addWidget(m_project);        // 0 = Project
    addWidget(m_trace);          // 1 = Trace
    addWidget(m_graphicConfig);  // 2 = Graphic
    addWidget(m_dbc);            // 3 = Dbc
    addWidget(m_send);           // 4 = Send
    addWidget(m_record);         // 5 = Record
    addWidget(m_device);         // 6 = Device
    addWidget(m_protocol);       // 7 = Protocol
    addWidget(m_analysis);       // 8 = Analysis
    addWidget(m_tools);          // 9 = Tools
    addWidget(m_settings);       // 10 = Settings

    setCurrentIndex(0);
    setMinimumWidth(220);
    setMaximumWidth(400);
}

void SideBar::showPanel(int index)
{
    if (index < 0 || index >= count()) return;
    setCurrentIndex(index);
}

void SideBar::togglePanel(int index)
{
    Q_UNUSED(index);
}

// ============================================================
//  协议面板
// ============================================================

ProtocolPanel::ProtocolPanel(QWidget *parent)
    : SidePanel("协议", parent)
{
    auto *layout = contentLayout();

    m_list = new QListWidget(this);
    m_list->setObjectName("ProtocolList");

    // 已实现的协议（可点击打开标签页）
    auto *udsItem = new QListWidgetItem("\xF0\x9F\x9A\x97 UDS 诊断 (ISO 14229)", m_list);
    udsItem->setData(Qt::UserRole, "UDS");
    udsItem->setToolTip("Unified Diagnostic Services — ECU 诊断服务交互");

    auto *canopenItem = new QListWidgetItem("\xF0\x9F\x94\x84 CANopen (CiA 301)", m_list);
    canopenItem->setData(Qt::UserRole, "CANopen");
    canopenItem->setToolTip("CANopen 协议 — NMT/SDO/PDO/Emergency/Heartbeat");

    // 未实现的协议（灰色显示）
    auto *j1939Item = new QListWidgetItem("\xF0\x9F\x9A\x9B J1939", m_list);
    j1939Item->setData(Qt::UserRole, "J1939");
    j1939Item->setToolTip("SAE J1939 — 商用车/工程机械协议（敬请期待）");

    auto *isotpItem = new QListWidgetItem("\xF0\x9F\x93\xA6 ISO-TP (ISO 15765-2)", m_list);
    isotpItem->setData(Qt::UserRole, "ISO-TP");
    isotpItem->setToolTip("CAN 传输层协议 — 多帧拆包/组包（敬请期待）");

    auto *obdItem = new QListWidgetItem("\xF0\x9F\x9A\x97 OBD-II", m_list);
    obdItem->setData(Qt::UserRole, "OBD-II");
    obdItem->setToolTip("车载诊断 — 故障码读取/排放监测（敬请期待）");

    auto *xcpItem = new QListWidgetItem("\xF0\x9F\x93\x8A XCP (CCP/Universal)", m_list);
    xcpItem->setData(Qt::UserRole, "XCP");
    xcpItem->setToolTip("通用标定测量协议 — ECU 标定/数据采集（敬请期待）");

    auto *nmeaItem = new QListWidgetItem("\xF0\x9F\x9A\xA2 NMEA 2000", m_list);
    nmeaItem->setData(Qt::UserRole, "NMEA2000");
    nmeaItem->setToolTip("船舶电子设备互联协议（敬请期待）");

    // 灰色标记未实现
    for (int i = 2; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        item->setForeground(QColor(0x80, 0x80, 0x80));
        QFont f = item->font();
        f.setItalic(true);
        item->setFont(f);
    }

    layout->addWidget(m_list);

    // 信息提示
    auto *infoLabel = new QLabel("点击协议名称打开对应标签页\n灰色项暂未实现", this);
    infoLabel->setStyleSheet("color: #888; font-size: 11px; padding: 4px;");
    infoLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(infoLabel);

    connect(m_list, &QListWidget::itemClicked, this, &ProtocolPanel::onItemClicked);
}

void ProtocolPanel::onItemClicked(QListWidgetItem *item)
{
    if (!item) return;
    QString protocol = item->data(Qt::UserRole).toString();
    if (protocol.isEmpty()) return;

    // 灰色项（未实现）不响应
    if (item->foreground() == QColor(0x80, 0x80, 0x80))
        return;

    emit protocolOpened(protocol);
}
