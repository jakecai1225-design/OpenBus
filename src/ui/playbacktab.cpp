#include "playbacktab.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSlider>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QFileDialog>
#include <QFileInfo>

PlaybackTab::PlaybackTab(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    // ---- 回放控制 ----
    auto *ctrlGroup = new QGroupBox("回放控制", this);
    auto *ctrlLayout = new QVBoxLayout(ctrlGroup);

    // Play / Pause / Stop
    auto *btnLayout = new QHBoxLayout;
    m_playBtn = new QPushButton("▶ 播放", ctrlGroup);
    m_pauseBtn = new QPushButton("⏸ 暂停", ctrlGroup);
    m_stopBtn = new QPushButton("⏹ 停止", ctrlGroup);
    m_playBtn->setEnabled(false);
    m_pauseBtn->setEnabled(false);
    m_stopBtn->setEnabled(false);
    m_playBtn->setMinimumWidth(100);
    m_pauseBtn->setMinimumWidth(100);
    m_stopBtn->setMinimumWidth(100);
    btnLayout->addWidget(m_playBtn);
    btnLayout->addWidget(m_pauseBtn);
    btnLayout->addWidget(m_stopBtn);
    btnLayout->addStretch();
    ctrlLayout->addLayout(btnLayout);

    // Seek slider
    auto *seekLayout = new QHBoxLayout;
    seekLayout->addWidget(new QLabel("进度:", ctrlGroup));
    m_seekSlider = new QSlider(Qt::Horizontal, ctrlGroup);
    m_seekSlider->setMinimum(0);
    m_seekSlider->setMaximum(1000);
    m_seekSlider->setEnabled(false);
    seekLayout->addWidget(m_seekSlider, 1);
    ctrlLayout->addLayout(seekLayout);

    m_posLabel = new QLabel("位置: 0.000s / 0.000s", ctrlGroup);
    ctrlLayout->addWidget(m_posLabel);

    m_fileInfoLabel = new QLabel("文件: (未加载)", ctrlGroup);
    m_fileInfoLabel->setStyleSheet("color: gray;");
    ctrlLayout->addWidget(m_fileInfoLabel);

    // Speed & options
    auto *optLayout = new QHBoxLayout;
    optLayout->addWidget(new QLabel("速度:", ctrlGroup));
    m_speedCombo = new QComboBox(ctrlGroup);
    m_speedCombo->addItem("0.1x", 0.1);
    m_speedCombo->addItem("0.5x", 0.5);
    m_speedCombo->addItem("1.0x", 1.0);
    m_speedCombo->addItem("2.0x", 2.0);
    m_speedCombo->addItem("5.0x", 5.0);
    m_speedCombo->addItem("10.0x", 10.0);
    m_speedCombo->setCurrentIndex(2);
    optLayout->addWidget(m_speedCombo);
    optLayout->addSpacing(20);
    m_loopChk = new QCheckBox("循环回放", ctrlGroup);
    m_autoScrollChk = new QCheckBox("回放时自动滚动", ctrlGroup);
    m_autoScrollChk->setChecked(true);
    optLayout->addWidget(m_loopChk);
    optLayout->addWidget(m_autoScrollChk);
    optLayout->addStretch();
    ctrlLayout->addLayout(optLayout);

    mainLayout->addWidget(ctrlGroup);

    // ---- 回放文件列表 ----
    auto *listGroup = new QGroupBox("回放文件列表", this);
    auto *listLayout = new QVBoxLayout(listGroup);

    m_fileList = new QTableWidget(0, 5, listGroup);
    m_fileList->setHorizontalHeaderLabels({"#", "文件名", "帧数", "时长", "状态"});
    m_fileList->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_fileList->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_fileList->verticalHeader()->setVisible(false);
    m_fileList->setSelectionBehavior(QAbstractItemView::SelectRows);
    listLayout->addWidget(m_fileList);

    auto *listBtnLayout = new QHBoxLayout;
    m_addFileBtn = new QPushButton("+ 添加文件", listGroup);
    m_removeFileBtn = new QPushButton("- 删除文件", listGroup);
    listBtnLayout->addWidget(m_addFileBtn);
    listBtnLayout->addWidget(m_removeFileBtn);
    listBtnLayout->addStretch();
    listLayout->addLayout(listBtnLayout);

    mainLayout->addWidget(listGroup, 1);

    // ---- 通道 & 过滤 ----
    auto *miscGroup = new QGroupBox("回放设置", this);
    auto *miscLayout = new QHBoxLayout(miscGroup);

    miscLayout->addWidget(new QLabel("回放通道:", miscGroup));
    m_channelCombo = new QComboBox(miscGroup);
    m_channelCombo->addItem("Channel 1");
    m_channelCombo->addItem("Channel 2");
    miscLayout->addWidget(m_channelCombo);

    miscLayout->addSpacing(20);

    miscLayout->addWidget(new QLabel("回放过滤:", miscGroup));
    m_filterEdit = new QLineEdit(miscGroup);
    m_filterEdit->setPlaceholderText("例如: id == 0x123");
    miscLayout->addWidget(m_filterEdit, 1);

    mainLayout->addWidget(miscGroup);

    // ---- 信号连接 ----
    connect(m_playBtn, &QPushButton::clicked, this, &PlaybackTab::onPlay);
    connect(m_pauseBtn, &QPushButton::clicked, this, &PlaybackTab::onPause);
    connect(m_stopBtn, &QPushButton::clicked, this, &PlaybackTab::onStop);
    connect(m_speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this](int idx) {
        emit speedChanged(m_speedCombo->itemData(idx).toDouble());
    });
    connect(m_seekSlider, &QSlider::sliderMoved, this, [this](int value) {
        emit seekChanged(value / 1000.0);
    });
    connect(m_addFileBtn, &QPushButton::clicked, this, &PlaybackTab::onAddFile);
    connect(m_removeFileBtn, &QPushButton::clicked, this, &PlaybackTab::onRemoveFile);
    connect(m_fileList, &QTableWidget::cellDoubleClicked,
            this, &PlaybackTab::onFileListDoubleClicked);
}

void PlaybackTab::onPlay() { emit playRequested(); }
void PlaybackTab::onPause() { emit pauseRequested(); }
void PlaybackTab::onStop() { emit stopRequested(); }

void PlaybackTab::onAddFile()
{
    QStringList paths = QFileDialog::getOpenFileNames(
        this, "添加回放文件", {},
        "sin 录制文件 (*.sin);;所有文件 (*.*)");
    if (paths.isEmpty()) return;

    for (const auto &path : paths) {
        QFileInfo fi(path);
        int row = m_fileList->rowCount();
        m_fileList->insertRow(row);

        auto *numItem = new QTableWidgetItem(QString::number(row + 1));
        numItem->setTextAlignment(Qt::AlignCenter);
        m_fileList->setItem(row, 0, numItem);

        auto *nameItem = new QTableWidgetItem(fi.fileName());
        nameItem->setData(Qt::UserRole, path);  // 存储完整路径
        m_fileList->setItem(row, 1, nameItem);

        m_fileList->setItem(row, 2, new QTableWidgetItem("-"));
        m_fileList->setItem(row, 3, new QTableWidgetItem("-"));
        m_fileList->setItem(row, 4, new QTableWidgetItem("就绪"));
    }
}

void PlaybackTab::onRemoveFile()
{
    int row = m_fileList->currentRow();
    if (row >= 0)
        m_fileList->removeRow(row);
}

void PlaybackTab::onFileListDoubleClicked(int row, int col)
{
    Q_UNUSED(col);
    if (row < 0 || row >= m_fileList->rowCount())
        return;

    auto *nameItem = m_fileList->item(row, 1);
    if (!nameItem) return;

    QString path = nameItem->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;

    // 更新列表中所有行的状态
    for (int i = 0; i < m_fileList->rowCount(); ++i) {
        auto *statusItem = m_fileList->item(i, 4);
        if (statusItem) {
            if (i == row)
                statusItem->setText("加载中");
            else if (statusItem->text() == "已加载" || statusItem->text() == "加载中")
                statusItem->setText("就绪");
        }
    }

    emit fileLoaded(path);
}

void PlaybackTab::setPlayerLoaded(bool loaded, bool playing)
{
    m_playBtn->setEnabled(loaded && !playing);
    m_pauseBtn->setEnabled(playing);
    m_stopBtn->setEnabled(loaded);
    m_seekSlider->setEnabled(loaded);
}

void PlaybackTab::setProgress(int cur, int total, double curTime, double totalTime)
{
    Q_UNUSED(cur);
    if (totalTime > 0)
        m_seekSlider->setValue(static_cast<int>(curTime / totalTime * 1000));
    if (totalTime > 0)
        m_posLabel->setText(QString("位置: %1s / %2s")
            .arg(curTime, 0, 'f', 3).arg(totalTime, 0, 'f', 3));
    else
        m_posLabel->setText(QString("位置: %1s").arg(curTime, 0, 'f', 3));
}

void PlaybackTab::setFileInfo(const QString &fileName, int frames, double duration)
{
    m_fileInfoLabel->setText(QString("文件: %1 (%2 帧, %3s)")
        .arg(fileName).arg(frames).arg(duration, 0, 'f', 3));

    // 更新文件列表中匹配行的信息
    for (int i = 0; i < m_fileList->rowCount(); ++i) {
        auto *nameItem = m_fileList->item(i, 1);
        if (nameItem && nameItem->text() == fileName) {
            auto *framesItem = m_fileList->item(i, 2);
            auto *durItem = m_fileList->item(i, 3);
            auto *statusItem = m_fileList->item(i, 4);
            if (framesItem)
                framesItem->setText(QString::number(frames));
            if (durItem)
                durItem->setText(QString::number(duration, 'f', 3) + "s");
            if (statusItem)
                statusItem->setText("已加载");
            break;
        }
    }
}
