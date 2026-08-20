#include "filterbar.h"
#include "core/signalrelay.h"   // DEF-08：字符串信号 → lambda 桥接
#include "utils/canutils.h"
#include "utils/svg_icon.h"
#include "ui/thememanager.h"
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

    m_statusIcon = new QLabel(this);
    m_statusIcon->setFixedSize(20, 20);
    m_statusIcon->setPixmap(renderSvgPixmap(":/icons/check.svg", "#888888", 16));
    m_statusIcon->setToolTip("过滤器语法正确");

    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText("显示过滤 (例如: id == 0x123 and fd, 或 0x200, 或 dlc > 8)...");
    m_edit->setClearButtonEnabled(true);
    // 原生清除按钮 × 不随主题（深色下不可见）→ 换主题色 SVG 图标
    applyClearButtonIcon(m_edit, ThemeManager::instance()->currentTheme().text);

    m_applyBtn = new QPushButton("Apply", this);
    m_clearBtn = new QPushButton("Clear", this);
    const QString iconCol = ThemeManager::instance()->currentTheme().text;
    m_helpBtn = new QToolButton(this);
    m_helpBtn->setIcon(svgIcon(":/icons/help.svg", iconCol, 16));
    m_helpBtn->setToolTip("过滤器语法帮助");

    m_presetBtn = new QToolButton(this);
    m_presetBtn->setIcon(svgIcon(":/icons/list.svg", iconCol, 16));
    m_presetBtn->setToolTip("过滤预设");
    m_presetBtn->setPopupMode(QToolButton::InstantPopup);

    // 设置按钮（齿轮图标，弹出菜单由外部设置）
    m_settingsBtn = new QToolButton(this);
    m_settingsBtn->setIcon(svgIcon(":/icons/gear.svg", iconCol, 16));
    m_settingsBtn->setToolTip("设置（时间格式等）");
    m_settingsBtn->setPopupMode(QToolButton::InstantPopup);
    m_settingsBtn->setAutoRaise(true);
    m_settingsBtn->setFixedSize(26, 22);

    // 清空列表按钮（清空全部报文数据，区别于 Clear 的仅清过滤表达式）
    m_clearListBtn = new QToolButton(this);
    m_clearListBtn->setIcon(svgIcon(":/icons/clear-all.svg", iconCol, 16));
    m_clearListBtn->setToolTip(QStringLiteral("清空列表（删除全部报文数据）"));
    m_clearListBtn->setAutoRaise(true);
    m_clearListBtn->setFixedSize(26, 22);

    // 分组统计标签
    m_packetCountLabel = new QLabel(this);
    m_packetCountLabel->setStyleSheet("font-size: 11px; color: #666;");
    m_packetCountLabel->setText(QStringLiteral("捕获: 0 | 显示: 0 | 标记: 0"));

    layout->addWidget(m_statusIcon);
    layout->addWidget(m_edit, 1);
    layout->addWidget(m_presetBtn);
    layout->addWidget(m_applyBtn);
    layout->addWidget(m_clearBtn);
    layout->addWidget(m_helpBtn);
    layout->addWidget(m_settingsBtn);
    layout->addWidget(m_clearListBtn);
    layout->addWidget(m_packetCountLabel);

    connect(m_applyBtn, &QPushButton::clicked, this, &FilterBar::onApply);
    connect(m_clearBtn, &QPushButton::clicked, this, &FilterBar::onClear);
    connect(m_helpBtn, &QToolButton::clicked, this, &FilterBar::showHelp);
    connect(m_clearListBtn, &QToolButton::clicked, this, &FilterBar::clearListRequested);
    connect(m_edit, &QLineEdit::returnPressed, this, &FilterBar::onApply);
    connect(m_edit, &QLineEdit::textChanged, this, &FilterBar::onTextChanged);

    // 主题切换 → 重刷图标颜色（状态图标由 onTextChanged 按当前状态重渲染）
    // DEF-08 字符串信号（同 filterheaderview）
    auto *themeRelay = new SignalRelay(this);
    themeRelay->fire0 = [this]() {
        const QString c = ThemeManager::instance()->currentTheme().text;
        m_helpBtn->setIcon(svgIcon(":/icons/help.svg", c, 16));
        m_presetBtn->setIcon(svgIcon(":/icons/list.svg", c, 16));
        m_settingsBtn->setIcon(svgIcon(":/icons/gear.svg", c, 16));
        m_clearListBtn->setIcon(svgIcon(":/icons/clear-all.svg", c, 16));
        onTextChanged();
    };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            themeRelay, SLOT(fire()));
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

void FilterBar::setPacketCountText(const QString &text)
{
    m_packetCountLabel->setText(text);
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
    const QString okCol = ThemeManager::instance()->currentTheme().text;
    if (expr.isEmpty()) {
        m_statusIcon->setPixmap(renderSvgPixmap(":/icons/check.svg", "#888888", 16));
        m_statusIcon->setToolTip("无过滤");
        m_edit->setStyleSheet("");
        return;
    }

    if (CanUtils::isFilterValid(expr)) {
        m_statusIcon->setPixmap(renderSvgPixmap(":/icons/check.svg", okCol, 16));
        m_statusIcon->setToolTip("语法正确");
        m_edit->setStyleSheet("QLineEdit { background-color: #eff6ee; }");
    } else {
        m_statusIcon->setPixmap(renderSvgPixmap(":/icons/close.svg", "#e51400", 16));
        m_statusIcon->setToolTip("语法错误");
        m_edit->setStyleSheet("QLineEdit { background-color: #fbeaea; }");
    }
}
