#include "activitybar.h"
#include "thememanager.h"
#include "utils/svg_icon.h"

#include <QCoreApplication>
#include <QToolButton>
#include <QVBoxLayout>

static QIcon makeActivityIcon(const QString &resourcePath)
{
    const Theme &t = ThemeManager::instance()->currentTheme();
    QIcon icon;
    icon.addPixmap(renderSvgPixmap(resourcePath, t.textDim, 24), QIcon::Normal, QIcon::Off);
    icon.addPixmap(renderSvgPixmap(resourcePath, t.text, 24), QIcon::Active, QIcon::Off);
    icon.addPixmap(renderSvgPixmap(resourcePath, t.accent, 24), QIcon::Normal, QIcon::On);
    return icon;
}

ActivityBar::ActivityBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedWidth(48);
    setObjectName("ActivityBar");
    setAttribute(Qt::WA_StyledBackground, true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Top buttons — order matches Activity enum
    m_buttons.append({createButton(":/icons/project.svg", tr("Project"), Project),
                      Project, QStringLiteral("Project"), QStringLiteral("Project"),
                      ":/icons/project.svg"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/flow.svg", tr("Flow"), Analysis),
                      Analysis, QStringLiteral("Flow"),
                      QStringLiteral("flow — CANoe Measurement Setup style"),
                      ":/icons/flow.svg"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/device.svg", tr("Devices"), Device),
                      Device, QStringLiteral("Devices"), QStringLiteral("Devices"),
                      ":/icons/device.svg"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/trace.svg", tr("Trace"), Trace),
                      Trace, QStringLiteral("Trace"), QStringLiteral("Trace"),
                      ":/icons/trace.svg"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/graphic.svg", tr("Graphic"), Graphic),
                      Graphic, QStringLiteral("Graphic"), QStringLiteral("Graphic"),
                      ":/icons/graphic.svg"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/database.svg", tr("Database"), Dbc),
                      Dbc, QStringLiteral("Database"),
                      QStringLiteral("Database — multi-protocol parse file management"),
                      ":/icons/database.svg"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/send.svg", tr("Transceive"), Transceive),
                      Transceive, QStringLiteral("Transceive"),
                      QStringLiteral("Transceive — send / playback / record"),
                      ":/icons/send.svg"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({
        createButton(":/icons/extensions.svg",
                     tr("Extensions — install / manage / search drivers and plugins"),
                     Extensions),
        Extensions, QStringLiteral("Extensions"),
        QStringLiteral("Extensions — install / manage / search drivers and plugins"),
        ":/icons/extensions.svg"});
    layout->addWidget(m_buttons.last().btn);

    layout->addStretch();

    auto *accountBtn = new QToolButton(this);
    accountBtn->setIcon(makeActivityIcon(":/icons/account.svg"));
    accountBtn->setIconSize(QSize(24, 24));
    accountBtn->setToolTip(tr("Account"));
    accountBtn->setCheckable(false);
    accountBtn->setAutoRaise(true);
    accountBtn->setFixedSize(48, 48);
    accountBtn->setObjectName("ActivityBtn");
    connect(accountBtn, &QToolButton::clicked, this, [this]() {
        setCurrentActivity(Settings);
        emit activityChanged(static_cast<int>(Settings));
    });
    layout->addWidget(accountBtn);
    m_buttons.append({accountBtn, None, QStringLiteral("Account"),
                      QStringLiteral("Account"), ":/icons/account.svg"});

    auto *settingsBtn = createButton(":/icons/settings.svg",
                                     tr("Settings"), Settings, true);
    layout->addWidget(settingsBtn);
    m_buttons.append({settingsBtn, Settings, QStringLiteral("Settings"),
                      QStringLiteral("Settings"), ":/icons/settings.svg"});

    m_buttons[0].btn->setChecked(true);
    m_current = Project;
}

QToolButton *ActivityBar::createButton(const QString &iconPath, const QString &tooltip,
                                       Activity act, bool atBottom)
{
    Q_UNUSED(atBottom);
    auto *btn = new QToolButton(this);
    btn->setIcon(makeActivityIcon(iconPath));
    btn->setIconSize(QSize(24, 24));
    btn->setToolTip(tooltip);
    btn->setCheckable(true);
    btn->setAutoRaise(true);
    btn->setFixedSize(48, 48);
    btn->setProperty("activity", static_cast<int>(act));
    btn->setObjectName("ActivityBtn");

    connect(btn, &QToolButton::clicked, this, &ActivityBar::onButtonClicked);

    return btn;
}

void ActivityBar::setCurrentActivity(Activity act)
{
    if (m_current == act) return;
    m_current = act;
    for (auto &info : m_buttons)
        info.btn->setChecked(info.activity == act);
}

void ActivityBar::onButtonClicked()
{
    auto *btn = qobject_cast<QToolButton *>(sender());
    if (!btn) return;

    Activity clicked = static_cast<Activity>(btn->property("activity").toInt());

    if (m_current == clicked) {
        btn->setChecked(false);
        m_current = None;
        emit activityToggled(static_cast<int>(clicked));
    } else {
        for (auto &info : m_buttons)
            info.btn->setChecked(info.activity == clicked);
        m_current = clicked;
        emit activityChanged(static_cast<int>(clicked));
    }
}

void ActivityBar::refreshIcons()
{
    for (const auto &info : m_buttons)
        info.btn->setIcon(makeActivityIcon(info.iconPath));
}

void ActivityBar::retranslateUi()
{
    for (auto &info : m_buttons) {
        const QString src = info.tooltip.isEmpty() ? info.text : info.tooltip;
        info.btn->setToolTip(QCoreApplication::translate("ActivityBar",
                                                         src.toUtf8().constData()));
    }
}
