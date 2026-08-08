#ifndef RECORDTAB_H
#define RECORDTAB_H

#include <QWidget>

class QPushButton;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QLabel;
class QRadioButton;
class QSpinBox;
class QGroupBox;
class QDoubleSpinBox;

/**
 * @brief 录制控制标签页 — 中央区域
 *
 * 包含录制按钮、文件路径/前缀/格式、文件分割、缓冲区、录制过滤、触发录制
 */
class RecordTab : public QWidget
{
    Q_OBJECT

public:
    explicit RecordTab(QWidget *parent = nullptr);

    void setRecording(bool recording);

signals:
    void recordToggled(bool on);
    void clearRequested();
    void autoScrollToggled(bool on);
    void triggerRecordingRequested(
        const QString &dir, const QString &prefix, const QString &format,
        bool splitBySize, int sizeMb, bool splitByTime, int timeSec,
        bool ringMode, int maxFiles,
        const QString &triggerExpr, double preTriggerSec, double postTriggerSec,
        bool repeatTrigger);

private slots:
    void onBrowse();
    void onRecord();
    void onTriggerRecord();

private:
    QPushButton *m_recordBtn;
    QPushButton *m_pauseBtn;
    QPushButton *m_stopBtn;
    QLineEdit *m_dirEdit;
    QLineEdit *m_prefixEdit;
    QComboBox *m_formatCombo;
    QCheckBox *m_splitBySize;
    QSpinBox *m_sizeSpin;
    QCheckBox *m_splitByTime;
    QSpinBox *m_timeSpin;
    QComboBox *m_bufferCombo;
    QCheckBox *m_filterAll;
    QCheckBox *m_filterRx;
    QCheckBox *m_filterTx;
    QCheckBox *m_filterFd;
    QLineEdit *m_idFilterEdit;
    QLabel *m_statusLabel;

    // 触发录制
    QGroupBox *m_triggerGroup;
    QCheckBox *m_triggerEnable;
    QLineEdit *m_triggerExprEdit;
    QDoubleSpinBox *m_preTriggerSpin;
    QDoubleSpinBox *m_postTriggerSpin;
    QCheckBox *m_repeatTriggerChk;
    QPushButton *m_triggerRecordBtn;
};

#endif // RECORDTAB_H
