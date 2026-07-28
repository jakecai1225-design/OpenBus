#ifndef PLAYBACKTAB_H
#define PLAYBACKTAB_H

#include <QWidget>

class QPushButton;
class QSlider;
class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QTableWidget;

/**
 * @brief 回放标签页 — 中央区域
 *
 * 独立的回放控制界面，包含：
 * - 播放/暂停/停止、进度条、速度
 * - 循环回放、自动滚动
 * - 回放文件列表（多文件管理，双击加载）
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

signals:
    void playRequested();
    void pauseRequested();
    void stopRequested();
    void speedChanged(double speed);
    void seekChanged(double ratio);
    void fileLoaded(const QString &path);

private slots:
    void onPlay();
    void onPause();
    void onStop();
    void onAddFile();
    void onRemoveFile();
    void onFileListDoubleClicked(int row, int col);

private:
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

    // 通道 & 过滤
    QComboBox *m_channelCombo;
    QLineEdit *m_filterEdit;
};

#endif // PLAYBACKTAB_H
