#ifndef TERMINALSESSION_H
#define TERMINALSESSION_H

#include <QPlainTextEdit>
#include <QProcess>
#include <QStringList>

/**
 * @brief VS Code-style terminal session: type in the buffer (no separate line edit).
 *
 * Modes:
 *  - Repl: openbus built-in commands (help/sim/dev/...)
 *  - Shell: lightweight host shell via QProcess (prefers MSYS2 bash, else PowerShell/cmd)
 */
class TerminalSession : public QPlainTextEdit
{
    Q_OBJECT

public:
    enum class Mode { Repl, Shell };

    explicit TerminalSession(QWidget *parent = nullptr);
    ~TerminalSession() override;

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);

    void appendSystem(const QString &text);
    void clearScreen();
    void focusInput();

    /** Kill shell process if running; keep widget. */
    void killShell();

signals:
    /** Emitted in Repl mode when user submits a line. */
    void commandEntered(const QString &cmd);
    void titleChanged(const QString &title);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private slots:
    void onShellReadyRead();
    void onShellFinished(int exitCode, QProcess::ExitStatus status);

private:
    void writePrompt();
    void ensureCursorInInput();
    int inputStartPos() const;
    QString currentInput() const;
    void replaceCurrentInput(const QString &text);
    void submitReplLine();
    void startShell();
    QString resolveShellProgram(QStringList *args) const;
    void stripAndAppend(QByteArray &buf);

    Mode m_mode = Mode::Repl;
    int m_promptPos = 0;
    QStringList m_history;
    int m_historyIndex = -1;
    QString m_historyDraft;
    QProcess *m_shell = nullptr;
    QByteArray m_shellCarry; // incomplete UTF-8 from shell
};

#endif // TERMINALSESSION_H
