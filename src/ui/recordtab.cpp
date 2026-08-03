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
    m_recordBtn = new QPushButton("● 开始录制", this);
    m_recordBtn->setCheckable(true);
    m_recordBtn->setMinimumWidth(120);
    m_pauseBtn = new QPushButton("⏸ 暂停", this);
    m_pauseBtn->setMinimumWidth(100);
    m_pauseBtn->setEnabled(false);
    m_stopBtn = new QPushButton("⏹ 停止", this);
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

    // ---- 状态 ----
    m_statusLabel = new QLabel("状态: 未录制", this);
    mainLayout->addWidget(m_statusLabel);

    mainLayout->addStretch();

    // ---- 信号连接 ----
    connect(browseBtn, &QPushButton::clicked, this, &RecordTab::onBrowse);
    connect(m_recordBtn, &QPushButton::toggled, this, &RecordTab::onRecord);
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
    m_recordBtn->setText(on ? "■ 停止录制" : "● 开始录制");
    m_pauseBtn->setEnabled(on);
    m_stopBtn->setEnabled(on);
    m_statusLabel->setText(on ? "状态: 录制中..." : "状态: 未录制");
    m_statusLabel->setStyleSheet(on ? "color: red;" : "color: gray;");
    emit recordToggled(on);
}

void RecordTab::setRecording(bool recording)
{
    m_recordBtn->blockSignals(true);
    m_recordBtn->setChecked(recording);
    m_recordBtn->setText(recording ? "■ 停止录制" : "● 开始录制");
    m_pauseBtn->setEnabled(recording);
    m_stopBtn->setEnabled(recording);
    m_statusLabel->setText(recording ? "状态: 录制中..." : "状态: 未录制");
    m_statusLabel->setStyleSheet(recording ? "color: red;" : "color: gray;");
    m_recordBtn->blockSignals(false);
}
