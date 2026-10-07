#include "recordtab.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QDesktopServices>
#include <QDir>
#include <QMessageBox>
#include <QUrl>
#include <QVariantMap>

RecordTab::RecordTab(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 16, 20, 16);
    mainLayout->setSpacing(8);
    setObjectName(QStringLiteral("FormPage"));

    auto *formHost = new QWidget(this);
    formHost->setObjectName(QStringLiteral("FormHost"));
    formHost->setMaximumWidth(640);
    auto *formLayout = new QVBoxLayout(formHost);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(8);

    auto *btnLayout = new QHBoxLayout;
    btnLayout->setSpacing(8);
    m_recordBtn = new QPushButton(this);
    m_recordBtn->setCheckable(true);
    m_recordBtn->setMinimumWidth(120);
    m_pauseBtn = new QPushButton(this);
    m_pauseBtn->setMinimumWidth(100);
    m_pauseBtn->setEnabled(false);
    m_stopBtn = new QPushButton(this);
    m_stopBtn->setMinimumWidth(100);
    m_stopBtn->setEnabled(false);
    btnLayout->addWidget(m_recordBtn);
    btnLayout->addWidget(m_pauseBtn);
    btnLayout->addWidget(m_stopBtn);
    btnLayout->addStretch();
    formLayout->addLayout(btnLayout);

    m_fileGroup = new QGroupBox(formHost);
    auto *fileLayout = new QGridLayout(m_fileGroup);
    fileLayout->setContentsMargins(0, 4, 0, 0);
    fileLayout->setSpacing(8);

    m_dirLabel = new QLabel(m_fileGroup);
    fileLayout->addWidget(m_dirLabel, 0, 0);
    m_dirEdit = new QLineEdit(QStringLiteral("D:/recordings"), m_fileGroup);
    fileLayout->addWidget(m_dirEdit, 0, 1);
    m_browseBtn = new QPushButton(m_fileGroup);
    fileLayout->addWidget(m_browseBtn, 0, 2);
    m_openDirBtn = new QPushButton(m_fileGroup);
    fileLayout->addWidget(m_openDirBtn, 0, 3);

    m_prefixLabel = new QLabel(m_fileGroup);
    fileLayout->addWidget(m_prefixLabel, 1, 0);
    m_prefixEdit = new QLineEdit(QStringLiteral("rec_"), m_fileGroup);
    fileLayout->addWidget(m_prefixEdit, 1, 1, 1, 2);

    m_formatLabel = new QLabel(m_fileGroup);
    fileLayout->addWidget(m_formatLabel, 2, 0);
    m_formatCombo = new QComboBox(m_fileGroup);
    m_formatCombo->addItem(QStringLiteral("BLF (.blf)"), QStringLiteral("blf"));
    m_formatCombo->addItem(QStringLiteral("ASC (.asc)"), QStringLiteral("asc"));
    m_formatCombo->addItem(QStringLiteral("CSV (.csv)"), QStringLiteral("csv"));
    m_formatCombo->setCurrentIndex(0);
    fileLayout->addWidget(m_formatCombo, 2, 1, 1, 2);
    formLayout->addWidget(m_fileGroup);

    m_splitGroup = new QGroupBox(formHost);
    auto *splitLayout = new QGridLayout(m_splitGroup);
    splitLayout->setContentsMargins(0, 4, 0, 0);
    splitLayout->setSpacing(8);

    m_splitBySize = new QCheckBox(m_splitGroup);
    m_splitBySize->setChecked(true);
    splitLayout->addWidget(m_splitBySize, 0, 0);
    m_everySizeLabel = new QLabel(m_splitGroup);
    splitLayout->addWidget(m_everySizeLabel, 0, 1);
    m_sizeSpin = new QSpinBox(m_splitGroup);
    m_sizeSpin->setRange(1, 9999);
    m_sizeSpin->setValue(100);
    splitLayout->addWidget(m_sizeSpin, 0, 2);

    m_splitByTime = new QCheckBox(m_splitGroup);
    splitLayout->addWidget(m_splitByTime, 1, 0);
    m_everyTimeLabel = new QLabel(m_splitGroup);
    splitLayout->addWidget(m_everyTimeLabel, 1, 1);
    m_timeSpin = new QSpinBox(m_splitGroup);
    m_timeSpin->setRange(1, 9999);
    m_timeSpin->setValue(60);
    m_timeSpin->setEnabled(false);
    splitLayout->addWidget(m_timeSpin, 1, 2);

    m_ringChk = new QCheckBox(m_splitGroup);
    splitLayout->addWidget(m_ringChk, 2, 0, 1, 3);
    connect(m_splitByTime, &QCheckBox::toggled, m_timeSpin, &QWidget::setEnabled);
    formLayout->addWidget(m_splitGroup);

    auto *bufLayout = new QHBoxLayout;
    m_bufferLabel = new QLabel(formHost);
    bufLayout->addWidget(m_bufferLabel);
    m_bufferCombo = new QComboBox(formHost);
    m_bufferCombo->addItem(QString(), 1000);
    m_bufferCombo->addItem(QString(), 5000);
    m_bufferCombo->addItem(QString(), 10000);
    m_bufferCombo->addItem(QString(), 50000);
    m_bufferCombo->setCurrentIndex(2);
    bufLayout->addWidget(m_bufferCombo);
    bufLayout->addStretch();
    formLayout->addLayout(bufLayout);

    m_filterGroup = new QGroupBox(formHost);
    auto *filterLayout = new QVBoxLayout(m_filterGroup);
    filterLayout->setContentsMargins(0, 4, 0, 0);

    auto *filterRow = new QHBoxLayout;
    m_filterAll = new QCheckBox(m_filterGroup);
    m_filterAll->setChecked(true);
    m_filterRx = new QCheckBox(m_filterGroup);
    m_filterTx = new QCheckBox(m_filterGroup);
    m_filterFd = new QCheckBox(m_filterGroup);
    filterRow->addWidget(m_filterAll);
    filterRow->addWidget(m_filterRx);
    filterRow->addWidget(m_filterTx);
    filterRow->addWidget(m_filterFd);
    filterRow->addStretch();
    filterLayout->addLayout(filterRow);

    auto *idRow = new QHBoxLayout;
    m_idFilterLabel = new QLabel(m_filterGroup);
    idRow->addWidget(m_idFilterLabel);
    m_idFilterEdit = new QLineEdit(m_filterGroup);
    idRow->addWidget(m_idFilterEdit, 1);
    filterLayout->addLayout(idRow);
    formLayout->addWidget(m_filterGroup);

    m_triggerGroup = new QGroupBox(formHost);
    auto *triggerLayout = new QGridLayout(m_triggerGroup);
    triggerLayout->setContentsMargins(0, 4, 0, 0);
    triggerLayout->setSpacing(8);

    m_triggerEnable = new QCheckBox(m_triggerGroup);
    triggerLayout->addWidget(m_triggerEnable, 0, 0, 1, 4);

    m_triggerCondLabel = new QLabel(m_triggerGroup);
    triggerLayout->addWidget(m_triggerCondLabel, 1, 0);
    m_triggerExprEdit = new QLineEdit(m_triggerGroup);
    triggerLayout->addWidget(m_triggerExprEdit, 1, 1, 1, 3);

    m_preTriggerLabel = new QLabel(m_triggerGroup);
    triggerLayout->addWidget(m_preTriggerLabel, 2, 0);
    m_preTriggerSpin = new QDoubleSpinBox(m_triggerGroup);
    m_preTriggerSpin->setRange(0.1, 600.0);
    m_preTriggerSpin->setValue(5.0);
    m_preTriggerSpin->setSuffix(QStringLiteral(" s"));
    triggerLayout->addWidget(m_preTriggerSpin, 2, 1);

    m_postTriggerLabel = new QLabel(m_triggerGroup);
    triggerLayout->addWidget(m_postTriggerLabel, 2, 2);
    m_postTriggerSpin = new QDoubleSpinBox(m_triggerGroup);
    m_postTriggerSpin->setRange(0.1, 3600.0);
    m_postTriggerSpin->setValue(10.0);
    m_postTriggerSpin->setSuffix(QStringLiteral(" s"));
    triggerLayout->addWidget(m_postTriggerSpin, 2, 3);

    m_repeatTriggerChk = new QCheckBox(m_triggerGroup);
    m_repeatTriggerChk->setChecked(true);
    triggerLayout->addWidget(m_repeatTriggerChk, 3, 0, 1, 2);

    m_triggerRecordBtn = new QPushButton(m_triggerGroup);
    m_triggerRecordBtn->setCheckable(true);
    triggerLayout->addWidget(m_triggerRecordBtn, 3, 2, 1, 2);
    formLayout->addWidget(m_triggerGroup);

    m_statusLabel = new QLabel(formHost);
    m_statusLabel->setObjectName(QStringLiteral("StatusDim"));
    formLayout->addWidget(m_statusLabel);

    formLayout->addStretch();
    mainLayout->addWidget(formHost, 0, Qt::AlignLeft | Qt::AlignTop);
    mainLayout->addStretch(1);

    connect(m_browseBtn, &QPushButton::clicked, this, &RecordTab::onBrowse);
    connect(m_openDirBtn, &QPushButton::clicked, this, &RecordTab::onOpenDir);
    connect(m_recordBtn, &QPushButton::toggled, this, &RecordTab::onRecord);
    connect(m_triggerRecordBtn, &QPushButton::toggled, this, &RecordTab::onTriggerRecord);
    connect(m_pauseBtn, &QPushButton::clicked, this, &RecordTab::onPauseClicked);
    connect(m_stopBtn, &QPushButton::clicked, this, &RecordTab::onStopClicked);

    retranslateUi();
}

void RecordTab::retranslateUi()
{
    m_fileGroup->setTitle(tr("File Settings"));
    m_dirLabel->setText(tr("Directory:"));
    m_browseBtn->setText(tr("Browse..."));
    m_openDirBtn->setText(tr("Open Folder"));
    m_openDirBtn->setToolTip(tr("Open the recording directory in the system file manager"));
    m_prefixLabel->setText(tr("File name prefix:"));
    m_formatLabel->setText(tr("File format:"));

    m_splitGroup->setTitle(tr("File Splitting"));
    m_splitBySize->setText(tr("By size"));
    m_everySizeLabel->setText(tr("Every"));
    m_sizeSpin->setSuffix(tr(" MB"));
    m_splitByTime->setText(tr("By time"));
    m_everyTimeLabel->setText(tr("Every"));
    m_timeSpin->setSuffix(tr(" s"));
    m_ringChk->setText(tr("Ring mode (overwrite oldest files)"));

    m_bufferLabel->setText(tr("Buffer size:"));
    const int bi = m_bufferCombo->currentIndex();
    m_bufferCombo->setItemText(0, tr("%1 frames").arg(1000));
    m_bufferCombo->setItemText(1, tr("%1 frames").arg(5000));
    m_bufferCombo->setItemText(2, tr("%1 frames").arg(10000));
    m_bufferCombo->setItemText(3, tr("%1 frames").arg(50000));
    if (bi >= 0 && bi < m_bufferCombo->count())
        m_bufferCombo->setCurrentIndex(bi);

    m_filterGroup->setTitle(tr("Recording Filter"));
    m_filterAll->setText(tr("All"));
    m_filterRx->setText(tr("Rx only"));
    m_filterTx->setText(tr("Tx only"));
    m_filterFd->setText(tr("CAN FD only"));
    m_idFilterLabel->setText(tr("ID filter:"));
    m_idFilterEdit->setPlaceholderText(tr("Comma-separated, e.g. 0x123,0x456"));

    m_triggerGroup->setTitle(tr("Trigger Recording"));
    m_triggerEnable->setText(tr("Enable trigger recording"));
    m_triggerCondLabel->setText(tr("Trigger condition:"));
    m_triggerExprEdit->setPlaceholderText(
        tr("e.g. id == 0x1A5 and data[0] == 0xFF"));
    m_preTriggerLabel->setText(tr("Pre-buffer:"));
    m_postTriggerLabel->setText(tr("Post-record:"));
    m_repeatTriggerChk->setText(tr("Repeat trigger"));

    m_stopBtn->setText(tr("Stop"));
    refreshDynamicLabels();
}

void RecordTab::refreshDynamicLabels()
{
    const bool recording = m_recordBtn->isChecked();
    m_recordBtn->setText(recording ? tr("Stop Recording") : tr("Start Recording"));
    m_pauseBtn->setText(m_paused ? tr("Resume") : tr("Pause"));
    const bool triggerOn = m_triggerRecordBtn->isChecked();
    m_triggerRecordBtn->setText(
        triggerOn ? tr("Stop Trigger Recording") : tr("Start Trigger Recording"));

    if (!recording)
        m_statusLabel->setText(tr("Status: Idle"));
    else if (m_paused)
        m_statusLabel->setText(tr("Status: Paused"));
    else
        m_statusLabel->setText(tr("Status: Recording..."));
}

void RecordTab::onBrowse()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select recording directory"), m_dirEdit->text());
    if (!dir.isEmpty())
        m_dirEdit->setText(dir);
}

void RecordTab::onOpenDir()
{
    const QString dir = m_dirEdit->text().trimmed();
    if (dir.isEmpty()) {
        QMessageBox::information(this, tr("Open Folder"),
                                 tr("Please set a recording directory first."));
        return;
    }
    if (!QDir(dir).exists()) {
        QMessageBox::information(this, tr("Open Folder"),
            tr("Directory does not exist:\n%1\n\n"
               "(It will be created when recording starts.)").arg(dir));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(dir).absolutePath()));
}

void RecordTab::onRecord()
{
    const bool on = m_recordBtn->isChecked();
    if (!on)
        m_paused = false;
    m_pauseBtn->setEnabled(on);
    m_stopBtn->setEnabled(on);
    m_statusLabel->setObjectName(on ? QStringLiteral("StatusRec")
                                    : QStringLiteral("StatusDim"));
    refreshDynamicLabels();
    emit recordToggled(on);
}

void RecordTab::setRecording(bool recording)
{
    m_recordBtn->blockSignals(true);
    m_recordBtn->setChecked(recording);
    if (!recording)
        m_paused = false;
    m_pauseBtn->setEnabled(recording);
    m_stopBtn->setEnabled(recording);
    m_statusLabel->setObjectName(recording ? QStringLiteral("StatusRec")
                                           : QStringLiteral("StatusDim"));
    m_recordBtn->blockSignals(false);
    refreshDynamicLabels();
}

void RecordTab::onTriggerRecord()
{
    const bool on = m_triggerRecordBtn->isChecked();
    refreshDynamicLabels();

    if (on) {
        if (!m_triggerEnable->isChecked()) {
            m_triggerRecordBtn->blockSignals(true);
            m_triggerRecordBtn->setChecked(false);
            m_triggerRecordBtn->blockSignals(false);
            refreshDynamicLabels();
            return;
        }
        emit triggerRecordingRequested(
            m_dirEdit->text(),
            m_prefixEdit->text(),
            m_formatCombo->currentData().toString(),
            m_splitBySize->isChecked(),
            m_sizeSpin->value(),
            m_splitByTime->isChecked(),
            m_timeSpin->value(),
            m_ringChk->isChecked(),
            10,
            m_triggerExprEdit->text(),
            m_preTriggerSpin->value(),
            m_postTriggerSpin->value(),
            m_repeatTriggerChk->isChecked());
    } else {
        emit triggerRecordingStopped();
    }
}

void RecordTab::onPauseClicked()
{
    m_paused = !m_paused;
    refreshDynamicLabels();
    emit pauseRequested(m_paused);
}

void RecordTab::onStopClicked()
{
    m_recordBtn->setChecked(false);
    m_paused = false;
    m_pauseBtn->setEnabled(false);
    m_stopBtn->setEnabled(false);
    refreshDynamicLabels();
}

QVariantMap RecordTab::configMap() const
{
    QVariantMap m;
    m.insert(QStringLiteral("directory"), m_dirEdit->text());
    m.insert(QStringLiteral("prefix"), m_prefixEdit->text());
    m.insert(QStringLiteral("format"), m_formatCombo->currentData().toString());
    m.insert(QStringLiteral("splitBySize"), m_splitBySize->isChecked());
    m.insert(QStringLiteral("sizeMb"), m_sizeSpin->value());
    m.insert(QStringLiteral("splitByTime"), m_splitByTime->isChecked());
    m.insert(QStringLiteral("timeSec"), m_timeSpin->value());
    m.insert(QStringLiteral("ringMode"), m_ringChk->isChecked());
    m.insert(QStringLiteral("maxFiles"), 10);
    m.insert(QStringLiteral("bufferIndex"), m_bufferCombo->currentIndex());
    m.insert(QStringLiteral("filterAll"), m_filterAll->isChecked());
    m.insert(QStringLiteral("filterRx"), m_filterRx->isChecked());
    m.insert(QStringLiteral("filterTx"), m_filterTx->isChecked());
    m.insert(QStringLiteral("filterFd"), m_filterFd->isChecked());
    m.insert(QStringLiteral("idFilter"), m_idFilterEdit->text());
    m.insert(QStringLiteral("triggerEnable"), m_triggerEnable->isChecked());
    m.insert(QStringLiteral("triggerExpr"), m_triggerExprEdit->text());
    m.insert(QStringLiteral("preTrigger"), m_preTriggerSpin->value());
    m.insert(QStringLiteral("postTrigger"), m_postTriggerSpin->value());
    m.insert(QStringLiteral("repeatTrigger"), m_repeatTriggerChk->isChecked());
    return m;
}

void RecordTab::loadConfig(const QVariantMap &map)
{
    if (map.isEmpty())
        return;

    if (map.contains(QStringLiteral("directory")))
        m_dirEdit->setText(map.value(QStringLiteral("directory")).toString());
    if (map.contains(QStringLiteral("prefix")))
        m_prefixEdit->setText(map.value(QStringLiteral("prefix")).toString());
    if (map.contains(QStringLiteral("format"))) {
        const QString fmt = map.value(QStringLiteral("format")).toString();
        const int idx = m_formatCombo->findData(fmt);
        if (idx >= 0)
            m_formatCombo->setCurrentIndex(idx);
    }
    if (map.contains(QStringLiteral("splitBySize")))
        m_splitBySize->setChecked(map.value(QStringLiteral("splitBySize")).toBool());
    if (map.contains(QStringLiteral("sizeMb")))
        m_sizeSpin->setValue(map.value(QStringLiteral("sizeMb")).toInt());
    if (map.contains(QStringLiteral("splitByTime")))
        m_splitByTime->setChecked(map.value(QStringLiteral("splitByTime")).toBool());
    if (map.contains(QStringLiteral("timeSec")))
        m_timeSpin->setValue(map.value(QStringLiteral("timeSec")).toInt());
    if (map.contains(QStringLiteral("ringMode")))
        m_ringChk->setChecked(map.value(QStringLiteral("ringMode")).toBool());
    if (map.contains(QStringLiteral("bufferIndex"))) {
        const int bi = map.value(QStringLiteral("bufferIndex")).toInt();
        if (bi >= 0 && bi < m_bufferCombo->count())
            m_bufferCombo->setCurrentIndex(bi);
    }
    if (map.contains(QStringLiteral("filterAll")))
        m_filterAll->setChecked(map.value(QStringLiteral("filterAll")).toBool());
    if (map.contains(QStringLiteral("filterRx")))
        m_filterRx->setChecked(map.value(QStringLiteral("filterRx")).toBool());
    if (map.contains(QStringLiteral("filterTx")))
        m_filterTx->setChecked(map.value(QStringLiteral("filterTx")).toBool());
    if (map.contains(QStringLiteral("filterFd")))
        m_filterFd->setChecked(map.value(QStringLiteral("filterFd")).toBool());
    if (map.contains(QStringLiteral("idFilter")))
        m_idFilterEdit->setText(map.value(QStringLiteral("idFilter")).toString());
    if (map.contains(QStringLiteral("triggerEnable")))
        m_triggerEnable->setChecked(map.value(QStringLiteral("triggerEnable")).toBool());
    if (map.contains(QStringLiteral("triggerExpr")))
        m_triggerExprEdit->setText(map.value(QStringLiteral("triggerExpr")).toString());
    if (map.contains(QStringLiteral("preTrigger")))
        m_preTriggerSpin->setValue(map.value(QStringLiteral("preTrigger")).toDouble());
    if (map.contains(QStringLiteral("postTrigger")))
        m_postTriggerSpin->setValue(map.value(QStringLiteral("postTrigger")).toDouble());
    if (map.contains(QStringLiteral("repeatTrigger")))
        m_repeatTriggerChk->setChecked(map.value(QStringLiteral("repeatTrigger")).toBool());
}
