#ifndef BOTTOMPANEL_H
#define BOTTOMPANEL_H

#include <QTabWidget>

class QPlainTextEdit;
class QTableWidget;
class QLineEdit;
class QLabel;

/**
 * @brief VS Code 风格底部面板
 *
 * 标签页：Problems（错误列表）/ Terminal（终端）/ Output（日志）/ Command（命令行）
 */
class BottomPanel : public QTabWidget
{
    Q_OBJECT

public:
    explicit BottomPanel(QWidget *parent = nullptr);

    enum TabIndex {
        TabProblems = 0,
        TabTerminal = 1,
        TabOutput = 2,
        TabCommand = 3
    };

public slots:
    void addProblem(int severity, const QString &source, const QString &message);
    void appendOutput(const QString &text);
    void appendTerminal(const QString &text);
    void clearProblems();

signals:
    void commandEntered(const QString &cmd);

private slots:
    void onCommandReturnPressed();

private:
    QTableWidget *m_problemsTable;
    QPlainTextEdit *m_terminal;
    QPlainTextEdit *m_output;
    QLineEdit *m_cmdInput;
    QPlainTextEdit *m_cmdOutput;
    QLabel *m_problemCount;
};

#endif // BOTTOMPANEL_H
