#include "dbcdetailtab.h"
#include "core/dbcmanager.h"

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

    // ---- 左侧: 搜索 + 树 ----
    auto *leftWidget = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("搜索信号...");
    m_searchEdit->setContentsMargins(4, 4, 4, 4);
    leftLayout->addWidget(m_searchEdit);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(16);
    leftLayout->addWidget(m_tree, 1);

    splitter->addWidget(leftWidget);

    // ---- 右侧: 详情 ----
    auto *rightWidget = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(8, 8, 8, 8);
    rightLayout->setSpacing(8);

    // 网络信息
    auto *netGroup = new QGroupBox("网络信息", rightWidget);
    auto *netLayout = new QVBoxLayout(netGroup);
    m_msgTitleLabel = new QLabel("(点击左侧 Message 查看详情)", netGroup);
    m_msgInfoLabel = new QLabel("", netGroup);
    netLayout->addWidget(m_msgTitleLabel);
    netLayout->addWidget(m_msgInfoLabel);
    rightLayout->addWidget(netGroup);

    // 信号表格
    auto *sigGroup = new QGroupBox("Signals", rightWidget);
    auto *sigLayout = new QVBoxLayout(sigGroup);
    m_sigTable = new QTableWidget(0, 7, sigGroup);
    m_sigTable->setHorizontalHeaderLabels({"名称", "起始位", "长度", "因子", "偏移", "最小值", "最大值"});
    m_sigTable->verticalHeader()->setVisible(false);
    m_sigTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_sigTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 7; ++i)
        m_sigTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    sigLayout->addWidget(m_sigTable);
    rightLayout->addWidget(sigGroup, 1);

    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 7);

    mainLayout->addWidget(splitter, 1);

    // 信号连接
    connect(m_searchEdit, &QLineEdit::textChanged, this, &DbcDetailTab::onSearchChanged);
    connect(m_tree, &QTreeWidget::itemClicked, this, &DbcDetailTab::onTreeItemClicked);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &DbcDetailTab::onTreeItemDoubleClicked);

    refreshTree();
}

void DbcDetailTab::refreshTree()
{
    m_tree->clear();
    if (!m_dbcMgr) return;

    for (const auto &file : m_dbcMgr->files()) {
        if (file.fileName != m_dbcFileName) continue;

        // 网络
        auto *netItem = new QTreeWidgetItem(m_tree, {"网络: " + file.fileName});
        netItem->setExpanded(true);

        for (const auto &msg : file.messages) {
            QString msgText = QString("Msg_0x%1 (%2)")
                .arg(msg.id, 0, 16).toUpper()
                .arg(msg.name);
            auto *msgItem = new QTreeWidgetItem(netItem, {msgText});
            msgItem->setData(0, Qt::UserRole, msg.id);

            for (const auto &sig : msg.signalList) {
                auto *sigItem = new QTreeWidgetItem(msgItem, {sig.name});
                sigItem->setData(0, Qt::UserRole + 0, msg.id);
                sigItem->setData(0, Qt::UserRole + 1, sig.name);
            }
        }
    }
}

void DbcDetailTab::onSearchChanged(const QString &text)
{
    // 隐藏不匹配的信号项
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *top = m_tree->topLevelItem(i);
        for (int j = 0; j < top->childCount(); ++j) {
            auto *msg = top->child(j);
            bool msgMatch = msg->text(0).contains(text, Qt::CaseInsensitive);
            for (int k = 0; k < msg->childCount(); ++k) {
                auto *sig = msg->child(k);
                bool sigMatch = sig->text(0).contains(text, Qt::CaseInsensitive);
                sig->setHidden(!text.isEmpty() && !sigMatch && !msgMatch);
            }
        }
    }
}

void DbcDetailTab::onTreeItemClicked(QTreeWidgetItem *item, int)
{
    if (!item) return;
    // 检查是否是 Message 级别
    QVariant idVar = item->data(0, Qt::UserRole);
    if (idVar.isValid()) {
        quint32 canId = idVar.toUInt();
        // 检查是否有子项 (Message 有 signal 子项)
        if (item->childCount() > 0) {
            showMessageDetail(canId);
        }
    }
}

void DbcDetailTab::onTreeItemDoubleClicked(QTreeWidgetItem *item, int)
{
    if (!item) return;
    QString sigName = item->data(0, Qt::UserRole + 1).toString();
    if (!sigName.isEmpty()) {
        quint32 canId = item->data(0, Qt::UserRole + 0).toUInt();
        emit signalDoubleClicked(canId, sigName);
    }
}

void DbcDetailTab::showMessageDetail(quint32 canId)
{
    if (!m_dbcMgr) return;

    const DbcMessage *msg = m_dbcMgr->findMessage(canId);
    if (!msg) return;

    m_msgTitleLabel->setText(QString("Message: %1 (0x%2)")
        .arg(msg->name).arg(msg->id, 0, 16).toUpper());
    m_msgInfoLabel->setText(QString("DLC: %1   发送节点: %2")
        .arg(msg->dlc).arg(msg->sender));

    m_sigTable->setRowCount(msg->signalList.size());
    for (int i = 0; i < msg->signalList.size(); ++i) {
        const auto &sig = msg->signalList[i];
        m_sigTable->setItem(i, 0, new QTableWidgetItem(sig.name));
        m_sigTable->setItem(i, 1, new QTableWidgetItem(QString::number(sig.startBit)));
        m_sigTable->setItem(i, 2, new QTableWidgetItem(QString::number(sig.bitLength)));
        m_sigTable->setItem(i, 3, new QTableWidgetItem(QString::number(sig.factor)));
        m_sigTable->setItem(i, 4, new QTableWidgetItem(QString::number(sig.offset)));
        m_sigTable->setItem(i, 5, new QTableWidgetItem(QString::number(sig.minimum)));
        m_sigTable->setItem(i, 6, new QTableWidgetItem(QString::number(sig.maximum)));
    }
}
