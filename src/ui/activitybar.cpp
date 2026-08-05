#include "activitybar.h"

#include <QToolButton>
#include <QVBoxLayout>

ActivityBar::ActivityBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedWidth(48);
    setObjectName("ActivityBar");
    setAttribute(Qt::WA_StyledBackground, true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 顶部按钮 — 按设计顺序: 工程 / Trace / Graphic / 数据库 / 回放 / 录制 / 设备
    m_buttons.append({createButton("\xF0\x9F\x93\x81", "工程管理", Project), Project, "工程管理", "工程管理"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton("\xF0\x9F\x93\x8B", "Trace", Trace), Trace, "Trace", "Trace"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton("\xF0\x9F\x93\x88", "Graphic", Graphic), Graphic, "Graphic", "Graphic"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton("\xF0\x9F\x97\x84", "数据库", Dbc), Dbc, "数据库", "数据库 — 多协议解析文件管理"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton("\xF0\x9F\x93\xA1", "发送", Send), Send, "发送", "发送"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton("\xE2\x97\x8F", "录制", Record), Record, "录制", "录制"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton("\xF0\x9F\x94\xA7", "设备连接", Device), Device, "设备连接", "设备连接"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton("\xF0\x9F\x93\xA6", "协议", Protocol), Protocol, "协议", "上层协议分析"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton("\xF0\x9F\x93\x8A", "Flow", Analysis), Analysis, "Flow", "flow — CANoe Measurement Setup 风格"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton("\xF0\x9F\x9B\xA0", "工具集", Tools), Tools, "工具集", "总线分析工具集 — 格式转换 / DBC 编辑 / 统计分析"});
    layout->addWidget(m_buttons.last().btn);

    layout->addStretch();

    // 底部按钮
    auto *settingsBtn = createButton("\xE2\x9A\x99", "配置", Settings, true);
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
