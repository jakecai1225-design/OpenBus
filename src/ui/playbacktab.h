#ifndef PLAYBACKTAB_H
#define PLAYBACKTAB_H

#include <QWidget>
#include <QQueue>
#include <QTimer>
#include <QVariantMap>

class QPushButton;
class QSlider;
class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QProgressBar;

/**
 * @brief 回放标签页 — 中央区域
 *
 * 独立的回放控制界面，包含：
 * - 播放/暂停/停止、进度条、速度
 * - 循环回放、自动滚动
 * - 回放文件列表（多文件管理，上移/下移排序，双击加载）
 * - 文件信息自动解析（帧数、时长）
 * - 每文件回放进度显示
 * - 指定回放通道
 * - 回放过滤
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

private:
    void parseFileInfo(int row);
    void renumberRows();
    void addFilePath(const QString &path);

    // 回放控制
    QPushButton *m_playBtn;
    QPushButton *m_pauseBtn;
    QPushButton *m_stopBtn;
    QSlider *m_seekSlider;
    QLabel *m_posLabel;
    QLabel *m_fileInfoLabel;

    // 速度 & 选项
    QComboBox *m_speedCombo;
    QCheckBox *m_loopChk;
    QCheckBox *m_autoScrollChk;

    // 回放文件列表
    QTableWidget *m_fileList;
    QPushButton *m_addFileBtn;
    QPushButton *m_removeFileBtn;
    QPushButton *m_moveUpBtn;
    QPushButton *m_moveDownBtn;

    // 通道 & 过滤
    QComboBox *m_channelCombo;
    QLineEdit *m_filterEdit;

    // 异步文件解析
    QTimer *m_parseTimer;
    QQueue<int> m_parseQueue;
    int m_currentLoadedRow = -1;  // 当前已加载到 Player 的文件行
};

#endif // PLAYBACKTAB_H
