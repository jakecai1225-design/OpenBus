#include "activitybar.h"
#include "utils/svg_icon.h"

#include <QToolButton>
#include <QVBoxLayout>

// 为 ActivityBar 按钮创建双状态图标 (未选中灰色 / 选中白色)
static QIcon makeActivityIcon(const QString &resourcePath)
{
    QIcon icon;
    icon.addPixmap(renderSvgPixmap(resourcePath, "#858585", 24), QIcon::Normal, QIcon::Off);
    icon.addPixmap(renderSvgPixmap(resourcePath, "#c8c8c8", 24), QIcon::Active, QIcon::Off);
    icon.addPixmap(renderSvgPixmap(resourcePath, "#ffffff", 24), QIcon::Normal, QIcon::On);
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

    // 顶部按钮 — 线条 SVG 图标
    m_buttons.append({createButton(":/icons/project.svg", "工程管理", Project), Project, "工程管理", "工程管理"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/trace.svg", "Trace", Trace), Trace, "Trace", "Trace"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/graphic.svg", "Graphic", Graphic), Graphic, "Graphic", "Graphic"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/database.svg", "数据库", Dbc), Dbc, "数据库", "数据库 — 多协议解析文件管理"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/send.svg", "发送", Send), Send, "发送", "发送"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/record.svg", "录制", Record), Record, "录制", "录制"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/device.svg", "设备连接", Device), Device, "设备连接", "设备连接"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/protocol.svg", "协议", Protocol), Protocol, "协议", "上层协议分析"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/flow.svg", "Flow", Analysis), Analysis, "Flow", "flow — CANoe Measurement Setup 风格"});
    layout->addWidget(m_buttons.last().btn);

    m_buttons.append({createButton(":/icons/tools.svg", "工具集", Tools), Tools, "工具集", "总线分析工具集 — 格式转换 / DBC 编辑 / 统计分析"});
    layout->addWidget(m_buttons.last().btn);

    layout->addStretch();

    // 底部按钮
    auto *settingsBtn = createButton(":/icons/settings.svg", "配置", Settings, true);
    layout->addWidget(settingsBtn);

    // 默认选中工程
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
