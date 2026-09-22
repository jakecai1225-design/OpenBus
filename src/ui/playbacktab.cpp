#include "playbacktab.h"
#include "core/signalrelay.h"   // DEF-08：字符串信号 → lambda 桥接

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
#include <QVariantMap>
#include <QVariantList>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QEvent>

PlaybackTab::PlaybackTab(QWidget *parent)
    : QWidget(parent)
{
    setAcceptDrops(true);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    // ---- Playback control ----
    auto *ctrlGroup = new QGroupBox(QStringLiteral("Playback"), this);
    auto *ctrlLayout = new QVBoxLayout(ctrlGroup);

    // Play / Pause / Stop
    auto *btnLayout = new QHBoxLayout;
    const QString iconCol = ThemeManager::instance()->currentTheme().text;
    m_playBtn = new QPushButton(svgIcon(":/icons/play.svg", iconCol, 16),
                                QStringLiteral("Play"), ctrlGroup);
    m_pauseBtn = new QPushButton(svgIcon(":/icons/pause.svg", iconCol, 16),
                                 QStringLiteral("Pause"), ctrlGroup);
    m_stopBtn = new QPushButton(svgIcon(":/icons/stop.svg", iconCol, 16),
                                QStringLiteral("Stop"), ctrlGroup);
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
    seekLayout->addWidget(new QLabel(QStringLiteral("Seek:"), ctrlGroup));
    m_seekSlider = new QSlider(Qt::Horizontal, ctrlGroup);
    m_seekSlider->setMinimum(0);
    m_seekSlider->setMaximum(1000);
    m_seekSlider->setEnabled(false);
    seekLayout->addWidget(m_seekSlider, 1);
    ctrlLayout->addLayout(seekLayout);

    m_posLabel = new QLabel(QStringLiteral("Position: 0.000s / 0.000s"), ctrlGroup);
    ctrlLayout->addWidget(m_posLabel);

    m_fileInfoLabel = new QLabel(QStringLiteral("File: (not loaded)"), ctrlGroup);
    m_fileInfoLabel->setObjectName("DimLabel");
    ctrlLayout->addWidget(m_fileInfoLabel);

    // Speed & options
    auto *optLayout = new QHBoxLayout;
    optLayout->addWidget(new QLabel(QStringLiteral("Speed:"), ctrlGroup));
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
    m_loopChk = new QCheckBox(QStringLiteral("Loop"), ctrlGroup);
    m_autoScrollChk = new QCheckBox(QStringLiteral("Auto-scroll during playback"), ctrlGroup);
    m_autoScrollChk->setChecked(true);
    optLayout->addWidget(m_loopChk);
    optLayout->addWidget(m_autoScrollChk);
    optLayout->addStretch();
    ctrlLayout->addLayout(optLayout);

    mainLayout->addWidget(ctrlGroup);

    // ---- Playback file list ----
    auto *listGroup = new QGroupBox("Playback files", this);
    auto *listLayout = new QVBoxLayout(listGroup);

    m_fileList = new QTableWidget(0, 6, listGroup);
    m_fileList->setHorizontalHeaderLabels(
        {QStringLiteral("#"), QStringLiteral("File"), QStringLiteral("Frames"),
         QStringLiteral("Duration"), QStringLiteral("Progress"), QStringLiteral("Status")});
    m_fileList->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_fileList->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_fileList->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_fileList->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_fileList->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_fileList->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_fileList->verticalHeader()->setVisible(false);
    m_fileList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fileList->setAcceptDrops(true);
    m_fileList->viewport()->setAcceptDrops(true);
    m_fileList->setDragDropMode(QAbstractItemView::DropOnly);
    m_fileList->setDropIndicatorShown(true);
    m_fileList->installEventFilter(this);
    m_fileList->viewport()->installEventFilter(this);
    m_fileList->setToolTip(QStringLiteral("Drop .blf / .asc / .csv / .pcap / .trc files here"));
    listLayout->addWidget(m_fileList);

    auto *listBtnLayout = new QHBoxLayout;
    m_addFileBtn = new QPushButton(svgIcon(":/icons/plus.svg", iconCol, 14), "Add files", listGroup);
    m_removeFileBtn = new QPushButton(svgIcon(":/icons/dash.svg", iconCol, 14), "Remove", listGroup);
    m_moveUpBtn = new QPushButton(svgIcon(":/icons/chevron-up.svg", iconCol, 14), "Up", listGroup);
    m_moveDownBtn = new QPushButton(svgIcon(":/icons/chevron-down.svg", iconCol, 14), "Down", listGroup);
    listBtnLayout->addWidget(m_addFileBtn);
    listBtnLayout->addWidget(m_removeFileBtn);
    listBtnLayout->addWidget(m_moveUpBtn);
    listBtnLayout->addWidget(m_moveDownBtn);
    listBtnLayout->addStretch();
    listLayout->addLayout(listBtnLayout);

    mainLayout->addWidget(listGroup, 1);

    // ---- Playback settings ----
    auto *miscGroup = new QGroupBox("Playback settings", this);
    auto *miscLayout = new QHBoxLayout(miscGroup);

    miscLayout->addWidget(new QLabel("Channel:", miscGroup));
    m_channelCombo = new QComboBox(miscGroup);
    m_channelCombo->addItem("Channel 1");
    m_channelCombo->addItem("Channel 2");
    miscLayout->addWidget(m_channelCombo);

    miscLayout->addSpacing(12);

    miscLayout->addWidget(new QLabel("Direction:", miscGroup));
    m_directionCombo = new QComboBox(miscGroup);
    m_directionCombo->addItem(QStringLiteral("All"), QStringLiteral("all"));
    m_directionCombo->addItem(QStringLiteral("Rx"), QStringLiteral("rx"));
    m_directionCombo->addItem(QStringLiteral("Tx"), QStringLiteral("tx"));
    miscLayout->addWidget(m_directionCombo);

    miscLayout->addSpacing(12);

    miscLayout->addWidget(new QLabel("Protocol:", miscGroup));
    m_protocolCombo = new QComboBox(miscGroup);
    m_protocolCombo->addItem(QStringLiteral("All"), QStringLiteral("all"));
    m_protocolCombo->addItem(QStringLiteral("CAN"), QStringLiteral("can"));
    m_protocolCombo->addItem(QStringLiteral("CAN FD"), QStringLiteral("canfd"));
    miscLayout->addWidget(m_protocolCombo);

    miscLayout->addSpacing(12);

    miscLayout->addWidget(new QLabel("Filter:", miscGroup));
    m_filterEdit = new QLineEdit(miscGroup);
    m_filterEdit->setPlaceholderText(QStringLiteral("e.g. id == 0x123"));
    miscLayout->addWidget(m_filterEdit, 1);

    m_applyFilterBtn = new QPushButton(QStringLiteral("Apply"), miscGroup);
    m_applyFilterBtn->setToolTip(
        QStringLiteral("Commit direction / protocol / expression filters. "
                       "Until Apply, playback uses no filter."));
    miscLayout->addWidget(m_applyFilterBtn);

    mainLayout->addWidget(miscGroup);

    // ---- Async metadata parse ----
    m_parseTimer = new QTimer(this);
    m_parseTimer->setInterval(50);
    m_parseTimer->setSingleShot(false);
    connect(m_parseTimer, &QTimer::timeout, this, &PlaybackTab::onParseTimer);

    // ---- Connections ----
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
    connect(m_applyFilterBtn, &QPushButton::clicked, this, &PlaybackTab::onApplyFilter);
    connect(m_directionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { markFilterDirty(); });
    connect(m_protocolCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { markFilterDirty(); });
    connect(m_filterEdit, &QLineEdit::textChanged, this, [this](const QString &) {
        markFilterDirty();
    });

    // Theme refresh → recolor button icons
    // DEF-08 string signal: ThemeManager lives in data.dll
    auto *themeRelay = new SignalRelay(this);
    themeRelay->fire0 = [this]() {
        const QString c = ThemeManager::instance()->currentTheme().text;
        m_playBtn->setIcon(svgIcon(":/icons/play.svg", c, 16));
        m_pauseBtn->setIcon(svgIcon(":/icons/pause.svg", c, 16));
        m_stopBtn->setIcon(svgIcon(":/icons/stop.svg", c, 16));
        m_addFileBtn->setIcon(svgIcon(":/icons/plus.svg", c, 14));
        m_removeFileBtn->setIcon(svgIcon(":/icons/dash.svg", c, 14));
        m_moveUpBtn->setIcon(svgIcon(":/icons/chevron-up.svg", c, 14));
        m_moveDownBtn->setIcon(svgIcon(":/icons/chevron-down.svg", c, 14));
    };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            themeRelay, SLOT(fire()));
}

void PlaybackTab::onPlay() { emit playRequested(); }
void PlaybackTab::onPause() { emit pauseRequested(); }
void PlaybackTab::onStop() { emit stopRequested(); }

void PlaybackTab::markFilterDirty()
{
    // UI draft changed; active filter stays until Apply.
    if (m_filterApplied)
        m_applyFilterBtn->setText(QStringLiteral("Apply*"));
    else
        m_applyFilterBtn->setText(QStringLiteral("Apply"));
}

void PlaybackTab::onApplyFilter()
{
    m_filterApplied = true;
    m_appliedDirection = m_directionCombo->currentData().toString();
    m_appliedProtocol = m_protocolCombo->currentData().toString();
    m_appliedFilter = m_filterEdit->text().trimmed();
    m_applyFilterBtn->setText(QStringLiteral("Apply"));
}

void PlaybackTab::onAddFile()
{
    QStringList paths = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Add playback files"), {},
        CanFileIO::allFileFilters());
    addFiles(paths);
}

bool PlaybackTab::isPlaybackFile(const QString &path)
{
    const QString suf = QFileInfo(path).suffix();
    return CanFileIO::formatFromSuffix(suf) != CanFileIO::Format::Unknown;
}

QStringList PlaybackTab::playbackPathsFromMime(const QMimeData *mime)
{
    QStringList out;
    if (!mime || !mime->hasUrls())
        return out;
    for (const QUrl &url : mime->urls()) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (isPlaybackFile(path))
            out << path;
    }
    return out;
}

void PlaybackTab::addFiles(const QStringList &paths)
{
    if (paths.isEmpty())
        return;

    const bool wasEmpty = (m_fileList->rowCount() == 0);
    const int firstNew = m_fileList->rowCount();
    int added = 0;
    for (const QString &path : paths) {
        if (!isPlaybackFile(path))
            continue;
        // Skip duplicates
        bool exists = false;
        for (int i = 0; i < m_fileList->rowCount(); ++i) {
            const auto *item = m_fileList->item(i, 1);
            if (item && item->data(Qt::UserRole).toString() == path) {
                exists = true;
                break;
            }
        }
        if (exists)
            continue;
        addFilePath(path);
        ++added;
    }
    if (added == 0)
        return;

    if (!m_parseTimer->isActive())
        m_parseTimer->start();

    // Auto-load into Player so Play works without double-click.
    if (wasEmpty || m_currentLoadedRow < 0)
        loadRowIntoPlayer(firstNew);
}

void PlaybackTab::addFilePath(const QString &path)
{
    QFileInfo fi(path);
    int row = m_fileList->rowCount();
    m_fileList->insertRow(row);

    auto *numItem = new QTableWidgetItem(QString::number(row + 1));
    numItem->setTextAlignment(Qt::AlignCenter);
    m_fileList->setItem(row, 0, numItem);

    auto *nameItem = new QTableWidgetItem(fi.fileName());
    nameItem->setData(Qt::UserRole, path);
    nameItem->setToolTip(path);
    m_fileList->setItem(row, 1, nameItem);

    m_fileList->setItem(row, 2, new QTableWidgetItem(QStringLiteral("Parsing…")));
    m_fileList->setItem(row, 3, new QTableWidgetItem(QStringLiteral("-")));
    m_fileList->setItem(row, 5, new QTableWidgetItem(QStringLiteral("Ready")));

    auto *bar = new QProgressBar();
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setFormat(QStringLiteral("—"));
    bar->setAlignment(Qt::AlignCenter);
    m_fileList->setCellWidget(row, 4, bar);

    m_parseQueue.enqueue(row);
}

void PlaybackTab::loadRowIntoPlayer(int row)
{
    if (row < 0 || row >= m_fileList->rowCount())
        return;

    auto *nameItem = m_fileList->item(row, 1);
    if (!nameItem)
        return;

    const QString path = nameItem->data(Qt::UserRole).toString();
    if (path.isEmpty())
        return;

    for (int i = 0; i < m_fileList->rowCount(); ++i) {
        auto *statusItem = m_fileList->item(i, 5);
        if (statusItem) {
            if (i == row)
                statusItem->setText(QStringLiteral("Loading"));
            else if (statusItem->text() == QStringLiteral("Loaded")
                     || statusItem->text() == QStringLiteral("Loading")
                     || statusItem->text() == QStringLiteral("Playing"))
                statusItem->setText(QStringLiteral("Ready"));
        }
        if (i != row) {
            auto *bar = qobject_cast<QProgressBar *>(m_fileList->cellWidget(i, 4));
            if (bar && bar->value() >= 100)
                bar->setValue(0);
        }
    }

    m_currentLoadedRow = row;
    emit fileLoaded(path);
}

void PlaybackTab::dragEnterEvent(QDragEnterEvent *event)
{
    if (!playbackPathsFromMime(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void PlaybackTab::dragMoveEvent(QDragMoveEvent *event)
{
    if (!playbackPathsFromMime(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void PlaybackTab::dropEvent(QDropEvent *event)
{
    const QStringList paths = playbackPathsFromMime(event->mimeData());
    if (paths.isEmpty())
        return;
    event->acceptProposedAction();
    addFiles(paths);
}

bool PlaybackTab::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_fileList || watched == m_fileList->viewport()) {
        switch (event->type()) {
        case QEvent::DragEnter: {
            auto *e = static_cast<QDragEnterEvent *>(event);
            if (!playbackPathsFromMime(e->mimeData()).isEmpty()) {
                e->acceptProposedAction();
                return true;
            }
            break;
        }
        case QEvent::DragMove: {
            auto *e = static_cast<QDragMoveEvent *>(event);
            if (!playbackPathsFromMime(e->mimeData()).isEmpty()) {
                e->acceptProposedAction();
                return true;
            }
            break;
        }
        case QEvent::Drop: {
            auto *e = static_cast<QDropEvent *>(event);
            const QStringList paths = playbackPathsFromMime(e->mimeData());
            if (!paths.isEmpty()) {
                e->acceptProposedAction();
                addFiles(paths);
                return true;
            }
            break;
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PlaybackTab::onRemoveFile()
{
    int row = m_fileList->currentRow();
    if (row < 0) return;

    // Drop from parse queue; reindex later rows
    m_parseQueue.removeOne(row);
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

    // Swap row with row-1 (progress column is a widget)
    for (int col = 0; col < m_fileList->columnCount(); ++col) {
        if (col == 4) {
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

    // Swap row with row+1
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
    loadRowIntoPlayer(row);
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

    auto reader = CanFileIOFactory::createReader(path);
    if (!reader || !reader->open(path)) {
        auto *framesItem = m_fileList->item(row, 2);
        if (framesItem)
            framesItem->setText(QStringLiteral("Parse failed"));
        auto *durItem = m_fileList->item(row, 3);
        if (durItem)
            durItem->setText(QStringLiteral("-"));
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
        m_posLabel->setText(QStringLiteral("Position: %1s / %2s")
            .arg(curTime, 0, 'f', 3).arg(totalTime, 0, 'f', 3));
    else
        m_posLabel->setText(QStringLiteral("Position: %1s").arg(curTime, 0, 'f', 3));

    if (m_currentLoadedRow >= 0 && m_currentLoadedRow < m_fileList->rowCount()) {
        auto *bar = qobject_cast<QProgressBar*>(m_fileList->cellWidget(m_currentLoadedRow, 4));
        if (bar) {
            if (total > 0) {
                int pct = static_cast<int>(cur * 100.0 / total);
                bar->setValue(pct);
                bar->setFormat(QStringLiteral("%1% (%2/%3)").arg(pct).arg(cur).arg(total));
            } else {
                bar->setValue(0);
                bar->setFormat(QStringLiteral("—"));
            }
        }

        auto *statusItem = m_fileList->item(m_currentLoadedRow, 5);
        if (statusItem && statusItem->text() != QStringLiteral("Playing"))
            statusItem->setText(QStringLiteral("Playing"));
    }
}

void PlaybackTab::setFileInfo(const QString &fileName, int frames, double duration)
{
    m_fileInfoLabel->setText(QStringLiteral("File: %1 (%2 frames, %3s)")
        .arg(fileName).arg(frames).arg(duration, 0, 'f', 3));

    for (int i = 0; i < m_fileList->rowCount(); ++i) {
        auto *nameItem = m_fileList->item(i, 1);
        if (nameItem && nameItem->text() == fileName) {
            auto *framesItem = m_fileList->item(i, 2);
            auto *durItem = m_fileList->item(i, 3);
            auto *statusItem = m_fileList->item(i, 5);
            if (framesItem)
                framesItem->setText(QString::number(frames));
            if (durItem)
                durItem->setText(QString::number(duration, 'f', 3) + QStringLiteral("s"));
            if (statusItem)
                statusItem->setText(QStringLiteral("Loaded"));
            m_currentLoadedRow = i;
            break;
        }
    }
}

QVariantMap PlaybackTab::configMap() const
{
    QVariantMap m;
    m.insert(QStringLiteral("speed"), m_speedCombo->currentData().toDouble());
    m.insert(QStringLiteral("loop"), m_loopChk->isChecked());
    m.insert(QStringLiteral("autoScroll"), m_autoScrollChk->isChecked());
    m.insert(QStringLiteral("channelIndex"), m_channelCombo->currentIndex());
    // Active filter only after Apply; otherwise playback sees no filter.
    if (m_filterApplied) {
        m.insert(QStringLiteral("direction"), m_appliedDirection);
        m.insert(QStringLiteral("protocol"), m_appliedProtocol);
        m.insert(QStringLiteral("filter"), m_appliedFilter);
    } else {
        m.insert(QStringLiteral("direction"), QStringLiteral("all"));
        m.insert(QStringLiteral("protocol"), QStringLiteral("all"));
        m.insert(QStringLiteral("filter"), QString());
    }
    m.insert(QStringLiteral("filterApplied"), m_filterApplied);
    m.insert(QStringLiteral("directionDraft"), m_directionCombo->currentData().toString());
    m.insert(QStringLiteral("protocolDraft"), m_protocolCombo->currentData().toString());
    m.insert(QStringLiteral("filterDraft"), m_filterEdit->text());
    m.insert(QStringLiteral("currentRow"), m_currentLoadedRow);

    QStringList files;
    files.reserve(m_fileList->rowCount());
    for (int i = 0; i < m_fileList->rowCount(); ++i) {
        const auto *nameItem = m_fileList->item(i, 1);
        if (nameItem) {
            const QString path = nameItem->data(Qt::UserRole).toString();
            if (!path.isEmpty())
                files << path;
        }
    }
    m.insert(QStringLiteral("files"), files);
    return m;
}

void PlaybackTab::loadConfig(const QVariantMap &map)
{
    if (map.isEmpty())
        return;

    if (map.contains(QStringLiteral("speed"))) {
        const double speed = map.value(QStringLiteral("speed")).toDouble();
        const int idx = m_speedCombo->findData(speed);
        if (idx >= 0)
            m_speedCombo->setCurrentIndex(idx);
        emit speedChanged(m_speedCombo->currentData().toDouble());
    }
    if (map.contains(QStringLiteral("loop"))) {
        const bool on = map.value(QStringLiteral("loop")).toBool();
        m_loopChk->setChecked(on);
        emit loopToggled(on);
    }
    if (map.contains(QStringLiteral("autoScroll"))) {
        const bool on = map.value(QStringLiteral("autoScroll")).toBool();
        m_autoScrollChk->setChecked(on);
        emit autoScrollToggled(on);
    }
    if (map.contains(QStringLiteral("channelIndex"))) {
        const int ci = map.value(QStringLiteral("channelIndex")).toInt();
        if (ci >= 0 && ci < m_channelCombo->count())
            m_channelCombo->setCurrentIndex(ci);
    }

    // Restore draft UI first (prefer *Draft keys, fall back to legacy keys).
    const QString dirDraft = map.value(
        QStringLiteral("directionDraft"),
        map.value(QStringLiteral("direction"), QStringLiteral("all"))).toString();
    const QString protoDraft = map.value(
        QStringLiteral("protocolDraft"),
        map.value(QStringLiteral("protocol"), QStringLiteral("all"))).toString();
    const QString filterDraft = map.value(
        QStringLiteral("filterDraft"),
        map.value(QStringLiteral("filter")).toString()).toString();

    {
        const int idx = m_directionCombo->findData(dirDraft);
        if (idx >= 0)
            m_directionCombo->setCurrentIndex(idx);
    }
    {
        const int idx = m_protocolCombo->findData(protoDraft);
        if (idx >= 0)
            m_protocolCombo->setCurrentIndex(idx);
    }
    m_filterEdit->setText(filterDraft);

    m_filterApplied = map.value(QStringLiteral("filterApplied"), false).toBool();
    if (m_filterApplied) {
        m_appliedDirection = map.value(
            QStringLiteral("direction"), QStringLiteral("all")).toString();
        m_appliedProtocol = map.value(
            QStringLiteral("protocol"), QStringLiteral("all")).toString();
        m_appliedFilter = map.value(QStringLiteral("filter")).toString();
        m_applyFilterBtn->setText(QStringLiteral("Apply"));
    } else {
        m_appliedDirection = QStringLiteral("all");
        m_appliedProtocol = QStringLiteral("all");
        m_appliedFilter.clear();
        m_applyFilterBtn->setText(QStringLiteral("Apply"));
    }

    const QStringList files = map.value(QStringLiteral("files")).toStringList();
    if (!files.isEmpty()) {
        m_fileList->setRowCount(0);
        m_parseQueue.clear();
        m_currentLoadedRow = -1;
        for (const auto &path : files)
            addFilePath(path);
        if (!m_parseTimer->isActive())
            m_parseTimer->start();

        int wantRow = map.value(QStringLiteral("currentRow"), 0).toInt();
        if (wantRow < 0 || wantRow >= m_fileList->rowCount())
            wantRow = 0;
        if (m_fileList->rowCount() > 0)
            loadRowIntoPlayer(wantRow);
    }
}
