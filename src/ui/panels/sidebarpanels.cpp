#include "sidebarpanels.h"
#include "core/dbcmanager.h"
#include "core/cansimulator.h"
#include "ui/graphicview.h"

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

    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(4, 4, 4, 4);
    auto *newBtn = new QPushButton("新建", this);
    auto *saveBtn = new QPushButton("保存", this);
    auto *delBtn = new QPushButton("删除", this);
    btnBar->addWidget(newBtn);
    btnBar->addWidget(saveBtn);
    btnBar->addWidget(delBtn);
    cl->addLayout(btnBar);

    ProjectContext defaultProj;
    defaultProj.name = "EngineAnalysis";
    m_projects.append(defaultProj);
    ProjectContext proj2;
    proj2.name = "BodyControl";
    m_projects.append(proj2);
    m_currentIndex = 0;
    refreshList();

    connect(newBtn, &QPushButton::clicked, this, &ProjectPanel::onNewProject);
    connect(saveBtn, &QPushButton::clicked, this, &ProjectPanel::onSaveProject);
    connect(delBtn, &QPushButton::clicked, this, &ProjectPanel::onDeleteProject);
    connect(m_projectList, &QListWidget::currentRowChanged,
            this, &ProjectPanel::onProjectSelected);
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

    QString path = QFileDialog::getSaveFileName(
        this, "保存工程", m_projects[m_currentIndex].name + ".sinproj",
        "sin 工程文件 (*.sinproj);;所有文件 (*.*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "保存工程", "无法创建文件: " + path);
        return;
    }
    QTextStream out(&file);
    const auto &p = m_projects[m_currentIndex];
    out << "name=" << p.name << "\n";
    out << "[dbc]\n";
    for (const auto &f : p.dbcFiles)
        out << f << "\n";
    out << "[record]\n";
    for (const auto &f : p.recordFiles)
        out << f << "\n";
    file.close();
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
    m_tree->setIndentation(16);
    m_tree->setColumnCount(1);

    auto *cl = contentLayout();
    cl->addWidget(m_tree);

    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(4, 4, 4, 4);
    auto *importBtn = new QPushButton("+ 加载 DBC", this);
    btnBar->addWidget(importBtn);
    btnBar->addStretch();
    cl->addLayout(btnBar);

    connect(importBtn, &QPushButton::clicked, this, &DbcPanel::onImportDbc);
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &DbcPanel::onItemDoubleClicked);
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

    for (const auto &file : m_dbcMgr->files()) {
        auto *fileItem = new QTreeWidgetItem(m_tree, {file.fileName});
        fileItem->setIcon(0, style()->standardIcon(QStyle::SP_FileDialogListView));

        for (const auto &msg : file.messages) {
            QString msgText = QString("Msg_0x%1 (%2)")
                .arg(msg.id, 0, 16).toUpper()
                .arg(msg.name);
            auto *msgItem = new QTreeWidgetItem(fileItem, {msgText});
            msgItem->setIcon(0, style()->standardIcon(QStyle::SP_ArrowRight));
            msgItem->setData(0, Qt::UserRole, msg.id);

            for (const auto &sig : msg.signalList) {
                auto *sigItem = new QTreeWidgetItem(msgItem, {sig.name});
                sigItem->setData(0, Qt::UserRole + 0, msg.id);
                sigItem->setData(0, Qt::UserRole + 1, sig.name);
            }
        }
    }
}

void DbcPanel::onItemClicked(QTreeWidgetItem *item, int)
{
    // 点击文件级节点 → 发出 dbcFileClicked 信号
    if (item && !item->parent()) {
        emit dbcFileClicked(item->text(0));
    }
}

void DbcPanel::onItemDoubleClicked(QTreeWidgetItem *item, int)
{
    QString sigName = item->data(0, Qt::UserRole + 1).toString();
    if (!sigName.isEmpty()) {
        quint32 canId = item->data(0, Qt::UserRole + 0).toUInt();
        emit signalDoubleClicked(canId, sigName);
    }
}

// ============================================================
//  TracePanel — 仅入口
// ============================================================

TracePanel::TracePanel(QWidget *parent)
    : SidePanel("Trace", parent)
{
    auto *cl = contentLayout();

    auto *btn = new QPushButton("📋 Trace  →  点击打开 Trace 标签页", this);
    btn->setStyleSheet("text-align: left; padding: 8px;");
    cl->addWidget(btn);
    cl->addStretch();

    connect(btn, &QPushButton::clicked, this, &TracePanel::onTraceClicked);
}

void TracePanel::onTraceClicked()
{
    emit openTraceRequested();
}

// ============================================================
//  GraphicConfigPanel — Graphic 页面列表
// ============================================================

GraphicConfigPanel::GraphicConfigPanel(QWidget *parent)
    : SidePanel("Graphic 页面列表", parent)
{
    auto *cl = contentLayout();

    m_pageList = new QListWidget(this);
    m_pageList->addItem("Graphic1  (3 个信号)");
    m_pageList->addItem("Graphic2  (2 个信号)");
    m_pageList->addItem("Graphic3  (空)");
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
    refreshList();
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

void GraphicConfigPanel::refreshList()
{
    // 未来根据 GraphicView 列表刷新
}

// ============================================================
//  DevicePanel — 仅设备连接
// ============================================================

DevicePanel::DevicePanel(QWidget *parent)
    : SidePanel("设备连接", parent)
{
    auto *cl = contentLayout();

    auto *formWidget = new QWidget(this);
    auto *formLayout = new QVBoxLayout(formWidget);
    formLayout->setContentsMargins(8, 8, 8, 8);
    formLayout->setSpacing(6);

    // 通道
    auto *chLayout = new QHBoxLayout;
    chLayout->addWidget(new QLabel("通道:", formWidget));
    m_channelCombo = new QComboBox(formWidget);
    m_channelCombo->addItem("1");
    m_channelCombo->addItem("2");
    chLayout->addWidget(m_channelCombo, 1);
    formLayout->addLayout(chLayout);

    // 波特率
    auto *brLayout = new QHBoxLayout;
    brLayout->addWidget(new QLabel("波特率:", formWidget));
    m_baudCombo = new QComboBox(formWidget);
    m_baudCombo->addItem("500000");
    m_baudCombo->addItem("250000");
    m_baudCombo->addItem("1000000");
    m_baudCombo->addItem("125000");
    m_baudCombo->addItem("800000");
    brLayout->addWidget(m_baudCombo, 1);
    formLayout->addLayout(brLayout);

    // FD 配置
    auto *fdLayout = new QHBoxLayout;
    fdLayout->addWidget(new QLabel("FD 配置:", formWidget));
    m_fdCombo = new QComboBox(formWidget);
    m_fdCombo->addItem("CAN 2.0");
    m_fdCombo->addItem("CAN FD");
    fdLayout->addWidget(m_fdCombo, 1);
    formLayout->addLayout(fdLayout);

    // 设备类型
    auto *devLayout = new QHBoxLayout;
    devLayout->addWidget(new QLabel("设备:", formWidget));
    m_deviceCombo = new QComboBox(formWidget);
    m_deviceCombo->addItem("模拟器 (内置)");
    m_deviceCombo->addItem("PCAN-USB");
    m_deviceCombo->addItem("Kvaser");
    m_deviceCombo->addItem("Vector VN1630");
    m_deviceCombo->addItem("SocketCAN");
    devLayout->addWidget(m_deviceCombo, 1);
    formLayout->addLayout(devLayout);

    // 按钮
    auto *btnBar = new QHBoxLayout;
    m_connectBtn = new QPushButton("连接", formWidget);
    m_disconnectBtn = new QPushButton("断开", formWidget);
    m_disconnectBtn->setEnabled(false);
    btnBar->addWidget(m_connectBtn);
    btnBar->addWidget(m_disconnectBtn);
    formLayout->addLayout(btnBar);

    m_statusLabel = new QLabel("● 未连接", formWidget);
    m_statusLabel->setContentsMargins(4, 0, 4, 4);
    formLayout->addWidget(m_statusLabel);

    formLayout->addStretch();
    cl->addWidget(formWidget);

    connect(m_connectBtn, &QPushButton::clicked, this, &DevicePanel::onConnect);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &DevicePanel::onDisconnect);
}

void DevicePanel::setSimulator(CanSimulator *sim)
{
    m_simulator = sim;
}

void DevicePanel::onConnect()
{
    QString device = m_deviceCombo->currentText();
    int baudrate = m_baudCombo->currentText().toInt();
    int channel = m_channelCombo->currentText().toInt();

    if (m_deviceCombo->currentIndex() == 0 && m_simulator) {
        m_simulator->setChannel(static_cast<quint8>(channel));
        m_simulator->start();
    }

    m_connectBtn->setEnabled(false);
    m_disconnectBtn->setEnabled(true);
    m_statusLabel->setText(QString("● 已连接: %1 (Ch%2, %3)")
        .arg(device).arg(channel).arg(baudrate));
    m_statusLabel->setStyleSheet("color: green;");

    emit deviceConnectRequested(device, baudrate);
}

void DevicePanel::onDisconnect()
{
    if (m_simulator)
        m_simulator->stop();

    m_connectBtn->setEnabled(true);
    m_disconnectBtn->setEnabled(false);
    m_statusLabel->setText("● 未连接");
    m_statusLabel->setStyleSheet("color: gray;");

    emit deviceDisconnectRequested();
}

// ============================================================
//  PlaybackPanel — 仅入口
// ============================================================

PlaybackPanel::PlaybackPanel(QWidget *parent)
    : SidePanel("回放", parent)
{
    auto *cl = contentLayout();

    auto *btn = new QPushButton("▶ 回放控制  →  点击打开回放标签页", this);
    btn->setStyleSheet("text-align: left; padding: 8px;");
    cl->addWidget(btn);
    cl->addStretch();

    connect(btn, &QPushButton::clicked, this, &PlaybackPanel::onPlaybackClicked);
}

void PlaybackPanel::onPlaybackClicked()
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

    auto *btn = new QPushButton("● 录制控制  →  点击打开录制标签页", this);
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
    m_list->addItem("界面设置");
    m_list->addItem("快捷键");
    cl->addWidget(m_list);

    connect(m_list, &QListWidget::itemClicked,
            this, &SettingsPanel::onItemClicked);
}

void SettingsPanel::onItemClicked(QListWidgetItem *item)
{
    if (item)
        emit settingsRequested(item->text());
}

// ============================================================
//  SideBar — 8 个面板，索引与 ActivityBar 一致
//  0=Project  1=Trace  2=Graphic  3=DBC
//  4=Playback 5=Record 6=Device   7=Settings
// ============================================================

SideBar::SideBar(QWidget *parent)
    : QStackedWidget(parent)
{
    m_project      = new ProjectPanel(this);
    m_trace        = new TracePanel(this);
    m_graphicConfig = new GraphicConfigPanel(this);
    m_dbc          = new DbcPanel(this);
    m_playback     = new PlaybackPanel(this);
    m_record       = new RecordPanel(this);
    m_device       = new DevicePanel(this);
    m_settings     = new SettingsPanel(this);

    addWidget(m_project);        // 0 = Project
    addWidget(m_trace);          // 1 = Trace
    addWidget(m_graphicConfig);  // 2 = Graphic
    addWidget(m_dbc);            // 3 = Dbc
    addWidget(m_playback);       // 4 = Playback
    addWidget(m_record);         // 5 = Record
    addWidget(m_device);         // 6 = Device
    addWidget(m_settings);       // 7 = Settings

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
