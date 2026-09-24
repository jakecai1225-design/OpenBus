#include "filterbar.h"
#include "core/signalrelay.h"   // DEF-08: string signal → lambda bridge
#include "utils/canutils.h"
#include "utils/svg_icon.h"
#include "ui/thememanager.h"
#include "core/filterpresetmanager.h"

#include <QLineEdit>
#include <QToolButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QStyle>
#include <QEnterEvent>
#include <QFrame>
#include <QMenu>
#include <QInputDialog>
#include <QScrollArea>
#include <QSizePolicy>
#include <QAction>

namespace {

QToolButton *makeIconToolButton(QWidget *parent, const QString &iconPath,
                                const QString &color, const QString &tip)
{
    auto *btn = new QToolButton(parent);
    btn->setIcon(svgIcon(iconPath, color, 16));
    btn->setToolTip(tip);
    btn->setAutoRaise(true);
    btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    btn->setFixedSize(26, 22);
    btn->setIconSize(QSize(16, 16));
    return btn;
}

} // namespace

FilterBar::FilterBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("FilterBar"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(3);

    const QString iconCol = ThemeManager::instance()->currentTheme().text;

    m_statusIcon = new QLabel(this);
    m_statusIcon->setFixedSize(20, 20);
    m_statusIcon->setPixmap(renderSvgPixmap(":/icons/check.svg", "#888888", 16));
    m_statusIcon->setToolTip(tr("Filter syntax OK"));

    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(
        tr("Display filter (e.g. id == 0x123 and fd)..."));
    m_edit->setClearButtonEnabled(true);
    m_edit->setMinimumWidth(360);
    m_edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    applyClearButtonIcon(m_edit, iconCol);

    // Filter apply / clear — icon-only, next to the edit
    m_applyBtn = makeIconToolButton(this, QStringLiteral(":/icons/apply.svg"), iconCol,
                                    tr("Apply display filter (Enter)"));
    m_clearBtn = makeIconToolButton(this, QStringLiteral(":/icons/close.svg"), iconCol,
                                    tr("Clear display filter expression"));

    m_helpBtn = makeIconToolButton(this, QStringLiteral(":/icons/help.svg"), iconCol,
                                   tr("Filter syntax help"));

    m_presetBtn = makeIconToolButton(this, QStringLiteral(":/icons/list.svg"), iconCol,
                                     tr("Filter presets"));
    m_presetBtn->setPopupMode(QToolButton::InstantPopup);

    m_settingsBtn = makeIconToolButton(this, QStringLiteral(":/icons/gear.svg"), iconCol,
                                       tr("Trace settings (time format, overwrite, colors)"));
    m_settingsBtn->setPopupMode(QToolButton::InstantPopup);

    m_clearListBtn = makeIconToolButton(this, QStringLiteral(":/icons/clear-all.svg"), iconCol,
                                        tr("Clear list (delete all frames)"));

    m_packetCountLabel = new QLabel(this);
    m_packetCountLabel->setVisible(false);

    // Trace actions slot (Find / Follow / …) — after filter apply/clear
    m_actionsHost = new QWidget(this);
    m_actionsHost->setObjectName(QStringLiteral("FilterBarActions"));
    m_actionsLay = new QHBoxLayout(m_actionsHost);
    m_actionsLay->setContentsMargins(2, 0, 0, 0);
    m_actionsLay->setSpacing(1);

    layout->addWidget(m_statusIcon);
    layout->addWidget(m_edit, /*stretch*/ 1);
    layout->addWidget(m_applyBtn);
    layout->addWidget(m_clearBtn);
    layout->addWidget(m_actionsHost, 0);
    layout->addStretch(0);
    layout->addWidget(m_presetBtn);
    layout->addWidget(m_helpBtn);
    layout->addWidget(m_settingsBtn);
    layout->addWidget(m_clearListBtn);

    connect(m_applyBtn, &QToolButton::clicked, this, &FilterBar::onApply);
    connect(m_clearBtn, &QToolButton::clicked, this, &FilterBar::onClear);
    connect(m_helpBtn, &QToolButton::clicked, this, &FilterBar::showHelp);
    connect(m_clearListBtn, &QToolButton::clicked, this, &FilterBar::clearListRequested);
    connect(m_edit, &QLineEdit::returnPressed, this, &FilterBar::onApply);
    connect(m_edit, &QLineEdit::textChanged, this, &FilterBar::onTextChanged);

    auto *themeRelay = new SignalRelay(this);
    themeRelay->fire0 = [this]() {
        const QString c = ThemeManager::instance()->currentTheme().text;
        m_applyBtn->setIcon(svgIcon(QStringLiteral(":/icons/apply.svg"), c, 16));
        m_clearBtn->setIcon(svgIcon(QStringLiteral(":/icons/close.svg"), c, 16));
        m_helpBtn->setIcon(svgIcon(QStringLiteral(":/icons/help.svg"), c, 16));
        m_presetBtn->setIcon(svgIcon(QStringLiteral(":/icons/list.svg"), c, 16));
        m_settingsBtn->setIcon(svgIcon(QStringLiteral(":/icons/gear.svg"), c, 16));
        m_clearListBtn->setIcon(svgIcon(QStringLiteral(":/icons/clear-all.svg"), c, 16));
        applyClearButtonIcon(m_edit, c);
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
    auto *saveAction = menu->addAction(QStringLiteral("Save current expression as preset..."));
    connect(saveAction, &QAction::triggered, this, &FilterBar::onSaveAsPreset);
    m_presetBtn->setMenu(menu);
}

void FilterBar::setPacketCountText(const QString &text)
{
    m_packetCountLabel->setText(text);
}

void FilterBar::onPresetMenu()
{
    // Handled by QToolButton::InstantPopup
}

void FilterBar::onSaveAsPreset()
{
    if (!m_presetMgr) return;

    QString expr = m_edit->text().trimmed();
    if (expr.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Save preset"),
                                 QStringLiteral("Enter a filter expression first."));
        return;
    }

    bool ok = false;
    QString name = QInputDialog::getText(this, QStringLiteral("Save filter preset"),
        QStringLiteral("Preset name:"), QLineEdit::Normal, QString(), &ok);
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

void FilterBar::setFilterText(const QString &text)
{
    m_edit->setText(text);
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
    QMessageBox::information(this, QStringLiteral("Filter syntax help"),
                             CanUtils::filterHelp());
}

void FilterBar::onTextChanged()
{
    QString expr = m_edit->text().trimmed();
    const QString okCol = ThemeManager::instance()->currentTheme().text;
    if (expr.isEmpty()) {
        m_statusIcon->setPixmap(renderSvgPixmap(":/icons/check.svg", "#888888", 16));
        m_statusIcon->setToolTip(QStringLiteral("No filter"));
        m_edit->setStyleSheet(QString());
        return;
    }

    if (CanUtils::isFilterValid(expr)) {
        m_statusIcon->setPixmap(renderSvgPixmap(":/icons/check.svg", okCol, 16));
        m_statusIcon->setToolTip(QStringLiteral("Syntax OK"));
        m_edit->setStyleSheet(QStringLiteral("QLineEdit { background-color: #eff6ee; }"));
    } else {
        m_statusIcon->setPixmap(renderSvgPixmap(":/icons/close.svg", "#e51400", 16));
        m_statusIcon->setToolTip(QStringLiteral("Syntax error"));
        m_edit->setStyleSheet(QStringLiteral("QLineEdit { background-color: #fbeaea; }"));
    }
}

// ============================================================
//  FilterChipBar — U2 active filter chips
// ============================================================

FilterChipBar::FilterChipBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("FilterChipBar"));
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(6, 2, 6, 2);
    outer->setSpacing(4);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFixedHeight(28);

    m_inner = new QWidget(m_scroll);
    m_lay = new QHBoxLayout(m_inner);
    m_lay->setContentsMargins(0, 0, 0, 0);
    m_lay->setSpacing(4);
    m_lay->addStretch(1);
    m_scroll->setWidget(m_inner);
    outer->addWidget(m_scroll, 1);

    m_clearAllBtn = new QToolButton(this);
    m_clearAllBtn->setText(QStringLiteral("Clear all"));
    m_clearAllBtn->setToolTip(QStringLiteral("Clear all active filters"));
    m_clearAllBtn->setAutoRaise(true);
    m_clearAllBtn->setVisible(false);
    connect(m_clearAllBtn, &QToolButton::clicked, this, &FilterChipBar::clearAllRequested);
    outer->addWidget(m_clearAllBtn);

    setVisible(false);
}

void FilterChipBar::setChips(const QVector<QPair<QString, QString>> &chips)
{
    rebuild(chips);
}

void FilterChipBar::rebuild(const QVector<QPair<QString, QString>> &chips)
{
    while (QLayoutItem *it = m_lay->takeAt(0)) {
        if (QWidget *w = it->widget())
            w->deleteLater();
        delete it;
    }

    m_chipCount = chips.size();
    for (const auto &chip : chips) {
        auto *btn = new QToolButton(m_inner);
        btn->setAutoRaise(true);
        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        QString label = chip.second;
        if (label.size() > 48)
            label = label.left(45) + QStringLiteral("...");
        btn->setText(label + QStringLiteral("  ×"));
        btn->setToolTip(QStringLiteral("Clear filter: %1").arg(chip.second));
        btn->setStyleSheet(QStringLiteral(
            "QToolButton {"
            "  background: transparent;"
            "  border: 1px solid palette(shadow);"
            "  border-radius: 9px;"
            "  padding: 1px 8px;"
            "  font-size: 11px;"
            "}"
            "QToolButton:hover { background: palette(midlight); }"));
        const QString id = chip.first;
        connect(btn, &QToolButton::clicked, this, [this, id]() {
            emit chipDismissed(id);
        });
        m_lay->addWidget(btn);
    }
    m_lay->addStretch(1);

    m_clearAllBtn->setVisible(m_chipCount > 0);
    setVisible(m_chipCount > 0);
    setFixedHeight(m_chipCount > 0 ? 32 : 0);
}

void FilterBar::retranslateUi()
{
    if (m_edit)
        m_edit->setPlaceholderText(tr("Display filter (e.g. id == 0x123 and fd)..."));
    if (m_applyBtn)
        m_applyBtn->setToolTip(tr("Apply display filter (Enter)"));
    if (m_clearBtn)
        m_clearBtn->setToolTip(tr("Clear display filter expression"));
    if (m_helpBtn)
        m_helpBtn->setToolTip(tr("Filter syntax help"));
    if (m_presetBtn)
        m_presetBtn->setToolTip(tr("Filter presets"));
    if (m_settingsBtn)
        m_settingsBtn->setToolTip(tr("Trace settings (time format, overwrite, colors)"));
    if (m_clearListBtn)
        m_clearListBtn->setToolTip(tr("Clear list (delete all frames)"));
}
