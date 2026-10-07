#ifndef RECORDTAB_H
#define RECORDTAB_H

#include <QWidget>
#include <QVariantMap>

class QPushButton;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QLabel;
class QSpinBox;
class QGroupBox;
class QDoubleSpinBox;

/**
 * @brief Record control tab — file path/prefix/format, split, buffer, filter, trigger.
 * Source strings are English; translations come from openbus_*.qm.
 */
class RecordTab : public QWidget
{
    Q_OBJECT

public:
    explicit RecordTab(QWidget *parent = nullptr);

    void setRecording(bool recording);
    void retranslateUi();

    QString directory() const { return m_dirEdit->text(); }
    QString prefix() const { return m_prefixEdit->text(); }
    QString format() const { return m_formatCombo->currentData().toString(); }

    QVariantMap configMap() const;
    void loadConfig(const QVariantMap &map);

signals:
    void recordToggled(bool on);
    void pauseRequested(bool paused);
    void triggerRecordingStopped();
    void triggerRecordingRequested(
        const QString &dir, const QString &prefix, const QString &format,
        bool splitBySize, int sizeMb, bool splitByTime, int timeSec,
        bool ringMode, int maxFiles,
        const QString &triggerExpr, double preTriggerSec, double postTriggerSec,
        bool repeatTrigger);

private slots:
    void onBrowse();
    void onOpenDir();
    void onRecord();
    void onTriggerRecord();
    void onPauseClicked();
    void onStopClicked();

private:
    void refreshDynamicLabels();

    QPushButton *m_recordBtn = nullptr;
    QPushButton *m_pauseBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QLineEdit *m_dirEdit = nullptr;
    QLineEdit *m_prefixEdit = nullptr;
    QComboBox *m_formatCombo = nullptr;
    QCheckBox *m_splitBySize = nullptr;
    QSpinBox *m_sizeSpin = nullptr;
    QCheckBox *m_splitByTime = nullptr;
    QSpinBox *m_timeSpin = nullptr;
    QCheckBox *m_ringChk = nullptr;
    QComboBox *m_bufferCombo = nullptr;
    QCheckBox *m_filterAll = nullptr;
    QCheckBox *m_filterRx = nullptr;
    QCheckBox *m_filterTx = nullptr;
    QCheckBox *m_filterFd = nullptr;
    QLineEdit *m_idFilterEdit = nullptr;
    QLabel *m_statusLabel = nullptr;

    QGroupBox *m_fileGroup = nullptr;
    QGroupBox *m_splitGroup = nullptr;
    QGroupBox *m_filterGroup = nullptr;
    QGroupBox *m_triggerGroup = nullptr;
    QLabel *m_dirLabel = nullptr;
    QLabel *m_prefixLabel = nullptr;
    QLabel *m_formatLabel = nullptr;
    QLabel *m_everySizeLabel = nullptr;
    QLabel *m_everyTimeLabel = nullptr;
    QLabel *m_bufferLabel = nullptr;
    QLabel *m_idFilterLabel = nullptr;
    QLabel *m_triggerCondLabel = nullptr;
    QLabel *m_preTriggerLabel = nullptr;
    QLabel *m_postTriggerLabel = nullptr;
    QPushButton *m_browseBtn = nullptr;
    QPushButton *m_openDirBtn = nullptr;
    QCheckBox *m_triggerEnable = nullptr;
    QLineEdit *m_triggerExprEdit = nullptr;
    QDoubleSpinBox *m_preTriggerSpin = nullptr;
    QDoubleSpinBox *m_postTriggerSpin = nullptr;
    QCheckBox *m_repeatTriggerChk = nullptr;
    QPushButton *m_triggerRecordBtn = nullptr;

    bool m_paused = false;
};

#endif // RECORDTAB_H
