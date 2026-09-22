#ifndef PLAYBACKTAB_H
#define PLAYBACKTAB_H

#include <QWidget>
#include <QQueue>
#include <QTimer>
#include <QVariantMap>
#include <QStringList>

class QPushButton;
class QSlider;
class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QProgressBar;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QEvent;

/**
 * @brief Playback tab — central workspace
 *
 * Controls, multi-file list (add / drag-drop / reorder), auto metadata parse,
 * auto-load into Player after add (Play without double-click), channel + filters.
 */
class PlaybackTab : public QWidget
{
    Q_OBJECT

public:
    explicit PlaybackTab(QWidget *parent = nullptr);

    void setPlayerLoaded(bool loaded, bool playing);
    void setProgress(int cur, int total, double curTime, double totalTime);
    void setFileInfo(const QString &fileName, int frames, double duration);

    /// Full UI config for project save/restore
    QVariantMap configMap() const;
    void loadConfig(const QVariantMap &map);

signals:
    void playRequested();
    void pauseRequested();
    void stopRequested();
    void speedChanged(double speed);
    void seekChanged(double ratio);
    void fileLoaded(const QString &path);
    void loopToggled(bool on);
    void autoScrollToggled(bool on);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onPlay();
    void onPause();
    void onStop();
    void onAddFile();
    void onRemoveFile();
    void onMoveUp();
    void onMoveDown();
    void onFileListDoubleClicked(int row, int col);
    void onParseTimer();
    void onApplyFilter();

private:
    void parseFileInfo(int row);
    void renumberRows();
    void addFilePath(const QString &path);
    void addFiles(const QStringList &paths);
    void loadRowIntoPlayer(int row);
    void markFilterDirty();
    static bool isPlaybackFile(const QString &path);
    static QStringList playbackPathsFromMime(const QMimeData *mime);

    // Playback control
    QPushButton *m_playBtn;
    QPushButton *m_pauseBtn;
    QPushButton *m_stopBtn;
    QSlider *m_seekSlider;
    QLabel *m_posLabel;
    QLabel *m_fileInfoLabel;

    // Speed & options
    QComboBox *m_speedCombo;
    QCheckBox *m_loopChk;
    QCheckBox *m_autoScrollChk;

    // File list
    QTableWidget *m_fileList;
    QPushButton *m_addFileBtn;
    QPushButton *m_removeFileBtn;
    QPushButton *m_moveUpBtn;
    QPushButton *m_moveDownBtn;

    // Channel & filter
    QComboBox *m_channelCombo;
    QComboBox *m_directionCombo;
    QComboBox *m_protocolCombo;
    QLineEdit *m_filterEdit;
    QPushButton *m_applyFilterBtn;

    // Applied filter (active only after Apply). Until then: no filter.
    bool m_filterApplied = false;
    QString m_appliedDirection = QStringLiteral("all");
    QString m_appliedProtocol = QStringLiteral("all");
    QString m_appliedFilter;

    // Async file parse
    QTimer *m_parseTimer;
    QQueue<int> m_parseQueue;
    int m_currentLoadedRow = -1;
};

#endif // PLAYBACKTAB_H
