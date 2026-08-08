#include "colorruleeditor.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QGroupBox>

ColorRuleEditor::ColorRuleEditor(QWidget *parent)
    : QDialog(parent)
    , m_currentBg(QColor(0xFF, 0xEB, 0x3B))  // 黄色默认
    , m_currentFg(Qt::black)
{
    setWindowTitle("着色规则编辑器");
    setMinimumSize(500, 400);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // ---- 规则列表 + 操作按钮 ----
    auto *listGroup = new QGroupBox("规则列表 (从上到下匹配)", this);
    auto *listLayout = new QGridLayout(listGroup);

    m_listWidget = new QListWidget(listGroup);
    m_listWidget->setAlternatingRowColors(true);
    listLayout->addWidget(m_listWidget, 0, 0, 1, 2);

    auto *addBtn = new QPushButton("+ 添加", listGroup);
    auto *removeBtn = new QPushButton("- 删除", listGroup);
    auto *upBtn = new QPushButton("↑ 上移", listGroup);
    auto *downBtn = new QPushButton("↓ 下移", listGroup);

    auto *btnLayout = new QHBoxLayout;
    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(removeBtn);
    btnLayout->addWidget(upBtn);
    btnLayout->addWidget(downBtn);
    btnLayout->addStretch();
    listLayout->addLayout(btnLayout, 1, 0, 1, 2);

    layout->addWidget(listGroup);

    // ---- 当前规则编辑 ----
    auto *editGroup = new QGroupBox("规则编辑", this);
    auto *editLayout = new QGridLayout(editGroup);
    editLayout->setSpacing(6);

    editLayout->addWidget(new QLabel("条件表达式:", editGroup), 0, 0);
    m_exprEdit = new QLineEdit(editGroup);
    m_exprEdit->setPlaceholderText("例: id == 0x123");
    editLayout->addWidget(m_exprEdit, 0, 1, 1, 3);

    editLayout->addWidget(new QLabel("背景色:", editGroup), 1, 0);
    m_bgColorBtn = new QPushButton("选择...", editGroup);
    editLayout->addWidget(m_bgColorBtn, 1, 1);

    editLayout->addWidget(new QLabel("前景色:", editGroup), 1, 2);
    m_fgColorBtn = new QPushButton("选择...", editGroup);
    editLayout->addWidget(m_fgColorBtn, 1, 3);

    m_enabledChk = new QCheckBox("启用", editGroup);
    m_enabledChk->setChecked(true);
    editLayout->addWidget(m_enabledChk, 2, 0, 1, 4);

    layout->addWidget(editGroup);

    // ---- 确定/取消 ----
    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttonBox);

    // ---- 信号连接 ----
    connect(addBtn, &QPushButton::clicked, this, &ColorRuleEditor::onAddRule);
    connect(removeBtn, &QPushButton::clicked, this, &ColorRuleEditor::onRemoveRule);
    connect(upBtn, &QPushButton::clicked, this, &ColorRuleEditor::onMoveUp);
    connect(downBtn, &QPushButton::clicked, this, &ColorRuleEditor::onMoveDown);
    connect(m_listWidget, &QListWidget::currentRowChanged,
            this, &ColorRuleEditor::onSelectionChanged);
    connect(m_bgColorBtn, &QPushButton::clicked, this, [this]() {
        QColor c = QColorDialog::getColor(m_currentBg, this, "选择背景色");
        if (c.isValid()) {
            m_currentBg = c;
            m_bgColorBtn->setStyleSheet(
                QString("background-color: %1;").arg(c.name()));
        }
    });
    connect(m_fgColorBtn, &QPushButton::clicked, this, [this]() {
        QColor c = QColorDialog::getColor(m_currentFg, this, "选择前景色");
        if (c.isValid()) {
            m_currentFg = c;
            m_fgColorBtn->setStyleSheet(
                QString("background-color: %1;").arg(c.name()));
        }
    });
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        applyCurrentEdit();
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // 初始化颜色按钮显示
    m_bgColorBtn->setStyleSheet(
        QString("background-color: %1;").arg(m_currentBg.name()));
    m_fgColorBtn->setStyleSheet(
        QString("background-color: %1;").arg(m_currentFg.name()));
}

void ColorRuleEditor::setRules(const QVector<ColorRule> &rules)
{
    m_listWidget->clear();
    for (const auto &rule : rules) {
        auto *item = new QListWidgetItem(
            QString("[%1] %2").arg(rule.enabled ? "ON" : "OFF", rule.expr));
        item->setBackground(rule.background);
        item->setForeground(rule.foreground);
        // 存储 QVariant: 使用自定义结构
        QVariant v;
        v.setValue(rule);
        item->setData(Qt::UserRole, v);
        m_listWidget->addItem(item);
    }
}

QVector<ColorRuleEditor::ColorRule> ColorRuleEditor::rules() const
{
    QVector<ColorRule> result;
    for (int i = 0; i < m_listWidget->count(); ++i) {
        auto *item = m_listWidget->item(i);
        QVariant v = item->data(Qt::UserRole);
        if (v.canConvert<ColorRule>())
            result.append(v.value<ColorRule>());
    }
    return result;
}

void ColorRuleEditor::onAddRule()
{
    applyCurrentEdit();
    ColorRule rule;
    rule.expr = m_exprEdit->text().trimmed();
    rule.background = m_currentBg;
    rule.foreground = m_currentFg;
    rule.enabled = m_enabledChk->isChecked();

    auto *item = new QListWidgetItem(
        QString("[%1] %2").arg(rule.enabled ? "ON" : "OFF", rule.expr));
    item->setBackground(rule.background);
    item->setForeground(rule.foreground);
    QVariant v;
    v.setValue(rule);
    item->setData(Qt::UserRole, v);
    m_listWidget->addItem(item);
    m_listWidget->setCurrentRow(m_listWidget->count() - 1);
}

void ColorRuleEditor::onRemoveRule()
{
    int row = m_listWidget->currentRow();
    if (row >= 0)
        delete m_listWidget->takeItem(row);
}

void ColorRuleEditor::onMoveUp()
{
    int row = m_listWidget->currentRow();
    if (row > 0) {
        auto *item = m_listWidget->takeItem(row);
        m_listWidget->insertItem(row - 1, item);
        m_listWidget->setCurrentRow(row - 1);
    }
}

void ColorRuleEditor::onMoveDown()
{
    int row = m_listWidget->currentRow();
    if (row >= 0 && row < m_listWidget->count() - 1) {
        auto *item = m_listWidget->takeItem(row);
        m_listWidget->insertItem(row + 1, item);
        m_listWidget->setCurrentRow(row + 1);
    }
}

void ColorRuleEditor::onSelectionChanged()
{
    int row = m_listWidget->currentRow();
    if (row < 0) return;

    auto *item = m_listWidget->item(row);
    QVariant v = item->data(Qt::UserRole);
    if (!v.canConvert<ColorRule>()) return;

    auto rule = v.value<ColorRule>();
    m_exprEdit->setText(rule.expr);
    m_currentBg = rule.background;
    m_currentFg = rule.foreground;
    m_enabledChk->setChecked(rule.enabled);

    m_bgColorBtn->setStyleSheet(
        QString("background-color: %1;").arg(m_currentBg.name()));
    m_fgColorBtn->setStyleSheet(
        QString("background-color: %1;").arg(m_currentFg.name()));
}

void ColorRuleEditor::applyCurrentEdit()
{
    int row = m_listWidget->currentRow();
    if (row < 0) return;

    auto *item = m_listWidget->item(row);
    ColorRule rule;
    rule.expr = m_exprEdit->text().trimmed();
    rule.background = m_currentBg;
    rule.foreground = m_currentFg;
    rule.enabled = m_enabledChk->isChecked();

    item->setText(QString("[%1] %2").arg(rule.enabled ? "ON" : "OFF", rule.expr));
    item->setBackground(rule.background);
    item->setForeground(rule.foreground);
    QVariant v;
    v.setValue(rule);
    item->setData(Qt::UserRole, v);
}

Q_DECLARE_METATYPE(ColorRuleEditor::ColorRule)
