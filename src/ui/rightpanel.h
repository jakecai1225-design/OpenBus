#ifndef RIGHTPANEL_H
#define RIGHTPANEL_H

#include <QTabWidget>

class QPlainTextEdit;
class QLineEdit;
class QPushButton;
class QLabel;
class QListWidget;
class BookmarkManager;

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

    /// 刷新书签列表
    void refreshBookmarks();
    /// 设置书签管理器
    void setBookmarkManager(BookmarkManager *mgr);

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
    /// 书签跳转 (frameIndex)
    void bookmarkJumped(int frameIndex);

private:
    // AI 对话
    QPlainTextEdit *m_chatMessages;
    QLineEdit *m_chatInput;

    // 书签
    QListWidget *m_bookmarkList;
    BookmarkManager *m_bookmarkMgr = nullptr;

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
