#include "columnfilterpopup.h"
#include "thememanager.h"
#include "utils/svg_icon.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QApplication>
#include <QScreen>

// ============================================================
//  构造
// ============================================================

ColumnFilterPopup::ColumnFilterPopup(int column, QWidget *parent)
    : QFrame(parent, Qt::Popup)
    , m_column(column)
{
    // 主题样式由全局 QSS 的 #ColumnFilterPopup 规则接管（随主题明暗切换）
    setFrameShape(QFrame::StyledPanel);
    setObjectName(QStringLiteral("ColumnFilterPopup"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    // 搜索框
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索..."));
    m_searchEdit->setClearButtonEnabled(true);
    // 原生清除按钮 × 不随主题（深色下不可见）→ 换主题色 SVG 图标
    applyClearButtonIcon(m_searchEdit, ThemeManager::instance()->currentTheme().text);
    layout->addWidget(m_searchEdit);

    // 全选/清除/反选 按钮行
    auto *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(4);
    m_selectAllBtn = new QPushButton(QStringLiteral("全选"), this);
    m_clearAllBtn = new QPushButton(QStringLiteral("清除"), this);
    m_invertBtn = new QPushButton(QStringLiteral("反选"), this);
    m_selectAllBtn->setFixedHeight(24);
    m_clearAllBtn->setFixedHeight(24);
    m_invertBtn->setFixedHeight(24);
    btnLayout->addWidget(m_selectAllBtn);
    btnLayout->addWidget(m_clearAllBtn);
    btnLayout->addWidget(m_invertBtn);
    layout->addLayout(btnLayout);

    // 值列表
    m_listWidget = new QListWidget(this);
    m_listWidget->setSelectionMode(QAbstractItemView::NoSelection);
    m_listWidget->setFocusPolicy(Qt::NoFocus);
    m_listWidget->setMinimumWidth(220);
    m_listWidget->setMaximumHeight(300);
    layout->addWidget(m_listWidget);

    // 底部按钮行
    auto *bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(4);
    auto *clearFilterBtn = new QPushButton(QStringLiteral("清除筛选"), this);
    auto *okBtn = new QPushButton(QStringLiteral("确定"), this);
    okBtn->setDefault(true);
    clearFilterBtn->setFixedHeight(26);
    okBtn->setFixedHeight(26);
    bottomLayout->addWidget(clearFilterBtn);
    bottomLayout->addStretch();
    bottomLayout->addWidget(okBtn);
    layout->addLayout(bottomLayout);

    // 信号连接
    connect(m_searchEdit, &QLineEdit::textChanged, this, &ColumnFilterPopup::onSearchChanged);
    connect(m_selectAllBtn, &QPushButton::clicked, this, &ColumnFilterPopup::onSelectAll);
    connect(m_clearAllBtn, &QPushButton::clicked, this, &ColumnFilterPopup::onClearAll);
    connect(m_invertBtn, &QPushButton::clicked, this, &ColumnFilterPopup::onInvert);
    connect(m_listWidget, &QListWidget::itemChanged, this, &ColumnFilterPopup::onItemChanged);
    connect(okBtn, &QPushButton::clicked, this, &ColumnFilterPopup::onOk);
    connect(clearFilterBtn, &QPushButton::clicked, this, &ColumnFilterPopup::onClearFilter);
}

void ColumnFilterPopup::setValues(const QList<ValueItem> &values)
{
    m_allValues = values;
    // 默认全选
    m_selectedValues.clear();
    for (const auto &v : values)
        m_selectedValues.insert(v.text);
    buildList();
    updateSelectAllState();
}

void ColumnFilterPopup::setSelectedValues(const QSet<QString> &selected)
{
    m_selectedValues = selected;
    buildList(m_searchEdit->text());
    updateSelectAllState();
}

QSet<QString> ColumnFilterPopup::selectedValues() const
{
    return m_selectedValues;
}

bool ColumnFilterPopup::isAllSelected() const
{
    return m_selectedValues.size() >= m_allValues.size();
}

// ============================================================
//  列表构建与交互
// ============================================================

void ColumnFilterPopup::buildList(const QString &filter)
{
    // 阻止 itemChanged 信号触发
    m_listWidget->blockSignals(true);
    m_listWidget->clear();

    QString lowerFilter = filter.toLower();

    for (const auto &v : m_allValues) {
        if (!lowerFilter.isEmpty() && !v.text.toLower().contains(lowerFilter))
            continue;

        QString label = v.text;
        if (v.count > 0)
            label += QStringLiteral("  (%1)").arg(v.count);

        auto *item = new QListWidgetItem(label, m_listWidget);
        item->setCheckState(m_selectedValues.contains(v.text) ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, v.text);  // 存储原始值文本
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    }

    m_listWidget->blockSignals(false);
}

void ColumnFilterPopup::updateSelectAllState()
{
    bool allChecked = (m_selectedValues.size() >= m_allValues.size());
    m_selectAllBtn->setEnabled(!allChecked);
    m_clearAllBtn->setEnabled(!m_selectedValues.isEmpty());
}

void ColumnFilterPopup::onSearchChanged(const QString &text)
{
    buildList(text);
}

void ColumnFilterPopup::onSelectAll()
{
    // 全选：对所有当前可见项（受搜索框影响）打勾
    for (int i = 0; i < m_listWidget->count(); ++i) {
        auto *item = m_listWidget->item(i);
        item->setCheckState(Qt::Checked);
        m_selectedValues.insert(item->data(Qt::UserRole).toString());
    }
    updateSelectAllState();
}

void ColumnFilterPopup::onClearAll()
{
    // 清除：对所有当前可见项取消打勾
    for (int i = 0; i < m_listWidget->count(); ++i) {
        auto *item = m_listWidget->item(i);
        item->setCheckState(Qt::Unchecked);
        m_selectedValues.remove(item->data(Qt::UserRole).toString());
    }
    updateSelectAllState();
}

void ColumnFilterPopup::onInvert()
{
    // 反选：对所有当前可见项切换勾选状态
    for (int i = 0; i < m_listWidget->count(); ++i) {
        auto *item = m_listWidget->item(i);
        QString val = item->data(Qt::UserRole).toString();
        if (item->checkState() == Qt::Checked) {
            item->setCheckState(Qt::Unchecked);
            m_selectedValues.remove(val);
        } else {
            item->setCheckState(Qt::Checked);
            m_selectedValues.insert(val);
        }
    }
    updateSelectAllState();
}

void ColumnFilterPopup::onItemChanged(QListWidgetItem *item)
{
    if (!item)
        return;
    QString val = item->data(Qt::UserRole).toString();
    if (item->checkState() == Qt::Checked)
        m_selectedValues.insert(val);
    else
        m_selectedValues.remove(val);
    updateSelectAllState();
}

void ColumnFilterPopup::onOk()
{
    emit filterApplied(m_column, m_selectedValues);
    close();
}

void ColumnFilterPopup::onClearFilter()
{
    m_selectedValues.clear();
    for (const auto &v : m_allValues)
        m_selectedValues.insert(v.text);
    emit filterCleared(m_column);
    close();
}
