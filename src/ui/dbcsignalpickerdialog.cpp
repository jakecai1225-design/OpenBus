#include "dbcsignalpickerdialog.h"
#include "core/dbcmanager.h"
#include "ui/thememanager.h"
#include "utils/canutils.h"
#include "utils/svg_icon.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <QPushButton>
#include <QToolButton>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QTimer>
#include <QSet>

// ============================================================
//  item 角色约定（三级树行的身份数据）
// ============================================================
namespace {
enum ItemRole {
    RoleType = Qt::UserRole + 1,   ///< 0=文件行 1=报文行 2=信号行
    RoleFileName,                  ///< 来源 DBC 文件名（三种行均带）
    RoleCanId,                     ///< 报文/信号行
    RoleSignalName,                ///< 信号行
    RoleMessageName                ///< 报文行 + 信号行（输出提示用）
};
} // namespace

// ============================================================

DbcSignalPickerDialog::DbcSignalPickerDialog(DbcManager *mgr, const QString &title,
                                             QWidget *parent)
    : QDialog(parent), m_mgr(mgr)
{
    setWindowTitle(title.isEmpty() ? QStringLiteral("添加信号到 Graphic") : title);
    setMinimumSize(520, 520);

    auto *lay = new QVBoxLayout(this);
    const auto &th = ThemeManager::instance()->currentTheme();
    const QString iconColor = th.text;

    auto *searchRow = new QHBoxLayout;
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(
        QStringLiteral("搜索信号 / 报文名（跨全部已加载 DBC 数据库）…"));
    applyExplorerSearch(m_searchEdit, th.text, th.textDim);
    searchRow->addWidget(m_searchEdit, 1);

    auto *expandBtn = new QToolButton(this);
    expandBtn->setIcon(svgIcon(":/icons/chevron-down.svg", iconColor, 14));
    expandBtn->setToolTip(QStringLiteral("全部展开"));
    auto *collapseBtn = new QToolButton(this);
    collapseBtn->setIcon(svgIcon(":/icons/chevron-up.svg", iconColor, 14));
    collapseBtn->setToolTip(QStringLiteral("全部收拢"));
    searchRow->addWidget(expandBtn);
    searchRow->addWidget(collapseBtn);
    lay->addLayout(searchRow);

    // ---- 中部：三级树（文件 → 报文 → 信号）----
    m_tree = new QTreeWidget(this);
    applyExplorerTree(m_tree, QStringLiteral("ContentTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    lay->addWidget(m_tree, 1);

    // 空态提示（无数据库 / 搜索无命中时显示）
    m_emptyLabel = new QLabel(this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QStringLiteral("color:#888;padding:24px;"));
    m_emptyLabel->hide();
    lay->addWidget(m_emptyLabel);

    // ---- 底部：已选计数 + 按钮 ----
    m_countLabel = new QLabel(this);
    lay->addWidget(m_countLabel);

    auto *btnBox = new QDialogButtonBox(this);
    m_addBtn = btnBox->addButton(QStringLiteral("添加"), QDialogButtonBox::AcceptRole);
    m_addBtn->setEnabled(false);
    m_addBtn->setDefault(true);
    auto *cancelBtn = btnBox->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
    lay->addWidget(btnBox);

    // ---- 信号连接 ----
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(250);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &DbcSignalPickerDialog::scheduleRebuild);
    connect(m_debounce, &QTimer::timeout, this, &DbcSignalPickerDialog::rebuildTree);
    connect(expandBtn, &QToolButton::clicked, m_tree, &QTreeWidget::expandAll);
    connect(collapseBtn, &QToolButton::clicked, m_tree, &QTreeWidget::collapseAll);
    connect(m_tree, &QTreeWidget::itemSelectionChanged,
            this, &DbcSignalPickerDialog::updateCountLabel);
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &DbcSignalPickerDialog::onItemDoubleClicked);
    connect(m_addBtn, &QPushButton::clicked, this, &DbcSignalPickerDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    // 对话框打开期间数据库加载/卸载 → 重灌（结果解析按文件名+ID 回查，
    // 不缓存指针，卸载中途的选择在 accept 时跳过）
    if (m_mgr) {
        connect(m_mgr, &DbcManager::dbcLoaded, this, [this]() { rebuildTree(); });
        connect(m_mgr, &DbcManager::dbcUnloaded, this, [this]() { rebuildTree(); });
    }

    rebuildTree();
}

// ============================================================
//  树构建 / 搜索过滤
// ============================================================

void DbcSignalPickerDialog::scheduleRebuild()
{
    m_debounce->start();
}

void DbcSignalPickerDialog::rebuildTree()
{
    m_tree->clear();

    const QString filter = m_searchEdit->text().trimmed();
    const bool filtering = !filter.isEmpty();
    const QString dimColor = ThemeManager::instance()->currentTheme().textDim;

    const bool hasFiles = m_mgr && !m_mgr->files().isEmpty();
    int visibleRows = 0;   // 树中报文行计数（空态判断用）

    if (hasFiles) {
        for (const auto &file : m_mgr->files()) {
            auto *fileItem = new QTreeWidgetItem(m_tree);
            fileItem->setText(0, QStringLiteral("%1（%2 报文）")
                                      .arg(file.fileName).arg(file.messages.size()));
            fileItem->setIcon(0, svgIcon(":/icons/database.svg", dimColor));
            // 文件行仅导航：不可选中（避免整库误加），点击仍可展开/收拢
            fileItem->setFlags(Qt::ItemIsEnabled);
            fileItem->setData(0, RoleType, 0);
            fileItem->setData(0, RoleFileName, file.fileName);
            fileItem->setToolTip(0, file.filePath);

            int msgShown = 0;
            for (const auto &msg : file.messages) {
                // 报文名命中 → 保留全部信号；否则仅保留信号名命中的行
                const bool msgNameHit = msg.name.contains(filter, Qt::CaseInsensitive);
                QList<const DbcSignal *> sigs;
                if (!filtering || msgNameHit) {
                    for (const auto &s : msg.signalList)
                        sigs.append(&s);
                } else {
                    for (const auto &s : msg.signalList)
                        if (s.name.contains(filter, Qt::CaseInsensitive))
                            sigs.append(&s);
                }
                if (sigs.isEmpty())
                    continue;

                auto *msgItem = new QTreeWidgetItem(fileItem);
                msgItem->setText(0, QStringLiteral("%1 %2（%3 信号）")
                                          .arg(CanUtils::formatId(msg.id, msg.id > 0x7FF),
                                               msg.name)
                                          .arg(msg.signalList.size()));
                msgItem->setIcon(0, svgIcon(":/icons/list.svg", dimColor));
                msgItem->setToolTip(0, QStringLiteral("选中报文 = 添加其全部 %1 个信号")
                                              .arg(msg.signalList.size()));
                msgItem->setData(0, RoleType, 1);
                msgItem->setData(0, RoleFileName, file.fileName);
                msgItem->setData(0, RoleCanId, msg.id);
                msgItem->setData(0, RoleMessageName, msg.name);

                for (const DbcSignal *s : sigs) {
                    auto *sigItem = new QTreeWidgetItem(msgItem);
                    QString text = s->name;
                    if (!s->unit.isEmpty())
                        text += QStringLiteral(" [%1]").arg(s->unit);
                    sigItem->setText(0, text);
                    sigItem->setIcon(0, svgIcon(":/icons/graphic.svg", dimColor));
                    sigItem->setToolTip(0, QStringLiteral("%1 · %2\n起始位 %3 · 长度 %4 · 因子 %5 · 偏移 %6")
                                                .arg(file.fileName, msg.name)
                                                .arg(s->startBit).arg(s->bitLength)
                                                .arg(s->factor).arg(s->offset));
                    sigItem->setData(0, RoleType, 2);
                    sigItem->setData(0, RoleFileName, file.fileName);
                    sigItem->setData(0, RoleCanId, msg.id);
                    sigItem->setData(0, RoleSignalName, s->name);
                    sigItem->setData(0, RoleMessageName, msg.name);
                }
                ++msgShown;
            }

            // 过滤态下整库无命中 → 移除文件行
            if (filtering && msgShown == 0) {
                delete fileItem;
                continue;
            }
            visibleRows += msgShown;

            // 展开策略：过滤态全部展开（直接看到命中行）；
            // 非过滤态展开文件层（报文可见，信号层收拢）
            fileItem->setExpanded(filtering);
        }
        if (filtering)
            m_tree->expandAll();
    }

    // 空态切换：无数据库 / 搜索无命中
    if (!hasFiles) {
        m_emptyLabel->setText(
            QStringLiteral("尚未加载数据库 — 请先在 DBC 面板加载数据库文件"));
        m_emptyLabel->show();
        m_tree->hide();
    } else if (visibleRows == 0) {
        m_emptyLabel->setText(QStringLiteral("无匹配的信号 / 报文"));
        m_emptyLabel->show();
        m_tree->hide();
    } else {
        m_emptyLabel->hide();
        m_tree->show();
    }

    updateCountLabel();
}

// ============================================================
//  选择计数 / 结果收集
// ============================================================

int DbcSignalPickerDialog::effectiveSignalCount() const
{
    int n = 0;
    const auto sel = m_tree->selectedItems();
    for (const auto *it : sel) {
        const int type = it->data(0, RoleType).toInt();
        if (type == 1)
            n += it->childCount();      // 报文行 = 全部子信号
        else if (type == 2)
            ++n;                         // 信号行
        // 文件行不可选，不入选择集
    }
    return n;
}

void DbcSignalPickerDialog::updateCountLabel()
{
    const int n = effectiveSignalCount();
    if (n > 0)
        m_countLabel->setText(QStringLiteral("已选 %1 个信号").arg(n));
    else
        m_countLabel->setText(
            QStringLiteral("未选择 — Ctrl/Shift 多选；选中报文行 = 添加其全部信号"));
    m_addBtn->setEnabled(n > 0);
}

void DbcSignalPickerDialog::onItemDoubleClicked(QTreeWidgetItem *item, int /*column*/)
{
    // 双击报文/信号行 = 直接确认添加；文件行双击保持默认展开行为
    const int type = item ? item->data(0, RoleType).toInt() : -1;
    if (type == 1 || type == 2)
        accept();
}

void DbcSignalPickerDialog::pickSignalItem(QTreeWidgetItem *sigItem, QSet<QString> &seen)
{
    // 角色数据 → 回查当前 DbcManager（对话框期间卸载的库自动跳过）
    const QString fileName = sigItem->data(0, RoleFileName).toString();
    const quint32 canId = sigItem->data(0, RoleCanId).toUInt();
    const QString sigName = sigItem->data(0, RoleSignalName).toString();

    const QString key = QStringLiteral("%1|%2|%3").arg(fileName).arg(canId).arg(sigName);
    if (seen.contains(key))
        return;   // 报文行与子信号行同选时去重
    seen.insert(key);

    const DbcFile *file = m_mgr ? m_mgr->findFile(fileName) : nullptr;
    const DbcMessage *msg = file ? file->findMessage(canId) : nullptr;
    const DbcSignal *sig = msg ? msg->findSignal(sigName) : nullptr;
    if (!sig)
        return;

    PickedSignal p;
    p.fileName = fileName;
    p.messageName = msg->name;
    p.canId = msg->id;
    p.extended = msg->id > 0x7FF;   // 与 onSignalDoubleClicked 的扩展帧判定一致
    p.signal = *sig;
    m_picked.append(p);
}

void DbcSignalPickerDialog::collectSelection()
{
    m_picked.clear();
    QSet<QString> seen;
    const auto sel = m_tree->selectedItems();
    for (auto *it : sel) {
        const int type = it->data(0, RoleType).toInt();
        if (type == 1) {
            for (int c = 0; c < it->childCount(); ++c)
                pickSignalItem(it->child(c), seen);
        } else if (type == 2) {
            pickSignalItem(it, seen);
        }
    }
}

void DbcSignalPickerDialog::accept()
{
    collectSelection();
    if (m_picked.isEmpty())
        return;   // 零选择不关窗（按钮已禁用；双击空树兜底保护）
    QDialog::accept();
}
