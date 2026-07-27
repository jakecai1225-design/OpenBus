#include "filterbar.h"
#include "utils/canutils.h"

#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QStyle>
#include <QEnterEvent>

FilterBar::FilterBar(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(4);

    m_statusIcon = new QLabel(this);
    m_statusIcon->setFixedSize(20, 20);
    m_statusIcon->setPixmap(style()->standardIcon(QStyle::SP_DialogOkButton).pixmap(16, 16));
    m_statusIcon->setToolTip("过滤器语法正确");

    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText("显示过滤 (例如: id == 0x123 and fd, 或 0x200, 或 dlc > 8)...");
    m_edit->setClearButtonEnabled(true);

    m_applyBtn = new QPushButton("Apply", this);
    m_clearBtn = new QPushButton("Clear", this);
    m_helpBtn = new QToolButton(this);
    m_helpBtn->setText("?");
    m_helpBtn->setToolTip("过滤器语法帮助");

    layout->addWidget(m_statusIcon);
    layout->addWidget(m_edit, 1);
    layout->addWidget(m_applyBtn);
    layout->addWidget(m_clearBtn);
    layout->addWidget(m_helpBtn);

    connect(m_applyBtn, &QPushButton::clicked, this, &FilterBar::onApply);
    connect(m_clearBtn, &QPushButton::clicked, this, &FilterBar::onClear);
    connect(m_helpBtn, &QToolButton::clicked, this, &FilterBar::showHelp);
    connect(m_edit, &QLineEdit::returnPressed, this, &FilterBar::onApply);
    connect(m_edit, &QLineEdit::textChanged, this, &FilterBar::onTextChanged);
}

QString FilterBar::filterText() const
{
    return m_edit->text();
}

bool FilterBar::filterActive() const
{
    return !m_edit->text().trimmed().isEmpty();
}

void FilterBar::onApply()
{
    QString expr = m_edit->text().trimmed();
    if (expr.isEmpty()) {
        emit filterCleared();
        return;
    }
    emit filterApplied(expr);
}

void FilterBar::onClear()
{
    m_edit->clear();
    emit filterCleared();
}

void FilterBar::showHelp()
{
    QMessageBox::information(this, "过滤器语法帮助", CanUtils::filterHelp());
}

void FilterBar::onTextChanged()
{
    QString expr = m_edit->text().trimmed();
    if (expr.isEmpty()) {
        m_statusIcon->setPixmap(style()->standardIcon(QStyle::SP_DialogOkButton).pixmap(16, 16));
        m_statusIcon->setToolTip("无过滤");
        m_edit->setStyleSheet("");
        return;
    }

    if (CanUtils::isFilterValid(expr)) {
        m_statusIcon->setPixmap(style()->standardIcon(QStyle::SP_DialogOkButton).pixmap(16, 16));
        m_statusIcon->setToolTip("语法正确");
        m_edit->setStyleSheet("QLineEdit { background-color: #E8F5E8; }");
    } else {
        m_statusIcon->setPixmap(style()->standardIcon(QStyle::SP_DialogCancelButton).pixmap(16, 16));
        m_statusIcon->setToolTip("语法错误");
        m_edit->setStyleSheet("QLineEdit { background-color: #FDE8E8; }");
    }
}
