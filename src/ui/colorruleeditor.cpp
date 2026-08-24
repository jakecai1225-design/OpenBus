#include "colorruleeditor.h"

#include "core/filter_engine.h"
#include "ui/thememanager.h"
#include "utils/svg_icon.h"

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
#include <QMessageBox>

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

    const QString iconCol = ThemeManager::instance()->currentTheme().text;
    auto *addBtn = new QPushButton(svgIcon(":/icons/plus.svg", iconCol, 14), "添加", listGroup);
    auto *removeBtn = new QPushButton(svgIcon(":/icons/dash.svg", iconCol, 14), "删除", listGroup);
    auto *upBtn = new QPushButton(svgIcon(":/icons/chevron-up.svg", iconCol, 14), "上移", listGroup);
    auto *downBtn = new QPushButton(svgIcon(":/icons/chevron-down.svg", iconCol, 14), "下移", listGroup);

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
    m_exprEdit->setPlaceholderText("例: id == 0x123（变量: id/dlc/ch/time/fd/ext/rx/tx/error）");
    editLayout->addWidget(m_exprEdit, 0, 1, 1, 3);

    // 实时校验当前表达式（编译失败的规则保存后不会生效——避免静默失效）
    m_exprStatus = new QLabel(editGroup);
    m_exprStatus->setStyleSheet(QStringLiteral("color: %1;").arg(QColor(0xC0, 0x39, 0x2B).name()));
    editLayout->addWidget(m_exprStatus, 3, 0, 1, 4);
    connect(m_exprEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        validateExpr(text);
    });

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
        // 保存前校验所有规则：编译失败的规则不会生效，直接拦截避免“标记了颜色却无颜色”
        const auto rs = rules();
        QStringList errors;
        for (int i = 0; i < rs.size(); ++i) {
            const QString &expr = rs[i].expr.trimmed();
            if (expr.isEmpty()) {
                errors << QStringLiteral("规则 %1: 条件表达式为空").arg(i + 1);
                continue;
            }
            FilterEngine fe;
            if (!fe.compile(expr))
                errors << QStringLiteral("规则 %1「%2」: %3")
                              .arg(i + 1).arg(expr, fe.errorString());
        }
        if (!errors.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("着色规则表达式错误"),
                QStringLiteral("以下规则表达式无法编译，保存后将不会生效：\n\n%1\n\n请修正后再保存。")
                    .arg(errors.join(QLatin1Char('\n'))));
            return;  // 不关闭对话框，留在编辑状态
        }
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

void ColorRuleEditor::validateExpr(const QString &expr)
{
    if (!m_exprStatus)
        return;
    const QString trimmed = expr.trimmed();
    if (trimmed.isEmpty()) {
        m_exprStatus->clear();
        return;
    }
    FilterEngine fe;
    m_exprStatus->setText(fe.compile(trimmed)
                              ? QString()
                              : QStringLiteral("✗ 表达式错误: %1").arg(fe.errorString()));
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
