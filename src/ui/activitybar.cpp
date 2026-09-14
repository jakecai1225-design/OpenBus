#include "activitybar.h"
#include "thememanager.h"
#include "utils/svg_icon.h"

#include <QToolButton>
#include <QVBoxLayout>

// 为 ActivityBar 按钮创建主题感知图标
// 未选中 → textDim 色 / 悬停 → text 色 / 选中 → accent 色
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

    // 顶部按钮 — 顺序与 Activity 枚举一致
    // 0=Project 1=Analysis(Flow) 2=Device 3=Trace 4=Graphic
    // 5=Dbc 6=Transceive(收发) 7=Extensions
    m_buttons.append({createButton(":/icons/project.svg", "工程管理", Project), Project, "工程管理", "工程管理", ":/icons/project.svg"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/flow.svg", "Flow", Analysis), Analysis, "Flow", "flow — CANoe Measurement Setup 风格", ":/icons/flow.svg"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/device.svg", "设备连接", Device), Device, "设备连接", "设备连接", ":/icons/device.svg"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/trace.svg", "Trace", Trace), Trace, "Trace", "Trace", ":/icons/trace.svg"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/graphic.svg", "Graphic", Graphic), Graphic, "Graphic", "Graphic", ":/icons/graphic.svg"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/database.svg", "数据库", Dbc), Dbc, "数据库", "数据库 — 多协议解析文件管理", ":/icons/database.svg"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/send.svg", "收发", Transceive), Transceive, "收发", "收发 — 发送 / 回放 / 录制", ":/icons/send.svg"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/extensions.svg", "插件市场 — 驱动与插件的安装 / 管理 / 搜索", Extensions), Extensions, "插件市场", "插件市场 — 驱动与插件的安装 / 管理 / 搜索", ":/icons/extensions.svg"});
    layout->addWidget(m_buttons.last().btn);

    layout->addStretch();

    // Bottom: account + settings (VS Code Activity Bar footer icons)
    auto *accountBtn = new QToolButton(this);
    accountBtn->setIcon(makeActivityIcon(":/icons/account.svg"));
    accountBtn->setIconSize(QSize(24, 24));
    accountBtn->setToolTip(QStringLiteral("Account"));
    accountBtn->setCheckable(false);
    accountBtn->setAutoRaise(true);
    accountBtn->setFixedSize(48, 48);
    accountBtn->setObjectName("ActivityBtn");
    connect(accountBtn, &QToolButton::clicked, this, [this]() {
        setCurrentActivity(Settings);
        emit activityChanged(static_cast<int>(Settings));
    });
    layout->addWidget(accountBtn);
    // Keep path so refreshIcons can update account too
    m_buttons.append({accountBtn, None, QStringLiteral("Account"),
                      QStringLiteral("Account"), ":/icons/account.svg"});

    auto *settingsBtn = createButton(":/icons/settings.svg",
                                     QStringLiteral("Settings"), Settings, true);
    layout->addWidget(settingsBtn);
    m_buttons.append({settingsBtn, Settings, QStringLiteral("Settings"),
                      QStringLiteral("Settings"), ":/icons/settings.svg"});

    // Default: Project selected
    m_buttons[0].btn->setChecked(true);
    m_current = Project;
}

QToolButton *ActivityBar::createButton(const QString &iconPath, const QString &tooltip,
                                       Activity act, bool atBottom)
{
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
        // 同一按钮再次点击 → 隐藏 SideBar
        btn->setChecked(false);
        m_current = None;
        emit activityToggled(static_cast<int>(clicked));
    } else {
        // 切换
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
