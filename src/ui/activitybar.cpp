#include "activitybar.h"

#include <QToolButton>
#include <QVBoxLayout>
#include <QPaintEvent>
#include <QPainter>

ActivityBar::ActivityBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedWidth(48);
    setObjectName("ActivityBar");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 顶部按钮
    m_buttons.append({createButton("📁", "工程管理", Project), Project, "工程管理", "工程管理"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton("📐", "DBC 数据库", Dbc), Dbc, "DBC 数据库", "DBC 数据库"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton("📋", "Trace 配置", Trace), Trace, "Trace 配置", "Trace 配置"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton("📈", "Graphic 配置", Graphic), Graphic, "Graphic 配置", "Graphic 配置"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton("🔌", "设备连接", Device), Device, "设备连接", "设备连接"});
    layout->addWidget(m_buttons.last().btn);

    layout->addStretch();

    // 底部按钮
    auto *settingsBtn = createButton("⚙", "设置", Settings, true);
    layout->addWidget(settingsBtn);

    // 默认选中工程
    m_buttons[0].btn->setChecked(true);
    m_current = Project;
}

QToolButton *ActivityBar::createButton(const QString &text, const QString &tooltip,
                                       Activity act, bool atBottom)
{
    auto *btn = new QToolButton(this);
    btn->setText(text);
    btn->setToolTip(tooltip);
    btn->setCheckable(true);
    btn->setAutoRaise(true);
    btn->setFixedSize(48, 48);
    btn->setProperty("activity", static_cast<int>(act));
    btn->setObjectName("ActivityBtn");

    connect(btn, &QToolButton::clicked, this, &ActivityBar::onButtonClicked);

    return btn;
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

void ActivityBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x2D, 0x2D, 0x2D));
}
