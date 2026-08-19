#include "playbacktab.h"

#include "core/canfileio/canfileio.h"
#include "core/canfileio/canfileio_factory.h"
#include "core/canframe.h"
#include "ui/thememanager.h"
#include "utils/svg_icon.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSlider>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QProgressBar>
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
    const QString iconCol = ThemeManager::instance()->currentTheme().text;
    m_playBtn = new QPushButton(svgIcon(":/icons/play.svg", iconCol, 16), "播放", ctrlGroup);
    m_pauseBtn = new QPushButton(svgIcon(":/icons/pause.svg", iconCol, 16), "暂停", ctrlGroup);
    m_stopBtn = new QPushButton(svgIcon(":/icons/stop.svg", iconCol, 16), "停止", ctrlGroup);
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
    m_fileInfoLabel->setObjectName("DimLabel");
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

    m_fileList = new QTableWidget(0, 6, listGroup);
    m_fileList->setHorizontalHeaderLabels({"#", "文件名", "帧数", "时长", "进度", "状态"});
    m_fileList->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_fileList->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_fileList->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_fileList->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_fileList->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_fileList->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_fileList->verticalHeader()->setVisible(false);
    m_fileList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    listLayout->addWidget(m_fileList);

    auto *listBtnLayout = new QHBoxLayout;
    m_addFileBtn = new QPushButton(svgIcon(":/icons/plus.svg", iconCol, 14), "添加文件", listGroup);
    m_removeFileBtn = new QPushButton(svgIcon(":/icons/dash.svg", iconCol, 14), "删除文件", listGroup);
    m_moveUpBtn = new QPushButton(svgIcon(":/icons/chevron-up.svg", iconCol, 14), "上移", listGroup);
    m_moveDownBtn = new QPushButton(svgIcon(":/icons/chevron-down.svg", iconCol, 14), "下移", listGroup);
    listBtnLayout->addWidget(m_addFileBtn);
    listBtnLayout->addWidget(m_removeFileBtn);
    listBtnLayout->addWidget(m_moveUpBtn);
    listBtnLayout->addWidget(m_moveDownBtn);
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

    // ---- 异步文件解析定时器 ----
    m_parseTimer = new QTimer(this);
    m_parseTimer->setInterval(50);
    m_parseTimer->setSingleShot(false);
    connect(m_parseTimer, &QTimer::timeout, this, &PlaybackTab::onParseTimer);

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
    connect(m_moveUpBtn, &QPushButton::clicked, this, &PlaybackTab::onMoveUp);
    connect(m_moveDownBtn, &QPushButton::clicked, this, &PlaybackTab::onMoveDown);
    connect(m_fileList, &QTableWidget::cellDoubleClicked,
            this, &PlaybackTab::onFileListDoubleClicked);
    connect(m_loopChk, &QCheckBox::toggled, this, &PlaybackTab::loopToggled);
    connect(m_autoScrollChk, &QCheckBox::toggled, this, &PlaybackTab::autoScrollToggled);

    // 主题切换 → 重刷全部按钮图标颜色
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        const QString c = ThemeManager::instance()->currentTheme().text;
        m_playBtn->setIcon(svgIcon(":/icons/play.svg", c, 16));
        m_pauseBtn->setIcon(svgIcon(":/icons/pause.svg", c, 16));
        m_stopBtn->setIcon(svgIcon(":/icons/stop.svg", c, 16));
        m_addFileBtn->setIcon(svgIcon(":/icons/plus.svg", c, 14));
        m_removeFileBtn->setIcon(svgIcon(":/icons/dash.svg", c, 14));
        m_moveUpBtn->setIcon(svgIcon(":/icons/chevron-up.svg", c, 14));
        m_moveDownBtn->setIcon(svgIcon(":/icons/chevron-down.svg", c, 14));
    });
}

void PlaybackTab::onPlay() { emit playRequested(); }
void PlaybackTab::onPause() { emit pauseRequested(); }
void PlaybackTab::onStop() { emit stopRequested(); }

void PlaybackTab::onAddFile()
{
    QStringList paths = QFileDialog::getOpenFileNames(
        this, "添加回放文件", {},
        CanFileIO::allFileFilters());
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
        nameItem->setToolTip(path);
        m_fileList->setItem(row, 1, nameItem);

        m_fileList->setItem(row, 2, new QTableWidgetItem("解析中..."));
        m_fileList->setItem(row, 3, new QTableWidgetItem("-"));
        m_fileList->setItem(row, 5, new QTableWidgetItem("就绪"));

        // 进度条
        auto *bar = new QProgressBar();
        bar->setRange(0, 100);
        bar->setValue(0);
        bar->setFormat("—");
        bar->setAlignment(Qt::AlignCenter);
        m_fileList->setCellWidget(row, 4, bar);

        // 加入解析队列
        m_parseQueue.enqueue(row);
    }

    if (!m_parseTimer->isActive())
        m_parseTimer->start();
}

void PlaybackTab::onRemoveFile()
{
    int row = m_fileList->currentRow();
    if (row < 0) return;

    // 从解析队列中移除
    m_parseQueue.removeOne(row);
    // 调整队列中大于该行的行号
    for (int i = 0; i < m_parseQueue.size(); ++i) {
        if (m_parseQueue[i] > row)
            m_parseQueue[i] = m_parseQueue[i] - 1;
    }

    m_fileList->removeRow(row);

    if (m_currentLoadedRow == row)
        m_currentLoadedRow = -1;
    else if (m_currentLoadedRow > row)
        m_currentLoadedRow--;

    renumberRows();
}

void PlaybackTab::onMoveUp()
{
    int row = m_fileList->currentRow();
    if (row <= 0) return;

    // 交换 row 和 row-1 的所有 cell 内容
    for (int col = 0; col < m_fileList->columnCount(); ++col) {
        if (col == 4) {
            // 进度列是 widget，需要交换 widget
            auto *w1 = m_fileList->cellWidget(row, col);
            auto *w2 = m_fileList->cellWidget(row - 1, col);
            m_fileList->removeCellWidget(row, col);
            m_fileList->removeCellWidget(row - 1, col);
            if (w1) m_fileList->setCellWidget(row - 1, col, w1);
            if (w2) m_fileList->setCellWidget(row, col, w2);
        } else {
            auto *item1 = m_fileList->takeItem(row, col);
            auto *item2 = m_fileList->takeItem(row - 1, col);
            if (item2) m_fileList->setItem(row, col, item2);
            if (item1) m_fileList->setItem(row - 1, col, item1);
        }
    }

    renumberRows();
    m_fileList->setCurrentCell(row - 1, 0);

    if (m_currentLoadedRow == row)
        m_currentLoadedRow = row - 1;
    else if (m_currentLoadedRow == row - 1)
        m_currentLoadedRow = row;
}

void PlaybackTab::onMoveDown()
{
    int row = m_fileList->currentRow();
    if (row < 0 || row >= m_fileList->rowCount() - 1) return;

    // 交换 row 和 row+1 的所有 cell 内容
    for (int col = 0; col < m_fileList->columnCount(); ++col) {
        if (col == 4) {
            auto *w1 = m_fileList->cellWidget(row, col);
            auto *w2 = m_fileList->cellWidget(row + 1, col);
            m_fileList->removeCellWidget(row, col);
            m_fileList->removeCellWidget(row + 1, col);
            if (w1) m_fileList->setCellWidget(row + 1, col, w1);
            if (w2) m_fileList->setCellWidget(row, col, w2);
        } else {
            auto *item1 = m_fileList->takeItem(row, col);
            auto *item2 = m_fileList->takeItem(row + 1, col);
            if (item2) m_fileList->setItem(row, col, item2);
            if (item1) m_fileList->setItem(row + 1, col, item1);
        }
    }

    renumberRows();
    m_fileList->setCurrentCell(row + 1, 0);

    if (m_currentLoadedRow == row)
        m_currentLoadedRow = row + 1;
    else if (m_currentLoadedRow == row + 1)
        m_currentLoadedRow = row;
}

void PlaybackTab::renumberRows()
{
    for (int i = 0; i < m_fileList->rowCount(); ++i) {
        auto *item = m_fileList->item(i, 0);
        if (item)
            item->setText(QString::number(i + 1));
    }
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
        auto *statusItem = m_fileList->item(i, 5);
        if (statusItem) {
            if (i == row)
                statusItem->setText("加载中");
            else if (statusItem->text() == "已加载" || statusItem->text() == "加载中")
                statusItem->setText("就绪");
        }
        // 重置非当前行的进度条
        if (i != row) {
            auto *bar = qobject_cast<QProgressBar*>(m_fileList->cellWidget(i, 4));
            if (bar && bar->value() >= 100)
                bar->setValue(0);
        }
    }

    m_currentLoadedRow = row;
    emit fileLoaded(path);
}

void PlaybackTab::onParseTimer()
{
    if (m_parseQueue.isEmpty()) {
        m_parseTimer->stop();
        return;
    }

    int row = m_parseQueue.dequeue();
    if (row >= m_fileList->rowCount())
        return;

    parseFileInfo(row);
}

void PlaybackTab::parseFileInfo(int row)
{
    if (row < 0 || row >= m_fileList->rowCount())
        return;

    auto *nameItem = m_fileList->item(row, 1);
    if (!nameItem) return;

    QString path = nameItem->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;

    // 使用工厂创建读取器，读取帧数和时长
    auto reader = CanFileIOFactory::createReader(path);
    if (!reader || !reader->open(path)) {
        auto *framesItem = m_fileList->item(row, 2);
        if (framesItem) framesItem->setText("解析失败");
        auto *durItem = m_fileList->item(row, 3);
        if (durItem) durItem->setText("-");
        return;
    }

    QVector<CanFrame> frames;
    int count = reader->readAll(frames);
    reader->close();

    double duration = 0.0;
    if (count > 0 && !frames.isEmpty())
        duration = frames.last().timestamp;

    auto *framesItem = m_fileList->item(row, 2);
    if (framesItem)
        framesItem->setText(QString::number(count));

    auto *durItem = m_fileList->item(row, 3);
    if (durItem) {
        if (duration > 0)
            durItem->setText(QString::number(duration, 'f', 3) + "s");
        else
            durItem->setText("0.000s");
    }
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

    // 更新文件列表中当前播放文件的进度条
    if (m_currentLoadedRow >= 0 && m_currentLoadedRow < m_fileList->rowCount()) {
        auto *bar = qobject_cast<QProgressBar*>(m_fileList->cellWidget(m_currentLoadedRow, 4));
        if (bar) {
            if (total > 0) {
                int pct = static_cast<int>(cur * 100.0 / total);
                bar->setValue(pct);
                bar->setFormat(QString("%1% (%2/%3)").arg(pct).arg(cur).arg(total));
            } else {
                bar->setValue(0);
                bar->setFormat("—");
            }
        }

        auto *statusItem = m_fileList->item(m_currentLoadedRow, 5);
        if (statusItem && statusItem->text() != "播放中")
            statusItem->setText("播放中");
    }
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
            auto *statusItem = m_fileList->item(i, 5);
            if (framesItem)
                framesItem->setText(QString::number(frames));
            if (durItem)
                durItem->setText(QString::number(duration, 'f', 3) + "s");
            if (statusItem)
                statusItem->setText("已加载");
            m_currentLoadedRow = i;
            break;
        }
    }
}
