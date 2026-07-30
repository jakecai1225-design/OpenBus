#include "dbcimportdialog.h"
#include "core/dbcmanager.h"
#include "core/dbcdata.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QButtonGroup>
#include <QLabel>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QCheckBox>

// ============================================================
//  Tree item roles
// ============================================================
static constexpr int RoleCanId   = Qt::UserRole + 1;
static constexpr int RoleIsMsg   = Qt::UserRole + 2;

DbcImportDialog::DbcImportDialog(DbcManager *dbcManager, QWidget *parent)
    : QDialog(parent)
    , m_dbcManager(dbcManager)
{
    setWindowTitle("从 DBC 导入报文");
    setMinimumSize(600, 500);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // ---- 搜索栏 ----
    auto *searchLayout = new QHBoxLayout;
    searchLayout->setSpacing(6);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("输入 CAN ID (如 0x123) 或信号名进行搜索...");
    m_searchEdit->setClearButtonEnabled(true);

    m_searchByIdBtn = new QRadioButton("按 ID", this);
    m_searchBySignalBtn = new QRadioButton("按信号", this);
    m_searchByIdBtn->setChecked(true);

    auto *searchGroup = new QButtonGroup(this);
    searchGroup->addButton(m_searchByIdBtn);
    searchGroup->addButton(m_searchBySignalBtn);

    auto *importFileBtn = new QPushButton("从文件导入...", this);

    searchLayout->addWidget(new QLabel("搜索:", this));
    searchLayout->addWidget(m_searchEdit, 1);
    searchLayout->addWidget(m_searchByIdBtn);
    searchLayout->addWidget(m_searchBySignalBtn);
    searchLayout->addWidget(importFileBtn);
    layout->addLayout(searchLayout);

    // ---- 树 ----
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({"报文 / 信号", "CAN ID", "DLC", "周期(ms)"});
    m_tree->setColumnWidth(0, 280);
    m_tree->setColumnWidth(1, 80);
    m_tree->setColumnWidth(2, 50);
    m_tree->setColumnWidth(3, 80);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(m_tree, 1);

    // ---- 底部按钮 ----
    auto *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    auto *okBtn = new QPushButton("确定", this);
    auto *cancelBtn = new QPushButton("取消", this);
    okBtn->setDefault(true);
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);

    // ---- 填充树 ----
    populateTree();

    // ---- 连接 ----
    connect(m_searchEdit, &QLineEdit::textChanged, this, &DbcImportDialog::onSearchChanged);
    connect(m_searchByIdBtn, &QRadioButton::toggled, this, [this]() {
        onSearchChanged(m_searchEdit->text());
    });
    connect(importFileBtn, &QPushButton::clicked, this, &DbcImportDialog::onImportFromFile);
    connect(okBtn, &QPushButton::clicked, this, [this]() {
        // 收集选中的 Message 级别条目
        m_selectedIds.clear();
        for (auto *item : m_tree->selectedItems()) {
            if (item->data(0, RoleIsMsg).toBool()) {
                quint32 id = item->data(0, RoleCanId).toUInt();
                if (!m_selectedIds.contains(id))
                    m_selectedIds.append(id);
            } else {
                // 如果选中的是信号节点，取其父（Message 节点）的 CAN ID
                auto *parent = item->parent();
                if (parent && parent->data(0, RoleIsMsg).toBool()) {
                    quint32 id = parent->data(0, RoleCanId).toUInt();
                    if (!m_selectedIds.contains(id))
                        m_selectedIds.append(id);
                }
            }
        }
        accept();
    });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void DbcImportDialog::populateTree()
{
    m_tree->clear();
    if (!m_dbcManager)
        return;

    for (const auto &file : m_dbcManager->files()) {
        // DBC 文件节点
        auto *fileItem = new QTreeWidgetItem(m_tree, {file.fileName});
        fileItem->setExpanded(true);
        QFont f = fileItem->font(0);
        f.setBold(true);
        fileItem->setFont(0, f);

        for (const auto &msg : file.messages) {
            QString idStr = QString("0x%1").arg(msg.id, 0, 16).toUpper();
            auto *msgItem = new QTreeWidgetItem(fileItem,
                {QString("%1  %2").arg(idStr).arg(msg.name),
                 idStr,
                 QString::number(msg.dlc),
                 msg.cycleTime > 0 ? QString::number(msg.cycleTime) : "-"});
            msgItem->setData(0, RoleCanId, msg.id);
            msgItem->setData(0, RoleIsMsg, true);
            msgItem->setExpanded(false);

            // 信号子节点
            for (const auto &sig : msg.signalList) {
                QString sigText = sig.name;
                if (!sig.valueTable.isEmpty()) {
                    QStringList vals;
                    for (const auto &vd : sig.valueTable)
                        vals << QString("%1=%2").arg(vd.value).arg(vd.description);
                    sigText += QString("  [%1]").arg(vals.join(", "));
                }
                auto *sigItem = new QTreeWidgetItem(msgItem, {sigText, "", "", ""});
                sigItem->setData(0, RoleIsMsg, false);
            }
        }
    }
}

void DbcImportDialog::onSearchChanged(const QString &text)
{
    bool byId = m_searchByIdBtn->isChecked();
    filterTree(text.trimmed(), byId);
}

void DbcImportDialog::filterTree(const QString &text, bool byId)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *fileItem = m_tree->topLevelItem(i);
        bool anyVisible = false;

        for (int j = 0; j < fileItem->childCount(); ++j) {
            auto *msgItem = fileItem->child(j);
            quint32 canId = msgItem->data(0, RoleCanId).toUInt();
            QString idStr = msgItem->text(1); // "0xNNN"
            QString msgName = msgItem->text(0);

            bool msgMatch = text.isEmpty();
            if (!msgMatch) {
                if (byId) {
                    // 按 CAN ID 搜索
                    msgMatch = idStr.contains(text, Qt::CaseInsensitive) ||
                               msgName.contains(text, Qt::CaseInsensitive);
                } else {
                    // 按信号名搜索 — 检查子节点
                    for (int k = 0; k < msgItem->childCount(); ++k) {
                        if (msgItem->child(k)->text(0).contains(text, Qt::CaseInsensitive)) {
                            msgMatch = true;
                            break;
                        }
                    }
                }
            }

            msgItem->setHidden(!msgMatch);

            // 如果按信号搜索且有匹配，展开报文节点
            if (msgMatch && !byId && !text.isEmpty()) {
                msgItem->setExpanded(true);
                // 隐藏不匹配的信号子节点
                for (int k = 0; k < msgItem->childCount(); ++k) {
                    auto *sigItem = msgItem->child(k);
                    sigItem->setHidden(!sigItem->text(0).contains(text, Qt::CaseInsensitive));
                }
            } else {
                msgItem->setExpanded(false);
                for (int k = 0; k < msgItem->childCount(); ++k) {
                    msgItem->child(k)->setHidden(false);
                }
            }

            if (msgMatch)
                anyVisible = true;
        }

        fileItem->setHidden(!anyVisible);
        if (anyVisible && !text.isEmpty())
            fileItem->setExpanded(true);
    }
}

void DbcImportDialog::onImportFromFile()
{
    QString path = QFileDialog::getOpenFileName(
        this, "选择 DBC 文件", QString(), "DBC 文件 (*.dbc);;所有文件 (*)");
    if (path.isEmpty())
        return;

    if (m_dbcManager->loadDbc(path)) {
        populateTree();
        // 重新应用当前搜索
        onSearchChanged(m_searchEdit->text());
    } else {
        QMessageBox::warning(this, "导入失败", "无法加载 DBC 文件:\n" + path);
    }
}

QList<quint32> DbcImportDialog::selectedCanIds() const
{
    return m_selectedIds;
}
