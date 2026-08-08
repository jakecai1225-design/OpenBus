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
#include <QDateTime>

RecordTab::RecordTab(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    // ---- 录制按钮 ----
    auto *btnLayout = new QHBoxLayout;
    btnLayout->setSpacing(8);
    m_recordBtn = new QPushButton("开始录制", this);
    m_recordBtn->setCheckable(true);
    m_recordBtn->setMinimumWidth(120);
    m_pauseBtn = new QPushButton("暂停", this);
    m_pauseBtn->setMinimumWidth(100);
    m_pauseBtn->setEnabled(false);
    m_stopBtn = new QPushButton("停止", this);
    m_stopBtn->setMinimumWidth(100);
    m_stopBtn->setEnabled(false);
    btnLayout->addWidget(m_recordBtn);
    btnLayout->addWidget(m_pauseBtn);
    btnLayout->addWidget(m_stopBtn);
    btnLayout->addStretch();
    mainLayout->addLayout(btnLayout);

    // ---- 文件设置 ----
    auto *fileGroup = new QGroupBox("文件设置", this);
    auto *fileLayout = new QGridLayout(fileGroup);
    fileLayout->setSpacing(6);

    fileLayout->addWidget(new QLabel("文件目录:", fileGroup), 0, 0);
    m_dirEdit = new QLineEdit("D:/recordings", fileGroup);
    fileLayout->addWidget(m_dirEdit, 0, 1);
    auto *browseBtn = new QPushButton("浏览...", fileGroup);
    fileLayout->addWidget(browseBtn, 0, 2);

    fileLayout->addWidget(new QLabel("文件名称前缀:", fileGroup), 1, 0);
    m_prefixEdit = new QLineEdit("rec_", fileGroup);
    fileLayout->addWidget(m_prefixEdit, 1, 1, 1, 2);

    fileLayout->addWidget(new QLabel("文件格式:", fileGroup), 2, 0);
    m_formatCombo = new QComboBox(fileGroup);
    m_formatCombo->addItem("BLF (.blf)", "blf");
    m_formatCombo->addItem("ASC (.asc)", "asc");
    m_formatCombo->addItem("CSV (.csv)", "csv");
    m_formatCombo->setCurrentIndex(0);
    fileLayout->addWidget(m_formatCombo, 2, 1, 1, 2);

    mainLayout->addWidget(fileGroup);

    // ---- 文件分割 ----
    auto *splitGroup = new QGroupBox("文件分割", this);
    auto *splitLayout = new QGridLayout(splitGroup);
    splitLayout->setSpacing(6);

    m_splitBySize = new QCheckBox("按大小", splitGroup);
    m_splitBySize->setChecked(true);
    splitLayout->addWidget(m_splitBySize, 0, 0);
    splitLayout->addWidget(new QLabel("每", splitGroup), 0, 1);
    m_sizeSpin = new QSpinBox(splitGroup);
    m_sizeSpin->setRange(1, 9999);
    m_sizeSpin->setValue(100);
    m_sizeSpin->setSuffix(" MB");
    splitLayout->addWidget(m_sizeSpin, 0, 2);

    m_splitByTime = new QCheckBox("按时间", splitGroup);
    splitLayout->addWidget(m_splitByTime, 1, 0);
    splitLayout->addWidget(new QLabel("每", splitGroup), 1, 1);
    m_timeSpin = new QSpinBox(splitGroup);
    m_timeSpin->setRange(1, 9999);
    m_timeSpin->setValue(60);
    m_timeSpin->setSuffix(" 秒");
    m_timeSpin->setEnabled(false);
    splitLayout->addWidget(m_timeSpin, 1, 2);

    // ---- 环形模式 ----
    auto *ringChk = new QCheckBox("环形模式 (覆盖最旧文件)", splitGroup);
    splitLayout->addWidget(ringChk, 2, 0, 1, 3);

    connect(m_splitByTime, &QCheckBox::toggled, m_timeSpin, &QWidget::setEnabled);

    mainLayout->addWidget(splitGroup);

    // ---- 缓冲区 ----
    auto *bufLayout = new QHBoxLayout;
    bufLayout->addWidget(new QLabel("缓冲区大小:", this));
    m_bufferCombo = new QComboBox(this);
    m_bufferCombo->addItem("1000 帧");
    m_bufferCombo->addItem("5000 帧");
    m_bufferCombo->addItem("10000 帧");
    m_bufferCombo->addItem("50000 帧");
    m_bufferCombo->setCurrentIndex(2);
    bufLayout->addWidget(m_bufferCombo);
    bufLayout->addStretch();
    mainLayout->addLayout(bufLayout);

    // ---- 录制过滤 ----
    auto *filterGroup = new QGroupBox("录制过滤", this);
    auto *filterLayout = new QVBoxLayout(filterGroup);

    auto *filterRow = new QHBoxLayout;
    m_filterAll = new QCheckBox("全部", filterGroup);
    m_filterAll->setChecked(true);
    m_filterRx = new QCheckBox("仅 Rx", filterGroup);
    m_filterTx = new QCheckBox("仅 Tx", filterGroup);
    m_filterFd = new QCheckBox("仅 CAN FD", filterGroup);
    filterRow->addWidget(m_filterAll);
    filterRow->addWidget(m_filterRx);
    filterRow->addWidget(m_filterTx);
    filterRow->addWidget(m_filterFd);
    filterRow->addStretch();
    filterLayout->addLayout(filterRow);

    auto *idRow = new QHBoxLayout;
    idRow->addWidget(new QLabel("ID 过滤:", filterGroup));
    m_idFilterEdit = new QLineEdit(filterGroup);
    m_idFilterEdit->setPlaceholderText("逗号分隔, 例: 0x123,0x456");
    idRow->addWidget(m_idFilterEdit, 1);
    filterLayout->addLayout(idRow);

    mainLayout->addWidget(filterGroup);

    // ---- 触发录制 ----
    m_triggerGroup = new QGroupBox("触发录制 (Trigger Recording)", this);
    auto *triggerLayout = new QGridLayout(m_triggerGroup);
    triggerLayout->setSpacing(6);

    m_triggerEnable = new QCheckBox("启用触发录制", m_triggerGroup);
    triggerLayout->addWidget(m_triggerEnable, 0, 0, 1, 4);

    triggerLayout->addWidget(new QLabel("触发条件:", m_triggerGroup), 1, 0);
    m_triggerExprEdit = new QLineEdit(m_triggerGroup);
    m_triggerExprEdit->setPlaceholderText("例: id == 0x1A5 and data[0] == 0xFF");
    triggerLayout->addWidget(m_triggerExprEdit, 1, 1, 1, 3);

    triggerLayout->addWidget(new QLabel("前置缓冲(s):", m_triggerGroup), 2, 0);
    m_preTriggerSpin = new QDoubleSpinBox(m_triggerGroup);
    m_preTriggerSpin->setRange(0.1, 600.0);
    m_preTriggerSpin->setValue(5.0);
    m_preTriggerSpin->setSuffix(" s");
    triggerLayout->addWidget(m_preTriggerSpin, 2, 1);

    triggerLayout->addWidget(new QLabel("后置录制(s):", m_triggerGroup), 2, 2);
    m_postTriggerSpin = new QDoubleSpinBox(m_triggerGroup);
    m_postTriggerSpin->setRange(0.1, 3600.0);
    m_postTriggerSpin->setValue(10.0);
    m_postTriggerSpin->setSuffix(" s");
    triggerLayout->addWidget(m_postTriggerSpin, 2, 3);

    m_repeatTriggerChk = new QCheckBox("重复触发", m_triggerGroup);
    m_repeatTriggerChk->setChecked(true);
    triggerLayout->addWidget(m_repeatTriggerChk, 3, 0, 1, 2);

    m_triggerRecordBtn = new QPushButton("开始触发录制", m_triggerGroup);
    m_triggerRecordBtn->setCheckable(true);
    triggerLayout->addWidget(m_triggerRecordBtn, 3, 2, 1, 2);

    mainLayout->addWidget(m_triggerGroup);

    // ---- 状态 ----
    m_statusLabel = new QLabel("状态: 未录制", this);
    mainLayout->addWidget(m_statusLabel);

    mainLayout->addStretch();

    // ---- 信号连接 ----
    connect(browseBtn, &QPushButton::clicked, this, &RecordTab::onBrowse);
    connect(m_recordBtn, &QPushButton::toggled, this, &RecordTab::onRecord);
    connect(m_triggerRecordBtn, &QPushButton::toggled, this, &RecordTab::onTriggerRecord);
}

void RecordTab::onBrowse()
{
    QString dir = QFileDialog::getExistingDirectory(this, "选择录制目录", m_dirEdit->text());
    if (!dir.isEmpty())
        m_dirEdit->setText(dir);
}

void RecordTab::onRecord()
{
    bool on = m_recordBtn->isChecked();
    m_recordBtn->setText(on ? "停止录制" : "开始录制");
    m_pauseBtn->setEnabled(on);
    m_stopBtn->setEnabled(on);
    m_statusLabel->setText(on ? "状态: 录制中..." : "状态: 未录制");
    m_statusLabel->setObjectName(on ? "StatusRec" : "StatusDim");
    emit recordToggled(on);
}

void RecordTab::setRecording(bool recording)
{
    m_recordBtn->blockSignals(true);
    m_recordBtn->setChecked(recording);
    m_recordBtn->setText(recording ? "停止录制" : "开始录制");
    m_pauseBtn->setEnabled(recording);
    m_stopBtn->setEnabled(recording);
    m_statusLabel->setText(recording ? "状态: 录制中..." : "状态: 未录制");
    m_statusLabel->setObjectName(recording ? "StatusRec" : "StatusDim");
    m_recordBtn->blockSignals(false);
}

void RecordTab::onTriggerRecord()
{
    bool on = m_triggerRecordBtn->isChecked();
    m_triggerRecordBtn->setText(on ? "停止触发录制" : "开始触发录制");

    if (on) {
        // 发送触发录制配置
        emit triggerRecordingRequested(
            m_dirEdit->text(),
            m_prefixEdit->text(),
            m_formatCombo->currentData().toString(),
            m_splitBySize->isChecked(),
            m_sizeSpin->value(),
            m_splitByTime->isChecked(),
            m_timeSpin->value(),
            false,  // ringMode - could add UI later
            10,     // maxFiles
            m_triggerExprEdit->text(),
            m_preTriggerSpin->value(),
            m_postTriggerSpin->value(),
            m_repeatTriggerChk->isChecked());
    } else {
        emit recordToggled(false);
    }
}
