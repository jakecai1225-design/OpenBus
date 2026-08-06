#include "sidebarpanels.h"
#include "utils/svg_icon.h"
#include "core/dbcmanager.h"
#include "core/cansimulator.h"
#include "core/candevicemanager.h"
#include "core/candevice.h"
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
    titleBar->setContentsMargins(0, 0, 0, 0);
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
    recentLabel->setObjectName("SidePanelSubTitle");
    cl->addWidget(recentLabel);
    m_recentList = new QListWidget(this);
    m_recentList->setMaximumHeight(100);
    cl->addWidget(m_recentList);

    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(8, 6, 8, 6);
    btnBar->setSpacing(4);
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
//  DbcPanel — 数据库面板（多协议树形分类）
// ============================================================

DbcPanel::DbcPanel(QWidget *parent)
    : SidePanel("数据库", parent)
{
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(16);         // 树形缩进
    m_tree->setColumnCount(1);
    m_tree->setRootIsDecorated(false);  // 顶层无展开箭头，分类节点自行控制
    m_tree->setExpandsOnDoubleClick(false);

    auto *cl = contentLayout();
    cl->addWidget(m_tree);

    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(8, 6, 8, 6);
    btnBar->setSpacing(4);
    auto *importBtn = new QPushButton("+ 加载数据库文件", this);
    btnBar->addWidget(importBtn);
    btnBar->addStretch();
    cl->addLayout(btnBar);

    initCategoryNodes();

    connect(importBtn, &QPushButton::clicked, this, &DbcPanel::onImportDatabase);
    connect(m_tree, &QTreeWidget::itemClicked,
            this, &DbcPanel::onItemClicked);
}

void DbcPanel::initCategoryNodes()
{
    // 创建协议分类根节点
    m_catCanFd    = new QTreeWidgetItem(m_tree, {"CAN / CANFD"});
    m_catCanopen  = new QTreeWidgetItem(m_tree, {"CANopen"});
    m_catEthercat = new QTreeWidgetItem(m_tree, {"EtherCAT"});
    m_catLin      = new QTreeWidgetItem(m_tree, {"LIN"});
    m_catJ1939    = new QTreeWidgetItem(m_tree, {"J1939"});
    m_catAutosar  = new QTreeWidgetItem(m_tree, {"AUTOSAR"});

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *cat = m_tree->topLevelItem(i);
        cat->setFlags(Qt::ItemIsEnabled);  // 分类节点不可选中，仅可展开
    }
}

QString DbcPanel::categoryForFile(const QString &fileName)
{
    QString ext = QFileInfo(fileName).suffix().toLower();
    if (ext == "dbc")
        return "CAN/CANFD";
    if (ext == "eds" || ext == "dcf" || ext == "xdd")
        return "CANopen";
    if (ext == "xml")
        return "EtherCAT";
    if (ext == "ldf" || ext == "ncf")
        return "LIN";
    if (ext == "dpf")
        return "J1939";
    if (ext == "arxml")
        return "AUTOSAR";
    return {};
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

void DbcPanel::onImportDatabase()
{
    QString path = QFileDialog::getOpenFileName(
        this, "加载数据库文件", {},
        "CAN/CANFD DBC (*.dbc);;"
        "CANopen EDS/DCF/XDD (*.eds *.dcf *.xdd);;"
        "EtherCAT ESI (*.xml);;"
        "LIN LDF/NCF (*.ldf *.ncf);;"
        "J1939 DPF (*.dpf);;"
        "AUTOSAR ARXML (*.arxml);;"
        "所有文件 (*.*)");
    if (path.isEmpty())
        return;

    QString category = categoryForFile(path);
    if (category.isEmpty()) {
        QMessageBox::warning(this, "不支持的格式",
            QString("无法识别文件类型: %1\n支持: DBC / EDS / DCF / XDD / XML / LDF / NCF / DPF / ARXML")
                .arg(QFileInfo(path).fileName()));
        return;
    }

    // DBC 文件交给 DbcManager 解析
    if (category == "CAN/CANFD" && m_dbcMgr) {
        if (!m_dbcMgr->loadDbc(path))
            QMessageBox::warning(this, "加载失败", "无法加载 DBC 文件: " + path);
        return;
    }

    // 其他协议文件加入本地列表
    DatabaseEntry entry;
    entry.fileName = QFileInfo(path).fileName();
    entry.filePath = path;
    entry.category = category;

    // 避免重复加载
    for (const auto &e : m_otherDbs) {
        if (e.filePath == path) {
            QMessageBox::information(this, "已加载", "该文件已在列表中");
            return;
        }
    }

    m_otherDbs.append(entry);
    refreshTree();
}

void DbcPanel::refreshTree()
{
    // 清空分类节点下的子项
    auto clearChildren = [](QTreeWidgetItem *cat) {
        while (cat->childCount() > 0)
            delete cat->takeChild(0);
    };
    clearChildren(m_catCanFd);
    clearChildren(m_catCanopen);
    clearChildren(m_catEthercat);
    clearChildren(m_catLin);
    clearChildren(m_catJ1939);
    clearChildren(m_catAutosar);

    // DBC 文件 → CAN/CANFD 分类
    if (m_dbcMgr) {
        for (const auto &file : m_dbcMgr->files()) {
            auto *item = new QTreeWidgetItem(m_catCanFd, {file.fileName});
            item->setIcon(0, svgIcon(":/icons/file.svg", "#6c6c6c"));
            item->setData(0, Qt::UserRole, "CAN/CANFD");
        }
    }

    // 其他协议文件
    for (const auto &entry : m_otherDbs) {
        QTreeWidgetItem *parent = nullptr;
        if (entry.category == "CAN/CANFD")       parent = m_catCanFd;
        else if (entry.category == "CANopen")    parent = m_catCanopen;
        else if (entry.category == "EtherCAT")   parent = m_catEthercat;
        else if (entry.category == "LIN")        parent = m_catLin;
        else if (entry.category == "J1939")      parent = m_catJ1939;
        else if (entry.category == "AUTOSAR")    parent = m_catAutosar;
        if (!parent) continue;

        auto *item = new QTreeWidgetItem(parent, {entry.fileName});
        item->setIcon(0, svgIcon(":/icons/file.svg", "#6c6c6c"));
        item->setData(0, Qt::UserRole, entry.category);
        item->setData(0, Qt::UserRole + 1, entry.filePath);
    }

    // 展开有内容的分类节点
    auto updateVisibility = [](QTreeWidgetItem *cat) {
        bool hasChildren = cat->childCount() > 0;
        cat->setHidden(!hasChildren);
        if (hasChildren)
            cat->setExpanded(true);
    };
    updateVisibility(m_catCanFd);
    updateVisibility(m_catCanopen);
    updateVisibility(m_catEthercat);
    updateVisibility(m_catLin);
    updateVisibility(m_catJ1939);
    updateVisibility(m_catAutosar);

    // 更新分类节点标题中的计数
    auto setCount = [](QTreeWidgetItem *cat, const QString &label) {
        int n = cat->childCount();
        cat->setText(0, QString("%1 %2").arg(label).arg(n > 0 ? QString("(%1)").arg(n) : ""));
    };
    setCount(m_catCanFd,    "CAN / CANFD");
    setCount(m_catCanopen,  "CANopen");
    setCount(m_catEthercat, "EtherCAT");
    setCount(m_catLin,      "LIN");
    setCount(m_catJ1939,    "J1939");
    setCount(m_catAutosar,  "AUTOSAR");
}

void DbcPanel::onItemClicked(QTreeWidgetItem *item, int)
{
    if (!item || item->flags() == Qt::NoItemFlags)
        return;

    // 分类节点 → 展开/折叠
    if (item->childCount() > 0) {
        item->setExpanded(!item->isExpanded());
        return;
    }

    // 叶子节点 → 发出信号
    QString category = item->data(0, Qt::UserRole).toString();
    if (category.isEmpty())
        return;

    if (category == "CAN/CANFD")
        emit dbcFileClicked(item->text(0));
    else
        emit databaseFileClicked(category, item->text(0));
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
    btnBar->setContentsMargins(8, 6, 8, 6);
    btnBar->setSpacing(4);
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
    btnBar->setContentsMargins(8, 6, 8, 6);
    btnBar->setSpacing(4);
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

    // 扫描设备按钮
    auto *scanBar = new QHBoxLayout;
    scanBar->setContentsMargins(8, 6, 8, 6);
    scanBar->setSpacing(4);
    m_scanBtn = new QPushButton(QStringLiteral("扫描设备"), this);
    scanBar->addWidget(m_scanBtn);
    scanBar->addStretch();
    cl->addLayout(scanBar);

    cl->addStretch();

    connect(m_deviceTree, &QTreeWidget::itemClicked,
            this, &DevicePanel::onItemClicked);
    connect(m_deviceTree, &QTreeWidget::itemDoubleClicked,
            this, &DevicePanel::onItemDoubleClicked);
    connect(m_scanBtn, &QPushButton::clicked, this, &DevicePanel::onScanClicked);

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

void DevicePanel::onScanClicked()
{
    // 重新枚举设备并刷新树
    if (m_scanBtn) {
        m_scanBtn->setEnabled(false);
        m_scanBtn->setText(QStringLiteral("扫描中..."));
    }
    refreshDevices();
    if (m_scanBtn) {
        m_scanBtn->setEnabled(true);
        m_scanBtn->setText(QStringLiteral("扫描设备"));
    }
}

void DevicePanel::populateTree()
{
    m_deviceTree->clear();

    // 模拟器（内置）
    auto *simItem = new QTreeWidgetItem(m_deviceTree);
    simItem->setText(0, QStringLiteral("模拟器 (内置)"));
    simItem->setData(0, Qt::UserRole, 0);       // deviceKind = 0 (Simulator)
    simItem->setData(0, Qt::UserRole + 1, 0);   // devIndex = 0

    // 统一枚举所有品牌的硬件设备
    auto allDevices = ICanDevice::enumerateAll();

    // 按品牌分组的辅助 lambda
    auto devicesOfBrand = [&allDevices](ICanDevice::Brand b) {
        QList<ICanDevice::DeviceInfo> result;
        for (const auto &d : allDevices)
            if (d.brand == b) result << d;
        return result;
    };

    // 添加品牌分组的辅助 lambda
    auto addBrandSection = [&](const QString &title, ICanDevice::Brand brand,
                               CanDeviceManager::DeviceKind kind,
                               const QString &emptyHint) {
        auto *parent = new QTreeWidgetItem(m_deviceTree);
        parent->setText(0, title);
        auto devs = devicesOfBrand(brand);
        if (devs.isEmpty()) {
            auto *empty = new QTreeWidgetItem(parent);
            empty->setText(0, emptyHint);
            empty->setData(0, Qt::UserRole, static_cast<int>(kind));
            empty->setData(0, Qt::UserRole + 1, 0);
        } else {
            for (const auto &d : devs) {
                auto *dev = new QTreeWidgetItem(parent);
                dev->setText(0, QStringLiteral("  ") + d.name);
                dev->setData(0, Qt::UserRole, static_cast<int>(kind));
                dev->setData(0, Qt::UserRole + 1, d.deviceIndex);
            }
        }
        parent->setExpanded(true);
        return parent;
    };

    // ---- 各品牌设备分组 ----
    addBrandSection(QStringLiteral("ZLG 致远电子"), ICanDevice::Brand::ZLG,
                    CanDeviceManager::DeviceKind::ZLG,
                    QStringLiteral("  ZLG USBCANFD (未检测到硬件)"));

    addBrandSection(QStringLiteral("PEAK PCAN"), ICanDevice::Brand::PEAK,
                    CanDeviceManager::DeviceKind::PEAK,
                    QStringLiteral("  PCAN-USB (未检测到硬件)"));

    addBrandSection(QStringLiteral("Kvaser"), ICanDevice::Brand::Kvaser,
                    CanDeviceManager::DeviceKind::Kvaser,
                    QStringLiteral("  Kvaser USBcan (未检测到硬件)"));

    addBrandSection(QStringLiteral("开源 USB-CAN (SLCAN)"), ICanDevice::Brand::SLCAN,
                    CanDeviceManager::DeviceKind::SLCAN,
                    QStringLiteral("  SLCAN (待实现)"));
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

    auto *sendBtn = new QPushButton("发送  →  点击打开发送标签页", this);
    sendBtn->setObjectName("SidePanelButton");
    cl->addWidget(sendBtn);

    auto *playbackBtn = new QPushButton("回放  →  点击打开回放标签页", this);
    playbackBtn->setObjectName("SidePanelButton");
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

    auto *btn = new QPushButton("录制  →  点击打开录制标签页", this);
    btn->setObjectName("SidePanelButton");
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
    themeLabel->setObjectName("SidePanelSubTitle");
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
    : SidePanel("Flow", parent)
{
    auto *cl = contentLayout();

    m_list = new QListWidget(this);
    m_list->addItem(new QListWidgetItem("flow"));
    cl->addWidget(m_list);

    auto *hint = new QLabel("\n"
                           "\xE2\x80\xA2 点击“flow”打开画布\n"
                           "\xE2\x80\xA2 点击模块块可启用/禁用\n"
                           "\xE2\x80\xA2 双击模块块可打开对应标签页", this);
    hint->setWordWrap(true);
    hint->setObjectName("SidePanelHint");
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
    auto *convItem = new QListWidgetItem("BLF ↔ ASC ↔ CSV 转换", m_list);
    convItem->setData(Qt::UserRole, "blf_converter");
    convItem->setToolTip("报文日志文件格式互转：BLF / ASC / CSV 之间转换");

    // DBC 工具类（合并：查看编辑 + 信号清单导出）
    auto *dbcItem = new QListWidgetItem("DBC 工具", m_list);
    dbcItem->setData(Qt::UserRole, "dbc_tool");
    dbcItem->setToolTip("DBC 查看/编辑 + 信号清单导出");

    // 总线统计分析类（合并：报文统计 + ID 频率/周期 + 总线负载率）
    auto *statItem = new QListWidgetItem("总线统计分析", m_list);
    statItem->setData(Qt::UserRole, "bus_analysis");
    statItem->setToolTip("报文统计 / ID 频率周期 / 总线负载率");

    cl->addWidget(m_list);

    auto *hint = new QLabel("\n"
                           "\xE2\x80\xA2 点击工具名打开对应标签页\n"
                           "\xE2\x80\xA2 工具独立运行，不影响当前工程\n"
                           "\xE2\x80\xA2 后续将持续集成更多总线分析工具", this);
    hint->setWordWrap(true);
    hint->setObjectName("SidePanelHint");
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
    setMinimumWidth(240);
    setMaximumWidth(500);
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
    auto *udsItem = new QListWidgetItem("UDS 诊断 (ISO 14229)", m_list);
    udsItem->setData(Qt::UserRole, "UDS");
    udsItem->setToolTip("Unified Diagnostic Services — ECU 诊断服务交互");

    auto *canopenItem = new QListWidgetItem("CANopen (CiA 301)", m_list);
    canopenItem->setData(Qt::UserRole, "CANopen");
    canopenItem->setToolTip("CANopen 协议 — NMT/SDO/PDO/Emergency/Heartbeat");

    // 未实现的协议（灰色显示）
    auto *j1939Item = new QListWidgetItem("J1939", m_list);
    j1939Item->setData(Qt::UserRole, "J1939");
    j1939Item->setToolTip("SAE J1939 — 商用车/工程机械协议（敬请期待）");

    auto *isotpItem = new QListWidgetItem("ISO-TP (ISO 15765-2)", m_list);
    isotpItem->setData(Qt::UserRole, "ISO-TP");
    isotpItem->setToolTip("CAN 传输层协议 — 多帧拆包/组包（敬请期待）");

    auto *obdItem = new QListWidgetItem("OBD-II", m_list);
    obdItem->setData(Qt::UserRole, "OBD-II");
    obdItem->setToolTip("车载诊断 — 故障码读取/排放监测（敬请期待）");

    auto *xcpItem = new QListWidgetItem("XCP (CCP/Universal)", m_list);
    xcpItem->setData(Qt::UserRole, "XCP");
    xcpItem->setToolTip("通用标定测量协议 — ECU 标定/数据采集（敬请期待）");

    auto *nmeaItem = new QListWidgetItem("NMEA 2000", m_list);
    nmeaItem->setData(Qt::UserRole, "NMEA2000");
    nmeaItem->setToolTip("船舶电子设备互联协议（敬请期待）");

    // 灰色标记未实现
    for (int i = 2; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        item->setForeground(QColor(0x6c, 0x6c, 0x6c));
        QFont f = item->font();
        f.setItalic(true);
        item->setFont(f);
    }

    layout->addWidget(m_list);

    // 信息提示
    auto *infoLabel = new QLabel("点击协议名称打开对应标签页\n灰色项暂未实现", this);
    infoLabel->setObjectName("SidePanelInfo");
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
