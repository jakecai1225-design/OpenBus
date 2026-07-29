#include "traceview.h"
#include "filterbar.h"
#include "models/cantracemodel.h"
#include "models/canfilterproxymodel.h"
#include "core/dbcmanager.h"
#include "core/dbcdata.h"
#include "utils/canutils.h"

#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QGuiApplication>
#include <QHeaderView>
#include <QFontDatabase>
#include <QMouseEvent>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QPlainTextEdit>
#include <QInputDialog>
#include <QLineEdit>
#include <QSet>
#include <algorithm>

// ============================================================
//  TraceView
// ============================================================

TraceView::TraceView(QWidget *parent)
    : QTableView(parent)
{
    setupAppearance();
}

void TraceView::setupAppearance()
{
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);
    setFont(mono);

    setAlternatingRowColors(true);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ContiguousSelection);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setSortingEnabled(true);
    setShowGrid(false);

    horizontalHeader()->setStretchLastSection(false);
    horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    horizontalHeader()->setSectionsClickable(true);
    horizontalHeader()->setSectionsMovable(true);
    horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    verticalHeader()->setDefaultSectionSize(22);
    verticalHeader()->setVisible(false);

    setColumnWidth(CanTraceModel::ColTime, 100);
    setColumnWidth(CanTraceModel::ColChannel, 40);
    setColumnWidth(CanTraceModel::ColDirection, 40);
    setColumnWidth(CanTraceModel::ColId, 120);
    setColumnWidth(CanTraceModel::ColDlc, 60);
    setColumnWidth(CanTraceModel::ColData, 300);
    setColumnWidth(CanTraceModel::ColFlags, 80);

    // 默认按时间升序排序
    sortByColumn(CanTraceModel::ColTime, Qt::AscendingOrder);

    // 表头信号
    connect(horizontalHeader(), &QHeaderView::sectionClicked,
            this, &TraceView::onHeaderClicked);
    connect(horizontalHeader(), &QHeaderView::customContextMenuRequested,
            this, &TraceView::onHeaderContextMenu);
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
            emit frameDoubleClicked(*frame);
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

// ============================================================
//  表头排序 + 筛选菜单
// ============================================================

void TraceView::onHeaderClicked(int column)
{
    // 点击表头自动切换升/降序（QTableView 内置已处理，这里仅做额外逻辑）
    Q_UNUSED(column);
}

void TraceView::onHeaderContextMenu(const QPoint &pos)
{
    int column = horizontalHeader()->logicalIndexAt(pos);
    if (column < 0) return;
    showHeaderMenu(column, horizontalHeader()->viewport()->mapToGlobal(pos));
}

QString TraceView::columnFilterHint(int column) const
{
    switch (column) {
    case CanTraceModel::ColTime:      return "例如: >0.5  或  <1.0  或  0.123";
    case CanTraceModel::ColChannel:   return "例如: 1  或  2";
    case CanTraceModel::ColDirection: return "rx  或  tx";
    case CanTraceModel::ColId:        return "例如: 0x123  或  >0x100  或  !=0x200";
    case CanTraceModel::ColDlc:       return "例如: 8  或  >4";
    case CanTraceModel::ColData:      return "例如: 01 02  或  FF";
    case CanTraceModel::ColFlags:     return "例如: FD  或  BRS";
    }
    return {};
}

void TraceView::showHeaderMenu(int column, const QPoint &pos)
{
    auto *proxy = qobject_cast<CanFilterProxyModel *>(model());
    if (!proxy) return;

    QMenu menu(this);

    // 排序选项
    QAction sortAsc("↑ 升序排序", this);
    QAction sortDesc("↓ 降序排序", this);
    menu.addAction(&sortAsc);
    menu.addAction(&sortDesc);

    menu.addSeparator();

    // 列筛选
    QString colName = model()->headerData(column, Qt::Horizontal).toString();
    QAction filterAction(QString("筛选 %1...").arg(colName), this);
    menu.addAction(&filterAction);

    // 快捷筛选（根据列类型）
    if (column == CanTraceModel::ColDirection) {
        menu.addSeparator();
        QAction rxOnly("仅 Rx", this);
        QAction txOnly("仅 Tx", this);
        menu.addAction(&rxOnly);
        menu.addAction(&txOnly);
        connect(&rxOnly, &QAction::triggered, this, [this, column]() {
            auto *p = qobject_cast<CanFilterProxyModel *>(model());
            if (p) p->setColumnFilter(column, "rx");
        });
        connect(&txOnly, &QAction::triggered, this, [this, column]() {
            auto *p = qobject_cast<CanFilterProxyModel *>(model());
            if (p) p->setColumnFilter(column, "tx");
        });
    }

    if (column == CanTraceModel::ColId) {
        menu.addSeparator();
        // 收集唯一 ID
        auto *src = qobject_cast<CanTraceModel *>(proxy->sourceModel());
        if (src) {
            QSet<quint32> ids;
            for (const auto &f : src->frames())
                ids.insert(f.id);
            // 只显示前 20 个，避免菜单过长
            QList<quint32> sortedIds = ids.values();
            std::sort(sortedIds.begin(), sortedIds.end());
            int shown = 0;
            for (quint32 id : sortedIds) {
                if (shown++ >= 20) {
                    menu.addAction(QString("... 共 %1 个 ID").arg(ids.size()))->setEnabled(false);
                    break;
                }
                QString idStr = CanUtils::formatId(id, id > 0x7FF);
                auto *act = menu.addAction(idStr);
                connect(act, &QAction::triggered, this, [this, column, idStr]() {
                    auto *p = qobject_cast<CanFilterProxyModel *>(model());
                    if (p) p->setColumnFilter(column, idStr);
                });
            }
        }
    }

    // 清除列筛选
    if (proxy->hasColumnFilter(column)) {
        menu.addSeparator();
        QAction clearColAct("✕ 清除本列筛选", this);
        menu.addAction(&clearColAct);
        connect(&clearColAct, &QAction::triggered, this, [this, column]() {
            onClearColumnFilter(column);
        });
    }

    // 清除所有筛选
    menu.addSeparator();
    QAction clearAllAct("清除所有筛选", this);
    menu.addAction(&clearAllAct);
    connect(&clearAllAct, &QAction::triggered, this, &TraceView::onClearAllFilters);

    // 排序连接
    connect(&sortAsc, &QAction::triggered, this, [this, column]() {
        sortByColumn(column, Qt::AscendingOrder);
    });
    connect(&sortDesc, &QAction::triggered, this, [this, column]() {
        sortByColumn(column, Qt::DescendingOrder);
    });

    // 列筛选对话框
    connect(&filterAction, &QAction::triggered, this, [this, column]() {
        onColumnFilter(column);
    });

    menu.exec(pos);
}

void TraceView::onColumnFilter(int column)
{
    auto *proxy = qobject_cast<CanFilterProxyModel *>(model());
    if (!proxy) return;

    QString colName = model()->headerData(column, Qt::Horizontal).toString();
    QString current = proxy->columnFilter(column);
    QString hint = columnFilterHint(column);

    bool ok = false;
    QString text = QInputDialog::getText(
        this, QString("筛选 %1").arg(colName),
        QString("输入筛选条件:\n  %1").arg(hint),
        QLineEdit::Normal, current, &ok);

    if (ok) {
        if (text.trimmed().isEmpty())
            proxy->clearColumnFilter(column);
        else
            proxy->setColumnFilter(column, text);
    }
}

void TraceView::onClearColumnFilter(int column)
{
    auto *proxy = qobject_cast<CanFilterProxyModel *>(model());
    if (proxy)
        proxy->clearColumnFilter(column);
}

void TraceView::onClearAllFilters()
{
    auto *proxy = qobject_cast<CanFilterProxyModel *>(model());
    if (proxy)
        proxy->clearAllColumnFilters();
}

// ============================================================
//  FrameInfoWidget — 紧凑文本帧结构
// ============================================================

FrameInfoWidget::FrameInfoWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *title = new QLabel("帧结构", this);
    title->setObjectName("DockPanelTitle");
    title->setContentsMargins(6, 3, 6, 3);
    layout->addWidget(title);

    m_edit = new QPlainTextEdit(this);
    m_edit->setReadOnly(true);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);
    m_edit->setFont(mono);
    m_edit->setPlaceholderText("选中报文查看帧结构...");
    layout->addWidget(m_edit, 1);
}

void FrameInfoWidget::setFrame(const CanFrame &frame)
{
    // 紧凑对齐文本 — 字段名宽度固定，值紧随其后
    auto fmt = [](const char *field, const QString &val) {
        return QString("%1:  %2").arg(field, 10).arg(val);
    };

    QString text;
    text += fmt("Time",      CanUtils::formatTime(frame.timestamp))        + "\n";
    text += fmt("Channel",   QString::number(frame.channel))               + "\n";
    text += fmt("Direction", frame.direction == CanFrame::Rx ? "Rx" : "Tx") + "\n";
    text += fmt("ID",        CanUtils::formatId(frame.id, frame.extended)) + "\n";
    text += fmt("DLC",       CanUtils::formatDlc(frame.dlc, frame.fd))     + "\n";
    text += fmt("Data",      CanUtils::formatData(frame.data))             + "\n";
    text += fmt("Flags",     CanUtils::formatFlags(frame))                 + "\n";

    // Hex dump (紧凑单行)
    text += "\nHex:  ";
    const auto &data = frame.data;
    for (int i = 0; i < data.size() && i < 64; ++i)
        text += QString("%1 ").arg(static_cast<quint8>(data[i]), 2, 16, QChar('0')).toUpper();
    if (data.size() > 64)
        text += "...";

    m_edit->setPlainText(text);
}

void FrameInfoWidget::clear()
{
    m_edit->clear();
}

// ============================================================
//  SignalDecodeWidget — 紧凑文本信号解析
// ============================================================

SignalDecodeWidget::SignalDecodeWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *title = new QLabel("信号解析", this);
    title->setObjectName("DockPanelTitle");
    title->setContentsMargins(6, 3, 6, 3);
    layout->addWidget(title);

    m_edit = new QPlainTextEdit(this);
    m_edit->setReadOnly(true);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);
    m_edit->setFont(mono);
    m_edit->setPlaceholderText("选中报文查看信号解析...");
    layout->addWidget(m_edit, 1);
}

void SignalDecodeWidget::setFrame(const CanFrame &frame)
{
    if (!m_dbcMgr) {
        m_edit->setPlainText("(未加载 DBC 文件)");
        return;
    }

    // 使用批量解码 API（O(1) 哈希查找 + 批量解码）
    auto decoded = m_dbcMgr->decodeFrame(frame.id, frame.data);
    if (decoded.isEmpty()) {
        m_edit->setPlainText(QString("ID %1 未在 DBC 中定义")
            .arg(CanUtils::formatId(frame.id, frame.extended)));
        return;
    }

    // 报文名称（从索引查找）
    const DbcMessage *msg = m_dbcMgr->findMessage(frame.id);
    QString text = QString("%1  (0x%2)\n")
        .arg(msg ? msg->name : "?")
        .arg(frame.id, 0, 16).toUpper();
    text += "-----------------------------------\n\n";

    // 计算信号名最大宽度
    int maxName = 0;
    for (const auto &ds : decoded)
        maxName = qMax(maxName, ds.name.length());
    maxName = qMin(maxName + 2, 24);

    for (const auto &ds : decoded) {
        QString valStr = QString::number(ds.physValue, 'f', 3);
        if (!ds.unit.isEmpty())
            valStr += " " + ds.unit;
        // 值表描述（如有）
        if (!ds.valueDesc.isEmpty())
            valStr += QString("  [%1]").arg(ds.valueDesc);
        text += QString("%1  %2\n").arg(ds.name, -maxName).arg(valStr);
    }

    m_edit->setPlainText(text);
}

void SignalDecodeWidget::clear()
{
    m_edit->clear();
}

// ============================================================
//  TraceTab — Wireshark 风格整体三栏
// ============================================================

TraceTab::TraceTab(QWidget *parent)
    : QWidget(parent)
{
    // 每个标签页拥有独立的数据模型
    m_traceModel = new CanTraceModel(this);
    m_proxyModel = new CanFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_traceModel);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 过滤栏（含 Start/Stop 按钮）
    m_filterBar = new FilterBar(this);
    layout->addWidget(m_filterBar);

    // 垂直分割: TraceView (上) | 底部信息 (下)
    m_vSplitter = new QSplitter(Qt::Vertical, this);
    m_vSplitter->setHandleWidth(2);

    // TraceView — 使用自己的代理模型
    m_traceView = new TraceView(this);
    m_traceView->setModel(m_proxyModel);
    m_vSplitter->addWidget(m_traceView);

    // 水平分割: 帧结构 (左) | 信号解析 (右)
    m_hSplitter = new QSplitter(Qt::Horizontal, this);
    m_hSplitter->setHandleWidth(2);

    m_frameInfo = new FrameInfoWidget(this);
    m_signalDecode = new SignalDecodeWidget(this);

    m_hSplitter->addWidget(m_frameInfo);
    m_hSplitter->addWidget(m_signalDecode);
    m_hSplitter->setSizes({400, 400});

    m_vSplitter->addWidget(m_hSplitter);
    m_vSplitter->setSizes({500, 200});
    m_vSplitter->setStretchFactor(0, 3);
    m_vSplitter->setStretchFactor(1, 1);

    layout->addWidget(m_vSplitter, 1);

    // 选中行变化 → 更新底部面板
    connect(m_traceView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &TraceTab::onSelectionChanged);
}

void TraceTab::setDbcManager(DbcManager *mgr)
{
    m_signalDecode->setDbcManager(mgr);
}

void TraceTab::setRunning(bool running)
{
    m_running = running;
    m_filterBar->setRunning(running);
}

void TraceTab::appendFrame(const CanFrame &frame)
{
    m_traceModel->appendFrame(frame);
}

void TraceTab::clearTrace()
{
    m_traceModel->clear();
}

int TraceTab::frameCount() const
{
    return m_traceModel->frameCount();
}

bool TraceTab::setFilterExpression(const QString &expr)
{
    return m_proxyModel->setFilterExpression(expr);
}

void TraceTab::clearFilter()
{
    m_proxyModel->clearFilter();
}

void TraceTab::onSelectionChanged()
{
    const CanFrame *frame = m_traceView->selectedFrame();
    if (frame) {
        m_frameInfo->setFrame(*frame);
        m_signalDecode->setFrame(*frame);
        emit m_traceView->frameSelected(*frame);
    }
}
