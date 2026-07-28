#ifndef PLAYBACKTAB_H
#define PLAYBACKTAB_H

#include <QWidget>

class QPushButton;
class QSlider;
class QComboBox;
class QCheckBox;
class QLabel;

/**
 * @brief 回放控制标签页 — 中央区域
 *
 * 包含播放/暂停/停止、进度条、速度选择、循环/自动滚动选项
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
    void changeFileRequested();

private:
    QPushButton *m_playBtn;
    QPushButton *m_pauseBtn;
    QPushButton *m_stopBtn;
    QSlider *m_seekSlider;
    QLabel *m_posLabel;
    QComboBox *m_speedCombo;
    QCheckBox *m_loopChk;
    QCheckBox *m_autoScrollChk;
    QLabel *m_fileLabel;
};

#endif // PLAYBACKTAB_H
