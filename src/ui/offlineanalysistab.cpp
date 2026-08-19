#include "offlineanalysistab.h"

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
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    // ---- 工具栏 ----
    auto *toolbarLayout = new QHBoxLayout;
    const QString iconCol = ThemeManager::instance()->currentTheme().text;
    m_addFileBtn = new QPushButton(svgIcon(":/icons/plus.svg", iconCol, 14), "添加文件", this);
    m_removeFileBtn = new QPushButton(svgIcon(":/icons/dash.svg", iconCol, 14), "删除文件", this);
    m_moveUpBtn = new QPushButton(svgIcon(":/icons/chevron-up.svg", iconCol, 14), "上移", this);
    m_moveDownBtn = new QPushButton(svgIcon(":/icons/chevron-down.svg", iconCol, 14), "下移", this);
    toolbarLayout->addWidget(m_addFileBtn);
    toolbarLayout->addWidget(m_removeFileBtn);
    toolbarLayout->addSpacing(10);
    toolbarLayout->addWidget(m_moveUpBtn);
    toolbarLayout->addWidget(m_moveDownBtn);
    toolbarLayout->addStretch();

    m_statusLabel = new QLabel("就绪", this);
    m_statusLabel->setObjectName("DimLabel");
    toolbarLayout->addWidget(m_statusLabel);
    mainLayout->addLayout(toolbarLayout);

    // ---- 文件列表 ----
    auto *listGroup = new QGroupBox("分析文件列表", this);
    auto *listLayout = new QVBoxLayout(listGroup);

    m_fileList = new QTableWidget(0, 5, listGroup);
    m_fileList->setHorizontalHeaderLabels(
        {"#", "文件名", "帧数", "时长", "大小"});
    m_fileList->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_fileList->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_fileList->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_fileList->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_fileList->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_fileList->verticalHeader()->setVisible(false);
    m_fileList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    listLayout->addWidget(m_fileList);

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
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        const QString c = ThemeManager::instance()->currentTheme().text;
        m_addFileBtn->setIcon(svgIcon(":/icons/plus.svg", c, 14));
        m_removeFileBtn->setIcon(svgIcon(":/icons/dash.svg", c, 14));
        m_moveUpBtn->setIcon(svgIcon(":/icons/chevron-up.svg", c, 14));
        m_moveDownBtn->setIcon(svgIcon(":/icons/chevron-down.svg", c, 14));
    });
}

void OfflineAnalysisTab::onAddFile()
{
    QStringList paths = QFileDialog::getOpenFileNames(
        this, "添加分析文件", {},
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
        nameItem->setData(Qt::UserRole, path);
        nameItem->setToolTip(path);
        m_fileList->setItem(row, 1, nameItem);

        m_fileList->setItem(row, 2, new QTableWidgetItem("解析中..."));
        m_fileList->setItem(row, 3, new QTableWidgetItem("-"));
        m_fileList->setItem(row, 4, new QTableWidgetItem(formatFileSize(fi.size())));

        m_parseQueue.enqueue(row);
    }

    m_statusLabel->setText(QString("共 %1 个文件，解析中...").arg(m_fileList->rowCount()));
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
    m_statusLabel->setText(QString("剩余 %1 个文件").arg(m_fileList->rowCount()));
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

void OfflineAnalysisTab::onParseTimer()
{
    if (m_parseQueue.isEmpty()) {
        m_parseTimer->stop();
        if (m_fileList->rowCount() > 0)
            m_statusLabel->setText(QString("共 %1 个文件，解析完成").arg(m_fileList->rowCount()));
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
    if (durItem)
        durItem->setText(formatDuration(duration));
}
