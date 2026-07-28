#include "playbacktab.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSlider>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QFileDialog>

PlaybackTab::PlaybackTab(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    // ---- 播放控制按钮 ----
    auto *btnLayout = new QHBoxLayout;
    btnLayout->setSpacing(8);
    m_playBtn = new QPushButton("▶ 播放", this);
    m_pauseBtn = new QPushButton("⏸ 暂停", this);
    m_stopBtn = new QPushButton("⏹ 停止", this);
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
    mainLayout->addLayout(btnLayout);

    // ---- 进度条 ----
    auto *seekLayout = new QHBoxLayout;
    seekLayout->addWidget(new QLabel("进度:", this));
    m_seekSlider = new QSlider(Qt::Horizontal, this);
    m_seekSlider->setMinimum(0);
    m_seekSlider->setMaximum(1000);
    m_seekSlider->setEnabled(false);
    seekLayout->addWidget(m_seekSlider, 1);
    mainLayout->addLayout(seekLayout);

    m_posLabel = new QLabel("位置: 0.000s / 0.000s", this);
    mainLayout->addWidget(m_posLabel);

    // ---- 速度选择 ----
    auto *speedLayout = new QHBoxLayout;
    speedLayout->addWidget(new QLabel("速度:", this));
    m_speedCombo = new QComboBox(this);
    m_speedCombo->addItem("0.1x", 0.1);
    m_speedCombo->addItem("0.5x", 0.5);
    m_speedCombo->addItem("1.0x", 1.0);
    m_speedCombo->addItem("2.0x", 2.0);
    m_speedCombo->addItem("5.0x", 5.0);
    m_speedCombo->addItem("10.0x", 10.0);
    m_speedCombo->setCurrentIndex(2);
    speedLayout->addWidget(m_speedCombo);
    speedLayout->addStretch();
    mainLayout->addLayout(speedLayout);

    // ---- 选项 ----
    m_loopChk = new QCheckBox("循环回放", this);
    m_autoScrollChk = new QCheckBox("回放时自动滚动", this);
    m_autoScrollChk->setChecked(true);
    mainLayout->addWidget(m_loopChk);
    mainLayout->addWidget(m_autoScrollChk);

    // ---- 文件信息 ----
    auto *fileLayout = new QHBoxLayout;
    m_fileLabel = new QLabel("文件: (未加载)", this);
    fileLayout->addWidget(m_fileLabel, 1);
    auto *changeBtn = new QPushButton("更换文件...", this);
    fileLayout->addWidget(changeBtn);
    mainLayout->addLayout(fileLayout);

    mainLayout->addStretch();

    // ---- 信号连接 ----
    connect(m_playBtn, &QPushButton::clicked, this, &PlaybackTab::playRequested);
    connect(m_pauseBtn, &QPushButton::clicked, this, &PlaybackTab::pauseRequested);
    connect(m_stopBtn, &QPushButton::clicked, this, &PlaybackTab::stopRequested);
    connect(m_speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this](int idx) {
        emit speedChanged(m_speedCombo->itemData(idx).toDouble());
    });
    connect(m_seekSlider, &QSlider::sliderMoved, this, [this](int value) {
        emit seekChanged(value / 1000.0);
    });
    connect(changeBtn, &QPushButton::clicked, this, &PlaybackTab::changeFileRequested);
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
    if (total > 0)
        m_seekSlider->setValue(static_cast<int>(curTime / totalTime * 1000));
    if (totalTime > 0)
        m_posLabel->setText(QString("位置: %1s / %2s")
            .arg(curTime, 0, 'f', 3).arg(totalTime, 0, 'f', 3));
    else
        m_posLabel->setText(QString("位置: %1s").arg(curTime, 0, 'f', 3));
}

void PlaybackTab::setFileInfo(const QString &fileName, int frames, double duration)
{
    m_fileLabel->setText(QString("文件: %1 (%2 帧, %3s)")
        .arg(fileName).arg(frames).arg(duration, 0, 'f', 3));
}
