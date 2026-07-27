#include "traceview.h"
#include "models/cantracemodel.h"
#include "models/canfilterproxymodel.h"

#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QGuiApplication>
#include <QHeaderView>
#include <QFontDatabase>
#include <QMouseEvent>
#include <QMessageBox>

TraceView::TraceView(QWidget *parent)
    : QTableView(parent)
{
    setupAppearance();
}

void TraceView::setupAppearance()
{
    // 等宽字体
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);
    setFont(mono);

    // 基本外观
    setAlternatingRowColors(true);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ContiguousSelection);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setSortingEnabled(false);
    setShowGrid(false);

    // 表头
    horizontalHeader()->setStretchLastSection(false);
    horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    verticalHeader()->setDefaultSectionSize(22);
    verticalHeader()->setVisible(false);

    // 初始列宽
    setColumnWidth(CanTraceModel::ColTime, 100);
    setColumnWidth(CanTraceModel::ColChannel, 40);
    setColumnWidth(CanTraceModel::ColDirection, 40);
    setColumnWidth(CanTraceModel::ColId, 120);
    setColumnWidth(CanTraceModel::ColDlc, 60);
    setColumnWidth(CanTraceModel::ColData, 300);
    setColumnWidth(CanTraceModel::ColFlags, 80);
}

void TraceView::scrollToBottom()
{
    if (model() && model()->rowCount() > 0)
        scrollTo(model()->index(model()->rowCount() - 1, 0));
}

const CanFrame *TraceView::selectedFrame() const
{
    if (!selectionModel())
        return nullptr;
    auto rows = selectionModel()->selectedRows();
    if (rows.isEmpty())
        return nullptr;

    auto *proxy = qobject_cast<CanFilterProxyModel *>(model());
    auto *source = qobject_cast<CanTraceModel *>(
        proxy ? proxy->sourceModel() : qobject_cast<CanTraceModel *>(model()));
    if (!source)
        return nullptr;

    QModelIndex sourceIndex = proxy ? proxy->mapToSource(rows.first()) : rows.first();
    return &source->frameAt(sourceIndex.row());
}

void TraceView::contextMenuEvent(QContextMenuEvent *event)
{
    QModelIndex index = indexAt(event->pos());
    QMenu menu(this);

    QAction copyAction("复制选中行", this);
    QAction copyDataAction("复制数据", this);
    QAction clearAction("清空所有", this);
    QAction filterIdAction("按此 ID 过滤", this);

    menu.addAction(&copyAction);
    menu.addAction(&copyDataAction);
    menu.addSeparator();
    menu.addAction(&filterIdAction);
    menu.addSeparator();
    menu.addAction(&clearAction);

    copyAction.setEnabled(index.isValid());
    copyDataAction.setEnabled(index.isValid());
    filterIdAction.setEnabled(index.isValid());

    QAction *selected = menu.exec(event->globalPos());
    if (!selected)
        return;

    if (selected == &copyAction || selected == &copyDataAction) {
        const CanFrame *frame = selectedFrame();
        if (!frame)
            return;

        QString text;
        if (selected == &copyAction) {
            text = QString("Time: %1  Ch: %2  Dir: %3  ID: %4  DLC: %5  Data: %6")
                       .arg(frame->timestamp, 0, 'f', 6)
                       .arg(frame->channel)
                       .arg(frame->direction == CanFrame::Rx ? "Rx" : "Tx")
                       .arg(frame->id, 0, 16)
                       .arg(frame->dlc)
                       .arg(QString(frame->data.toHex(' ')));
        } else {
            text = QString(frame->data.toHex(' '));
        }
        QGuiApplication::clipboard()->setText(text);
    } else if (selected == &filterIdAction) {
        const CanFrame *frame = selectedFrame();
        if (frame)
            emit frameDoubleClicked(*frame); // 复用此信号传递选中帧给 MainWindow 做过滤
    } else if (selected == &clearAction) {
        auto *proxy = qobject_cast<CanFilterProxyModel *>(model());
        auto *source = qobject_cast<CanTraceModel *>(
            proxy ? proxy->sourceModel() : qobject_cast<CanTraceModel *>(model()));
        if (source)
            source->clear();
    }
}

void TraceView::mouseDoubleClickEvent(QMouseEvent *event)
{
    QModelIndex index = indexAt(event->pos());
    if (index.isValid()) {
        const CanFrame *frame = selectedFrame();
        if (frame)
            emit frameDoubleClicked(*frame);
    }
    QTableView::mouseDoubleClickEvent(event);
}
