#include "offlineanalysistab.h"
#include "core/signalrelay.h"   // DEF-08：字符串信号 → lambda 桥接

#include "core/canfileio/canfileio.h"
#include "core/canfileio/canfileio_factory.h"
#include "core/canframe.h"
#include "ui/thememanager.h"
#include "utils/svg_icon.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QFileDialog>
#include <QFileInfo>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QAbstractItemView>

// ---- 工具函数 ----

static QString formatFileSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024 * 1024 * 1024)
        return QString::number(bytes / (1024.0 * 1024), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 2) + " GB";
}

static QString formatDuration(double seconds)
{
    if (seconds < 60.0)
        return QString::number(seconds, 'f', 3) + "s";
    int m = static_cast<int>(seconds) / 60;
    double s = seconds - m * 60;
    return QString("%1m %2s").arg(m).arg(s, 0, 'f', 1);
}

// ---- OfflineAnalysisTab ----

OfflineAnalysisTab::OfflineAnalysisTab(QWidget *parent)
    : QWidget(parent)
{
    setAcceptDrops(true);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    auto *toolbarLayout = new QHBoxLayout;
    const QString iconCol = ThemeManager::instance()->currentTheme().text;
    m_addFileBtn = new QPushButton(svgIcon(":/icons/plus.svg", iconCol, 14), tr("Add File"), this);
    m_removeFileBtn = new QPushButton(svgIcon(":/icons/dash.svg", iconCol, 14), tr("Remove"), this);
    m_moveUpBtn = new QPushButton(svgIcon(":/icons/chevron-up.svg", iconCol, 14), tr("Move Up"), this);
    m_moveDownBtn = new QPushButton(svgIcon(":/icons/chevron-down.svg", iconCol, 14), tr("Move Down"), this);
    toolbarLayout->addWidget(m_addFileBtn);
    toolbarLayout->addWidget(m_removeFileBtn);
    toolbarLayout->addSpacing(10);
    toolbarLayout->addWidget(m_moveUpBtn);
    toolbarLayout->addWidget(m_moveDownBtn);
    toolbarLayout->addStretch();

    m_statusLabel = new QLabel(tr("Ready"), this);
    m_statusLabel->setObjectName("DimLabel");
    toolbarLayout->addWidget(m_statusLabel);
    mainLayout->addLayout(toolbarLayout);

    auto *listGroup = new QGroupBox(tr("Analysis files"), this);
    listGroup->setObjectName(QStringLiteral("OfflineListGroup"));
    auto *listLayout = new QVBoxLayout(listGroup);

    m_fileList = new QTableWidget(0, 5, listGroup);
    m_fileList->setHorizontalHeaderLabels(
        {QStringLiteral("#"), tr("File"), tr("Frames"), tr("Duration"), tr("Size")});
    m_fileList->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_fileList->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_fileList->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_fileList->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_fileList->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_fileList->verticalHeader()->setVisible(false);
    m_fileList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    listLayout->addWidget(m_fileList);

    m_dropHint = new QLabel(tr("Drop files here"), this);
    m_dropHint->setAlignment(Qt::AlignCenter);
    m_dropHint->setStyleSheet(QString(
        "color: %1;"
        "font-size: 14px;"
        "padding: 40px;"
    ).arg(ThemeManager::instance()->currentTheme().textDim));
    listLayout->addWidget(m_dropHint);

    mainLayout->addWidget(listGroup, 1);

    // ---- 异步解析定时器 ----
    m_parseTimer = new QTimer(this);
    m_parseTimer->setInterval(50);
    m_parseTimer->setSingleShot(false);
    connect(m_parseTimer, &QTimer::timeout, this, &OfflineAnalysisTab::onParseTimer);

    // ---- 信号连接 ----
    connect(m_addFileBtn, &QPushButton::clicked, this, &OfflineAnalysisTab::onAddFile);
    connect(m_removeFileBtn, &QPushButton::clicked, this, &OfflineAnalysisTab::onRemoveFile);
    connect(m_moveUpBtn, &QPushButton::clicked, this, &OfflineAnalysisTab::onMoveUp);
    connect(m_moveDownBtn, &QPushButton::clicked, this, &OfflineAnalysisTab::onMoveDown);

    // 主题切换 → 重刷工具栏按钮图标颜色
    // DEF-08 字符串信号：ThemeManager 定义于 data.dll
    auto *themeRelay = new SignalRelay(this);
    themeRelay->fire0 = [this]() {
        const QString c = ThemeManager::instance()->currentTheme().text;
        m_addFileBtn->setIcon(svgIcon(":/icons/plus.svg", c, 14));
        m_removeFileBtn->setIcon(svgIcon(":/icons/dash.svg", c, 14));
        m_moveUpBtn->setIcon(svgIcon(":/icons/chevron-up.svg", c, 14));
        m_moveDownBtn->setIcon(svgIcon(":/icons/chevron-down.svg", c, 14));
    };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            themeRelay, SLOT(fire()));
}

void OfflineAnalysisTab::onAddFile()
{
    QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Add analysis files"), {},
        CanFileIO::allFileFilters());
    if (paths.isEmpty()) return;

    addFiles(paths);
}

void OfflineAnalysisTab::addFiles(const QStringList &paths)
{
    if (paths.isEmpty()) return;

    m_dropHint->hide();

    for (const auto &path : paths) {
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

        m_fileList->setItem(row, 2, new QTableWidgetItem(tr("Parsing...")));
        m_fileList->setItem(row, 3, new QTableWidgetItem(QStringLiteral("-")));
        m_fileList->setItem(row, 4, new QTableWidgetItem(formatFileSize(fi.size())));

        m_parseQueue.enqueue(row);
    }

    m_statusLabel->setText(tr("%1 file(s), parsing...").arg(m_fileList->rowCount()));
    if (!m_parseTimer->isActive())
        m_parseTimer->start();
}

void OfflineAnalysisTab::onRemoveFile()
{
    int row = m_fileList->currentRow();
    if (row < 0) return;

    m_parseQueue.removeOne(row);
    for (int i = 0; i < m_parseQueue.size(); ++i) {
        if (m_parseQueue[i] > row)
            m_parseQueue[i] = m_parseQueue[i] - 1;
    }

    m_fileList->removeRow(row);
    renumberRows();

    if (m_fileList->rowCount() == 0) {
        m_dropHint->show();
    } else {
        m_statusLabel->setText(tr("%1 file(s) remaining").arg(m_fileList->rowCount()));
    }
}

void OfflineAnalysisTab::onMoveUp()
{
    int row = m_fileList->currentRow();
    if (row <= 0) return;

    for (int col = 0; col < m_fileList->columnCount(); ++col) {
        auto *item1 = m_fileList->takeItem(row, col);
        auto *item2 = m_fileList->takeItem(row - 1, col);
        if (item2) m_fileList->setItem(row, col, item2);
        if (item1) m_fileList->setItem(row - 1, col, item1);
    }

    renumberRows();
    m_fileList->setCurrentCell(row - 1, 0);
}

void OfflineAnalysisTab::onMoveDown()
{
    int row = m_fileList->currentRow();
    if (row < 0 || row >= m_fileList->rowCount() - 1) return;

    for (int col = 0; col < m_fileList->columnCount(); ++col) {
        auto *item1 = m_fileList->takeItem(row, col);
        auto *item2 = m_fileList->takeItem(row + 1, col);
        if (item2) m_fileList->setItem(row, col, item2);
        if (item1) m_fileList->setItem(row + 1, col, item1);
    }

    renumberRows();
    m_fileList->setCurrentCell(row + 1, 0);
}

void OfflineAnalysisTab::renumberRows()
{
    for (int i = 0; i < m_fileList->rowCount(); ++i) {
        auto *item = m_fileList->item(i, 0);
        if (item)
            item->setText(QString::number(i + 1));
    }
}

QStringList OfflineAnalysisTab::filePaths() const
{
    QStringList paths;
    for (int i = 0; i < m_fileList->rowCount(); ++i) {
        auto *nameItem = m_fileList->item(i, 1);
        if (nameItem) {
            QString path = nameItem->data(Qt::UserRole).toString();
            if (!path.isEmpty())
                paths << path;
        }
    }
    return paths;
}

void OfflineAnalysisTab::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void OfflineAnalysisTab::dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if (!mimeData->hasUrls()) {
        event->ignore();
        return;
    }

    QStringList paths;
    const QList<QUrl> urls = mimeData->urls();
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) continue;
        
        QString filePath = url.toLocalFile();
        QFileInfo fi(filePath);
        
        // ✅ 支持 blf 和 asc 格式
        QString suffix = fi.suffix().toLower();
        if (suffix == "blf" || suffix == "asc") {
            paths << filePath;
        }
    }

    if (!paths.isEmpty()) {
        addFiles(paths);
    }
    
    event->acceptProposedAction();
}

void OfflineAnalysisTab::onParseTimer()
{
    if (m_parseQueue.isEmpty()) {
        m_parseTimer->stop();
        if (m_fileList->rowCount() > 0)
            m_statusLabel->setText(tr("%1 file(s), parse complete").arg(m_fileList->rowCount()));
        return;
    }

    int row = m_parseQueue.dequeue();
    if (row >= m_fileList->rowCount())
        return;

    parseFileInfo(row);
}

void OfflineAnalysisTab::parseFileInfo(int row)
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
        if (framesItem) framesItem->setText(tr("Parse failed"));
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
    if (durItem)
        durItem->setText(formatDuration(duration));
}

void OfflineAnalysisTab::retranslateUi()
{
    if (m_addFileBtn)
        m_addFileBtn->setText(tr("Add File"));
    if (m_removeFileBtn)
        m_removeFileBtn->setText(tr("Remove"));
    if (m_moveUpBtn)
        m_moveUpBtn->setText(tr("Move Up"));
    if (m_moveDownBtn)
        m_moveDownBtn->setText(tr("Move Down"));
    if (m_dropHint)
        m_dropHint->setText(tr("Drop files here"));
    if (m_fileList) {
        m_fileList->setHorizontalHeaderLabels(
            {QStringLiteral("#"), tr("File"), tr("Frames"), tr("Duration"), tr("Size")});
    }
    for (QGroupBox *g : findChildren<QGroupBox *>()) {
        if (g->objectName() == QLatin1String("OfflineListGroup"))
            g->setTitle(tr("Analysis files"));
    }
}
