#ifndef RIGHTPANEL_H
#define RIGHTPANEL_H

#include <QTabWidget>

class QPlainTextEdit;
class QLineEdit;
class QPushButton;
class QLabel;

/**
 * @brief 右侧面板 — AI 对话 + 快捷按钮
 */
class RightPanel : public QTabWidget
{
    Q_OBJECT

public:
    explicit RightPanel(QWidget *parent = nullptr);

public slots:
    void appendAiMessage(const QString &role, const QString &text);

signals:
    void aiMessageSent(const QString &text);
    void recordRequested();
    void stopRecordRequested();
    void playRequested();
    void pauseRequested();
    void stopRequested();
    void clearTraceRequested();
    void autoScrollToggled(bool on);
    void connectRequested();
    void disconnectRequested();

private:
    // AI 对话
    QPlainTextEdit *m_chatMessages;
    QLineEdit *m_chatInput;

    // 快捷按钮
    QPushButton *m_recordBtn;
    QPushButton *m_stopRecBtn;
    QPushButton *m_playBtn;
    QPushButton *m_pauseBtn;
    QPushButton *m_stopBtn;
    QPushButton *m_clearTraceBtn;
    QPushButton *m_autoScrollBtn;
    QPushButton *m_connectBtn;
    QPushButton *m_disconnectBtn;
};

#endif // RIGHTPANEL_H
