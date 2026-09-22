#ifndef RIGHTPANEL_H
#define RIGHTPANEL_H

#include <QTabWidget>
#include <QVector>
#include <QHash>

#include "core/canframe.h"

class QPlainTextEdit;
class QLabel;
class QListWidget;
class QPushButton;
class QLineEdit;
class BookmarkManager;
class DbcManager;

/**
 * @brief Right assist dock — Inspector / Bookmarks / Watch
 *
 * Secondary chrome for measurement: inspect selection, jump bookmarks,
 * watch live ID/signal values. Not a second toolbar.
 */
class RightPanel : public QTabWidget
{
    Q_OBJECT

public:
    explicit RightPanel(QWidget *parent = nullptr);

    void setBookmarkManager(BookmarkManager *mgr);
    void setDbcManager(DbcManager *mgr);

public slots:
    void refreshBookmarks();
    /// Fill Inspector from a Trace selection (or clear when invalid).
    void inspectFrame(const CanFrame &frame);
    void clearInspection();
    /// Update Watch last-values from a live/playback frame batch.
    void updateFromFrames(const QVector<CanFrame> &frames);
    /// Pin a CAN ID (raw hex watch) from Inspector.
    void addWatchCanId(quint32 canId);
    /// Pin a decoded signal (id + name).
    void addWatchSignal(quint32 canId, const QString &signalName);

signals:
    void bookmarkJumped(int frameIndex);
    void openAiAgentRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct WatchEntry {
        quint32 canId = 0;
        QString signalName;   ///< empty → watch raw data hex
        QString lastValue;
        QString displayName() const;
    };

    void rebuildWatchList();
    void renderInspectorEmpty();
    void renderInspectorFrame(const CanFrame &frame);

    // Inspector
    QPlainTextEdit *m_inspectorText = nullptr;
    QPushButton *m_addWatchBtn = nullptr;
    QPushButton *m_aiAgentBtn = nullptr;
    CanFrame m_lastFrame;
    bool m_hasInspection = false;

    // Bookmarks
    QListWidget *m_bookmarkList = nullptr;
    BookmarkManager *m_bookmarkMgr = nullptr;

    // Watch
    QListWidget *m_watchList = nullptr;
    QLineEdit *m_watchIdEdit = nullptr;
    QVector<WatchEntry> m_watches;
    DbcManager *m_dbcMgr = nullptr;
};

#endif // RIGHTPANEL_H
