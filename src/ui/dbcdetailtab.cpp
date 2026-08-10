#include "dbcdetailtab.h"
#include "core/dbcmanager.h"
#include "core/logging.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QLineEdit>
#include <QLabel>
#include <QHeaderView>
#include <QGroupBox>
#include <QStackedWidget>
#include <QScrollArea>
#include <QMenu>
#include <QAction>
#include <QFont>
#include <QBrush>
#include <QDebug>
#include <QStyleFactory>
#include <QStyle>

// 树节点 UserRole
static const int RoleNodeType = Qt::UserRole;       // int -> NodeType
static const int RoleCanId    = Qt::UserRole + 1;   // uint -> message/signal canId
static const int RoleName     = Qt::UserRole + 2;   // QString -> signal/node/vt name

DbcDetailTab::DbcDetailTab(const QString &dbcFileName, DbcManager *mgr, QWidget *parent)
    : QWidget(parent)
    , m_dbcFileName(dbcFileName)
    , m_dbcMgr(mgr)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 标题栏
    auto *titleBar = new QLabel("DBC: " + dbcFileName, this);
    titleBar->setObjectName("DbcDetailTitle");
    titleBar->setContentsMargins(8, 6, 8, 6);
    mainLayout->addWidget(titleBar);

    // 左右分割
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    buildLeftPane(splitter);
    buildRightPane(splitter);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 7);

    mainLayout->addWidget(splitter, 1);

    // 信号连接
    connect(m_searchEdit, &QLineEdit::textChanged, this, &DbcDetailTab::onSearchChanged);
    connect(m_tree, &QTreeWidget::itemClicked, this, &DbcDetailTab::onTreeItemClicked);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &DbcDetailTab::onTreeItemDoubleClicked);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &DbcDetailTab::onTreeContextMenu);

    refreshTree();
    showPlaceholder();
}

// ============================================================
//  左侧面板
// ============================================================

void DbcDetailTab::buildLeftPane(QSplitter *splitter)
{
    auto *leftWidget = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("搜索信号/报文/节点...");
    m_searchEdit->setContentsMargins(4, 4, 4, 4);
    leftLayout->addWidget(m_searchEdit);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(18);
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setAnimated(true);
    m_tree->setExpandsOnDoubleClick(true);
    // CANdb++ 风格: 经典 +/- 折叠按钮 + 树形连接线
    m_tree->setStyle(QStyleFactory::create("Windows"));
    // 局部样式: 配色与整体 VS Code 风格主题一致
    m_tree->setStyleSheet(
        "QTreeWidget { background-color: #f8f8f8; border: none; outline: none; font-size: 12px; }"
        "QTreeWidget::item { padding: 3px 2px; min-height: 20px; }"
        "QTreeWidget::item:hover { background-color: #e8e8e8; }"
        "QTreeWidget::item:selected { background-color: #c5d9f1; color: #000; }"
    );
    leftLayout->addWidget(m_tree, 1);

    splitter->addWidget(leftWidget);
}

// ============================================================
//  右侧面板 — StackedWidget
// ============================================================

void DbcDetailTab::buildRightPane(QSplitter *splitter)
{
    m_detailStack = new QStackedWidget(this);

    // Page: Message
    auto *msgPage = new QWidget(m_detailStack);
    buildMessagePage(msgPage);
    m_pageMessage = m_detailStack->addWidget(msgPage);

    // Page: Signal
    auto *sigPage = new QWidget(m_detailStack);
    buildSignalPage(sigPage);
    m_pageSignal = m_detailStack->addWidget(sigPage);

    // Page: Node
    auto *nodePage = new QWidget(m_detailStack);
    buildNodePage(nodePage);
    m_pageNode = m_detailStack->addWidget(nodePage);

    // Page: ValueTable
    auto *vtPage = new QWidget(m_detailStack);
    buildValueTablePage(vtPage);
    m_pageValueTable = m_detailStack->addWidget(vtPage);

    // Page: Placeholder
    auto *placeholder = new QWidget(m_detailStack);
    auto *phLayout = new QVBoxLayout(placeholder);
    phLayout->setAlignment(Qt::AlignCenter);
    auto *phLabel = new QLabel("点击左侧树中的条目查看详情", placeholder);
    phLabel->setAlignment(Qt::AlignCenter);
    phLabel->setObjectName("DbcDetailPlaceholder");
    phLayout->addWidget(phLabel);
    m_pagePlaceholder = m_detailStack->addWidget(placeholder);

    splitter->addWidget(m_detailStack);
}

// ---- Message 详情页 ----

void DbcDetailTab::buildMessagePage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_msgTitleLabel = new QLabel(page);
    m_msgTitleLabel->setObjectName("DbcDetailPageTitle");
    layout->addWidget(m_msgTitleLabel);

    m_msgInfoLabel = new QLabel(page);
    m_msgInfoLabel->setObjectName("DbcDetailInfo");
    layout->addWidget(m_msgInfoLabel);

    m_msgCommentLabel = new QLabel(page);
    m_msgCommentLabel->setWordWrap(true);
    m_msgCommentLabel->setObjectName("DbcDetailComment");
    m_msgCommentLabel->setVisible(false);
    layout->addWidget(m_msgCommentLabel);

    // 信号表格
    auto *sigGroup = new QGroupBox("Signals", page);
    auto *sigLayout = new QVBoxLayout(sigGroup);
    m_msgSigTable = new QTableWidget(0, 9, sigGroup);
    m_msgSigTable->setHorizontalHeaderLabels(
        {"名称", "起始位", "长度", "字节序", "符号", "因子", "偏移", "最小/最大", "单位"});
    m_msgSigTable->verticalHeader()->setVisible(false);
    m_msgSigTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_msgSigTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 9; ++i)
        m_msgSigTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    sigLayout->addWidget(m_msgSigTable);
    layout->addWidget(sigGroup, 1);
}

// ---- Signal 详情页 ----

void DbcDetailTab::buildSignalPage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_sigTitleLabel = new QLabel(page);
    m_sigTitleLabel->setObjectName("DbcDetailPageTitle");
    layout->addWidget(m_sigTitleLabel);

    // 属性表
    auto *propGroup = new QGroupBox("属性", page);
    auto *propLayout = new QVBoxLayout(propGroup);
    m_sigPropTable = new QTableWidget(0, 2, propGroup);
    m_sigPropTable->setHorizontalHeaderLabels({"属性", "值"});
    m_sigPropTable->verticalHeader()->setVisible(false);
    m_sigPropTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_sigPropTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_sigPropTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    propLayout->addWidget(m_sigPropTable);
    layout->addWidget(propGroup);

    // 注释
    m_sigCommentLabel = new QLabel(page);
    m_sigCommentLabel->setWordWrap(true);
    m_sigCommentLabel->setObjectName("DbcDetailComment");
    m_sigCommentLabel->setVisible(false);
    layout->addWidget(m_sigCommentLabel);

    // 值表
    auto *vtGroup = new QGroupBox("Value Table", page);
    auto *vtLayout = new QVBoxLayout(vtGroup);
    m_sigValueTable = new QTableWidget(0, 2, vtGroup);
    m_sigValueTable->setHorizontalHeaderLabels({"值", "描述"});
    m_sigValueTable->verticalHeader()->setVisible(false);
    m_sigValueTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_sigValueTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_sigValueTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    vtLayout->addWidget(m_sigValueTable);
    vtGroup->setVisible(false);
    layout->addWidget(vtGroup, 1);
}

// ---- Node 详情页 ----

void DbcDetailTab::buildNodePage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_nodeTitleLabel = new QLabel(page);
    m_nodeTitleLabel->setObjectName("DbcDetailPageTitle");
    layout->addWidget(m_nodeTitleLabel);

    m_nodeCommentLabel = new QLabel(page);
    m_nodeCommentLabel->setWordWrap(true);
    m_nodeCommentLabel->setObjectName("DbcDetailComment");
    m_nodeCommentLabel->setVisible(false);
    layout->addWidget(m_nodeCommentLabel);

    // TX 报文表
    auto *txGroup = new QGroupBox("发送的报文 (TX)", page);
    auto *txLayout = new QVBoxLayout(txGroup);
    m_nodeTxTable = new QTableWidget(0, 3, txGroup);
    m_nodeTxTable->setHorizontalHeaderLabels({"ID", "名称", "DLC"});
    m_nodeTxTable->verticalHeader()->setVisible(false);
    m_nodeTxTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_nodeTxTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    txLayout->addWidget(m_nodeTxTable);
    layout->addWidget(txGroup, 1);

    // RX 信号表
    auto *rxGroup = new QGroupBox("接收的信号 (RX)", page);
    auto *rxLayout = new QVBoxLayout(rxGroup);
    m_nodeRxTable = new QTableWidget(0, 3, rxGroup);
    m_nodeRxTable->setHorizontalHeaderLabels({"报文ID", "信号名", "报文名"});
    m_nodeRxTable->verticalHeader()->setVisible(false);
    m_nodeRxTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_nodeRxTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    rxLayout->addWidget(m_nodeRxTable);
    layout->addWidget(rxGroup, 1);
}

// ---- ValueTable 详情页 ----

void DbcDetailTab::buildValueTablePage(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_vtTitleLabel = new QLabel(page);
    m_vtTitleLabel->setObjectName("DbcDetailPageTitle");
    layout->addWidget(m_vtTitleLabel);

    m_vtTable = new QTableWidget(0, 2, page);
    m_vtTable->setHorizontalHeaderLabels({"值", "描述"});
    m_vtTable->verticalHeader()->setVisible(false);
    m_vtTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_vtTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_vtTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(m_vtTable, 1);
}

// ============================================================
//  树刷新
// ============================================================

const DbcFile *DbcDetailTab::currentDbcFile() const
{
    if (!m_dbcMgr) return nullptr;
    return m_dbcMgr->findFile(m_dbcFileName);
}

void DbcDetailTab::refreshTree()
{
    m_tree->clear();
    const DbcFile *file = currentDbcFile();
    if (!file) {
        SIN_LOG_WARN("DbcDetailTab", "currentDbcFile() returned nullptr for {}",
                     m_dbcFileName.toStdString());
        return;
    }
    SIN_LOG_DEBUG("DbcDetailTab", "refreshTree: {} messages: {} nodes: {} valueTables: {}",
                  file->fileName.toStdString(), file->messages.size(),
                  file->nodes.size(), file->valueTables.size());

    // 顶层: 网络
    auto *netItem = new QTreeWidgetItem(m_tree, {file->fileName});
    netItem->setData(0, RoleNodeType, static_cast<int>(NodeType::Network));
    netItem->setExpanded(true);  // 仅顶层网络节点默认展开
    {
        QFont f = netItem->font(0);
        f.setBold(true);
        f.setPointSize(f.pointSize() + 1);
        netItem->setFont(0, f);
    }

    // 辅助：给分类节点设置加粗字体
    auto makeCategoryBold = [](QTreeWidgetItem *item) {
        QFont f = item->font(0);
        f.setBold(true);
        item->setFont(0, f);
    };

    // 辅助：给信号节点设置稍微淡化的颜色
    auto makeSignalItalic = [](QTreeWidgetItem *item) {
        QFont f = item->font(0);
        f.setItalic(false);
        item->setFont(0, f);
        // 值表条目用斜体灰色
        QBrush brush(Qt::darkGray);
        for (int i = 0; i < item->childCount(); ++i) {
            QTreeWidgetItem *child = item->child(i);
            QFont cf = child->font(0);
            cf.setItalic(true);
            child->setFont(0, cf);
            child->setForeground(0, brush);
        }
    };

    // ---- 辅助 lambda：为信号节点创建子项（值表条目） ----
    auto buildSignalChildren = [&](QTreeWidgetItem *sigItem, const DbcSignal &sig) {
        if (!sig.valueTable.isEmpty()) {
            for (const auto &vd : sig.valueTable) {
                auto *vtEntry = new QTreeWidgetItem(sigItem,
                    {QString("%1 = %2").arg(vd.value).arg(vd.description)});
                vtEntry->setData(0, RoleNodeType, static_cast<int>(NodeType::Signal));
                vtEntry->setData(0, RoleCanId, sigItem->data(0, RoleCanId).toUInt());
                vtEntry->setData(0, RoleName, sig.name);
            }
        }
    };

    // ---- 辅助 lambda：创建信号节点（带属性信息） ----
    auto createSignalItem = [&](QTreeWidgetItem *parent, const DbcMessage &msg, const DbcSignal &sig) {
        // 信号名 + 属性信息（对齐 CANdb++ 显示风格）
        QString sigText = sig.name;
        // 多路复用标记
        if (sig.muxType == DbcSignal::MuxType::Multiplexor)
            sigText += "  [M]";
        else if (sig.muxType == DbcSignal::MuxType::Multiplexed)
            sigText += QString("  [m%1]").arg(sig.muxValue);
        // 起始位/长度/字节序
        sigText += QString("  [%1|%2 %3]")
            .arg(sig.startBit)
            .arg(sig.bitLength)
            .arg(sig.littleEndian ? QStringLiteral("Intel") : QStringLiteral("Motorola"));
        // 单位
        if (!sig.unit.isEmpty())
            sigText += QString("  %1").arg(sig.unit);

        auto *sigItem = new QTreeWidgetItem(parent, {sigText});
        sigItem->setData(0, RoleNodeType, static_cast<int>(NodeType::Signal));
        sigItem->setData(0, RoleCanId, msg.id);
        sigItem->setData(0, RoleName, sig.name);
        // 值表条目作为子项
        buildSignalChildren(sigItem, sig);
        // 值表条目用斜体灰色
        makeSignalItalic(sigItem);
        return sigItem;
    };

    // ---- Category: Network Nodes ----
    auto *catNodes = new QTreeWidgetItem(netItem,
        {QString("Network Nodes (%1)").arg(file->nodes.size())});
    catNodes->setData(0, RoleNodeType, static_cast<int>(NodeType::CategoryNodes));
    catNodes->setExpanded(false);  // 默认折叠
    makeCategoryBold(catNodes);
    for (const auto &node : file->nodes) {
        auto *nodeItem = new QTreeWidgetItem(catNodes, {node.name});
        nodeItem->setData(0, RoleNodeType, static_cast<int>(NodeType::Node));
        nodeItem->setData(0, RoleName, node.name);
        nodeItem->setExpanded(false);
        if (!node.txMessageIds.isEmpty()) {
            auto *txCat = new QTreeWidgetItem(nodeItem, {QString("TX (%1)").arg(node.txMessageIds.size())});
            txCat->setExpanded(false);
            for (quint32 txId : node.txMessageIds) {
                const DbcMessage *msg = file->findMessage(txId);
                QString text = QString("0x%1  %2")
                    .arg(txId, 0, 16).toUpper()
                    .arg(msg ? msg->name : "?");
                if (msg && msg->dlc > 0)
                    text += QString("  [DLC=%1]").arg(msg->dlc);
                if (msg && msg->cycleTime > 0)
                    text += QString("  [%1ms]").arg(msg->cycleTime);
                auto *txItem = new QTreeWidgetItem(txCat, {text});
                txItem->setData(0, RoleNodeType, static_cast<int>(NodeType::Message));
                txItem->setData(0, RoleCanId, txId);
                txItem->setExpanded(false);
                // 信号子项
                if (msg) {
                    for (const auto &sig : msg->signalList)
                        createSignalItem(txItem, *msg, sig);
                }
            }
        }

        // RX 子分类 — 按报文分组（与 TX 层级一致：RX → Message → Signal）
        if (!node.rxSignals.isEmpty()) {
            // 按 CAN ID 分组
            QHash<quint32, QStringList> rxById;
            for (const auto &rx : node.rxSignals)
                rxById[rx.first].append(rx.second);

            auto *rxCat = new QTreeWidgetItem(nodeItem, {QString("RX (%1)").arg(rxById.size())});
            rxCat->setExpanded(false);
            for (auto it = rxById.constBegin(); it != rxById.constEnd(); ++it) {
                quint32 rxId = it.key();
                const DbcMessage *msg = file->findMessage(rxId);
                QString text = QString("0x%1  %2")
                    .arg(rxId, 0, 16).toUpper()
                    .arg(msg ? msg->name : "?");
                if (msg && msg->dlc > 0)
                    text += QString("  [DLC=%1]").arg(msg->dlc);
                if (msg && msg->cycleTime > 0)
                    text += QString("  [%1ms]").arg(msg->cycleTime);
                auto *rxMsgItem = new QTreeWidgetItem(rxCat, {text});
                rxMsgItem->setData(0, RoleNodeType, static_cast<int>(NodeType::Message));
                rxMsgItem->setData(0, RoleCanId, rxId);
                rxMsgItem->setExpanded(false);
                // 信号子项 — 只显示该节点接收的信号
                if (msg) {
                    for (const auto &sigName : it.value()) {
                        const DbcSignal *sig = msg->findSignal(sigName);
                        if (sig)
                            createSignalItem(rxMsgItem, *msg, *sig);
                    }
                }
            }
        }
    }

    // ---- Category: Messages ----
    auto *catMsgs = new QTreeWidgetItem(netItem,
        {QString("Messages (%1)").arg(file->messages.size())});
    catMsgs->setData(0, RoleNodeType, static_cast<int>(NodeType::CategoryMessages));
    catMsgs->setExpanded(false);
    makeCategoryBold(catMsgs);
    for (const auto &msg : file->messages) {
        // 报文名 + DLC + 周期信息（对齐 CANdb++）
        QString msgText = QString("0x%1  %2")
            .arg(msg.id, 0, 16).toUpper()
            .arg(msg.name);
        if (msg.dlc > 0)
            msgText += QString("  [DLC=%1]").arg(msg.dlc);
        if (msg.cycleTime > 0)
            msgText += QString("  [%1ms]").arg(msg.cycleTime);
        if (!msg.sender.isEmpty())
            msgText += QString("  <%1>").arg(msg.sender);

        auto *msgItem = new QTreeWidgetItem(catMsgs, {msgText});
        msgItem->setData(0, RoleNodeType, static_cast<int>(NodeType::Message));
        msgItem->setData(0, RoleCanId, msg.id);
        msgItem->setExpanded(false);

        for (const auto &sig : msg.signalList)
            createSignalItem(msgItem, msg, sig);
    }

    // ---- Category: Value Tables ----
    if (!file->valueTables.isEmpty()) {
        auto *catVT = new QTreeWidgetItem(netItem,
            {QString("Value Tables (%1)").arg(file->valueTables.size())});
        catVT->setData(0, RoleNodeType, static_cast<int>(NodeType::CategoryValueTables));
        catVT->setExpanded(false);
        makeCategoryBold(catVT);
        QBrush entryBrush(Qt::darkGray);
        for (const auto &vt : file->valueTables) {
            auto *vtItem = new QTreeWidgetItem(catVT, {vt.name});
            vtItem->setData(0, RoleNodeType, static_cast<int>(NodeType::ValueTable));
            vtItem->setData(0, RoleName, vt.name);
            vtItem->setExpanded(false);
            // 值表条目作为子项（对齐 CANdb++）
            for (const auto &entry : vt.entries) {
                auto *entryItem = new QTreeWidgetItem(vtItem,
                    {QString("%1 = %2").arg(entry.value).arg(entry.description)});
                entryItem->setData(0, RoleNodeType, static_cast<int>(NodeType::ValueTable));
                entryItem->setData(0, RoleName, vt.name);
                // 斜体灰色
                QFont ef = entryItem->font(0);
                ef.setItalic(true);
                entryItem->setFont(0, ef);
                entryItem->setForeground(0, entryBrush);
            }
        }
    }
}

// ============================================================
//  搜索过滤
// ============================================================

static bool filterTreeItem(QTreeWidgetItem *item, const QString &text)
{
    if (text.isEmpty()) {
        item->setHidden(false);
        for (int i = 0; i < item->childCount(); ++i)
            filterTreeItem(item->child(i), text);
        return true;
    }

    bool selfMatch = item->text(0).contains(text, Qt::CaseInsensitive);
    bool anyChildMatch = false;
    for (int i = 0; i < item->childCount(); ++i) {
        if (filterTreeItem(item->child(i), text))
            anyChildMatch = true;
    }
    item->setHidden(!selfMatch && !anyChildMatch);
    // 搜索时自动展开包含匹配子项的节点
    if (anyChildMatch)
        item->setExpanded(true);
    return selfMatch || anyChildMatch;
}

void DbcDetailTab::onSearchChanged(const QString &text)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        filterTreeItem(m_tree->topLevelItem(i), text);
}

// ============================================================
//  树点击 → 切换右侧详情
// ============================================================

void DbcDetailTab::onTreeItemClicked(QTreeWidgetItem *item, int)
{
    if (!item) return;

    auto typeVal = item->data(0, RoleNodeType);
    if (!typeVal.isValid()) return;

    NodeType type = static_cast<NodeType>(typeVal.toInt());

    switch (type) {
    case NodeType::Message: {
        quint32 canId = item->data(0, RoleCanId).toUInt();
        showMessageDetail(canId);
        break;
    }
    case NodeType::Signal: {
        quint32 canId = item->data(0, RoleCanId).toUInt();
        QString sigName = item->data(0, RoleName).toString();
        showSignalDetail(canId, sigName);
        break;
    }
    case NodeType::Node: {
        QString nodeName = item->data(0, RoleName).toString();
        showNodeDetail(nodeName);
        break;
    }
    case NodeType::ValueTable: {
        QString vtName = item->data(0, RoleName).toString();
        showValueTableDetail(vtName);
        break;
    }
    default:
        showPlaceholder();
        break;
    }
}

void DbcDetailTab::onTreeItemDoubleClicked(QTreeWidgetItem *item, int)
{
    if (!item) return;
    auto typeVal = item->data(0, RoleNodeType);
    if (!typeVal.isValid()) return;

    NodeType type = static_cast<NodeType>(typeVal.toInt());
    if (type == NodeType::Signal) {
        quint32 canId = item->data(0, RoleCanId).toUInt();
        QString sigName = item->data(0, RoleName).toString();
        emit signalDoubleClicked(canId, sigName);
    }
}

void DbcDetailTab::onTreeContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    if (!item) return;

    auto typeVal = item->data(0, RoleNodeType);
    if (!typeVal.isValid()) return;
    NodeType type = static_cast<NodeType>(typeVal.toInt());

    // 信号节点和报文节点都可以提供“添加”菜单
    bool isSignal = (type == NodeType::Signal);
    bool isMessage = (type == NodeType::Message);
    if (!isSignal && !isMessage) return;

    quint32 canId = item->data(0, RoleCanId).toUInt();
    QString sigName = item->data(0, RoleName).toString();

    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    if (isSignal) {
        auto *actGraphic = menu->addAction(QStringLiteral(" 添加到 Graphic"));
        auto *actTrace = menu->addAction(QStringLiteral(" 添加到 Trace"));
        menu->addSeparator();
        auto *actDetail = menu->addAction(QStringLiteral("查看信号详情"));

        connect(actGraphic, &QAction::triggered, this, [this, canId, sigName]() {
            emit signalAddToGraphic(canId, sigName);
        });
        connect(actTrace, &QAction::triggered, this, [this, canId, sigName]() {
            emit signalAddToTrace(canId, sigName);
        });
        connect(actDetail, &QAction::triggered, this, [this, canId, sigName]() {
            showSignalDetail(canId, sigName);
        });
    } else {
        // Message: 添加该报文下所有信号
        auto *actGraphic = menu->addAction(QStringLiteral(" 添加全部信号到 Graphic"));
        auto *actTrace = menu->addAction(QStringLiteral(" 添加全部信号到 Trace"));

        connect(actGraphic, &QAction::triggered, this, [this, canId]() {
            const DbcFile *file = currentDbcFile();
            if (!file) return;
            const DbcMessage *msg = file->findMessage(canId);
            if (!msg) return;
            for (const auto &sig : msg->signalList)
                emit signalAddToGraphic(canId, sig.name);
        });
        connect(actTrace, &QAction::triggered, this, [this, canId]() {
            const DbcFile *file = currentDbcFile();
            if (!file) return;
            const DbcMessage *msg = file->findMessage(canId);
            if (!msg) return;
            for (const auto &sig : msg->signalList)
                emit signalAddToTrace(canId, sig.name);
        });
    }

    menu->popup(m_tree->viewport()->mapToGlobal(pos));
}

// ============================================================
//  详情显示 — Message
// ============================================================

void DbcDetailTab::showMessageDetail(quint32 canId)
{
    const DbcFile *file = currentDbcFile();
    if (!file) return;
    const DbcMessage *msg = file->findMessage(canId);
    if (!msg) return;

    m_msgTitleLabel->setText(QString("Message: %1  (0x%2)")
        .arg(msg->name).arg(msg->id, 0, 16).toUpper());

    QStringList info;
    info << QString("ID: 0x%1 (%2)").arg(msg->id, 0, 16).toUpper().arg(msg->id);
    info << QString("DLC: %1").arg(msg->dlc);
    info << QString("Sender: %1").arg(msg->sender);
    if (msg->cycleTime > 0)
        info << QString("Cycle Time: %1 ms").arg(msg->cycleTime);
    if (!msg->sendType.isEmpty())
        info << QString("Send Type: %1").arg(msg->sendType);
    if (!msg->txNodes.isEmpty())
        info << QString("TX Nodes: %1").arg(msg->txNodes.join(", "));
    m_msgInfoLabel->setText(info.join("    |    "));

    if (!msg->comment.isEmpty()) {
        m_msgCommentLabel->setText("Comment: " + msg->comment);
        m_msgCommentLabel->setVisible(true);
    } else {
        m_msgCommentLabel->setVisible(false);
    }

    // 信号表格
    m_msgSigTable->setRowCount(msg->signalList.size());
    for (int i = 0; i < msg->signalList.size(); ++i) {
        const auto &sig = msg->signalList[i];
        m_msgSigTable->setItem(i, 0, new QTableWidgetItem(sig.name));
        m_msgSigTable->setItem(i, 1, new QTableWidgetItem(QString::number(sig.startBit)));
        m_msgSigTable->setItem(i, 2, new QTableWidgetItem(QString::number(sig.bitLength)));
        m_msgSigTable->setItem(i, 3, new QTableWidgetItem(sig.littleEndian ? "Intel" : "Motorola"));
        m_msgSigTable->setItem(i, 4, new QTableWidgetItem(sig.isSigned ? "Signed" : "Unsigned"));
        m_msgSigTable->setItem(i, 5, new QTableWidgetItem(QString::number(sig.factor)));
        m_msgSigTable->setItem(i, 6, new QTableWidgetItem(QString::number(sig.offset)));
        m_msgSigTable->setItem(i, 7, new QTableWidgetItem(
            QString("[%1, %2]").arg(sig.minimum).arg(sig.maximum)));
        m_msgSigTable->setItem(i, 8, new QTableWidgetItem(sig.unit));
    }

    m_detailStack->setCurrentIndex(m_pageMessage);
}

// ============================================================
//  详情显示 — Signal
// ============================================================

void DbcDetailTab::showSignalDetail(quint32 canId, const QString &sigName)
{
    const DbcFile *file = currentDbcFile();
    if (!file) return;
    const DbcMessage *msg = file->findMessage(canId);
    if (!msg) return;
    const DbcSignal *sig = msg->findSignal(sigName);
    if (!sig) return;

    m_sigTitleLabel->setText(QString("Signal: %1  (Message: %2, 0x%3)")
        .arg(sig->name).arg(msg->name).arg(msg->id, 0, 16).toUpper());

    // 属性表
    auto addRow = [&](const QString &name, const QString &value) {
        int row = m_sigPropTable->rowCount();
        m_sigPropTable->insertRow(row);
        m_sigPropTable->setItem(row, 0, new QTableWidgetItem(name));
        m_sigPropTable->setItem(row, 1, new QTableWidgetItem(value));
    };

    m_sigPropTable->setRowCount(0);
    addRow("Name", sig->name);
    addRow("Start Bit", QString::number(sig->startBit));
    addRow("Bit Length", QString::number(sig->bitLength));
    addRow("Byte Order", sig->littleEndian ? "Intel (Little Endian)" : "Motorola (Big Endian)");
    addRow("Value Type", sig->isSigned ? "Signed" : "Unsigned");
    addRow("Factor", QString::number(sig->factor));
    addRow("Offset", QString::number(sig->offset));
    addRow("Minimum", QString::number(sig->minimum));
    addRow("Maximum", QString::number(sig->maximum));
    addRow("Unit", sig->unit.isEmpty() ? "(none)" : sig->unit);
    addRow("Receiver", sig->receiver);
    addRow("Sender", msg->sender);

    // 多路复用
    if (sig->muxType == DbcSignal::MuxType::Multiplexor)
        addRow("Multiplexing", "Multiplexor");
    else if (sig->muxType == DbcSignal::MuxType::Multiplexed)
        addRow("Multiplexing", QString("Multiplexed (value=%1)").arg(sig->muxValue));
    else
        addRow("Multiplexing", "None");

    // 值表名称
    if (!sig->valueTableName.isEmpty())
        addRow("Value Table", sig->valueTableName);

    // 注释
    if (!sig->comment.isEmpty()) {
        m_sigCommentLabel->setText("Comment: " + sig->comment);
        m_sigCommentLabel->setVisible(true);
    } else {
        m_sigCommentLabel->setVisible(false);
    }

    // 值表
    auto *vtGroup = m_sigValueTable->parentWidget();
    if (!sig->valueTable.isEmpty()) {
        m_sigValueTable->setRowCount(sig->valueTable.size());
        for (int i = 0; i < sig->valueTable.size(); ++i) {
            m_sigValueTable->setItem(i, 0, new QTableWidgetItem(QString::number(sig->valueTable[i].value)));
            m_sigValueTable->setItem(i, 1, new QTableWidgetItem(sig->valueTable[i].description));
        }
        vtGroup->setVisible(true);
    } else {
        vtGroup->setVisible(false);
    }

    m_detailStack->setCurrentIndex(m_pageSignal);
}

// ============================================================
//  详情显示 — Node
// ============================================================

void DbcDetailTab::showNodeDetail(const QString &nodeName)
{
    const DbcFile *file = currentDbcFile();
    if (!file) return;
    const DbcNode *node = file->findNode(nodeName);
    if (!node) return;

    m_nodeTitleLabel->setText(QString("Node: %1").arg(node->name));

    if (!node->comment.isEmpty()) {
        m_nodeCommentLabel->setText("Comment: " + node->comment);
        m_nodeCommentLabel->setVisible(true);
    } else {
        m_nodeCommentLabel->setVisible(false);
    }

    // TX 报文
    m_nodeTxTable->setRowCount(node->txMessageIds.size());
    for (int i = 0; i < node->txMessageIds.size(); ++i) {
        quint32 id = node->txMessageIds[i];
        const DbcMessage *msg = file->findMessage(id);
        m_nodeTxTable->setItem(i, 0, new QTableWidgetItem(QString("0x%1").arg(id, 0, 16).toUpper()));
        m_nodeTxTable->setItem(i, 1, new QTableWidgetItem(msg ? msg->name : "?"));
        m_nodeTxTable->setItem(i, 2, new QTableWidgetItem(msg ? QString::number(msg->dlc) : "?"));
    }

    // RX 信号
    m_nodeRxTable->setRowCount(node->rxSignals.size());
    for (int i = 0; i < node->rxSignals.size(); ++i) {
        quint32 id = node->rxSignals[i].first;
        const QString &sigName = node->rxSignals[i].second;
        const DbcMessage *msg = file->findMessage(id);
        m_nodeRxTable->setItem(i, 0, new QTableWidgetItem(QString("0x%1").arg(id, 0, 16).toUpper()));
        m_nodeRxTable->setItem(i, 1, new QTableWidgetItem(sigName));
        m_nodeRxTable->setItem(i, 2, new QTableWidgetItem(msg ? msg->name : "?"));
    }

    m_detailStack->setCurrentIndex(m_pageNode);
}

// ============================================================
//  详情显示 — ValueTable
// ============================================================

void DbcDetailTab::showValueTableDetail(const QString &vtName)
{
    const DbcFile *file = currentDbcFile();
    if (!file) return;
    const DbcValueTable *vt = file->findValueTable(vtName);
    if (!vt) return;

    m_vtTitleLabel->setText(QString("Value Table: %1").arg(vt->name));

    m_vtTable->setRowCount(vt->entries.size());
    for (int i = 0; i < vt->entries.size(); ++i) {
        m_vtTable->setItem(i, 0, new QTableWidgetItem(QString::number(vt->entries[i].value)));
        m_vtTable->setItem(i, 1, new QTableWidgetItem(vt->entries[i].description));
    }

    m_detailStack->setCurrentIndex(m_pageValueTable);
}

// ============================================================
//  Placeholder
// ============================================================

void DbcDetailTab::showPlaceholder()
{
    m_detailStack->setCurrentIndex(m_pagePlaceholder);
}
