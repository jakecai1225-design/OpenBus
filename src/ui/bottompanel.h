#ifndef BOTTOMPANEL_H
#define BOTTOMPANEL_H

#include <QTabWidget>

class QPlainTextEdit;
class QTableWidget;
class QLineEdit;
class QLabel;

/**
 * @brief 底部面板 — 终端 / 输出 / 问题
 *
 * 终端标签页集成命令行输入，支持 help/clear/sim/record/play/filter 等命令
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

private slots:
    void onCommandReturnPressed();

private:
    QPlainTextEdit *m_terminal;
    QPlainTextEdit *m_output;
    QPlainTextEdit *m_pluginOutput;  ///< 插件输出文本框
    QTableWidget *m_problemsTable;
    QLineEdit *m_cmdInput;
    QLabel *m_problemCount;
};

#endif // BOTTOMPANEL_H
