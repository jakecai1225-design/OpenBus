#include "ui/terminal/terminalworkspace.h"
#include "ui/terminal/terminalsession.h"

#include <QEvent>
#include <QSplitter>
#include <QVBoxLayout>

TerminalWorkspace::TerminalWorkspace(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_root = new QSplitter(Qt::Horizontal, this);
    m_root->setChildrenCollapsible(false);
    m_root->setHandleWidth(3);
    m_root->setObjectName(QStringLiteral("TerminalSplitter"));
    lay->addWidget(m_root);

    createSession();
}

TerminalSession *TerminalWorkspace::activeSession() const
{
    return m_active;
}

void TerminalWorkspace::appendToActive(const QString &text)
{
    if (m_active)
        m_active->appendSystem(text);
}

void TerminalWorkspace::clearActive()
{
    if (m_active)
        m_active->clearScreen();
}

void TerminalWorkspace::wrapSession(TerminalSession *session)
{
    connect(session, &TerminalSession::commandEntered,
            this, &TerminalWorkspace::commandEntered);
    session->installEventFilter(this);
}

bool TerminalWorkspace::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FocusIn
        || event->type() == QEvent::MouseButtonPress) {
        if (auto *s = qobject_cast<TerminalSession *>(watched))
            setActive(s);
    }
    return QWidget::eventFilter(watched, event);
}

void TerminalWorkspace::setActive(TerminalSession *session)
{
    m_active = session;
}

TerminalSession *TerminalWorkspace::createSession()
{
    auto *session = new TerminalSession(m_root);
    wrapSession(session);
    m_root->addWidget(session);
    setActive(session);
    session->focusInput();
    return session;
}

void TerminalWorkspace::splitActive(Qt::Orientation orientation)
{
    if (!m_active)
        return;

    QWidget *parentW = m_active->parentWidget();
    auto *parentSplit = qobject_cast<QSplitter *>(parentW);
    if (!parentSplit)
        parentSplit = m_root;

    if (parentSplit->orientation() != orientation && parentSplit->count() > 1) {
        const int idx = parentSplit->indexOf(m_active);
        auto *nested = new QSplitter(orientation, parentSplit);
        nested->setChildrenCollapsible(false);
        nested->setHandleWidth(3);
        nested->setObjectName(QStringLiteral("TerminalSplitter"));
        parentSplit->insertWidget(idx, nested);
        nested->addWidget(m_active);
        auto *session = new TerminalSession(nested);
        wrapSession(session);
        nested->addWidget(session);
        nested->setSizes({1, 1});
        setActive(session);
        session->focusInput();
        return;
    }

    parentSplit->setOrientation(orientation);
    auto *session = new TerminalSession(parentSplit);
    wrapSession(session);
    parentSplit->addWidget(session);
    QList<int> sizes;
    const int n = parentSplit->count();
    for (int i = 0; i < n; ++i)
        sizes << 1;
    parentSplit->setSizes(sizes);
    setActive(session);
    session->focusInput();
}

void TerminalWorkspace::closeActive()
{
    if (!m_active)
        return;

    const auto sessions = findChildren<TerminalSession *>();
    if (sessions.size() <= 1) {
        m_active->clearScreen();
        return;
    }

    TerminalSession *dying = m_active;
    TerminalSession *next = nullptr;
    for (TerminalSession *s : sessions) {
        if (s != dying) {
            next = s;
            break;
        }
    }
    dying->killShell();
    dying->deleteLater();
    setActive(next);
    if (next)
        next->focusInput();
}
