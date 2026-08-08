#include "filterbar.h"
#include "utils/canutils.h"
#include "core/filterpresetmanager.h"

#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QStyle>
#include <QEnterEvent>
#include <QFrame>
#include <QMenu>
#include <QInputDialog>

FilterBar::FilterBar(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(4);

    // 覆盖模式按钮（checkable）
    m_overwriteBtn = new QPushButton("覆盖模式", this);
    m_overwriteBtn->setCheckable(true);
    m_overwriteBtn->setToolTip("开启后每个 CAN ID 固定一行，新帧刷新行数据和帧数\n"
                               "关闭后为滚动模式，每帧新增一行");
    layout->addWidget(m_overwriteBtn);

    // 分隔线
    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    layout->addWidget(sep);

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

    m_presetBtn = new QToolButton(this);
    m_presetBtn->setText("☰");
    m_presetBtn->setToolTip("过滤预设");
    m_presetBtn->setPopupMode(QToolButton::InstantPopup);

    layout->addWidget(m_statusIcon);
    layout->addWidget(m_edit, 1);
    layout->addWidget(m_presetBtn);
    layout->addWidget(m_applyBtn);
    layout->addWidget(m_clearBtn);
    layout->addWidget(m_helpBtn);

    connect(m_overwriteBtn, &QPushButton::toggled, this, &FilterBar::overwriteModeToggled);
    connect(m_applyBtn, &QPushButton::clicked, this, &FilterBar::onApply);
    connect(m_clearBtn, &QPushButton::clicked, this, &FilterBar::onClear);
    connect(m_helpBtn, &QToolButton::clicked, this, &FilterBar::showHelp);
    connect(m_edit, &QLineEdit::returnPressed, this, &FilterBar::onApply);
    connect(m_edit, &QLineEdit::textChanged, this, &FilterBar::onTextChanged);
}

void FilterBar::setPresetManager(FilterPresetManager *mgr)
{
    m_presetMgr = mgr;
    refreshPresets();
}

void FilterBar::refreshPresets()
{
    if (!m_presetMgr) return;

    auto *menu = new QMenu(this);
    for (const auto &p : m_presetMgr->presets()) {
        auto *action = menu->addAction(p.name);
        action->setToolTip(p.expr);
        connect(action, &QAction::triggered, this, [this, expr = p.expr]() {
            m_edit->setText(expr);
            onApply();
        });
    }
    menu->addSeparator();
    auto *saveAction = menu->addAction("保存当前表达式为预设...");
    connect(saveAction, &QAction::triggered, this, &FilterBar::onSaveAsPreset);
    m_presetBtn->setMenu(menu);
}

void FilterBar::onPresetMenu()
{
    // 由 QToolButton::InstantPopup 自动处理
}

void FilterBar::onSaveAsPreset()
{
    if (!m_presetMgr) return;

    QString expr = m_edit->text().trimmed();
    if (expr.isEmpty()) {
        QMessageBox::information(this, "保存预设", "请先输入过滤表达式");
        return;
    }

    bool ok = false;
    QString name = QInputDialog::getText(this, "保存过滤预设",
        "预设名称:", QLineEdit::Normal, QString(), &ok);
    if (ok && !name.isEmpty()) {
        m_presetMgr->addPreset(name, expr);
        m_presetMgr->saveDefault();
        refreshPresets();
    }
}

QString FilterBar::filterText() const
{
    return m_edit->text();
}

bool FilterBar::filterActive() const
{
    return !m_edit->text().trimmed().isEmpty();
}

void FilterBar::setOverwriteMode(bool enabled)
{
    m_overwriteBtn->setChecked(enabled);
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
        m_edit->setStyleSheet("QLineEdit { background-color: #eff6ee; }");
    } else {
        m_statusIcon->setPixmap(style()->standardIcon(QStyle::SP_DialogCancelButton).pixmap(16, 16));
        m_statusIcon->setToolTip("语法错误");
        m_edit->setStyleSheet("QLineEdit { background-color: #fbeaea; }");
    }
}
