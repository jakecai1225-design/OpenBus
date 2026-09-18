#ifndef BOTTOMPANEL_H
#define BOTTOMPANEL_H

#include <QTabWidget>

class QPlainTextEdit;
class QTableWidget;
class QLabel;
class QToolButton;
class TerminalWorkspace;

/**
 * @brief Bottom panel — Terminal / Output / Problems / Extensions
 *
 * Terminal tab: VS Code-style inline input (no separate line edit),
 * split panes, openbus REPL + optional host bash/PowerShell.
 */
class BottomPanel : public QTabWidget
{
    Q_OBJECT

public:
    explicit BottomPanel(QWidget *parent = nullptr);

    enum TabIndex {
        TabTerminal = 0,
        TabOutput = 1,
        TabProblems = 2,
        TabPlugin = 3
    };

public slots:
    void addProblem(int severity, const QString &source, const QString &message);
    void appendOutput(const QString &text);
    void appendTerminal(const QString &text);
    void appendPluginOutput(const QString &text);
    void clearPluginOutput();
    void clearProblems();

signals:
    void commandEntered(const QString &cmd);
    void closeRequested();   // Corner "x" — hide bottom panel (VS Code)

private slots:
    void refreshCornerIcons();

private:
    TerminalWorkspace *m_termWorkspace = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QPlainTextEdit *m_pluginOutput = nullptr;
    QTableWidget *m_problemsTable = nullptr;
    QLabel *m_problemCount = nullptr;
    QToolButton *m_clearBtn = nullptr;
    QToolButton *m_splitBtn = nullptr;
    QToolButton *m_moreBtn = nullptr;
    QToolButton *m_closePanelBtn = nullptr;
};

#endif // BOTTOMPANEL_H
