#include "ui/commandcenter.h"
#include "thememanager.h"
#include "utils/svg_icon.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QToolButton>

CommandCenter::CommandCenter(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("CommandCenter"));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::ClickFocus);
    setFixedHeight(26);
    setMinimumWidth(280);
    setMaximumWidth(520);

    m_placeholder = QStringLiteral("Search OpenBus");

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(6, 0, 10, 0);
    lay->setSpacing(4);

    auto makeNav = [this](const QString &tip) {
        auto *b = new QToolButton(this);
        b->setObjectName(QStringLiteral("CommandCenterNav"));
        b->setAutoRaise(true);
        b->setIconSize(QSize(kIconSm, kIconSm));
        b->setFixedSize(22, 22);
        b->setToolTip(tip);
        b->setCursor(Qt::ArrowCursor);
        return b;
    };
    m_backBtn = makeNav(QStringLiteral("Go Back"));
    m_fwdBtn = makeNav(QStringLiteral("Go Forward"));
    lay->addWidget(m_backBtn);
    lay->addWidget(m_fwdBtn);

    m_label = new QLabel(m_placeholder, this);
    m_label->setObjectName(QStringLiteral("CommandCenterLabel"));
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    lay->addWidget(m_label, 1);

    connect(m_backBtn, &QToolButton::clicked, this, &CommandCenter::navigateBack);
    connect(m_fwdBtn, &QToolButton::clicked, this, &CommandCenter::navigateForward);

    // Theme icons
    auto refreshIcons = [this]() {
        const QString c = ThemeManager::instance()->currentTheme().textDim;
        m_backBtn->setIcon(svgIcon(QStringLiteral(":/icons/chevron-left.svg"), c, kIconSm));
        m_fwdBtn->setIcon(svgIcon(QStringLiteral(":/icons/chevron-right.svg"), c, kIconSm));
    };
    refreshIcons();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [refreshIcons](const QString &) { refreshIcons(); });

    installEventFilter(this);
}

void CommandCenter::setPlaceholder(const QString &text)
{
    m_placeholder = text;
    if (m_label->text().isEmpty() || m_label->property("preview").toBool() == false)
        m_label->setText(m_placeholder);
}

void CommandCenter::setQueryPreview(const QString &text)
{
    if (text.isEmpty()) {
        m_label->setProperty("preview", false);
        m_label->setText(m_placeholder);
    } else {
        m_label->setProperty("preview", true);
        m_label->setText(text);
    }
}

void CommandCenter::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit activated();
    QFrame::mousePressEvent(event);
}

bool CommandCenter::eventFilter(QObject *obj, QEvent *event)
{
    Q_UNUSED(obj);
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter
            || ke->key() == Qt::Key_Space) {
            emit activated();
            return true;
        }
    }
    return QFrame::eventFilter(obj, event);
}
