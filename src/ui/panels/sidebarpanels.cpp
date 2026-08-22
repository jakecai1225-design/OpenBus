#include "sidebarpanels.h"
#include "core/signalrelay.h"   // DEF-08：字符串信号 → lambda 桥接
#include "core/protocol/protocolregistry.h"   // M2：Flow 模板行注册表枚举（doc/flow.md §13.3）
#include "core/protocol/iprotocoladapter.h"   // M2：适配器接口完整类型（枚举访问）
#include "utils/svg_icon.h"
#include "core/dbcmanager.h"
#include "core/cansimulator.h"
#include "core/candevicemanager.h"
#include "core/candevice.h"
#include "core/driver/driverregistry.h"
#include "core/appconfig.h"
#include "core/sessionmanager.h"
// ui/graphicview.h 已移除 — Graphic 视图经 ModuleRegistry "graphic" 模块操控（B5-5）
#include "ui/thememanager.h"
#include "core/marketmodel.h"
#include "core/driver/marketindex.h"
#include "core/plugin/pluginmanager.h"

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
#include <QPointer>
#include <QScrollArea>

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

        // ---- 子节点：离线分析文件 ----
        auto offlineFiles = extractOfflineFiles(proj.stateJson);
        if (!offlineFiles.isEmpty()) {
            auto *catItem = new QTreeWidgetItem(projItem,
                {QStringLiteral("离线分析文件 (") + QString::number(offlineFiles.size()) + ")"});
            for (const auto &f : offlineFiles) {
                auto *fItem = new QTreeWidgetItem(catItem, {QFileInfo(f).fileName()});
                fItem->setData(0, Qt::UserRole, f);
                fItem->setToolTip(0, f);
            }
        }

        // 无文件时的提示
        if (proj.filePath.isEmpty() && playback.isEmpty() &&
            dbcFiles.isEmpty() && recFiles.isEmpty() && offlineFiles.isEmpty()) {
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

QStringList ProjectPanel::extractOfflineFiles(const QString &stateJson) const
{
    QStringList result;
    if (stateJson.isEmpty()) return result;
    try {
        auto j = nlohmann::json::parse(stateJson.toStdString());
        // v2 格式: resources.offline
        if (j.contains("resources") && j["resources"].contains("offline") &&
            j["resources"]["offline"].is_array()) {
            for (const auto &f : j["resources"]["offline"])
                if (f.is_string())
                    result << QString::fromStdString(f.get<std::string>());
        }
    } catch (...) {}
    return result;
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
    const QString dbcIconCol = ThemeManager::instance()->currentTheme().text;
    auto *importBtn = new QPushButton(
        svgIcon(":/icons/plus.svg", dbcIconCol, 14), "加载数据库文件", this);
    auto *removeBtn = new QPushButton(
        svgIcon(":/icons/dash.svg", dbcIconCol, 14), "删除", this);
    // 主题切换 → 重刷按钮图标颜色
    auto *importRelay = new SignalRelay(this);
    importRelay->fire0 = [importBtn, removeBtn]() {
        const QString c = ThemeManager::instance()->currentTheme().text;
        importBtn->setIcon(svgIcon(":/icons/plus.svg", c, 14));
        removeBtn->setIcon(svgIcon(":/icons/dash.svg", c, 14));
    };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            importRelay, SLOT(fire()));
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
        // DEF-08 字符串信号：DbcManager 定义于 data.dll，跨 DLL PMF connect 断连
        auto *dbcRelay = new SignalRelay(this);
        dbcRelay->fire0 = [this]() { refreshTree(); };
        connect(m_dbcMgr, SIGNAL(dbcLoaded(QString)), dbcRelay, SLOT(fire()));
        connect(m_dbcMgr, SIGNAL(dbcUnloaded(QString)), dbcRelay, SLOT(fire()));
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
//  TracePanel — 形态模板平铺 + 已打开实例列表
// ============================================================

TracePanel::TracePanel(QWidget *parent)
    : SidePanel("Trace", parent)
{
    auto *cl = contentLayout();

    // 模板平铺（doc/flow.md §7.2 平铺修订）：一形态一行，已实现可点击新建，
    // 未实现置灰占位（TR 系列落地后启用）——不再分节嵌套
    m_templateList = new QListWidget(this);
    const QString tmplIconCol = ThemeManager::instance()->currentTheme().text;
    auto addTemplate = [this, tmplIconCol](const QString &name,
                                           const QString &formId,
                                           const QString &tip, bool enabled) {
        auto *row = new QListWidgetItem(m_templateList);
        row->setText(name);
        row->setData(Qt::UserRole, formId);
        row->setToolTip(tip);
        if (enabled)
            row->setIcon(svgIcon(":/icons/plus.svg", tmplIconCol, 14));
        else {
            row->setFlags(Qt::NoItemFlags);   // 置灰占位：不可选中不可点击
            row->setIcon(svgIcon(":/icons/plus.svg", "#6c6c6c", 14));
        }
    };
    addTemplate(QStringLiteral("帧列表"), QStringLiteral("framelist"),
                QStringLiteral("新建帧列表 Trace（TR1，当前形态）"), true);
    addTemplate(QStringLiteral("事务配对"), QStringLiteral("transaction"),
                QStringLiteral("UDS / CANopen SDO 请求-响应事务视图（TR 系列规划）"), false);
    addTemplate(QStringLiteral("聚合监视"), QStringLiteral("aggregwatch"),
                QStringLiteral("报文 / 信号最新值监视（TR 系列规划）"), false);
    addTemplate(QStringLiteral("文本日志流"), QStringLiteral("textlog"),
                QStringLiteral("串口 ASCII / 插件输出 / 系统事件（TR 系列规划）"), false);
    addTemplate(QStringLiteral("字节流"), QStringLiteral("bytestream"),
                QStringLiteral("通用二进制 / HEX 模式（TR 系列规划）"), false);
    addTemplate(QStringLiteral("时序段"), QStringLiteral("timeline"),
                QStringLiteral("LIN 调度表 / FlexRay 周期时间轴（TR 系列规划）"), false);
    cl->addWidget(m_templateList);

    connect(m_templateList, &QListWidget::itemClicked,
            this, &TracePanel::onTemplateClicked);

    // 已打开实例列表（原交互保留：切换 / 右键 / 删除）
    auto *openedLabel = new QLabel(QStringLiteral("已打开"), this);
    openedLabel->setObjectName("SidePanelSubTitle");
    cl->addWidget(openedLabel);

    m_traceList = new QListWidget(this);
    m_traceList->setContextMenuPolicy(Qt::CustomContextMenu);
    cl->addWidget(m_traceList, 1);

    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(8, 6, 8, 6);
    btnBar->setSpacing(4);
    m_delBtn = new QPushButton(
        svgIcon(":/icons/dash.svg", ThemeManager::instance()->currentTheme().text, 14),
        "删除", this);
    // 主题切换 → 重刷模板行与删除按钮图标颜色
    auto *traceBtnRelay = new SignalRelay(this);
    traceBtnRelay->fire0 = [this]() {
        const QString c = ThemeManager::instance()->currentTheme().text;
        m_delBtn->setIcon(svgIcon(":/icons/dash.svg", c, 14));
        for (int i = 0; i < m_templateList->count(); ++i) {
            auto *row = m_templateList->item(i);
            if (row->flags().testFlag(Qt::ItemIsEnabled))
                row->setIcon(svgIcon(":/icons/plus.svg", c, 14));
        }
    };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            traceBtnRelay, SLOT(fire()));
    btnBar->addWidget(m_delBtn);
    btnBar->addStretch();
    cl->addLayout(btnBar);

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

void TracePanel::onTemplateClicked(QListWidgetItem *item)
{
    // 模板行点击：仅已实现形态可新建（当前仅帧列表）；置灰占位行不响应。
    // TR1 落地 TraceFormRegistry 后按 formId 分发到对应形态工厂
    if (!item || !item->flags().testFlag(Qt::ItemIsEnabled))
        return;
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
//  GraphicConfigPanel — 形态模板平铺 + 已打开页面列表
// ============================================================

GraphicConfigPanel::GraphicConfigPanel(QWidget *parent)
    : SidePanel("Graphic 页面列表", parent)
{
    auto *cl = contentLayout();

    // 模板平铺（doc/flow.md §7.2 平铺修订）：一形态一行，已实现可点击新建，
    // 未实现置灰占位（GV 系列落地后启用）——不再分节嵌套
    m_templateList = new QListWidget(this);
    const QString tmplIconCol = ThemeManager::instance()->currentTheme().text;
    auto addTemplate = [this, tmplIconCol](const QString &name,
                                           const QString &formId,
                                           const QString &tip, bool enabled) {
        auto *row = new QListWidgetItem(m_templateList);
        row->setText(name);
        row->setData(Qt::UserRole, formId);
        row->setToolTip(tip);
        if (enabled)
            row->setIcon(svgIcon(":/icons/plus.svg", tmplIconCol, 14));
        else {
            row->setFlags(Qt::NoItemFlags);   // 置灰占位：不可选中不可点击
            row->setIcon(svgIcon(":/icons/plus.svg", "#6c6c6c", 14));
        }
    };
    addTemplate(QStringLiteral("时序波形"), QStringLiteral("waveform"),
                QStringLiteral("新建时序波形 Graphic（当前形态）"), true);
    addTemplate(QStringLiteral("XY 关联"), QStringLiteral("xyplot"),
                QStringLiteral("X/Y 信号关联轨迹图（GV 系列规划）"), false);
    addTemplate(QStringLiteral("数字总线"), QStringLiteral("digital"),
                QStringLiteral("位信号方波轨道（逻辑分析仪风格，GV 系列规划）"), false);
    addTemplate(QStringLiteral("状态时间线"), QStringLiteral("statetimeline"),
                QStringLiteral("枚举值色带段 + 状态图例（GV 系列规划）"), false);
    addTemplate(QStringLiteral("仪表盘"), QStringLiteral("gauge"),
                QStringLiteral("表盘 / 条形 / LED / 数值组件网格（GV 系列规划）"), false);
    addTemplate(QStringLiteral("柱状统计"), QStringLiteral("barstats"),
                QStringLiteral("时间分桶聚合柱 / 面积图（GV 系列规划）"), false);
    cl->addWidget(m_templateList);

    connect(m_templateList, &QListWidget::itemClicked,
            this, &GraphicConfigPanel::onTemplateClicked);

    // 已打开页面列表（原交互保留：切换 / 右键 / 删除）
    auto *openedLabel = new QLabel(QStringLiteral("已打开"), this);
    openedLabel->setObjectName("SidePanelSubTitle");
    cl->addWidget(openedLabel);

    m_pageList = new QListWidget(this);
    m_pageList->setContextMenuPolicy(Qt::CustomContextMenu);
    cl->addWidget(m_pageList, 1);

    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(8, 6, 8, 6);
    btnBar->setSpacing(4);
    m_delBtn = new QPushButton(
        svgIcon(":/icons/dash.svg", ThemeManager::instance()->currentTheme().text, 14),
        "删除", this);
    // 主题切换 → 重刷模板行与删除按钮图标颜色
    auto *graphBtnRelay = new SignalRelay(this);
    graphBtnRelay->fire0 = [this]() {
        const QString c = ThemeManager::instance()->currentTheme().text;
        m_delBtn->setIcon(svgIcon(":/icons/dash.svg", c, 14));
        for (int i = 0; i < m_templateList->count(); ++i) {
            auto *row = m_templateList->item(i);
            if (row->flags().testFlag(Qt::ItemIsEnabled))
                row->setIcon(svgIcon(":/icons/plus.svg", c, 14));
        }
    };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            graphBtnRelay, SLOT(fire()));
    btnBar->addWidget(m_delBtn);
    btnBar->addStretch();
    cl->addLayout(btnBar);

    connect(m_delBtn, &QPushButton::clicked, this, &GraphicConfigPanel::onDeleteGraphic);
    connect(m_pageList, &QListWidget::currentRowChanged,
            this, &GraphicConfigPanel::onPageSelected);
    connect(m_pageList, &QWidget::customContextMenuRequested,
            this, &GraphicConfigPanel::onContextMenu);
}

// setGraphicView 已随 B5-5 移除 — 面板不再持有 GraphicView 指针，
// Graphic 实例编排统一经壳 → ModuleRegistry "graphic" 模块

void GraphicConfigPanel::onTemplateClicked(QListWidgetItem *item)
{
    // 模板行点击：仅已实现形态可新建（当前仅时序波形）；置灰占位行不响应。
    // GV1 落地 GraphicFormRegistry 后按 formId 分发到对应形态工厂
    if (!item || !item->flags().testFlag(Qt::ItemIsEnabled))
        return;
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
    connect(DriverRegistry::instance(), SIGNAL(driversChanged()),
            this, SLOT(refreshDevices()));

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

    // 模拟器（openbus 官方，内置；不经 Registry，方案 §13.3）
    auto *simItem = new QTreeWidgetItem(m_deviceTree);
    simItem->setText(0, QStringLiteral("openbus 模拟器"));
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

    // ---- 「新增设备」折叠栏底部固定入口 → 设备市场标签页 ----
    auto *addItem = new QTreeWidgetItem(m_deviceTree);
    addItem->setText(0, QStringLiteral("新增设备"));
    addItem->setIcon(0, svgIcon(":/icons/plus.svg",
                                ThemeManager::instance()->currentTheme().text, 16));
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

    auto *sendBtn = new QPushButton(QStringLiteral("发送"), this);
    sendBtn->setObjectName("SidePanelButton");
    sendBtn->setToolTip(QStringLiteral("点击打开发送标签页"));
    cl->addWidget(sendBtn);

    auto *playbackBtn = new QPushButton(QStringLiteral("回放"), this);
    playbackBtn->setObjectName("SidePanelButton");
    playbackBtn->setToolTip(QStringLiteral("点击打开回放标签页"));
    cl->addWidget(playbackBtn);

    auto *offlineBtn = new QPushButton(QStringLiteral("离线分析"), this);
    offlineBtn->setObjectName("SidePanelButton");
    offlineBtn->setToolTip(QStringLiteral("点击打开离线分析标签页"));
    cl->addWidget(offlineBtn);

    auto *recordBtn = new QPushButton(QStringLiteral("录制"), this);
    recordBtn->setObjectName("SidePanelButton");
    recordBtn->setToolTip(QStringLiteral("点击打开录制标签页"));
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
    cl->addWidget(m_list, 1);

    connect(m_list, &QListWidget::itemClicked,
            this, &SettingsPanel::onItemClicked);
}

void SettingsPanel::onItemClicked(QListWidgetItem *item)
{
    if (item)
        emit settingsRequested(item->text());
}

// ============================================================
//  MeasurementSetupPanel — 协议流模板平铺
// ============================================================

MeasurementSetupPanel::MeasurementSetupPanel(QWidget *parent)
    : SidePanel("Flow", parent)
{
    auto *cl = contentLayout();

    // 协议流模板平铺（doc/flow.md §7.2 平铺修订）：注册表适配器 → 可点击行；
    // 未落地协议 → 置灰占位行（同 protocolId 适配器注册后由 rebuildTemplates 接管）
    m_templateList = new QListWidget(this);
    cl->addWidget(m_templateList);
    rebuildTemplates();

    connect(m_templateList, &QListWidget::itemClicked,
            this, &MeasurementSetupPanel::onTemplateClicked);

    auto *hint = new QLabel("\n"
                            "\xE2\x80\xA2 点击 CAN Flow 打开画布\n"
                            "\xE2\x80\xA2 未启用块：单击启用\n"
                            "\xE2\x80\xA2 已启用块：单击/双击进入配置\n"
                            "\xE2\x80\xA2 右键菜单：配置 / 启停 / 增删", this);
    hint->setWordWrap(true);
    hint->setObjectName("SidePanelHint");
    cl->addWidget(hint);

    cl->addStretch();

    // M1 预埋：新增协议流占位入口（协议市场 F1 剩余就绪前禁用）
    auto *addFlowBtn = new QPushButton(QStringLiteral("从市场添加协议流"), this);
    addFlowBtn->setEnabled(false);
    addFlowBtn->setToolTip(QStringLiteral("协议市场就绪后启用（doc/flow.md §十三 M1）"));
    auto *addFlowBar = new QHBoxLayout;
    addFlowBar->setContentsMargins(8, 6, 8, 6);
    addFlowBar->addWidget(addFlowBtn);
    cl->addLayout(addFlowBar);

    // 模板行重灌：注册表适配器注册（F4 协议包）或主题切换（图标颜色）
    auto *registryRelay = new SignalRelay(this);
    registryRelay->fire0 = [this]() { rebuildTemplates(); };
    connect(ProtocolRegistry::instance(), SIGNAL(adapterRegistered(QString)),
            registryRelay, SLOT(fire()));
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            registryRelay, SLOT(fire()));
}

void MeasurementSetupPanel::rebuildTemplates()
{
    m_templateList->clear();
    const QString iconCol = ThemeManager::instance()->currentTheme().text;

    // ① 注册表适配器 → 可点击模板行（点击新建/打开该协议流）
    QStringList registered;
    for (auto *adapter : ProtocolRegistry::instance()->adapters()) {
        registered << adapter->protocolId();
        auto *row = new QListWidgetItem(adapter->displayName(), m_templateList);
        row->setData(Qt::UserRole, adapter->protocolId());
        row->setIcon(svgIcon(":/icons/plus.svg", iconCol, 14));
    }

    // ② 未落地协议 → 置灰占位行（预埋模板入口；F 系列落地/协议包安装后启用）
    struct Placeholder { const char *pid; const char *title; const char *tip; };
    static const Placeholder placeholders[] = {
        { "ethercat", "EtherCAT Flow", "EtherCAT 适配器（F3）落地后启用" },
        { "canopen",  "CANopen Flow",  "CANopen 适配器落地后启用（规划中）" },
        { "general",  "通用 Flow",     "通用 Flow 适配器（F2）落地后启用" },
    };
    for (const auto &p : placeholders) {
        if (registered.contains(QLatin1String(p.pid)))
            continue;   // 注册表已接管：占位行让位
        auto *row = new QListWidgetItem(QString::fromUtf8(p.title), m_templateList);
        row->setFlags(Qt::NoItemFlags);   // 置灰占位：不可选中不可点击
        row->setToolTip(QString::fromUtf8(p.tip));
        row->setData(Qt::UserRole, QLatin1String(p.pid));
        row->setIcon(svgIcon(":/icons/plus.svg", "#6c6c6c", 14));
    }
}

void MeasurementSetupPanel::onTemplateClicked(QListWidgetItem *item)
{
    // 模板行点击：仅注册表行可点（当前仅 CAN Flow，打开/聚焦画布页）；
    // F1 FlowSession 落地后按 protocolId 创建新流实例
    if (!item || !item->flags().testFlag(Qt::ItemIsEnabled))
        return;
    emit openMeasurementSetupRequested();
}

// ============================================================
//  ExtensionsPanel — 迷你市场（与插件市场页同源，方案 §13.10）
// ============================================================

ExtensionsPanel::ExtensionsPanel(QWidget *parent)
    : SidePanel("插件市场", parent)
{
    auto *cl = contentLayout();

    // 搜索栏 + 右上角 "…" 菜单（与市场页工具栏同构）
    auto *searchRow = new QHBoxLayout;
    searchRow->setContentsMargins(8, 8, 4, 8);
    searchRow->setSpacing(4);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName("ExtensionSearch");
    m_searchEdit->setPlaceholderText("在驱动与插件中搜索...");
    m_searchEdit->setClearButtonEnabled(true);
    // 原生清除按钮 × 不随主题（深色下不可见）→ 换主题色 SVG 图标
    applyClearButtonIcon(m_searchEdit, ThemeManager::instance()->currentTheme().text);
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

    // 主题切换 → 重刷菜单图标颜色
    auto *menuRelay = new SignalRelay(this);
    menuRelay->fire0 = [this]() {
        const QString c = ThemeManager::instance()->currentTheme().text;
        m_menuBtn->setIcon(svgIcon(":/icons/kebab.svg", c, 16));
    };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            menuRelay, SLOT(fire()));

    // 已装条目列表（FrameRow，与市场页同行风格；市场分组已移至标签页）
    auto *listHost = new QWidget(this);
    m_listLay = new QVBoxLayout(listHost);
    m_listLay->setContentsMargins(0, 0, 0, 0);
    m_listLay->setSpacing(2);
    m_listLay->addStretch(1);
    m_listArea = new QScrollArea(this);
    m_listArea->setWidgetResizable(true);
    m_listArea->setWidget(listHost);
    m_listArea->setFrameShape(QFrame::NoFrame);
    cl->addWidget(m_listArea, 1);

    // 命令分组（插件命令入口，无命令时隐藏）
    m_cmdHeader = new QLabel(QString::fromUtf8("命令"), this);
    m_cmdHeader->setStyleSheet(
        QStringLiteral("color: #888888; font-weight: bold; padding: 6px 4px 2px 4px;"));
    m_cmdHeader->setHidden(true);
    cl->addWidget(m_cmdHeader);
    m_cmdList = new QListWidget(this);
    m_cmdList->setHidden(true);
    m_cmdList->setMaximumHeight(200);
    cl->addWidget(m_cmdList);

    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &ExtensionsPanel::onSearchChanged);
    connect(m_cmdList, &QListWidget::itemClicked,
            this, &ExtensionsPanel::onCommandClicked);

    // 四数据源变化自动刷新（与市场页一致：安装/卸载/启停/索引加载）
    connect(DriverRegistry::instance(), SIGNAL(driversChanged()),
            this, SLOT(refreshEntries()));
    connect(PluginManager::instance(), SIGNAL(pluginListChanged()),
            this, SLOT(refreshEntries()));
    auto *loadedRelay = new SignalRelay(this);
    loadedRelay->fire0 = [this]() { refreshEntries(); };
    connect(MarketIndex::instance(), SIGNAL(loaded(bool,QString)),
            loadedRelay, SLOT(fire()));

    refreshEntries();
}

void ExtensionsPanel::refreshEntries()
{
    rebuild();
}

void ExtensionsPanel::onSearchChanged()
{
    rebuild();
}

void ExtensionsPanel::rebuild()
{
    // 清空旧行（尾部重新补 stretch）
    while (m_listLay->count()) {
        QLayoutItem *child = m_listLay->takeAt(0);
        if (child->widget())
            child->widget()->deleteLater();
        delete child;
    }
    m_listLay->addStretch(1);
    const auto insertBeforeStretch = [this](QWidget *w) {
        m_listLay->insertWidget(m_listLay->count() - 1, w);
    };

    const QString text = m_searchEdit->text();
    int shown = 0;

    // ---- 分组：已安装（驱动 + 插件混合） ----
    int installed = 0;
    for (const auto &e : MarketModel::collectInstalledDrivers()) {
        if (!MarketIndex::matchWords(text, e.searchFields))
            continue;
        if (installed == 0)
            addSectionLabel(QStringLiteral("已安装"));
        insertBeforeStretch(makeRow(e));
        ++installed;
        ++shown;
    }
    for (const auto &e : MarketModel::collectInstalledPlugins()) {
        if (!MarketIndex::matchWords(text, e.searchFields))
            continue;
        if (installed == 0)
            addSectionLabel(QStringLiteral("已安装"));
        insertBeforeStretch(makeRow(e));
        ++installed;
        ++shown;
    }

    // 「驱动市场」「插件市场」分组已移至标签页 MarketTab（v2.2 市场入口分工）：
    // sidebar 仅保留已装启停管理，发现与安装归标签页，避免与标签页市场重复。

    if (shown == 0) {
        auto *empty = new QLabel(QStringLiteral("没有匹配的条目"), this);
        empty->setStyleSheet(QStringLiteral("color: #777777; padding: 8px;"));
        insertBeforeStretch(empty);
    }
}

void ExtensionsPanel::addSectionLabel(const QString &title)
{
    auto *label = new QLabel(title, this);
    label->setStyleSheet(
        QStringLiteral("color: #888888; font-weight: bold; padding: 6px 4px 2px 4px;"));
    m_listLay->insertWidget(m_listLay->count() - 1, label);
}

FrameRow *ExtensionsPanel::makeRow(const MarketEntryData &e)
{
    auto *row = new FrameRow;
    row->item = e.item;

    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(8, 4, 4, 4);
    lay->setSpacing(8);

    // 图标：已装插件本地优先，其余市场 icon 异步兑底，最终首字母头像
    auto *icon = new QLabel;
    icon->setFixedSize(24, 24);
    icon->setAlignment(Qt::AlignCenter);
    row->iconLabel = icon;
    if (e.item.kind == MarketItem::InstalledPlugin) {
        const QPixmap local = MarketModel::pluginIconLocal(e.item.id);
        if (!local.isNull())
            icon->setPixmap(local.scaled(24, 24, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation));
    }
    if (icon->pixmap().isNull() && !e.marketIcon.isEmpty()) {
        MarketModel::fetchMarketPixmap(
            MarketIndex::instance()->resolveUrl(e.marketIcon),
            [icon](const QPixmap &pm) {
                QPointer<QLabel> g(icon);
                if (g)
                    g->setPixmap(pm.scaled(24, 24, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation));
            });
    }
    if (icon->pixmap().isNull())
        icon->setPixmap(PluginUi::pluginIconPixmap(QString(), e.title, 24));
    lay->addWidget(icon);

    auto *tbox = new QVBoxLayout;
    tbox->setSpacing(0);
    auto *titleLabel = new QLabel(e.title);
    QFont bold = titleLabel->font();
    bold.setBold(true);
    titleLabel->setFont(bold);
    // Ignored 策略：允许压缩到内容以下，避免挤占右侧状态词/齿轮按钮的空间
    // （窄面板下 QLabel 默认最小宽度会把尾部控件推出可视区）
    titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *metaLabel = new QLabel(e.meta);
    QFont small = metaLabel->font();
    small.setPointSize(qMax(small.pointSize() - 1, 1));
    metaLabel->setFont(small);
    metaLabel->setStyleSheet(QStringLiteral("color: #9d9d9d;"));
    metaLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    // 单行截断（窄面板）
    const QFontMetrics fm(metaLabel->font());
    metaLabel->setText(fm.elidedText(e.meta, Qt::ElideRight, 120));
    tbox->addWidget(titleLabel);
    tbox->addWidget(metaLabel);
    lay->addLayout(tbox, 1);

    if (!e.status.isEmpty()) {
        auto *statusLabel = new QLabel(e.status);
        statusLabel->setStyleSheet(QStringLiteral("color: #9d9d9d;"));
        lay->addWidget(statusLabel, 0, Qt::AlignVCenter);
    }

    // 齿轮菜单（已装驱动/已装插件；市场条目点击行跳市场页操作）
    if (e.item.kind == MarketItem::InstalledPlugin
        || e.item.kind == MarketItem::InstalledDriver) {
        auto *gear = new QToolButton;
        gear->setIcon(svgIcon(":/icons/gear.svg",
                              ThemeManager::instance()->currentTheme().text, 14));
        gear->setToolTip("更多操作");
        gear->setAutoRaise(true);
        MarketEntryData entry = e;
        connect(gear, &QToolButton::clicked, this, [this, gear, entry]() {
            showGearMenu(entry, gear->mapToGlobal(QPoint(0, gear->height())));
        });
        lay->addWidget(gear, 0, Qt::AlignTop);
    }

    // 非按钮子控件鼠标事件穿透 → 行点击
    icon->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    for (QLabel *l : row->findChildren<QLabel *>())
        l->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    row->setOnClick([this, item = e.item]() { emit itemActivated(item); });
    return row;
}

void ExtensionsPanel::addCommand(const QString &id, const QString &title)
{
    // 避免重复
    for (int i = 0; i < m_cmdList->count(); ++i) {
        if (m_cmdList->item(i)->data(Qt::UserRole).toString() == id)
            return;
    }
    auto *item = new QListWidgetItem(title);
    item->setData(Qt::UserRole, id);
    m_cmdList->addItem(item);
    m_cmdHeader->setHidden(false);
    m_cmdList->setHidden(false);
}

void ExtensionsPanel::clearCommands()
{
    m_cmdList->clear();
    m_cmdHeader->setHidden(true);
    m_cmdList->setHidden(true);
}

void ExtensionsPanel::onCommandClicked(QListWidgetItem *item)
{
    if (!item)
        return;
    const QString cmdId = item->data(Qt::UserRole).toString();
    if (!cmdId.isEmpty())
        emit commandTriggered(cmdId);
}

void ExtensionsPanel::onMenuClicked()
{
    QMenu menu(this);
    auto *installAct = menu.addAction(
        QString::fromUtf8("从 .odp / .opk 离线安装..."));
    connect(installAct, &QAction::triggered, this, [this]() {
        emit installFromFileRequested();
    });
    auto *openAct = menu.addAction(QString::fromUtf8("打开市场页"));
    connect(openAct, &QAction::triggered, this, [this]() {
        emit openMarketRequested();
    });
    auto *refreshAct = menu.addAction(QString::fromUtf8("刷新"));
    connect(refreshAct, &QAction::triggered, this, [this]() {
        MarketIndex::instance()->refresh();
        DriverRegistry::instance()->scanAndLoad();   // driversChanged → 自动刷新
        refreshEntries();
    });
    menu.exec(m_menuBtn->mapToGlobal(QPoint(0, m_menuBtn->height())));
}

void ExtensionsPanel::showGearMenu(const MarketEntryData &e, const QPoint &globalPos)
{
    QMenu menu(this);

    // 在插件市场中查看详情（与行点击同一联动）
    auto *viewAct = menu.addAction(QString::fromUtf8("在插件市场中查看"));
    connect(viewAct, &QAction::triggered, this, [this, item = e.item]() {
        emit itemActivated(item);
    });
    menu.addSeparator();

    if (e.item.kind == MarketItem::InstalledPlugin) {
        auto *pm = PluginManager::instance();
        const bool enabled = pm->isPluginEnabled(e.item.id);
        const bool activated = pm->isPluginActivated(e.item.id);
        if (!enabled) {
            auto *enableAct = menu.addAction(QString::fromUtf8("启用"));
            connect(enableAct, &QAction::triggered, this, [this, id = e.item.id]() {
                emit pluginToggleRequested(id, true);
            });
        } else {
            if (activated) {
                auto *stopAct = menu.addAction(QString::fromUtf8("停止"));
                connect(stopAct, &QAction::triggered, this, [this, id = e.item.id]() {
                    emit pluginDeactivateRequested(id);
                });
            }
            auto *startAct = menu.addAction(activated ? QString::fromUtf8("重启")
                                                      : QString::fromUtf8("启动"));
            connect(startAct, &QAction::triggered, this, [this, id = e.item.id]() {
                emit pluginActivated(id);
            });
            auto *disableAct = menu.addAction(QString::fromUtf8("禁用"));
            connect(disableAct, &QAction::triggered, this, [this, id = e.item.id]() {
                emit pluginToggleRequested(id, false);
            });
        }
        menu.addSeparator();
        auto *uninstallAct = menu.addAction(QString::fromUtf8("卸载"));
        connect(uninstallAct, &QAction::triggered, this, [this, id = e.item.id]() {
            emit pluginUninstallRequested(id);
        });
    } else {
        // 已装驱动：状态从 Registry 实时取
        const auto drivers = DriverRegistry::instance()->drivers();
        bool enabledNow = true;
        bool builtin = false;
        for (const auto &d : drivers) {
            if (d.driverId == e.item.id) {
                enabledNow = d.enabled;
                builtin = d.builtin;
                break;
            }
        }
        auto *toggleAct = menu.addAction(enabledNow ? QString::fromUtf8("禁用驱动")
                                                     : QString::fromUtf8("启用驱动"));
        toggleAct->setToolTip(QString::fromUtf8(
            "禁用后设备树隐藏且不参与枚举/创建，重启后不加载（方案 §7.4）"));
        connect(toggleAct, &QAction::triggered, this,
                [this, id = e.item.id, to = !enabledNow]() {
            emit driverToggleRequested(id, to);
        });
        auto *uninstallAct = menu.addAction(QString::fromUtf8("卸载驱动"));
        uninstallAct->setEnabled(!builtin);
        uninstallAct->setToolTip(builtin
            ? QString::fromUtf8("内置驱动不可卸载")
            : QString::fromUtf8(
                "已加载的 DLL 在重启程序前仍驻留内存（方案 §7.4）"));
        connect(uninstallAct, &QAction::triggered, this, [this, id = e.item.id]() {
            emit driverUninstallRequested(id);
        });
    }
    menu.exec(globalPos);
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
