#include "sidebarpanels.h"
#include "utils/svg_icon.h"
#include "core/dbcmanager.h"
#include "core/cansimulator.h"
#include "core/candevicemanager.h"
#include "core/candevice.h"
#include "core/driver/driverregistry.h"
#include "core/appconfig.h"
#include "core/sessionmanager.h"
#include "ui/graphicview.h"
#include "ui/thememanager.h"
#include "ui/plugindetailpage.h"

#include <nlohmann/json.hpp>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFont>
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
#include <QMenu>
#include <QAction>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QLinearGradient>
#include <QFile>
#include <QTextStream>
#include <QLineEdit>
#include <QToolButton>

// ============================================================
//  SidePanel 基类
// ============================================================

SidePanel::SidePanel(const QString &title, QWidget *parent)
    : QWidget(parent), m_contentLayout(nullptr)
{
    setAttribute(Qt::WA_StyledBackground, true);
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

    m_projectTree = new QTreeWidget(this);
    m_projectTree->setObjectName("ProjectTree");
    m_projectTree->setHeaderHidden(true);
    m_projectTree->setIndentation(16);
    m_projectTree->setColumnCount(1);
    m_projectTree->setRootIsDecorated(true);
    m_projectTree->setExpandsOnDoubleClick(false);  // 双击不折叠，用于切换工程
    cl->addWidget(m_projectTree, 1);

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
    connect(m_projectTree, &QTreeWidget::itemClicked,
            this, &ProjectPanel::onProjectItemClicked);
    connect(m_projectTree, &QTreeWidget::itemDoubleClicked,
            this, &ProjectPanel::onProjectItemDoubleClicked);
    connect(m_recentList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *item) {
        QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty())
            emit openProjectRequested(path);
    });
}

void ProjectPanel::refreshList()
{
    m_projectTree->blockSignals(true);
    m_projectTree->clear();

    for (int i = 0; i < m_projects.size(); ++i) {
        const auto &proj = m_projects[i];
        QString label = proj.name;
        if (i == m_currentIndex)
            label += "  (当前)";

        auto *projItem = new QTreeWidgetItem(m_projectTree, {label});
        projItem->setData(0, Qt::UserRole, i);  // 存储工程索引
        projItem->setExpanded(false);  // 默认折叠，点击箭头展开

        // ---- 子节点：工程文件 ----
        if (!proj.filePath.isEmpty()) {
            auto *fItem = new QTreeWidgetItem(projItem,
                {QStringLiteral("[工程] ") + QFileInfo(proj.filePath).fileName()});
            fItem->setData(0, Qt::UserRole, proj.filePath);
            fItem->setToolTip(0, proj.filePath);
        }

        // ---- 子节点：回放文件 ----
        QString playback = extractPlaybackFile(proj.stateJson);
        if (!playback.isEmpty()) {
            auto *fItem = new QTreeWidgetItem(projItem,
                {QStringLiteral("[回放] ") + QFileInfo(playback).fileName()});
            fItem->setData(0, Qt::UserRole, playback);
            fItem->setToolTip(0, playback);
        }

        // ---- 子节点：DBC 文件 ----
        auto dbcFiles = extractDbcFiles(proj.stateJson);
        if (!dbcFiles.isEmpty()) {
            auto *catItem = new QTreeWidgetItem(projItem,
                {QStringLiteral("DBC 文件 (") + QString::number(dbcFiles.size()) + ")"});
            for (const auto &f : dbcFiles) {
                auto *fItem = new QTreeWidgetItem(catItem, {QFileInfo(f).fileName()});
                fItem->setData(0, Qt::UserRole, f);
                fItem->setToolTip(0, f);
            }
        }

        // ---- 子节点：录制文件 ----
        auto recFiles = extractRecordFiles(proj.stateJson);
        // 合并 ProjectContext 中直接存储的录制文件
        for (const auto &f : proj.recordFiles) {
            if (!recFiles.contains(f))
                recFiles << f;
        }
        if (!recFiles.isEmpty()) {
            auto *catItem = new QTreeWidgetItem(projItem,
                {QStringLiteral("录制文件 (") + QString::number(recFiles.size()) + ")"});
            for (const auto &f : recFiles) {
                auto *fItem = new QTreeWidgetItem(catItem, {QFileInfo(f).fileName()});
                fItem->setData(0, Qt::UserRole, f);
                fItem->setToolTip(0, f);
            }
        }

        // 无文件时的提示
        if (proj.filePath.isEmpty() && playback.isEmpty() &&
            dbcFiles.isEmpty() && recFiles.isEmpty()) {
            auto *empty = new QTreeWidgetItem(projItem,
                {QStringLiteral("(无关联文件)")});
            empty->setFlags(Qt::NoItemFlags);
        }
    }

    // 选中当前工程
    if (m_currentIndex >= 0 && m_currentIndex < m_projectTree->topLevelItemCount())
        m_projectTree->setCurrentItem(m_projectTree->topLevelItem(m_currentIndex));
    m_projectTree->blockSignals(false);
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
    // 不再 emit projectSwitched — onProjectCreated 已处理全部逻辑
    // 避免 newProject 被重复调用导致工程状态反复重置
}

void ProjectPanel::onSaveProject()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_projects.size()) return;

    QString path = m_projects[m_currentIndex].filePath;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, "保存工程", m_projects[m_currentIndex].name + ".openbusproj",
            "openbus 工程文件 (*.openbusproj);;所有文件 (*.*)");
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
        "openbus 工程文件 (*.openbusproj);;所有文件 (*.*)");
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

    // 从 SessionManager 读取最近工程列表（含元数据）
    auto items = SessionManager::instance()->recentItems();
    for (const auto &var : items) {
        auto map = var.toMap();
        QString p = map.value("path").toString();
        if (p.isEmpty()) continue;
        QString name = map.value("name").toString();
        if (name.isEmpty())
            name = QFileInfo(p).fileName();
        if (map.value("pinned").toBool())
            name = QStringLiteral("[置顶] ") + name;

        auto *listItem = new QListWidgetItem(name);
        listItem->setToolTip(p);
        listItem->setData(Qt::UserRole, p);
        m_recentList->addItem(listItem);
    }
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

void ProjectPanel::onProjectItemClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)
    if (!item) return;
    // 只处理顶层工程节点
    if (item->parent()) return;
    int idx = item->data(0, Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_projects.size()) return;
    // 单击仅更新选中索引，不触发切换
    m_currentIndex = idx;
}

void ProjectPanel::onProjectItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)
    if (!item) return;

    // 子节点双击 — 打开文件预览标签页
    if (item->parent()) {
        QString filePath = item->data(0, Qt::UserRole).toString();
        if (!filePath.isEmpty())
            emit filePreviewRequested(filePath);
        return;  // 分类节点无 UserRole，不做操作
    }

    // 顶层工程节点双击 — 切换工程
    int idx = item->data(0, Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_projects.size()) return;
    m_currentIndex = idx;
    emit projectSwitched(m_currentIndex);
}

// ============================================================
//  ProjectPanel — 工程文件信息解析辅助
// ============================================================

QStringList ProjectPanel::extractDbcFiles(const QString &stateJson) const
{
    QStringList result;
    if (stateJson.isEmpty()) return result;
    try {
        auto j = nlohmann::json::parse(stateJson.toStdString());
        // v2 格式: resources.dbc
        if (j.contains("resources") && j["resources"].contains("dbc") &&
            j["resources"]["dbc"].is_array()) {
            for (const auto &f : j["resources"]["dbc"])
                if (f.is_string())
                    result << QString::fromStdString(f.get<std::string>());
        }
        // v1 格式: dbc.files
        if (result.isEmpty() && j.contains("dbc") && j["dbc"].contains("files") &&
            j["dbc"]["files"].is_array()) {
            for (const auto &f : j["dbc"]["files"])
                if (f.is_string())
                    result << QString::fromStdString(f.get<std::string>());
        }
    } catch (...) {}
    return result;
}

QStringList ProjectPanel::extractRecordFiles(const QString &stateJson) const
{
    QStringList result;
    if (stateJson.isEmpty()) return result;
    try {
        auto j = nlohmann::json::parse(stateJson.toStdString());
        // v2 格式: resources.logs
        if (j.contains("resources") && j["resources"].contains("logs") &&
            j["resources"]["logs"].is_array()) {
            for (const auto &f : j["resources"]["logs"])
                if (f.is_string())
                    result << QString::fromStdString(f.get<std::string>());
        }
        // v1 格式: record.files
        if (result.isEmpty() && j.contains("record") && j["record"].contains("files") &&
            j["record"]["files"].is_array()) {
            for (const auto &f : j["record"]["files"])
                if (f.is_string())
                    result << QString::fromStdString(f.get<std::string>());
        }
    } catch (...) {}
    return result;
}

QString ProjectPanel::extractPlaybackFile(const QString &stateJson) const
{
    if (stateJson.isEmpty()) return {};
    try {
        auto j = nlohmann::json::parse(stateJson.toStdString());
        // v2 格式: source.filePath
        if (j.contains("source") && j["source"].contains("filePath") &&
            j["source"]["filePath"].is_string())
            return QString::fromStdString(j["source"]["filePath"].get<std::string>());
        // v1 格式: filePath
        if (j.contains("filePath") && j["filePath"].is_string())
            return QString::fromStdString(j["filePath"].get<std::string>());
    } catch (...) {}
    return {};
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
    auto *removeBtn = new QPushButton("- 删除", this);
    btnBar->addWidget(importBtn);
    btnBar->addWidget(removeBtn);
    btnBar->addStretch();
    cl->addLayout(btnBar);

    initCategoryNodes();

    connect(importBtn, &QPushButton::clicked, this, &DbcPanel::onImportDatabase);
    connect(removeBtn, &QPushButton::clicked, this, &DbcPanel::onRemoveDatabase);
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

void DbcPanel::onRemoveDatabase()
{
    // 获取当前选中的叶子节点
    auto *item = m_tree->currentItem();
    if (!item || item->childCount() > 0) {
        QMessageBox::information(this, "删除", "请先选择一个数据库文件");
        return;
    }

    QString category = item->data(0, Qt::UserRole).toString();
    QString filePath = item->data(0, Qt::UserRole + 1).toString();
    QString fileName = item->text(0);

    if (filePath.isEmpty()) {
        QMessageBox::information(this, "删除", "无法获取文件路径");
        return;
    }

    auto reply = QMessageBox::question(this, "删除数据库文件",
        QString("确定删除 \"%1\"?").arg(fileName));
    if (reply != QMessageBox::Yes)
        return;

    if (category == "CAN/CANFD" && m_dbcMgr) {
        // DBC 文件 → 通过 DbcManager 卸载，同时通知 MainWindow 清理关联标签页
        emit dbcRemoveRequested(filePath);
        m_dbcMgr->unloadDbc(filePath);
    } else {
        // 其他协议文件 → 从本地列表移除
        for (int i = 0; i < m_otherDbs.size(); ++i) {
            if (m_otherDbs[i].filePath == filePath) {
                m_otherDbs.removeAt(i);
                break;
            }
        }
        refreshTree();
    }
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
            item->setData(0, Qt::UserRole + 1, file.filePath);  // 存储完整路径用于删除
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
    m_traceList->setContextMenuPolicy(Qt::CustomContextMenu);
    cl->addWidget(m_traceList, 1);

    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(8, 6, 8, 6);
    btnBar->setSpacing(4);
    auto *newBtn = new QPushButton("+ 新建 Trace", this);
    m_delBtn = new QPushButton("− 删除", this);
    btnBar->addWidget(newBtn);
    btnBar->addWidget(m_delBtn);
    btnBar->addStretch();
    cl->addLayout(btnBar);

    connect(newBtn, &QPushButton::clicked, this, &TracePanel::onTraceClicked);
    connect(m_delBtn, &QPushButton::clicked, this, &TracePanel::onDeleteTrace);
    connect(m_traceList, &QListWidget::currentRowChanged,
            this, &TracePanel::onPageSelected);
    connect(m_traceList, &QWidget::customContextMenuRequested,
            this, &TracePanel::onContextMenu);
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

void TracePanel::onDeleteTrace()
{
    int row = m_traceList->currentRow();
    if (row >= 0)
        emit traceDeleteRequested(row);
}

void TracePanel::onContextMenu(const QPoint &pos)
{
    auto *item = m_traceList->itemAt(pos);
    if (!item) return;
    int row = m_traceList->row(item);

    QMenu menu(this);
    auto *actJump = menu.addAction(QStringLiteral("跳转到此标签页"));
    auto *actDel = menu.addAction(QStringLiteral("删除此 Trace"));
    QAction *chosen = menu.exec(m_traceList->viewport()->mapToGlobal(pos));
    if (chosen == actJump) {
        emit tracePageSelected(row);
    } else if (chosen == actDel) {
        emit traceDeleteRequested(row);
    }
}

// ============================================================
//  GraphicConfigPanel — Graphic 页面列表
// ============================================================

GraphicConfigPanel::GraphicConfigPanel(QWidget *parent)
    : SidePanel("Graphic 页面列表", parent)
{
    auto *cl = contentLayout();

    m_pageList = new QListWidget(this);
    m_pageList->setContextMenuPolicy(Qt::CustomContextMenu);
    cl->addWidget(m_pageList, 1);

    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(8, 6, 8, 6);
    btnBar->setSpacing(4);
    auto *newBtn = new QPushButton("+ 新建 Graphic", this);
    m_delBtn = new QPushButton("− 删除", this);
    btnBar->addWidget(newBtn);
    btnBar->addWidget(m_delBtn);
    btnBar->addStretch();
    cl->addLayout(btnBar);

    connect(newBtn, &QPushButton::clicked, this, &GraphicConfigPanel::onNewGraphic);
    connect(m_delBtn, &QPushButton::clicked, this, &GraphicConfigPanel::onDeleteGraphic);
    connect(m_pageList, &QListWidget::currentRowChanged,
            this, &GraphicConfigPanel::onPageSelected);
    connect(m_pageList, &QWidget::customContextMenuRequested,
            this, &GraphicConfigPanel::onContextMenu);
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

void GraphicConfigPanel::onDeleteGraphic()
{
    int row = m_pageList->currentRow();
    if (row >= 0)
        emit graphicDeleteRequested(row);
}

void GraphicConfigPanel::onContextMenu(const QPoint &pos)
{
    auto *item = m_pageList->itemAt(pos);
    if (!item) return;
    int row = m_pageList->row(item);

    QMenu menu(this);
    auto *actJump = menu.addAction(QStringLiteral("跳转到此标签页"));
    auto *actDel = menu.addAction(QStringLiteral("删除此 Graphic"));
    QAction *chosen = menu.exec(m_pageList->viewport()->mapToGlobal(pos));
    if (chosen == actJump) {
        emit graphicPageSelected(row);
    } else if (chosen == actDel) {
        emit graphicDeleteRequested(row);
    }
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

    // 外置驱动安装/卸载后设备树即时刷新（热加载）
    connect(DriverRegistry::instance(), &DriverRegistry::driversChanged,
            this, &DevicePanel::refreshDevices);

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

    // 统一枚举（DriverRegistry 聚合内置 + 外置，DeviceInfo.driverId 分组）
    const auto allDevices = ICanDevice::enumerateAll();

    // ---- 驱动分区（Registry 动态生成，不再硬编码品牌；禁用的隐藏 §7.4） ----
    const auto drivers = DriverRegistry::instance()->drivers();
    for (const auto &drv : drivers) {
        if (!drv.enabled)
            continue;
        auto *parent = new QTreeWidgetItem(m_deviceTree);
        parent->setText(0, drv.displayName);

        // 该驱动的在线设备
        QList<ICanDevice::DeviceInfo> devs;
        for (const auto &d : allDevices) {
            if (d.driverId == drv.driverId)
                devs << d;
        }

        if (!devs.isEmpty()) {
            for (const auto &d : devs) {
                auto *dev = new QTreeWidgetItem(parent);
                dev->setText(0, QStringLiteral("  ") + d.name);
                dev->setData(0, Qt::UserRole, drv.deviceKind);
                dev->setData(0, Qt::UserRole + 1, d.deviceIndex);
                dev->setData(0, Qt::UserRole + 2, d.deviceType);
            }
        } else if (drv.available) {
            // 驱动可用但无在线设备：提示叶子（点击打开连接页手动配置）
            auto *empty = new QTreeWidgetItem(parent);
            empty->setText(0, QStringLiteral("  %1 (未检测到硬件)")
                                   .arg(drv.displayName));
            empty->setData(0, Qt::UserRole, drv.deviceKind);
            empty->setData(0, Qt::UserRole + 1, 0);
        } else {
            // 驱动不可用（厂商 DLL 缺失/预检失败）：展示原因，禁用点击
            auto *empty = new QTreeWidgetItem(parent);
            empty->setText(0, QStringLiteral("  (%1)").arg(
                drv.disabledReason.isEmpty() ? QStringLiteral("不可用")
                                             : drv.disabledReason));
            empty->setFlags(empty->flags() & ~Qt::ItemIsEnabled);
        }
        parent->setExpanded(true);
    }

    // ---- 「＋ 新增设备」折叠栏底部固定入口 → 设备市场标签页 ----
    auto *addItem = new QTreeWidgetItem(m_deviceTree);
    addItem->setText(0, QStringLiteral("＋ 新增设备"));
    QFont addFont = addItem->font(0);
    addFont.setBold(true);
    addItem->setFont(0, addFont);
    addItem->setData(0, Qt::UserRole + 3, QStringLiteral("__add__"));
}

void DevicePanel::onItemClicked(QTreeWidgetItem *item, int /*column*/)
{
    // 「＋ 新增设备」入口 → 设备市场标签页
    if (item->data(0, Qt::UserRole + 3).toString() == QLatin1String("__add__")) {
        emit addDeviceRequested();
        return;
    }

    // 父节点 → 展开/折叠
    if (item->childCount() > 0) {
        item->setExpanded(!item->isExpanded());
        return;
    }
    // 叶子节点 → 发出打开请求
    int deviceKind = item->data(0, Qt::UserRole).toInt();
    int devIndex = item->data(0, Qt::UserRole + 1).toInt();
    int deviceType = item->data(0, Qt::UserRole + 2).toInt();
    emit deviceOpenRequested(deviceKind, devIndex, item->text(0).trimmed(), deviceType);
}

void DevicePanel::onItemDoubleClicked(QTreeWidgetItem *item, int /*column*/)
{
    if (item->data(0, Qt::UserRole + 3).toString() == QLatin1String("__add__")) {
        emit addDeviceRequested();
        return;
    }
    if (item->childCount() > 0)
        return;
    int deviceKind = item->data(0, Qt::UserRole).toInt();
    int devIndex = item->data(0, Qt::UserRole + 1).toInt();
    int deviceType = item->data(0, Qt::UserRole + 2).toInt();
    emit deviceOpenRequested(deviceKind, devIndex, item->text(0).trimmed(), deviceType);
}

// ============================================================
//  TransceivePanel — 收发面板（发送 / 回放 / 离线分析 / 录制）
// ============================================================

TransceivePanel::TransceivePanel(QWidget *parent)
    : SidePanel("收发", parent)
{
    auto *cl = contentLayout();

    auto *sendBtn = new QPushButton("发送  →  点击打开发送标签页", this);
    sendBtn->setObjectName("SidePanelButton");
    cl->addWidget(sendBtn);

    auto *playbackBtn = new QPushButton("回放  →  点击打开回放标签页", this);
    playbackBtn->setObjectName("SidePanelButton");
    cl->addWidget(playbackBtn);

    auto *offlineBtn = new QPushButton("离线分析  →  点击打开离线分析标签页", this);
    offlineBtn->setObjectName("SidePanelButton");
    cl->addWidget(offlineBtn);

    auto *recordBtn = new QPushButton("录制  →  点击打开录制标签页", this);
    recordBtn->setObjectName("SidePanelButton");
    cl->addWidget(recordBtn);

    cl->addStretch();

    connect(sendBtn, &QPushButton::clicked, this, &TransceivePanel::onSendClicked);
    connect(playbackBtn, &QPushButton::clicked, this, &TransceivePanel::onPlaybackClicked);
    connect(offlineBtn, &QPushButton::clicked, this, &TransceivePanel::onOfflineAnalysisClicked);
    connect(recordBtn, &QPushButton::clicked, this, &TransceivePanel::onRecordClicked);
}

void TransceivePanel::onSendClicked()
{
    emit openSendRequested();
}

void TransceivePanel::onPlaybackClicked()
{
    emit openPlaybackRequested();
}

void TransceivePanel::onOfflineAnalysisClicked()
{
    emit openOfflineAnalysisRequested();
}

void TransceivePanel::onRecordClicked()
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
//  ExtensionsPanel — 插件管理面板（VS Code 扩展视图）
// ============================================================

ExtensionsPanel::ExtensionsPanel(QWidget *parent)
    : SidePanel("扩展", parent)
{
    auto *cl = contentLayout();

    // 搜索栏 + 右上角 "…" 菜单（VS Code 布局）
    auto *searchRow = new QHBoxLayout;
    searchRow->setContentsMargins(8, 8, 4, 8);
    searchRow->setSpacing(4);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName("ExtensionSearch");
    m_searchEdit->setPlaceholderText("在扩展中搜索...");
    m_searchEdit->setClearButtonEnabled(true);
    searchRow->addWidget(m_searchEdit, 1);

    m_menuBtn = new QToolButton(this);
    m_menuBtn->setIcon(svgIcon(":/icons/kebab.svg",
                               ThemeManager::instance()->currentTheme().text, 16));
    m_menuBtn->setToolTip("视图和更多操作");
    m_menuBtn->setAutoRaise(true);
    connect(m_menuBtn, &QToolButton::clicked,
            this, &ExtensionsPanel::onMenuClicked);
    searchRow->addWidget(m_menuBtn);
    cl->addLayout(searchRow);

    // 主题切换 → 重刷菜单/行内图标颜色
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this]() {
        const QString c = ThemeManager::instance()->currentTheme().text;
        m_menuBtn->setIcon(svgIcon(":/icons/kebab.svg", c, 16));
        if (!m_lastEntries.isEmpty())
            refreshInstalledPlugins(m_lastEntries);
    });

    // 插件列表树
    m_tree = new QTreeWidget(this);
    m_tree->setObjectName("ExtensionTree");
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(0);             // VS Code：行铺满整宽
    m_tree->setColumnCount(1);
    m_tree->setRootIsDecorated(false);
    m_tree->setExpandsOnDoubleClick(false);
    cl->addWidget(m_tree, 1);

    // 分区标题字体
    QFont headerFont = font();
    headerFont.setBold(true);
    QFont placeholderFont = font();
    placeholderFont.setItalic(true);

    // 已启用
    m_installedHeader = new QTreeWidgetItem;
    m_installedHeader->setText(0, "已启用");
    m_installedHeader->setFont(0, headerFont);
    m_installedHeader->setFlags(Qt::ItemIsEnabled);
    m_tree->addTopLevelItem(m_installedHeader);
    m_installedHeader->setExpanded(true);

    // 已禁用
    m_disabledHeader = new QTreeWidgetItem;
    m_disabledHeader->setText(0, "已禁用");
    m_disabledHeader->setFont(0, headerFont);
    m_disabledHeader->setFlags(Qt::ItemIsEnabled);
    m_tree->addTopLevelItem(m_disabledHeader);
    m_disabledHeader->setExpanded(true);

    // 命令
    m_commandsHeader = new QTreeWidgetItem;
    m_commandsHeader->setText(0, "命令");
    m_commandsHeader->setFont(0, headerFont);
    m_commandsHeader->setFlags(Qt::ItemIsEnabled);
    m_tree->addTopLevelItem(m_commandsHeader);
    m_commandsHeader->setExpanded(true);

    auto *cmdHint = new QTreeWidgetItem(m_commandsHeader);
    cmdHint->setText(0, "暂无插件命令");
    cmdHint->setFont(0, placeholderFont);
    cmdHint->setFlags(Qt::ItemIsEnabled);

    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &ExtensionsPanel::onSearchChanged);
    connect(m_tree, &QTreeWidget::itemClicked,
            this, &ExtensionsPanel::onItemClicked);
}

void ExtensionsPanel::refreshInstalledPlugins(const QList<ExtensionEntry> &entries)
{
    m_lastEntries = entries;

    // 清空已启用/已禁用两个分区
    for (auto *header : {m_installedHeader, m_disabledHeader}) {
        while (header->childCount() > 0)
            delete header->takeChild(0);
    }

    for (const auto &entry : entries) {
        const bool disabledSection = !entry.enabled;
        auto *header = disabledSection ? m_disabledHeader : m_installedHeader;
        auto *item = new QTreeWidgetItem(header);
        item->setData(0, Qt::UserRole, entry.name);
        item->setData(0, Qt::UserRole + 1, entry.name + " " + entry.description);
        item->setSizeHint(0, QSize(0, 56));
        m_tree->setItemWidget(item, 0, createPluginWidget(entry, disabledSection));
    }

    // 空分区隐藏（VS Code 行为）
    m_installedHeader->setHidden(m_installedHeader->childCount() == 0);
    m_disabledHeader->setHidden(m_disabledHeader->childCount() == 0);
    m_installedHeader->setExpanded(true);
    m_disabledHeader->setExpanded(true);
}

QWidget *ExtensionsPanel::createPluginWidget(const ExtensionEntry &entry, bool disabledSection)
{
    auto *widget = new QWidget;
    widget->setObjectName("ExtensionItem");
    auto *layout = new QHBoxLayout(widget);
    layout->setContentsMargins(8, 6, 4, 6);
    layout->setSpacing(8);

    // 图标（VS Code 行首图标）
    auto *iconLabel = new QLabel(widget);
    iconLabel->setPixmap(PluginUi::pluginIconPixmap(entry.iconPath, entry.name, 32));
    iconLabel->setFixedSize(32, 32);
    layout->addWidget(iconLabel);

    // 名称 + 版本 / 描述 / 状态
    auto *col = new QVBoxLayout;
    col->setSpacing(1);

    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(6);
    auto *nameLabel = new QLabel(entry.name, widget);
    QFont nameFont = nameLabel->font();
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);
    topRow->addWidget(nameLabel);
    if (!entry.version.isEmpty()) {
        auto *verLabel = new QLabel(entry.version, widget);
        verLabel->setStyleSheet("color: #888; font-size: 11px;");
        topRow->addWidget(verLabel);
    }
    topRow->addStretch();
    col->addLayout(topRow);

    if (!entry.description.isEmpty()) {
        auto *descLabel = new QLabel(entry.description, widget);
        descLabel->setStyleSheet("color: #aaa; font-size: 11px;");
        descLabel->setWordWrap(false);
        col->addWidget(descLabel);
    }

    if (entry.activated) {
        auto *runningLabel = new QLabel(QString::fromUtf8("● 运行中"), widget);
        runningLabel->setStyleSheet("color: #4ec9b0; font-size: 11px;");
        col->addWidget(runningLabel);
    } else if (disabledSection) {
        auto *disabledLabel = new QLabel(QString::fromUtf8("● 已禁用"), widget);
        disabledLabel->setStyleSheet("color: #888; font-size: 11px;");
        col->addWidget(disabledLabel);
    }

    layout->addLayout(col, 1);

    // 齿轮菜单按钮（启动/停止/启用/禁用/卸载）
    auto *gear = new QToolButton(widget);
    gear->setIcon(svgIcon(":/icons/gear.svg",
                          ThemeManager::instance()->currentTheme().text, 16));
    gear->setToolTip("更多操作");
    gear->setAutoRaise(true);
    const QString name = entry.name;
    const bool enabled = entry.enabled;
    const bool activated = entry.activated;
    connect(gear, &QToolButton::clicked, this,
            [this, gear, name, enabled, activated]() {
        showGearMenu(name, enabled, activated,
                     gear->mapToGlobal(QPoint(0, gear->height())));
    });
    layout->addWidget(gear, 0, Qt::AlignTop);

    // 单击/双击捕获：itemWidget 会吞掉树控件的鼠标事件，
    // 行 widget 安装事件过滤器，子标签对鼠标事件透明（齿轮按钮除外）
    widget->setProperty("pluginName", entry.name);
    widget->installEventFilter(this);
    const auto labels = widget->findChildren<QLabel *>();
    for (auto *lbl : labels)
        lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    return widget;
}

void ExtensionsPanel::addCommand(const QString &id, const QString &title)
{
    // 移除“暂无插件命令”占位项
    for (int i = 0; i < m_commandsHeader->childCount(); ++i) {
        auto *child = m_commandsHeader->child(i);
        if (child->text(0) == QString::fromUtf8("\u6682\u65E0\u63D2\u4EF6\u547D\u4EE4")) {
            m_commandsHeader->removeChild(child);
            delete child;
            break;
        }
    }

    // 避免重复
    for (int i = 0; i < m_commandsHeader->childCount(); ++i) {
        if (m_commandsHeader->child(i)->data(0, Qt::UserRole).toString() == id)
            return;
    }

    auto *item = new QTreeWidgetItem(m_commandsHeader);
    item->setText(0, title);
    item->setData(0, Qt::UserRole, id);
    m_commandsHeader->setExpanded(true);
}

void ExtensionsPanel::clearCommands()
{
    while (m_commandsHeader->childCount() > 0) {
        auto *child = m_commandsHeader->child(0);
        m_commandsHeader->removeChild(child);
        delete child;
    }

    auto *cmdHint = new QTreeWidgetItem(m_commandsHeader);
    cmdHint->setText(0, QString::fromUtf8("\u6682\u65E0\u63D2\u4EF6\u547D\u4EE4"));
    QFont italicFont = font();
    italicFont.setItalic(true);
    cmdHint->setFont(0, italicFont);
    cmdHint->setFlags(Qt::ItemIsEnabled);
}

void ExtensionsPanel::onSearchChanged(const QString &text)
{
    filterPlugins(text);
}

void ExtensionsPanel::onItemClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (!item) return;

    // 分区标题：点击折叠/展开（VS Code 行为）
    if (item == m_installedHeader || item == m_disabledHeader ||
        item == m_commandsHeader) {
        item->setExpanded(!item->isExpanded());
        return;
    }

    // 只处理命令分区的子项
    QTreeWidgetItem *parent = item->parent();
    if (parent != m_commandsHeader) return;

    QString cmdId = item->data(0, Qt::UserRole).toString();
    if (!cmdId.isEmpty())
        emit commandTriggered(cmdId);
}

bool ExtensionsPanel::eventFilter(QObject *obj, QEvent *ev)
{
    auto *w = qobject_cast<QWidget *>(obj);
    if (w) {
        const QString name = w->property("pluginName").toString();
        if (!name.isEmpty()) {
            if (ev->type() == QEvent::MouseButtonRelease) {
                emit pluginSelected(name);   // 单击 → 打开详情页
                return true;
            }
            if (ev->type() == QEvent::MouseButtonDblClick) {
                emit pluginActivated(name);  // 双击 → 启动（重启）
                return true;
            }
        }
    }
    return SidePanel::eventFilter(obj, ev);
}

void ExtensionsPanel::onMenuClicked()
{
    QMenu menu(this);
    auto *installAct = menu.addAction(QString::fromUtf8("从 .opk 离线安装..."));
    connect(installAct, &QAction::triggered, this, [this]() {
        emit installOpkRequested();
    });
    auto *refreshAct = menu.addAction(QString::fromUtf8("刷新"));
    connect(refreshAct, &QAction::triggered, this, [this]() {
        emit refreshPluginsRequested();
    });
    menu.exec(m_menuBtn->mapToGlobal(QPoint(0, m_menuBtn->height())));
}

void ExtensionsPanel::showGearMenu(const QString &name, bool enabled, bool activated,
                                   const QPoint &globalPos)
{
    QMenu menu(this);
    if (!enabled) {
        auto *enableAct = menu.addAction(QString::fromUtf8("启用"));
        connect(enableAct, &QAction::triggered, this, [this, name]() {
            emit pluginToggleRequested(name, true);
        });
    } else {
        if (activated) {
            auto *stopAct = menu.addAction(QString::fromUtf8("停止"));
            connect(stopAct, &QAction::triggered, this, [this, name]() {
                emit pluginDeactivateRequested(name);
            });
        }
        auto *startAct = menu.addAction(activated ? QString::fromUtf8("重启")
                                                  : QString::fromUtf8("启动"));
        connect(startAct, &QAction::triggered, this, [this, name]() {
            emit pluginActivated(name);
        });
        auto *disableAct = menu.addAction(QString::fromUtf8("禁用"));
        connect(disableAct, &QAction::triggered, this, [this, name]() {
            emit pluginToggleRequested(name, false);
        });
    }
    menu.addSeparator();
    auto *uninstallAct = menu.addAction(QString::fromUtf8("卸载"));
    connect(uninstallAct, &QAction::triggered, this, [this, name]() {
        emit pluginUninstallRequested(name);
    });
    menu.exec(globalPos);
}

void ExtensionsPanel::filterPlugins(const QString &text)
{
    for (auto *header : {m_installedHeader, m_disabledHeader}) {
        int visible = 0;
        for (int i = 0; i < header->childCount(); ++i) {
            auto *child = header->child(i);
            bool show = true;
            if (!text.isEmpty()) {
                QString haystack = child->data(0, Qt::UserRole + 1).toString().toLower();
                show = haystack.contains(text.toLower());
            }
            child->setHidden(!show);
            if (show) ++visible;
        }
        // 无可见子项的分区隐藏
        header->setHidden(visible == 0);
    }
}

// ============================================================
//  SideBar — 10 个面板，索引与 ActivityBar 一致
//  0=Project  1=Analysis(Flow)  2=Device  3=Trace  4=Graphic
//  5=Dbc      6=Transceive     7=Extensions        8=Settings
// ============================================================

SideBar::SideBar(QWidget *parent)
    : QStackedWidget(parent)
{
    setObjectName("SideBar");
    setAttribute(Qt::WA_StyledBackground, true);

    m_project      = new ProjectPanel(this);
    m_trace        = new TracePanel(this);
    m_graphicConfig = new GraphicConfigPanel(this);
    m_dbc          = new DbcPanel(this);
    m_transceive   = new TransceivePanel(this);
    m_device       = new DevicePanel(this);
    m_analysis     = new MeasurementSetupPanel(this);
    m_extensions   = new ExtensionsPanel(this);
    m_settings     = new SettingsPanel(this);

    addWidget(m_project);        // 0 = Project
    addWidget(m_analysis);       // 1 = Analysis (Flow)
    addWidget(m_device);         // 2 = Device
    addWidget(m_trace);          // 3 = Trace
    addWidget(m_graphicConfig);  // 4 = Graphic
    addWidget(m_dbc);            // 5 = Dbc
    addWidget(m_transceive);     // 6 = Transceive (收发)
    addWidget(m_extensions);     // 7 = Extensions
    addWidget(m_settings);       // 8 = Settings

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
