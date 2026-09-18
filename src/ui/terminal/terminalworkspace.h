#ifndef TERMINALWORKSPACE_H
#define TERMINALWORKSPACE_H

#include <QWidget>

class QSplitter;
class TerminalSession;

/**
 * @brief VS Code-like terminal area: one or more TerminalSession panes in a splitter.
 */
class TerminalWorkspace : public QWidget
{
    Q_OBJECT

public:
    explicit TerminalWorkspace(QWidget *parent = nullptr);

    TerminalSession *activeSession() const;
    void appendToActive(const QString &text);
    void clearActive();

    TerminalSession *createSession();
    void splitActive(Qt::Orientation orientation);
    void closeActive();

signals:
    void commandEntered(const QString &cmd);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void wrapSession(TerminalSession *session);
    void setActive(TerminalSession *session);

    QSplitter *m_root = nullptr;
    TerminalSession *m_active = nullptr;
};

#endif // TERMINALWORKSPACE_H
