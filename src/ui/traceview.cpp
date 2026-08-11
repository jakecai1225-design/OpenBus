#include "traceview.h"
#include "filterbar.h"
#include "filterheaderview.h"
#include "models/cantracemodel.h"
#include "models/canfilterproxymodel.h"
#include "models/viewportproxy.h"
#include "core/dbcmanager.h"
#include "core/dbcdata.h"
#include "utils/canutils.h"

#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QGuiApplication>
#include <QHeaderView>
#include <QScrollBar>
#include <QFontDatabase>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QHideEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QCursor>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QPlainTextEdit>
#include <QInputDialog>
#include <QLineEdit>
#include <QSet>
#include <QColorDialog>
#include <QList>
#include <algorithm>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QThread>
#include <QShortcut>
#include <QActionGroup>
#include <QTimer>
#include <QKeySequence>
#include "core/canfileio/canfileio.h"
#include "core/canfileio/canfileio_factory.h"

// ============================================================
//  TraceView
// ============================================================

TraceView::TraceView(QWidget *parent)
    : QTableView(parent)
{
    setupAppearance();

    // Wireshark 风格快捷键
    auto addShortcut = [this](const QKeySequence &key, void (TraceView::*slot)()) {
        auto *sc = new QShortcut(key, this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, slot);
    };
    addShortcut(QKeySequence("Ctrl+G"),       &TraceView::onGoToPacket);
    addShortcut(QKeySequence("Ctrl+F"),       &TraceView::onFind);
    addShortcut(QKeySequence("F3"),           &TraceView::onFindNext);
    addShortcut(QKeySequence("Shift+F3"),     &TraceView::onFindPrevious);
    addShortcut(QKeySequence("Ctrl+Down"),    &TraceView::goToNextSameId);
    addShortcut(QKeySequence("Ctrl+Up"),      &TraceView::goToPrevSameId);
}

void TraceView::setupAppearance()
{
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);
    setFont(mono);

    setAlternatingRowColors(true);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setSortingEnabled(false);  // 自行处理表头点击排序（3-state）
    setShowGrid(false);

    // 替换为 Wireshark 风格漏斗表头
    auto *filterHeader = new FilterHeaderView(Qt::Horizontal, this);
    setHorizontalHeader(filterHeader);
    filterHeader->setStretchLastSection(false);
    filterHeader->setSectionResizeMode(QHeaderView::Interactive);
    filterHeader->setSectionsClickable(true);
    filterHeader->setSectionsMovable(true);
    filterHeader->setContextMenuPolicy(Qt::CustomContextMenu);
    verticalHeader()->setDefaultSectionSize(22);
    verticalHeader()->setVisible(false);

    // 列宽 — 参照 CANoe 风格，Data 列拉伸填充剩余空间
    setColumnWidth(CanTraceModel::ColNo, 70);
    setColumnWidth(CanTraceModel::ColTime, 110);
    setColumnWidth(CanTraceModel::ColDelta, 100);
    setColumnWidth(CanTraceModel::ColChannel, 50);
    setColumnWidth(CanTraceModel::ColDirection, 50);
    setColumnWidth(CanTraceModel::ColId, 130);
    setColumnWidth(CanTraceModel::ColDlc, 60);
    setColumnWidth(CanTraceModel::ColData, 400);
    setColumnWidth(CanTraceModel::ColFlags, 90);
    setColumnWidth(CanTraceModel::ColFrameCount, 80);

    // 默认按帧编号升序排序（实际排序在 setModel 后执行）
    m_sortColumn = CanTraceModel::ColNo;
    m_sortOrder = Qt::AscendingOrder;
    auto *fh = qobject_cast<FilterHeaderView *>(horizontalHeader());
    if (fh)
        fh->setSortState(CanTraceModel::ColNo, Qt::AscendingOrder);

    // 表头信号
    connect(filterHeader, &QHeaderView::sectionClicked,
            this, &TraceView::onHeaderClicked);
    connect(filterHeader, &QHeaderView::customContextMenuRequested,
            this, &TraceView::onHeaderContextMenu);
    connect(filterHeader, &FilterHeaderView::filterClicked,
            this, &TraceView::onFilterIconClicked);
}

void TraceView::setModel(QAbstractItemModel *model)
{
    QTableView::setModel(model);
    // 自动将过滤代理模型传递给 FilterHeaderView
    auto *fh = qobject_cast<FilterHeaderView *>(horizontalHeader());
    if (fh) {
        // 穿越视窗代理层找到 CanFilterProxyModel
        auto *fp = filterProxy();
        fh->setProxyModel(fp);
        // Data 列自动拉伸填满剩余宽度（需在模型设置后调用）
        fh->setSectionResizeMode(CanTraceModel::ColData, QHeaderView::Stretch);
    }
    // 应用初始排序（setupAppearance 中设置的状态）
    if (m_sortColumn >= 0) {
        auto *fp = filterProxy();
        if (fp)
            fp->sort(m_sortColumn, m_sortOrder);
    }
}

void TraceView::scrollToBottom()
{
    auto *vp = viewportProxy();
    if (vp) {
        // CANoe 视窗模式: 移动视窗到末尾
        vp->scrollToEnd();
        // 视窗内滚动到底部
        if (model() && model()->rowCount() > 0)
            scrollTo(model()->index(model()->rowCount() - 1, 0));
        return;
    }
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

    auto *vp = viewportProxy();
    auto *fp = filterProxy();
    auto *source = traceSource();
    if (!source)
        return nullptr;

    // 穿越视窗代理 → 过滤代理 → 源模型
    QModelIndex proxyIdx = rows.first();
    QModelIndex filterIdx = vp ? vp->mapToSource(proxyIdx) : proxyIdx;
    QModelIndex sourceIdx = fp ? fp->mapToSource(filterIdx) : filterIdx;
    if (!sourceIdx.isValid() || sourceIdx.row() < 0 || sourceIdx.row() >= source->rowCount())
        return nullptr;
    return &source->frameAt(sourceIdx.row());
}

void TraceView::contextMenuEvent(QContextMenuEvent *event)
{
    QModelIndex index = indexAt(event->pos());
    QMenu menu(this);

    QAction copyAction(QStringLiteral("复制选中行"), this);
    QAction copyDataAction(QStringLiteral("复制数据"), this);
    QAction addToGraphicAction(QStringLiteral("发送到 Graphic"), this);
    QAction clearFilterAction(QStringLiteral("x 清除过滤"), this);

    menu.addAction(&copyAction);
    menu.addAction(&copyDataAction);
    menu.addSeparator();
    menu.addAction(&addToGraphicAction);
    menu.addAction(&clearFilterAction);

    // ---- Wireshark 风格 Apply as Filter ----
    if (index.isValid()) {
        int col = index.column();
        QString cellValue = index.data(Qt::DisplayRole).toString();
        if (!cellValue.isEmpty()) {
            menu.addSeparator();
            QMenu *applyMenu = menu.addMenu(QStringLiteral("应用为过滤"));

            // 根据列类型提供不同的过滤操作
            bool isNumeric = (col == CanTraceModel::ColNo || col == CanTraceModel::ColTime ||
                              col == CanTraceModel::ColDelta || col == CanTraceModel::ColId ||
                              col == CanTraceModel::ColDlc || col == CanTraceModel::ColFrameCount);

            // == (等于 / 包含)
            auto *actEq = applyMenu->addAction(QStringLiteral("==  (等于)"));
            connect(actEq, &QAction::triggered, this, [this, col, cellValue]() {
                auto *p = filterProxy();
                if (p) { pinSelection(); p->setColumnFilter(col, cellValue); restoreSelection(); }
            });

            // != (不等于)
            auto *actNe = applyMenu->addAction(QStringLiteral("!=  (不等于)"));
            connect(actNe, &QAction::triggered, this, [this, col, cellValue]() {
                auto *p = filterProxy();
                if (p) { pinSelection(); p->setColumnFilter(col, "!=" + cellValue); restoreSelection(); }
            });

            // > 和 < (仅数值列)
            if (isNumeric) {
                auto *actGt = applyMenu->addAction(QStringLiteral(">   (大于)"));
                connect(actGt, &QAction::triggered, this, [this, col, cellValue]() {
                    auto *p = filterProxy();
                    if (p) { pinSelection(); p->setColumnFilter(col, ">" + cellValue); restoreSelection(); }
                });

                auto *actLt = applyMenu->addAction(QStringLiteral("<   (小于)"));
                connect(actLt, &QAction::triggered, this, [this, col, cellValue]() {
                    auto *p = filterProxy();
                    if (p) { pinSelection(); p->setColumnFilter(col, "<" + cellValue); restoreSelection(); }
                });
            }

            // contains (包含, 仅文本列)
            if (!isNumeric && col != CanTraceModel::ColDirection) {
                auto *actContains = applyMenu->addAction(QStringLiteral("contains  (包含)"));
                connect(actContains, &QAction::triggered, this, [this, col, cellValue]() {
                    auto *p = filterProxy();
                    if (p) { pinSelection(); p->setColumnFilter(col, cellValue); restoreSelection(); }
                });
            }

            // 会话过滤: 同 ID
            if (col == CanTraceModel::ColId) {
                applyMenu->addSeparator();
                auto *actConv = applyMenu->addAction(QStringLiteral("同 ID 会话"));
                connect(actConv, &QAction::triggered, this, [this, col, cellValue]() {
                    auto *p = filterProxy();
                    if (p) { pinSelection(); p->setColumnFilter(col, cellValue); restoreSelection(); }
                });
            }
        }
    }

    // 标记与着色子菜单
    auto rows = selectedSourceRows();
    if (!rows.isEmpty()) {
        menu.addSeparator();
        QMenu *markMenu = menu.addMenu(QStringLiteral("标记与着色"));

        // ---- 快速着色（Notepad++ 风格预设色） ----
        QMenu *colorMenu = markMenu->addMenu(QStringLiteral("着色选中行"));
        struct PresetColor { const char *name; QColor color; };
        static const PresetColor presetColors[] = {
            { "红色",   QColor(0xFF, 0xCDD, 0xCD) },
            { "橙色",   QColor(0xFF, 0xE0, 0xB2) },
            { "黄色",   QColor(0xFF, 0xF3, 0xB0) },
            { "绿色",   QColor(0xC8, 0xE6, 0xC9) },
            { "青色",   QColor(0xB2, 0xDF, 0xDB) },
            { "蓝色",   QColor(0xBB, 0xDE, 0xFB) },
            { "紫色",   QColor(0xE1, 0xBE, 0xE7) },
            { "灰色",   QColor(0xE0, 0xE0, 0xE0) },
        };
        for (const auto &pc : presetColors) {
            auto *act = colorMenu->addAction(QString::fromUtf8(pc.name));
            connect(act, &QAction::triggered, this, [this, rows, pc]() {
                auto *source = traceSource();
                if (!source) return;
                for (int row : rows)
                    source->setRowColor(row, pc.color);
            });
        }
        colorMenu->addSeparator();
        QAction customColorAct(QStringLiteral("自定义颜色..."), this);
        colorMenu->addAction(&customColorAct);
        connect(&customColorAct, &QAction::triggered, this, &TraceView::onColorSelected);

        // ---- 标签 ----
        QAction labelAct(QStringLiteral("设置标签..."), this);
        markMenu->addAction(&labelAct);
        connect(&labelAct, &QAction::triggered, this, [this, rows]() {
            auto *source = traceSource();
            if (!source) return;
            bool ok = false;
            QString text = QInputDialog::getText(
                this, QStringLiteral("设置标签"),
                QStringLiteral("标签文字:"), QLineEdit::Normal, {}, &ok);
            if (!ok) return;
            for (int row : rows) {
                source->setRowColor(row, QColor(0xFF, 0xF3, 0xB0));
                source->setRowLabel(row, text);
            }
        });

        markMenu->addSeparator();

        // ---- 跳转到标记 ----
        auto *source = traceSource();
        if (source) {
            auto marks = source->labeledMarks();
            if (!marks.isEmpty()) {
                QMenu *jumpMenu = markMenu->addMenu(QStringLiteral("跳转到标记"));
                for (const auto &mark : marks) {
                    QString text = QStringLiteral("行 %1: %2")
                                       .arg(mark.first + 1).arg(mark.second);
                    auto *act = jumpMenu->addAction(text);
                    connect(act, &QAction::triggered, this, [this, mark]() {
                        selectSourceRow(mark.first);
                    });
                }
                jumpMenu->addSeparator();
                QAction clearLabelsAct(QStringLiteral("清除所有标签"), this);
                jumpMenu->addAction(&clearLabelsAct);
                connect(&clearLabelsAct, &QAction::triggered, this, [this]() {
                    auto *src = traceSource();
                    if (src) src->clearLabels();
                });
            }
        }

        markMenu->addSeparator();

        QAction toggleMarkAct(QStringLiteral("标记/取消标记"), this);
        QAction clearMarkAct(QStringLiteral("清除所有标记"), this);
        QAction clearColorAct(QStringLiteral("清除所有颜色"), this);
        markMenu->addAction(&toggleMarkAct);
        markMenu->addSeparator();
        markMenu->addAction(&clearMarkAct);
        markMenu->addAction(&clearColorAct);

        connect(&toggleMarkAct, &QAction::triggered, this, &TraceView::onToggleMarkSelected);
        connect(&clearMarkAct, &QAction::triggered, this, &TraceView::onClearMarks);
        connect(&clearColorAct, &QAction::triggered, this, &TraceView::onClearColors);
    }

    // ---- Wireshark 风格导航 ----
    menu.addSeparator();
    QMenu *navMenu = menu.addMenu(QStringLiteral("导航"));

    QAction goToAct(QStringLiteral("转到分组...  (Ctrl+G)"), this);
    QAction findAct(QStringLiteral("查找...  (Ctrl+F)"), this);
    QAction findNextAct(QStringLiteral("查找下一个  (F3)"), this);
    QAction findPrevAct(QStringLiteral("查找上一个  (Shift+F3)"), this);
    QAction nextSameIdAct(QStringLiteral("下一个相同 ID  (Ctrl+Down)"), this);
    QAction prevSameIdAct(QStringLiteral("上一个相同 ID  (Ctrl+Up)"), this);

    navMenu->addAction(&goToAct);
    navMenu->addSeparator();
    navMenu->addAction(&findAct);
    navMenu->addAction(&findNextAct);
    navMenu->addAction(&findPrevAct);
    navMenu->addSeparator();
    navMenu->addAction(&nextSameIdAct);
    navMenu->addAction(&prevSameIdAct);

    findNextAct.setEnabled(!m_lastFindText.isEmpty());
    findPrevAct.setEnabled(!m_lastFindText.isEmpty());
    nextSameIdAct.setEnabled(index.isValid());
    prevSameIdAct.setEnabled(index.isValid());

    connect(&goToAct, &QAction::triggered, this, &TraceView::onGoToPacket);
    connect(&findAct, &QAction::triggered, this, &TraceView::onFind);
    connect(&findNextAct, &QAction::triggered, this, &TraceView::onFindNext);
    connect(&findPrevAct, &QAction::triggered, this, &TraceView::onFindPrevious);
    connect(&nextSameIdAct, &QAction::triggered, this, &TraceView::goToNextSameId);
    connect(&prevSameIdAct, &QAction::triggered, this, &TraceView::goToPrevSameId);

    menu.addSeparator();
    QAction clearAction(QStringLiteral("清空所有"), this);
    menu.addAction(&clearAction);

    copyAction.setEnabled(index.isValid());
    copyDataAction.setEnabled(index.isValid());
    addToGraphicAction.setEnabled(index.isValid());
    clearFilterAction.setEnabled(true);

    QAction *selected = menu.exec(event->globalPos());
    if (!selected)
        return;

    if (selected == &copyAction || selected == &copyDataAction) {
        const CanFrame *frame = selectedFrame();
        if (!frame)
            return;

        QString text;
        if (selected == &copyAction) {
            text = QString("No.: %1  Time: %2  Ch: %3  Dir: %4  ID: %5  DLC: %6  Data: %7")
                       .arg(rows.isEmpty() ? 0 : rows.first() + 1)
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
    } else if (selected == &addToGraphicAction) {
        const CanFrame *frame = selectedFrame();
        if (frame)
            emit frameAddToGraphic(*frame);
    } else if (selected == &clearFilterAction) {
        emit clearFilterRequested();
    } else if (selected == &clearAction) {
        auto *source = traceSource();
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

void TraceView::wheelEvent(QWheelEvent *event)
{
    // CANoe 风格: 鼠标滑轮只在当前视窗内滚动，不移动视窗
    QTableView::wheelEvent(event);
}

ViewportProxyModel *TraceView::viewportProxy() const
{
    return qobject_cast<ViewportProxyModel *>(model());
}

CanFilterProxyModel *TraceView::filterProxy() const
{
    auto *vp = viewportProxy();
    if (vp)
        return qobject_cast<CanFilterProxyModel *>(vp->sourceModel());
    return qobject_cast<CanFilterProxyModel *>(model());
}

CanTraceModel *TraceView::traceSource() const
{
    auto *fp = filterProxy();
    return fp ? qobject_cast<CanTraceModel *>(fp->sourceModel()) : nullptr;
}

// ============================================================
//  表头排序 + 筛选菜单
// ============================================================

void TraceView::onHeaderClicked(int column)
{
    // Wireshark 风格 3-state 排序: Asc → Desc → No Sort → Asc...
    auto *fh = qobject_cast<FilterHeaderView *>(horizontalHeader());
    auto *fp = filterProxy();
    if (!fp)
        return;
    if (column == m_sortColumn) {
        // 点击当前排序列: Asc → Desc → No Sort
        if (m_sortOrder == Qt::AscendingOrder) {
            m_sortOrder = Qt::DescendingOrder;
            if (fh)
                fh->setSortState(column, m_sortOrder);
            fp->sort(column, m_sortOrder);
        } else if (m_sortOrder == Qt::DescendingOrder) {
            // 切换到无排序
            m_sortColumn = -1;
            m_sortOrder = Qt::AscendingOrder;
            if (fh)
                fh->clearSortState();
            fp->sort(-1, Qt::AscendingOrder);
        }
    } else {
        // 点击新列: 从升序开始
        m_sortColumn = column;
        m_sortOrder = Qt::AscendingOrder;
        if (fh)
            fh->setSortState(column, m_sortOrder);
        fp->sort(column, m_sortOrder);
    }
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
    case CanTraceModel::ColNo:         return QStringLiteral("例如: >10  或  <50  或  123");
    case CanTraceModel::ColTime:      return QStringLiteral("例如: >0.5  或  <1.0  或  0.123");
    case CanTraceModel::ColDelta:     return QStringLiteral("例如: >0.01  或  <0.1  排查周期异常");
    case CanTraceModel::ColChannel:   return QStringLiteral("例如: 1  或  2");
    case CanTraceModel::ColDirection: return QStringLiteral("rx  或  tx");
    case CanTraceModel::ColId:        return QStringLiteral("例如: 0x123  或  >0x100  或  !=0x200");
    case CanTraceModel::ColDlc:       return QStringLiteral("例如: 8  或  >4");
    case CanTraceModel::ColData:      return QStringLiteral("例如: 01 02  或  FF");
    case CanTraceModel::ColFlags:     return QStringLiteral("例如: FD  或  BRS");
    case CanTraceModel::ColFrameCount: return QStringLiteral("例如: >10  或  50");
    }
    return {};
}

void TraceView::showHeaderMenu(int column, const QPoint &pos)
{
    auto *proxy = filterProxy();
    if (!proxy) return;

    QMenu menu(this);

    // 排序子菜单
    QMenu *sortMenu = menu.addMenu(QStringLiteral("排序"));
    QAction sortAsc(QStringLiteral("↑ 升序排序"), this);
    QAction sortDesc(QStringLiteral("↓ 降序排序"), this);
    QAction sortNone(QStringLiteral("不排序"), this);
    sortMenu->addAction(&sortAsc);
    sortMenu->addAction(&sortDesc);
    sortMenu->addSeparator();
    sortMenu->addAction(&sortNone);
    // 根据当前排序状态标记
    if (m_sortColumn == column && m_sortOrder == Qt::AscendingOrder)
        sortAsc.setChecked(true);
    else if (m_sortColumn == column && m_sortOrder == Qt::DescendingOrder)
        sortDesc.setChecked(true);
    sortAsc.setCheckable(true);
    sortDesc.setCheckable(true);
    sortNone.setCheckable(m_sortColumn < 0);
    sortNone.setCheckable(true);
    if (m_sortColumn < 0)
        sortNone.setChecked(true);

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
            auto *p = filterProxy();
            if (p) { pinSelection(); p->setColumnFilter(column, "rx"); restoreSelection(); }
        });
        connect(&txOnly, &QAction::triggered, this, [this, column]() {
            auto *p = filterProxy();
            if (p) { pinSelection(); p->setColumnFilter(column, "tx"); restoreSelection(); }
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
                    auto *p = filterProxy();
                    if (p) { pinSelection(); p->setColumnFilter(column, idStr); restoreSelection(); }
                });
            }
        }
    }

    // 清除列筛选
    if (proxy->hasColumnFilter(column)) {
        menu.addSeparator();
        QAction clearColAct("清除本列筛选", this);
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

    // 排序连接 — 使用 3-state 机制
    connect(&sortAsc, &QAction::triggered, this, [this, column]() {
        m_sortColumn = column;
        m_sortOrder = Qt::AscendingOrder;
        auto *fh = qobject_cast<FilterHeaderView *>(horizontalHeader());
        if (fh)
            fh->setSortState(column, m_sortOrder);
        auto *fp = filterProxy();
        if (fp)
            fp->sort(column, m_sortOrder);
    });
    connect(&sortDesc, &QAction::triggered, this, [this, column]() {
        m_sortColumn = column;
        m_sortOrder = Qt::DescendingOrder;
        auto *fh = qobject_cast<FilterHeaderView *>(horizontalHeader());
        if (fh)
            fh->setSortState(column, m_sortOrder);
        auto *fp = filterProxy();
        if (fp)
            fp->sort(column, m_sortOrder);
    });
    connect(&sortNone, &QAction::triggered, this, [this]() {
        m_sortColumn = -1;
        m_sortOrder = Qt::AscendingOrder;
        auto *fh = qobject_cast<FilterHeaderView *>(horizontalHeader());
        if (fh)
            fh->clearSortState();
        auto *fp = filterProxy();
        if (fp)
            fp->sort(-1, Qt::AscendingOrder);
    });

    // 列筛选对话框
    connect(&filterAction, &QAction::triggered, this, [this, column]() {
        onColumnFilter(column);
    });

    menu.exec(pos);
}

void TraceView::onColumnFilter(int column)
{
    auto *proxy = filterProxy();
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
        pinSelection();
        if (text.trimmed().isEmpty())
            proxy->clearColumnFilter(column);
        else
            proxy->setColumnFilter(column, text);
        restoreSelection();
    }
}

void TraceView::onClearColumnFilter(int column)
{
    auto *proxy = filterProxy();
    if (!proxy) return;
    pinSelection();
    proxy->clearColumnFilter(column);
    restoreSelection();
}

void TraceView::onClearAllFilters()
{
    auto *proxy = filterProxy();
    if (!proxy) return;
    pinSelection();
    proxy->clearAllColumnFilters();
    restoreSelection();
}

// ============================================================
//  漏斗图标点击 → 弹出列筛选
// ============================================================

void TraceView::onFilterIconClicked(int column)
{
    onColumnFilter(column);
}

// ============================================================
//  行标记与着色
// ============================================================

int TraceView::toSourceRow(const QModelIndex &proxyIndex) const
{
    if (!proxyIndex.isValid())
        return -1;
    auto *vp = viewportProxy();
    auto *fp = filterProxy();
    // 穿越视窗代理 → 过滤代理 → 源模型
    QModelIndex filterIdx = vp ? vp->mapToSource(proxyIndex) : proxyIndex;
    QModelIndex sourceIdx = fp ? fp->mapToSource(filterIdx) : filterIdx;
    return sourceIdx.isValid() ? sourceIdx.row() : -1;
}

QList<int> TraceView::selectedSourceRows() const
{
    QList<int> rows;
    if (!selectionModel())
        return rows;
    const auto indexes = selectionModel()->selectedRows();
    rows.reserve(indexes.size());
    for (const auto &idx : indexes)
        rows.append(toSourceRow(idx));
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;
}

void TraceView::onToggleMarkSelected()
{
    auto *source = traceSource();
    if (!source)
        return;
    for (int row : selectedSourceRows())
        source->toggleMark(row);
}

void TraceView::onColorSelected()
{
    auto *source = traceSource();
    if (!source)
        return;
    QColor color = QColorDialog::getColor(Qt::yellow, this, QStringLiteral("选择行颜色"));
    if (!color.isValid())
        return;
    for (int row : selectedSourceRows())
        source->setRowColor(row, color);
}

void TraceView::onClearMarks()
{
    auto *source = traceSource();
    if (source)
        source->clearMarks();
}

void TraceView::onClearColors()
{
    auto *source = traceSource();
    if (source)
        source->clearColors();
}

// ============================================================
//  选中行保持 — 过滤变化后恢复定位
// ============================================================

void TraceView::pinSelection()
{
    m_pinnedSourceRow = -1;
    auto rows = selectedSourceRows();
    if (!rows.isEmpty())
        m_pinnedSourceRow = rows.first();
}

void TraceView::restoreSelection()
{
    if (m_pinnedSourceRow < 0)
        return;
    selectSourceRow(m_pinnedSourceRow);
    m_pinnedSourceRow = -1;
}

void TraceView::selectSourceRow(int sourceRow)
{
    auto *fp = filterProxy();
    auto *source = traceSource();
    if (!source || sourceRow < 0 || sourceRow >= source->rowCount())
        return;

    // 源模型 → 过滤代理行号
    QModelIndex sourceIdx = source->index(sourceRow, 0);
    QModelIndex filterIdx = fp ? fp->mapFromSource(sourceIdx) : sourceIdx;
    if (!filterIdx.isValid())
        return;  // 该行被过滤隐藏

    auto *vp = viewportProxy();
    if (vp) {
        // 确保过滤代理行号在视窗内
        vp->ensureVisible(filterIdx.row());
        QModelIndex viewIdx = vp->mapFromSource(filterIdx);
        if (!viewIdx.isValid())
            return;
        selectionModel()->select(viewIdx,
            QItemSelectionModel::Select | QItemSelectionModel::Rows | QItemSelectionModel::Clear);
        setCurrentIndex(viewIdx);
        scrollTo(viewIdx, QAbstractItemView::EnsureVisible);
    } else {
        selectionModel()->select(filterIdx,
            QItemSelectionModel::Select | QItemSelectionModel::Rows | QItemSelectionModel::Clear);
        setCurrentIndex(filterIdx);
        scrollTo(filterIdx, QAbstractItemView::EnsureVisible);
    }
}

// ============================================================
//  Wireshark 风格导航
// ============================================================

void TraceView::goToPacket(int frameNumber)
{
    selectSourceRow(frameNumber - 1);  // 1-based → 0-based
}

bool TraceView::findNext(const QString &text)
{
    if (text.isEmpty()) return false;
    auto *source = traceSource();
    if (!source) return false;

    int startRow = 0;
    auto rows = selectedSourceRows();
    if (!rows.isEmpty())
        startRow = rows.first() + 1;

    QString needle = text.toLower();
    for (int r = startRow; r < source->frameCount(); ++r) {
        const CanFrame &f = source->frameAt(r);
        QString hay = QString("%1 %2 %3 %4")
            .arg(CanUtils::formatId(f.id, f.extended))
            .arg(CanUtils::formatData(f.data))
            .arg(f.direction == CanFrame::Rx ? "Rx" : "Tx")
            .arg(CanUtils::formatFlags(f))
            .toLower();
        if (hay.contains(needle)) {
            selectSourceRow(r);
            return true;
        }
    }
    return false;
}

bool TraceView::findPrevious(const QString &text)
{
    if (text.isEmpty()) return false;
    auto *source = traceSource();
    if (!source) return false;

    int startRow = source->frameCount() - 1;
    auto rows = selectedSourceRows();
    if (!rows.isEmpty())
        startRow = rows.first() - 1;

    QString needle = text.toLower();
    for (int r = startRow; r >= 0; --r) {
        const CanFrame &f = source->frameAt(r);
        QString hay = QString("%1 %2 %3 %4")
            .arg(CanUtils::formatId(f.id, f.extended))
            .arg(CanUtils::formatData(f.data))
            .arg(f.direction == CanFrame::Rx ? "Rx" : "Tx")
            .arg(CanUtils::formatFlags(f))
            .toLower();
        if (hay.contains(needle)) {
            selectSourceRow(r);
            return true;
        }
    }
    return false;
}

void TraceView::goToNextSameId()
{
    const CanFrame *frame = selectedFrame();
    if (!frame) return;
    quint32 targetId = frame->id;

    auto *source = traceSource();
    if (!source) return;

    auto rows = selectedSourceRows();
    if (rows.isEmpty()) return;
    int curRow = rows.first();

    for (int r = curRow + 1; r < source->frameCount(); ++r) {
        if (source->frameAt(r).id == targetId) {
            selectSourceRow(r);
            return;
        }
    }
}

void TraceView::goToPrevSameId()
{
    const CanFrame *frame = selectedFrame();
    if (!frame) return;
    quint32 targetId = frame->id;

    auto *source = traceSource();
    if (!source) return;

    auto rows = selectedSourceRows();
    if (rows.isEmpty()) return;
    int curRow = rows.first();

    for (int r = curRow - 1; r >= 0; --r) {
        if (source->frameAt(r).id == targetId) {
            selectSourceRow(r);
            return;
        }
    }
}

// ============================================================
//  转到 / 查找 — 对话框入口
// ============================================================

void TraceView::onGoToPacket()
{
    auto *source = traceSource();
    if (!source || source->frameCount() == 0) return;

    bool ok = false;
    int num = QInputDialog::getInt(this, QStringLiteral("转到分组"),
        QStringLiteral("帧编号 (1-%1):").arg(source->frameCount()),
        1, 1, source->frameCount(), 1, &ok);
    if (ok)
        goToPacket(num);
}

void TraceView::onFind()
{
    bool ok = false;
    QString text = QInputDialog::getText(this, QStringLiteral("查找"),
        QStringLiteral("查找内容 (匹配 ID / 数据 / 方向 / 标志):"),
        QLineEdit::Normal, m_lastFindText, &ok);
    if (!ok || text.isEmpty()) return;
    m_lastFindText = text;
    if (!findNext(text))
        QMessageBox::information(this, QStringLiteral("查找"),
            QStringLiteral("未找到匹配: %1").arg(text));
}

void TraceView::onFindNext()
{
    if (m_lastFindText.isEmpty()) return;
    if (!findNext(m_lastFindText))
        QMessageBox::information(this, QStringLiteral("查找"),
            QStringLiteral("已到末尾，未找到更多匹配: %1").arg(m_lastFindText));
}

void TraceView::onFindPrevious()
{
    if (m_lastFindText.isEmpty()) return;
    if (!findPrevious(m_lastFindText))
        QMessageBox::information(this, QStringLiteral("查找"),
            QStringLiteral("已到开头，未找到更多匹配: %1").arg(m_lastFindText));
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
//  ViewportOverview — CANoe 风格视窗缩略图控件
// ============================================================

ViewportOverview::ViewportOverview(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setFixedWidth(60);

    m_rebuildTimer = new QTimer(this);
    m_rebuildTimer->setSingleShot(true);
    m_rebuildTimer->setInterval(200);
    connect(m_rebuildTimer, &QTimer::timeout, this, [this]() {
        m_cacheDirty = true;
        update();
    });

    // 拖拽定时器 — 轮询全局鼠标位置，不依赖隐式鼠标 grab
    m_dragTimer = new QTimer(this);
    m_dragTimer->setInterval(16);  // ~60fps
    connect(m_dragTimer, &QTimer::timeout, this, [this]() { onDragTimer(); });
}

void ViewportOverview::setViewportProxy(ViewportProxyModel *proxy)
{
    m_proxy = proxy;
    m_cacheDirty = true;
    update();
}

void ViewportOverview::setFilterProxy(CanFilterProxyModel *proxy)
{
    m_filterProxy = proxy;
    m_cacheDirty = true;
    update();
}

void ViewportOverview::setTraceSource(CanTraceModel *model)
{
    m_traceModel = model;
    m_cacheDirty = true;
    update();
}

void ViewportOverview::markCacheDirty()
{
    // 节流: 高频帧到达时最多每 200ms 重建一次缓存
    m_rebuildTimer->start();
}

void ViewportOverview::scheduleRebuild()
{
    m_rebuildTimer->start();
}

void ViewportOverview::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_cacheDirty = true;
}

void ViewportOverview::rebuildCache()
{
    if (!m_proxy || !m_filterProxy || !m_traceModel) {
        m_cachePixmap = QPixmap();
        return;
    }

    int total = m_proxy->sourceRowCount();
    m_cachedTotal = total;
    m_cachedHeight = height();
    int h = height();
    if (h < 2 || total == 0) {
        m_cachePixmap = QPixmap();
        return;
    }

    m_cachePixmap = QPixmap(size());
    m_cachePixmap.fill(QColor(0xf8, 0xf8, 0xf8));

    QPainter p(&m_cachePixmap);
    p.setRenderHint(QPainter::Antialiasing, false);

    // 每个像素行映射到 total/h 行，采样统计 Rx/Tx 比例
    double step = (double)total / h;
    QColor rxColor(76, 175, 80, 200);    // 绿色
    QColor txColor(33, 150, 243, 200);  // 蓝色
    int barWidth = width() - 4;

    for (int y = 0; y < h; ++y) {
        int rowStart = (int)(y * step);
        int rowEnd = (int)((y + 1) * step);
        if (rowEnd <= rowStart)
            rowEnd = rowStart + 1;
        if (rowEnd > total)
            rowEnd = total;

        // 直接访问 CanFrame::direction — 避免 data() 字符串格式化开销
        int rxCount = 0, txCount = 0;
        int sampleStep = qMax(1, (rowEnd - rowStart) / 10);
        for (int r = rowStart; r < rowEnd; r += sampleStep) {
            QModelIndex filterIdx = m_filterProxy->index(r, 0);
            if (!filterIdx.isValid())
                continue;
            QModelIndex sourceIdx = m_filterProxy->mapToSource(filterIdx);
            if (!sourceIdx.isValid() || sourceIdx.row() >= m_traceModel->frameCount())
                continue;
            const CanFrame &frame = m_traceModel->frameAt(sourceIdx.row());
            if (frame.direction == CanFrame::Rx)
                rxCount++;
            else
                txCount++;
        }

        int sampled = rxCount + txCount;
        if (sampled == 0)
            continue;

        // 按 Rx/Tx 比例绘制水平密度条
        int rxW = (int)((double)rxCount / sampled * barWidth);
        int txW = barWidth - rxW;
        if (rxW > 0)
            p.fillRect(2, y, rxW, 1, rxColor);
        if (txW > 0)
            p.fillRect(2 + rxW, y, txW, 1, txColor);
    }
}

void ViewportOverview::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 背景
    p.fillRect(rect(), QColor(0xf8, 0xf8, 0xf8));

    if (!m_proxy)
        return;

    int total = m_proxy->sourceRowCount();
    if (total == 0)
        return;

    // 重建缓存（如有需要）
    if (m_cacheDirty || m_cachedTotal != total || m_cachedHeight != height()) {
        rebuildCache();
        m_cacheDirty = false;
    }

    // 绘制密度缓存
    if (!m_cachePixmap.isNull())
        p.drawPixmap(0, 0, m_cachePixmap);

    // 10% 网格线
    p.setPen(QPen(QColor(0xd0, 0xd0, 0xd0), 1, Qt::DotLine));
    for (int i = 1; i < 10; ++i) {
        int y = height() * i / 10;
        p.drawLine(0, y, width(), y);
    }

    // 绘制视窗高亮矩形
    QRect vpRect = viewportRect();
    if (!vpRect.isNull() && vpRect.height() > 0) {
        // 半透明蓝色填充
        p.fillRect(vpRect, QColor(0x4a, 0x90, 0xd9, 50));
        // 边框
        p.setPen(QPen(QColor(0x2a, 0x70, 0xb9), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(vpRect.adjusted(0, 0, -1, -1), 3, 3);
        // 顶部和底部拖拽手柄
        p.setBrush(QColor(0x2a, 0x70, 0xb9));
        p.setPen(Qt::NoPen);
        int cx = width() / 2;
        p.drawRoundedRect(QRect(cx - 10, vpRect.top(), 20, 5), 2, 2);
        p.drawRoundedRect(QRect(cx - 10, vpRect.bottom() - 4, 20, 5), 2, 2);
    }

    // 位置文本
    int vpStart = m_proxy->viewportStart();
    int vpSize = m_proxy->viewportSize();
    QFont smallFont = font();
    smallFont.setPointSize(7);
    p.setFont(smallFont);
    p.setPen(QColor(0x55, 0x55, 0x55));
    p.drawText(QRect(0, 0, width(), 16), Qt::AlignCenter,
               QString::number(vpStart + 1));
    if (total > vpSize) {
        p.drawText(QRect(0, height() - 16, width(), 16), Qt::AlignCenter,
                   QString::number(total));
    }
}

QRect ViewportOverview::viewportRect() const
{
    if (!m_proxy)
        return {};
    int total = m_proxy->sourceRowCount();
    if (total == 0)
        return {};
    int vpStart = m_proxy->viewportStart();
    int vpSize = m_proxy->viewportSize();
    int h = height();
    double fracStart = (double)vpStart / total;
    double fracEnd = (double)(vpStart + qMin(vpSize, total - vpStart)) / total;
    int y1 = (int)(fracStart * h);
    int y2 = (int)(fracEnd * h);
    if (y2 <= y1)
        y2 = y1 + 1;
    return QRect(1, y1, width() - 2, y2 - y1);
}

int ViewportOverview::yToViewportStart(int y) const
{
    if (!m_proxy)
        return 0;
    int total = m_proxy->sourceRowCount();
    if (total == 0)
        return 0;
    int vpSize = m_proxy->viewportSize();
    int h = height();
    double frac = (double)y / h;
    int start = (int)(frac * total) - vpSize / 2;
    return qMax(0, qMin(start, total - qMin(vpSize, total)));
}

void ViewportOverview::mousePressEvent(QMouseEvent *event)
{
    if (!m_proxy || event->button() != Qt::LeftButton)
        return;

    event->accept();
    QRect vpRect = viewportRect();
    if (vpRect.contains(event->pos())) {
        // 点击在视窗区域内 → 开始拖拽
        m_dragging = true;
        m_dragStartGlobalY = (int)event->globalPosition().y();
        m_dragStartViewport = m_proxy->viewportStart();
        setCursor(Qt::SizeVerCursor);
        m_dragTimer->start();
    } else {
        // 点击在视窗外 → 跳转到该位置
        int newStart = yToViewportStart(event->pos().y());
        emit viewportMoved(newStart);
        // 立即开始拖拽
        m_dragging = true;
        m_dragStartGlobalY = (int)event->globalPosition().y();
        m_dragStartViewport = newStart;
        setCursor(Qt::SizeVerCursor);
        m_dragTimer->start();
    }
}

void ViewportOverview::mouseMoveEvent(QMouseEvent *event)
{
    // 拖拽由定时器处理 — 此处仅接受事件，不重复计算
    if (m_dragging)
        event->accept();
}

void ViewportOverview::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging) {
        m_dragging = false;
        m_dragTimer->stop();
        setCursor(Qt::ArrowCursor);
    }
    event->accept();
}

void ViewportOverview::hideEvent(QHideEvent *event)
{
    // widget 隐藏时结束拖拽，防止定时器残留
    if (m_dragging) {
        m_dragging = false;
        m_dragTimer->stop();
        setCursor(Qt::ArrowCursor);
    }
    QWidget::hideEvent(event);
}

void ViewportOverview::onDragTimer()
{
    if (!m_dragging || !m_proxy)
        return;

    // 左键已释放 → 结束拖拽（兜底，防止 mouseReleaseEvent 未送达）
    if (!(QGuiApplication::mouseButtons() & Qt::LeftButton)) {
        m_dragging = false;
        m_dragTimer->stop();
        setCursor(Qt::ArrowCursor);
        return;
    }

    // 使用全局鼠标坐标计算增量 — 不依赖 widget 内部事件传递
    int currentGlobalY = QCursor::pos().y();
    int deltaY = currentGlobalY - m_dragStartGlobalY;

    int total = m_proxy->sourceRowCount();
    if (total == 0)
        return;
    int h = height();
    if (h == 0)
        return;

    int vpSize = m_proxy->viewportSize();
    int dragRange = total - qMin(vpSize, total);  // 可拖拽的行范围
    QRect vpRect = viewportRect();
    int dragPixels = h - vpRect.height();          // 可拖拽的像素范围

    if (dragPixels <= 0)
        return;  // 视窗占满整个高度 — 无法拖拽

    // 像素增量转换为行号增量 — 按可拖拽范围映射
    double rowsPerPixel = (double)dragRange / dragPixels;
    int rowDelta = (int)(deltaY * rowsPerPixel);
    int newStart = m_dragStartViewport + rowDelta;

    emit viewportMoved(newStart);
}

void ViewportOverview::wheelEvent(QWheelEvent *event)
{
    if (!m_proxy)
        return;

    int total = m_proxy->sourceRowCount();
    if (total == 0)
        return;

    // 滚轮移动视窗
    int delta = event->angleDelta().y();
    int step = m_proxy->viewportSize() / 4;  // 每次滚动 1/4 视窗
    if (step < 1)
        step = 1;
    int direction = delta > 0 ? -step : step;
    int newStart = m_proxy->viewportStart() + direction;
    emit viewportMoved(newStart);
}

// ============================================================
//  TraceTab — Wireshark 风格整体三栏
// ============================================================

TraceTab::TraceTab(QWidget *parent)
    : QWidget(parent)
{
    // 每个标签页拥有独立的数据模型
    // 模型链: CanTraceModel → CanFilterProxyModel → ViewportProxyModel → TraceView
    m_traceModel = new CanTraceModel(this);
    m_proxyModel = new CanFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_traceModel);
    m_viewportProxy = new ViewportProxyModel(this);
    m_viewportProxy->setSourceModel(m_proxyModel);
    m_viewportProxy->setViewportSize(2000);  // CANoe 风格: 固定 2000 行视窗

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 过滤栏（含覆盖模式、过滤输入、设置按钮、分组统计）
    m_filterBar = new FilterBar(this);
    layout->addWidget(m_filterBar);

    // ---- 设置菜单（时间格式等，挂载到 FilterBar 的设置按钮上） ----
    auto *settingsMenu = new QMenu(m_filterBar->settingsButton());
    m_timeFormatGroup = new QActionGroup(settingsMenu);
    m_timeFormatGroup->setExclusive(true);

    auto *tsTitle = settingsMenu->addAction(QStringLiteral("时间格式"));
    tsTitle->setEnabled(false);
    settingsMenu->addSeparator();

    auto *actAbs = settingsMenu->addAction(QStringLiteral("自捕获开始"));
    actAbs->setCheckable(true);
    actAbs->setChecked(true);
    actAbs->setData(CanFilterProxyModel::Absolute);
    m_timeFormatGroup->addAction(actAbs);

    auto *actSinceCap = settingsMenu->addAction(QStringLiteral("自上一捕获分组"));
    actSinceCap->setCheckable(true);
    actSinceCap->setData(CanFilterProxyModel::SinceCapture);
    m_timeFormatGroup->addAction(actSinceCap);

    auto *actSinceDisp = settingsMenu->addAction(QStringLiteral("自上一显示分组"));
    actSinceDisp->setCheckable(true);
    actSinceDisp->setData(CanFilterProxyModel::SinceDisplay);
    m_timeFormatGroup->addAction(actSinceDisp);

    auto *actDate = settingsMenu->addAction(QStringLiteral("日期和时间"));
    actDate->setCheckable(true);
    actDate->setData(CanFilterProxyModel::DateTimeOfDay);
    m_timeFormatGroup->addAction(actDate);

    auto *actEpoch = settingsMenu->addAction(QStringLiteral("Unix 时间戳"));
    actEpoch->setCheckable(true);
    actEpoch->setData(CanFilterProxyModel::SecondsSinceEpoch);
    m_timeFormatGroup->addAction(actEpoch);

    settingsMenu->addSeparator();
    settingsMenu->addAction(QStringLiteral(
        "说明:\n"
        "  自捕获开始 — 自捕获开始的相对时间\n"
        "  自上一捕获分组 — 与前一帧的时间差\n"
        "  自上一显示分组 — 与前一可见帧的时间差\n"
        "  日期和时间 — 完整日期+时间\n"
        "  Unix 时间戳 — 自 1970-01-01 的秒数"))->setEnabled(false);

    // ---- 时间精度 ----
    settingsMenu->addSeparator();
    auto *tpTitle = settingsMenu->addAction(QStringLiteral("时间精度"));
    tpTitle->setEnabled(false);
    settingsMenu->addSeparator();

    auto *tpGroup = new QActionGroup(settingsMenu);
    tpGroup->setExclusive(true);

    auto *tpAuto = settingsMenu->addAction(QStringLiteral("自动"));
    tpAuto->setCheckable(true);
    tpAuto->setData(-1);
    tpGroup->addAction(tpAuto);

    auto *tpSec = settingsMenu->addAction(QStringLiteral("秒 (0)"));
    tpSec->setCheckable(true);
    tpSec->setData(0);
    tpGroup->addAction(tpSec);

    auto *tpMs = settingsMenu->addAction(QStringLiteral("毫秒 (3)"));
    tpMs->setCheckable(true);
    tpMs->setData(3);
    tpGroup->addAction(tpMs);

    auto *tpUs = settingsMenu->addAction(QStringLiteral("微秒 (6)"));
    tpUs->setCheckable(true);
    tpUs->setChecked(true);
    tpUs->setData(6);
    tpGroup->addAction(tpUs);

    auto *tpNs = settingsMenu->addAction(QStringLiteral("纳秒 (9)"));
    tpNs->setCheckable(true);
    tpNs->setData(9);
    tpGroup->addAction(tpNs);

    // ---- Phase 2: 刷新率设置 ----
    settingsMenu->addSeparator();
    auto *rrTitle = settingsMenu->addAction(QStringLiteral("刷新率"));
    rrTitle->setEnabled(false);
    settingsMenu->addSeparator();

    auto *rrGroup = new QActionGroup(settingsMenu);
    rrGroup->setExclusive(true);

    auto *rrHigh = settingsMenu->addAction(QStringLiteral("高 (50ms)"));
    rrHigh->setCheckable(true);
    rrHigh->setChecked(true);
    rrHigh->setData(50);
    rrGroup->addAction(rrHigh);

    auto *rrMed = settingsMenu->addAction(QStringLiteral("中 (100ms)"));
    rrMed->setCheckable(true);
    rrMed->setData(100);
    rrGroup->addAction(rrMed);

    auto *rrLow = settingsMenu->addAction(QStringLiteral("低 (200ms)"));
    rrLow->setCheckable(true);
    rrLow->setData(200);
    rrGroup->addAction(rrLow);

    auto *rrPause = settingsMenu->addAction(QStringLiteral("暂停刷新"));
    rrPause->setCheckable(true);
    rrPause->setData(0);
    rrGroup->addAction(rrPause);

    // ---- 覆盖模式 ----
    settingsMenu->addSeparator();
    auto *owAct = settingsMenu->addAction(QStringLiteral("覆盖模式"));
    owAct->setCheckable(true);
    owAct->setChecked(false);  // 默认不开启
    owAct->setToolTip(QStringLiteral("开启后每个 CAN ID 固定一行，新帧刷新行数据和帧数\n关闭后为滚动模式，每帧新增一行"));
    connect(owAct, &QAction::toggled, m_traceModel, &CanTraceModel::setOverwriteMode);

    m_filterBar->settingsButton()->setMenu(settingsMenu);

    connect(m_timeFormatGroup, &QActionGroup::triggered, this,
            [this](QAction *act) {
        int mode = act->data().toInt();
        m_proxyModel->setTimestampMode(
            static_cast<CanFilterProxyModel::TimestampMode>(mode));
    });

    // 时间精度切换
    connect(tpGroup, &QActionGroup::triggered, this,
            [this](QAction *act) {
        m_proxyModel->setTimePrecision(act->data().toInt());
    });

    // Phase 2: 刷新率切换 → CanTraceModel
    connect(rrGroup, &QActionGroup::triggered, this,
            [this](QAction *act) {
        int interval = act->data().toInt();
        m_traceModel->setRefreshRate(
            static_cast<CanTraceModel::RefreshRate>(interval));
    });

    // 垂直分割: TraceView (上) | 底部信息 (下)
    m_vSplitter = new QSplitter(Qt::Vertical, this);
    m_vSplitter->setHandleWidth(2);

    // CANoe 风格: 视窗缩略图 (左侧) + TraceView (右侧，内置滚动条)
    auto *viewportContainer = new QWidget(this);
    auto *hLayout = new QHBoxLayout(viewportContainer);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(0);

    // 视窗缩略图 — 可拖拽的缩略图导航条 (左侧)
    m_viewportOverview = new ViewportOverview(viewportContainer);
    m_viewportOverview->setViewportProxy(m_viewportProxy);
    m_viewportOverview->setFilterProxy(m_proxyModel);
    m_viewportOverview->setTraceSource(m_traceModel);
    hLayout->addWidget(m_viewportOverview);

    m_traceView = new TraceView(this);
    m_traceView->setModel(m_viewportProxy);
    hLayout->addWidget(m_traceView, 1);

    m_vSplitter->addWidget(viewportContainer);

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

    // CANoe 风格: 视窗缩略图 ↔ 视窗位置
    connect(m_viewportOverview, &ViewportOverview::viewportMoved,
            this, [this](int start) {
        m_viewportProxy->setViewportStart(start);
        // 视窗切换后重置滚动条到新视窗顶部
        m_traceView->verticalScrollBar()->setValue(0);
        // 用户手动拖动视窗 → 取消自动跟随
        m_autoScrollViewport = false;
    });

    // 视窗位置变化 → 更新缩略图
    connect(m_viewportProxy, &ViewportProxyModel::viewportChanged,
            this, [this]() {
        m_viewportOverview->update();
    });

    // Phase 1: 可见行范围 → CanTraceModel 行缓存淘汰
    connect(m_traceView->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this]() {
        auto *sb = m_traceView->verticalScrollBar();
        int first = m_viewportProxy->viewportStart() + sb->value();
        int pageHeight = m_traceView->viewport()->height() / 22; // 行高 22px
        int last = first + pageHeight + 5;
        m_traceModel->setVisibleRange(first, last);
    });

    // Phase 2: 帧提交后更新分组统计 + 视窗自动跟随
    connect(m_traceModel, &CanTraceModel::framesCommitted,
            this, [this](int) {
        m_packetCountDirty = true;
        // 自动跟随: 视窗滚动到末尾显示最新数据
        if (m_autoScrollViewport && m_traceView->autoScrollEnabled())
            m_traceView->scrollToBottom();
        // 标记缩略图缓存为脏
        m_viewportOverview->markCacheDirty();
    });

    // 过滤/排序变化后重置视窗到开头
    connect(m_proxyModel, &CanFilterProxyModel::layoutAboutToBeChanged,
            this, [this]() { m_autoScrollViewport = true; });
    connect(m_proxyModel, &CanFilterProxyModel::packetCountChanged,
            this, [this](int, int) {
        m_viewportOverview->markCacheDirty();
        m_viewportOverview->update();
    });

    // 分组计数防抖 — 高频帧到达时最多每 100ms 刷新一次
    m_packetCountTimer = new QTimer(this);
    m_packetCountTimer->setSingleShot(false);
    m_packetCountTimer->setInterval(100);
    connect(m_packetCountTimer, &QTimer::timeout, this, &TraceTab::onPacketCountTimer);
    m_packetCountTimer->start();

    // 过滤条件变化时立即更新（不防抖）
    connect(m_proxyModel, &CanFilterProxyModel::packetCountChanged,
            this, [this](int captured, int displayed) {
        int marked = m_traceModel->markedRows().size();
        m_filterBar->setPacketCountText(
            QStringLiteral("捕获: %1 | 显示: %2 | 标记: %3").arg(captured).arg(displayed).arg(marked));
        m_packetCountDirty = false;
    });

    // 启用拖放
    setAcceptDrops(true);
}

void TraceTab::setDbcManager(DbcManager *mgr)
{
    m_signalDecode->setDbcManager(mgr);
}

void TraceTab::setRunning(bool running)
{
    m_running = running;
}

bool TraceTab::isOverwriteMode() const
{
    return m_traceModel->isOverwriteMode();
}

void TraceTab::appendFrame(const CanFrame &frame)
{
    m_traceModel->appendFrame(frame);
    m_packetCountDirty = true;  // 由防抖定时器批量刷新
}

void TraceTab::appendFrames(const QVector<CanFrame> &frames)
{
    m_traceModel->appendFrames(frames);
    m_packetCountDirty = true;
}

void TraceTab::clearTrace()
{
    m_traceModel->clear();
    m_packetCountDirty = true;  // 防抖定时器会处理
    m_autoScrollViewport = true;
    updateViewportOverview();
}

int TraceTab::frameCount() const
{
    return m_traceModel->frameCount();
}

bool TraceTab::setFilterExpression(const QString &expr)
{
    m_traceView->pinSelection();
    bool ok = m_proxyModel->setFilterExpression(expr);
    m_traceView->restoreSelection();
    m_autoScrollViewport = true;
    updateViewportOverview();
    return ok;
}

void TraceTab::clearFilter()
{
    m_traceView->pinSelection();
    m_proxyModel->clearFilter();  // 仅清除主表达式，保留列过滤
    m_traceView->restoreSelection();
    m_autoScrollViewport = true;
    updateViewportOverview();
}

void TraceTab::clearAllFilters()
{
    m_traceView->pinSelection();
    m_proxyModel->clearFilter();
    m_proxyModel->clearAllColumnFilters();
    m_traceView->restoreSelection();
    m_autoScrollViewport = true;
    updateViewportOverview();
}

QString TraceTab::filterExpression() const
{
    return m_proxyModel->filterExpression();
}

void TraceTab::updatePacketCount()
{
    m_proxyModel->emitPacketCount();
}

void TraceTab::updateViewportOverview()
{
    if (!m_viewportOverview || !m_viewportProxy)
        return;
    int total = m_viewportProxy->sourceRowCount();
    int vpSize = m_viewportProxy->viewportSize();
    // 标记缓存为脏 + 重绘缩略图
    m_viewportOverview->markCacheDirty();
    // 数据不足时隐藏缩略图
    m_viewportOverview->setVisible(total > vpSize);
}

void TraceTab::onPacketCountTimer()
{
    if (!m_packetCountDirty)
        return;
    m_packetCountDirty = false;
    int marked = m_traceModel->markedRows().size();
    m_filterBar->setPacketCountText(
        QStringLiteral("捕获: %1 | 显示: %2 | 标记: %3")
            .arg(m_proxyModel->capturedCount())
            .arg(m_proxyModel->displayedCount())
            .arg(marked));
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

// ============================================================
//  拖放加载文件
// ============================================================

void TraceTab::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        const auto urls = event->mimeData()->urls();
        for (const auto &url : urls) {
            QString suffix = QFileInfo(url.toLocalFile()).suffix().toLower();
            if (CanFileIO::formatFromSuffix(suffix) != CanFileIO::Format::Unknown) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void TraceTab::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        event->ignore();
}

void TraceTab::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    QString path = event->mimeData()->urls().first().toLocalFile();
    QString suffix = QFileInfo(path).suffix().toLower();
    if (CanFileIO::formatFromSuffix(suffix) == CanFileIO::Format::Unknown) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    loadFile(path);
}

void TraceTab::loadFile(const QString &path)
{
    // 清除现有数据
    clearTrace();
    setRunning(false);

    // 后台加载文件
    auto *thread = QThread::create([this, path]() {
        QVector<CanFrame> frames;
        auto reader = CanFileIOFactory::createReader(path);
        if (!reader || !reader->open(path)) {
            QMetaObject::invokeMethod(this, [this]() {
                emit fileLoaded(-1);
            }, Qt::QueuedConnection);
            return;
        }
        int count = reader->readAll(frames);
        reader->close();

        QMetaObject::invokeMethod(this, [this, frames, count]() {
            if (count > 0)
                appendFrames(frames);
            emit fileLoaded(count);
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}
