#include "cantraceproxymodel.h"
#include "cantracemodel.h"
#include "core/canframe.h"
#include "core/filter_engine.h"
#include "utils/canutils.h"

#include <algorithm>

CanTraceProxyModel::CanTraceProxyModel(QObject *parent)
    : QAbstractProxyModel(parent)
{
}

// ============================================================
//  QAbstractProxyModel 基础映射
// ============================================================

void CanTraceProxyModel::setSourceModel(QAbstractItemModel *sourceModel)
{
    if (sourceModel == this->sourceModel())
        return;

    beginResetModel();

    // 断开旧源模型信号
    if (this->sourceModel())
        disconnect(this->sourceModel(), nullptr, this, nullptr);

    QAbstractProxyModel::setSourceModel(sourceModel);

    // 断开基类可能建立的转发连接，全部信号由本类处理
    if (sourceModel)
        disconnect(sourceModel, nullptr, this, nullptr);

    if (sourceModel) {
        connect(sourceModel, &QAbstractItemModel::rowsInserted,
                this, &CanTraceProxyModel::onSourceRowsInserted);
        connect(sourceModel, &QAbstractItemModel::rowsRemoved,
                this, &CanTraceProxyModel::onSourceRowsRemoved);
        connect(sourceModel, &QAbstractItemModel::dataChanged,
                this, &CanTraceProxyModel::onSourceDataChanged);
        if (auto *tm = qobject_cast<CanTraceModel *>(sourceModel)) {
            connect(tm, &CanTraceModel::ringWrapped,
                    this, &CanTraceProxyModel::onSourceRingWrapped);
        }
        connect(sourceModel, &QAbstractItemModel::modelAboutToBeReset,
                this, &CanTraceProxyModel::onSourceModelAboutToBeReset);
        connect(sourceModel, &QAbstractItemModel::modelReset,
                this, &CanTraceProxyModel::onSourceModelReset);
        connect(sourceModel, &QAbstractItemModel::headerDataChanged,
                this, &QAbstractItemModel::headerDataChanged);
    }

    buildMapping();   // 在 reset 事务内完成重建，不发额外信号
    if (auto *m = traceModel())
        m_lastSeq = m->seqCounter();
    endResetModel();
    emitPacketCount();
}

QModelIndex CanTraceProxyModel::mapToSource(const QModelIndex &proxyIndex) const
{
    if (!proxyIndex.isValid() || proxyIndex.model() != this)
        return {};
    if (proxyIndex.row() < 0 || proxyIndex.row() >= m_proxyRows.size())
        return {};
    int srcRow = m_proxyRows.at(proxyIndex.row());
    auto *src = sourceModel();
    if (!src || srcRow < 0 || srcRow >= src->rowCount())
        return {};
    return src->index(srcRow, proxyIndex.column());
}

QModelIndex CanTraceProxyModel::mapFromSource(const QModelIndex &sourceIndex) const
{
    if (!sourceIndex.isValid() || sourceIndex.model() != sourceModel())
        return {};
    int p = m_sourceToProxy.value(sourceIndex.row(), -1);
    if (p < 0 || p >= m_proxyRows.size())
        return {};
    return createIndex(p, sourceIndex.column());
}

int CanTraceProxyModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_proxyRows.size();
}

int CanTraceProxyModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    auto *src = sourceModel();
    return src ? src->columnCount() : 0;
}

QModelIndex CanTraceProxyModel::index(int row, int column, const QModelIndex &parent) const
{
    if (parent.isValid())
        return {};
    if (row < 0 || row >= m_proxyRows.size() || column < 0 || column >= columnCount())
        return {};
    return createIndex(row, column);
}

QModelIndex CanTraceProxyModel::parent(const QModelIndex &child) const
{
    Q_UNUSED(child);
    return {};
}

QVariant CanTraceProxyModel::data(const QModelIndex &proxyIndex, int role) const
{
    // 仅翻译 Time 列显示文本（时间戳模式/精度），其余角色与列直接转发源模型
    if (role == Qt::DisplayRole && proxyIndex.isValid()
        && proxyIndex.column() == CanTraceModel::ColTime) {
        auto *model = traceModel();
        if (!model)
            return {};
        QModelIndex sourceIdx = mapToSource(proxyIndex);
        if (!sourceIdx.isValid())
            return {};
        const CanFrame &f = model->frameAt(sourceIdx.row());
        int prec = m_timePrecision;

        switch (m_timestampMode) {
        case Absolute:
            return CanUtils::formatTime(f.timestamp, prec);
        case SinceCapture: {
            double prev = (sourceIdx.row() > 0)
                ? model->frameAt(sourceIdx.row() - 1).timestamp : 0.0;
            return CanUtils::formatTime(f.timestamp - prev, prec);
        }
        case SinceDisplay:
            // 冻结模式：显示值 = 排序键快照（顺序与显示值严格对应）；
            // 否则按当前代理序实时推导
            return CanUtils::formatTime(deltaSortFrozen() ? deltaKey(sourceIdx.row())
                                                          : displayDelta(sourceIdx.row()), prec);
        case DateTimeOfDay:
            return CanUtils::formatDateTime(model->captureStartTime(), f.timestamp, prec);
        case SecondsSinceEpoch: {
            QDateTime start = model->captureStartTime();
            if (start.isValid())
                return CanUtils::formatTime(start.toMSecsSinceEpoch() / 1000.0 + f.timestamp, prec);
            return CanUtils::formatTime(f.timestamp, prec);
        }
        }
        return {};
    }
    return QAbstractProxyModel::data(proxyIndex, role);
}

// ============================================================
//  主过滤表达式
// ============================================================

bool CanTraceProxyModel::setFilterExpression(const QString &expr)
{
    m_expr = expr;
    if (expr.trimmed().isEmpty()) {
        m_filterEngine.reset();
    } else {
        auto engine = std::make_unique<FilterEngine>();
        if (!engine->compile(expr)) {
            m_filterEngine.reset();
            rebuildMapping();
            emitPacketCount();
            return false;
        }
        m_filterEngine = std::move(engine);
    }
    rebuildMapping();
    emitPacketCount();
    return true;
}

void CanTraceProxyModel::clearFilter()
{
    m_expr.clear();
    m_filterEngine.reset();
    rebuildMapping();
    emitPacketCount();
}

// ============================================================
//  按列过滤
// ============================================================

void CanTraceProxyModel::setColumnFilter(int column, const QString &text)
{
    if (text.trimmed().isEmpty())
        m_columnFilters.remove(column);
    else
        m_columnFilters[column] = text.trimmed();
    rebuildMapping();
    emitPacketCount();
}

void CanTraceProxyModel::clearColumnFilter(int column)
{
    m_columnFilters.remove(column);
    rebuildMapping();
    emitPacketCount();
}

void CanTraceProxyModel::clearAllColumnFilters()
{
    m_columnFilters.clear();
    m_columnFilterValues.clear();
    rebuildMapping();
    emitPacketCount();
}

bool CanTraceProxyModel::hasColumnFilter(int column) const
{
    return m_columnFilters.contains(column) || m_columnFilterValues.contains(column);
}

bool CanTraceProxyModel::matchColumnFilter(int sourceRow, int column) const
{
    auto *model = traceModel();
    if (!model) return true;

    const CanFrame &frame = model->frameAt(sourceRow);
    QString filter = m_columnFilters.value(column).toLower();

    switch (column) {
    case CanTraceModel::ColNo: {
        // 支持 ">10", "<50", "123" 等（No. 取真实帧序号，环形覆盖后仍正确）
        qint64 seq = static_cast<qint64>(model->seqCounter() - (quint64)model->rowCount()
                                         + (quint64)sourceRow + 1);
        if (filter.startsWith(">"))
            return seq > filter.mid(1).trimmed().toLongLong();
        if (filter.startsWith("<"))
            return seq < filter.mid(1).trimmed().toLongLong();
        return QString::number(seq).contains(filter);
    }
    case CanTraceModel::ColTime: {
        // 支持 ">0.5", "<1.0", "0.3" 等范围
        QString val = CanUtils::formatTime(frame.timestamp);
        if (filter.startsWith(">"))
            return frame.timestamp > filter.mid(1).trimmed().toDouble();
        if (filter.startsWith("<"))
            return frame.timestamp < filter.mid(1).trimmed().toDouble();
        return val.contains(filter);
    }
    case CanTraceModel::ColDelta: {
        // 计算与上一帧的时间增量
        double prev = (sourceRow > 0) ? model->frameAt(sourceRow - 1).timestamp : frame.timestamp;
        double delta = frame.timestamp - prev;
        QString val = CanUtils::formatTime(delta);
        if (filter.startsWith(">"))
            return delta > filter.mid(1).trimmed().toDouble();
        if (filter.startsWith("<"))
            return delta < filter.mid(1).trimmed().toDouble();
        return val.contains(filter);
    }
    case CanTraceModel::ColChannel:
        return QString::number(frame.channel).contains(filter);
    case CanTraceModel::ColDirection: {
        QString dir = (frame.direction == CanFrame::Rx) ? "rx" : "tx";
        return dir.contains(filter);
    }
    case CanTraceModel::ColId: {
        // 支持 "0x123", "123", ">0x100" 等
        if (filter.startsWith(">")) {
            quint32 cmp = CanUtils::parseHex(filter.mid(1).trimmed());
            return frame.id > cmp;
        }
        if (filter.startsWith("<")) {
            quint32 cmp = CanUtils::parseHex(filter.mid(1).trimmed());
            return frame.id < cmp;
        }
        if (filter.startsWith("!=")) {
            quint32 cmp = CanUtils::parseHex(filter.mid(2).trimmed());
            return frame.id != cmp;
        }
        QString idStr = CanUtils::formatId(frame.id, frame.extended).toLower();
        return idStr.contains(filter);
    }
    case CanTraceModel::ColName:
        // DBC 报文名包含匹配
        return messageName(sourceRow).toLower().contains(filter);
    case CanTraceModel::ColDlc:
        return CanUtils::formatDlc(frame.dlc, frame.fd).contains(filter);
    case CanTraceModel::ColData:
        return CanUtils::formatData(frame.data).toLower().contains(filter);
    case CanTraceModel::ColFlags:
        return CanUtils::formatFlags(frame).toLower().contains(filter);
    case CanTraceModel::ColFrameCount: {
        int count = model->frameCountForId(frame.id);
        if (filter.startsWith(">"))
            return count > filter.mid(1).trimmed().toInt();
        if (filter.startsWith("<"))
            return count < filter.mid(1).trimmed().toInt();
        return QString::number(count).contains(filter);
    }
    }
    return true;
}

// ============================================================
//  值集过滤（Excel 风格复选框）
// ============================================================

void CanTraceProxyModel::setColumnFilterValues(int column, const QSet<QString> &values)
{
    if (values.isEmpty())
        m_columnFilterValues.remove(column);
    else
        m_columnFilterValues[column] = values;
    rebuildMapping();
    emitPacketCount();
}

void CanTraceProxyModel::clearColumnFilterValues(int column)
{
    m_columnFilterValues.remove(column);
    rebuildMapping();
    emitPacketCount();
}

QString CanTraceProxyModel::columnDisplayText(int sourceRow, int column) const
{
    auto *model = traceModel();
    if (!model)
        return {};
    // 获取源模型的显示文本（已格式化，走行缓存）
    QModelIndex idx = model->index(sourceRow, column);
    return model->data(idx, Qt::DisplayRole).toString();
}

bool CanTraceProxyModel::matchColumnFilterValues(int sourceRow, int column) const
{
    auto it = m_columnFilterValues.constFind(column);
    if (it == m_columnFilterValues.end())
        return true;  // 该列无值集过滤

    QString displayText = columnDisplayText(sourceRow, column);
    return it.value().contains(displayText);
}

// ============================================================
//  行过滤（主表达式 + 列过滤 + 值集过滤）
// ============================================================

bool CanTraceProxyModel::filterAcceptsRow(int sourceRow) const
{
    // 主过滤表达式
    if (m_filterEngine && m_filterEngine->isValid() && !m_filterEngine->isEmpty()) {
        auto *model = traceModel();
        if (!model) return true;
        const CanFrame &frame = model->frameAt(sourceRow);
        if (!m_filterEngine->evaluate(frame))
            return false;
    }

    // 按列过滤
    for (auto it = m_columnFilters.constBegin(); it != m_columnFilters.constEnd(); ++it) {
        if (!matchColumnFilter(sourceRow, it.key()))
            return false;
    }

    // 值集过滤（Excel 风格复选框）
    for (auto it = m_columnFilterValues.constBegin(); it != m_columnFilterValues.constEnd(); ++it) {
        if (!matchColumnFilterValues(sourceRow, it.key()))
            return false;
    }

    return true;
}

// ============================================================
//  时间戳显示模式
// ============================================================

void CanTraceProxyModel::setTimestampMode(TimestampMode mode)
{
    if (m_timestampMode == mode)
        return;
    m_timestampMode = mode;

    // Time 列排序激活时排序键语义随模式切换：
    // 切到 SinceDisplay → 按当前显示序拍增量快照（进入冻结态）后按快照重排；
    // 切走 → 快照作废，回到绝对时间戳键重排
    if (m_sortColumn == CanTraceModel::ColTime) {
        if (mode == SinceDisplay) {
            refreshDeltaKeys();
            m_deltaSortFrozen = true;
        } else {
            m_deltaSortFrozen = false;
        }
        resortCurrent();
    }

    // 刷新 Time 列所有可见行（显示模式影响显示文本；排序链变化见上）
    int rows = m_proxyRows.size();
    if (rows > 0) {
        emit dataChanged(index(0, CanTraceModel::ColTime),
                         index(rows - 1, CanTraceModel::ColTime),
                         {Qt::DisplayRole});
    }
}

void CanTraceProxyModel::setTimePrecision(int precision)
{
    if (m_timePrecision == precision)
        return;
    m_timePrecision = precision;

    // 刷新 Time 列和 Delta 列所有可见行
    int rows = m_proxyRows.size();
    if (rows > 0) {
        emit dataChanged(index(0, CanTraceModel::ColTime),
                         index(rows - 1, CanTraceModel::ColTime),
                         {Qt::DisplayRole});
        emit dataChanged(index(0, CanTraceModel::ColDelta),
                         index(rows - 1, CanTraceModel::ColDelta),
                         {Qt::DisplayRole});
    }
}

int CanTraceProxyModel::capturedCount() const
{
    auto *model = traceModel();
    return model ? model->frameCount() : 0;
}

void CanTraceProxyModel::emitPacketCount()
{
    emit packetCountChanged(capturedCount(), m_proxyRows.size());
}

// ============================================================
//  排序
// ============================================================

void CanTraceProxyModel::sort(int column, Qt::SortOrder order)
{
    if (column == m_sortColumn && order == m_sortOrder)
        return;

    m_sortColumn = column;
    m_sortOrder = order;

    if (column < 0) {
        // 取消排序 — 恢复捕获顺序（源行号升序）；快照冻结同步解除
        // （显示回到实时推导）
        m_deltaSortFrozen = false;
        withLayoutChange([this]() {
            std::sort(m_proxyRows.begin(), m_proxyRows.end());
            rebuildSourceToProxy();
        });
        return;
    }

    // 进入 Time+SinceDisplay 排序：以点击时刻显示序拍增量快照（排序键
    // =用户此刻看到的显示值；冻结中切换升降序沿用旧快照，值不漂移）；
    // 离开（排到其他列）则解除冻结
    if (column == CanTraceModel::ColTime && m_timestampMode == SinceDisplay) {
        if (!m_deltaSortFrozen) {
            refreshDeltaKeys();
            m_deltaSortFrozen = true;
        }
    } else {
        m_deltaSortFrozen = false;
    }

    resortCurrent();
}

void CanTraceProxyModel::resortCurrent()
{
    withLayoutChange([this]() {
        // 稳定排序：等值元素保持输入序（源序），升降序由比较器方向决定
        std::stable_sort(m_proxyRows.begin(), m_proxyRows.end(),
                         [this](int a, int b) {
                             return m_sortOrder == Qt::AscendingOrder
                                 ? lessThan(a, b) : lessThan(b, a);
                         });
        rebuildSourceToProxy();
    });
}

bool CanTraceProxyModel::lessThan(int sourceLeft, int sourceRight) const
{
    auto *model = traceModel();
    if (!model)
        return sourceLeft < sourceRight;

    const CanFrame &fl = model->frameAt(sourceLeft);
    const CanFrame &fr = model->frameAt(sourceRight);

    switch (m_sortColumn) {
    case CanTraceModel::ColNo:
        // 源行号序 == No. 序（环形覆盖下仍单调）
        return sourceLeft < sourceRight;
    case CanTraceModel::ColTime:
        // 对齐 Wireshark：Time 列排序键恒为帧的绝对捕获时间戳，
        // 显示模式（绝对/增量/日期/Unix）仅改变显示文本，不改变排序语义。
        // 例外（用户需求 2026-08-24）：SinceDisplay 模式下按显示分组增量
        // 排序——快照键（进入该排序时冻结的显示值，见 refreshDeltaKeys）
        if (m_deltaSortFrozen)
            return deltaKey(sourceLeft) < deltaKey(sourceRight);
        return fl.timestamp < fr.timestamp;
    case CanTraceModel::ColDelta: {
        double dl = (sourceLeft > 0) ? fl.timestamp - model->frameAt(sourceLeft - 1).timestamp : 0.0;
        double dr = (sourceRight > 0) ? fr.timestamp - model->frameAt(sourceRight - 1).timestamp : 0.0;
        return dl < dr;
    }
    case CanTraceModel::ColChannel:
        return fl.channel < fr.channel;
    case CanTraceModel::ColDirection:
        return fl.direction < fr.direction;
    case CanTraceModel::ColId:
        return fl.id < fr.id;
    case CanTraceModel::ColName:
        return messageName(sourceLeft).toLower() < messageName(sourceRight).toLower();
    case CanTraceModel::ColDlc:
        return fl.dlc < fr.dlc;
    case CanTraceModel::ColData:
        return fl.data < fr.data;
    case CanTraceModel::ColFlags:
        return CanUtils::formatFlags(fl) < CanUtils::formatFlags(fr);
    case CanTraceModel::ColFrameCount:
        return model->frameCountForId(fl.id) < model->frameCountForId(fr.id);
    }

    return sourceLeft < sourceRight;
}

// ============================================================
//  Phase 3: 双向映射
// ============================================================

CanTraceModel *CanTraceProxyModel::traceModel() const
{
    return qobject_cast<CanTraceModel *>(sourceModel());
}

QString CanTraceProxyModel::messageName(int sourceRow) const
{
    return columnDisplayText(sourceRow, CanTraceModel::ColName);
}

double CanTraceProxyModel::displayDelta(int sourceRow) const
{
    // O(1) 由映射推导：与上一个“显示”行的增量（SinceDisplay 模式）
    auto *model = traceModel();
    if (!model || sourceRow < 0 || sourceRow >= model->rowCount())
        return 0.0;
    int p = m_sourceToProxy.value(sourceRow, -1);
    double ts = model->frameAt(sourceRow).timestamp;
    if (p <= 0)
        return ts;  // 首个显示行：显示其自身时间戳（与旧行为一致）
    return ts - model->frameAt(m_proxyRows.at(p - 1)).timestamp;
}

void CanTraceProxyModel::refreshDeltaKeys()
{
    // 按当前代理序（=此刻显示序）逐行推导增量快照：在排序发生前调用，
    // 快照与用户此刻看到的显示值一致（首行 = 自身时间戳，与 displayDelta
    // 首行语义一致）
    auto *model = traceModel();
    int n = model ? model->rowCount() : 0;
    m_displayDeltaKeys.resize(n);
    m_displayDeltaKeys.fill(0.0);
    if (!model || m_proxyRows.isEmpty()) {
        m_lastAcceptedSourceRow = -1;
        return;
    }
    double prevTs = 0.0;
    for (int p = 0; p < m_proxyRows.size(); ++p) {
        const double ts = model->frameAt(m_proxyRows.at(p)).timestamp;
        m_displayDeltaKeys[m_proxyRows.at(p)] = (p == 0) ? ts : ts - prevTs;
        prevTs = ts;
    }
    // 源序最后一个显示行（新帧增量推导链尾；排序后代理序乱序，取最大源行）
    m_lastAcceptedSourceRow = *std::max_element(m_proxyRows.cbegin(), m_proxyRows.cend());
}

double CanTraceProxyModel::deltaKey(int sourceRow) const
{
    return (sourceRow >= 0 && sourceRow < m_displayDeltaKeys.size())
               ? m_displayDeltaKeys.at(sourceRow) : 0.0;
}

void CanTraceProxyModel::buildMapping()
{
    // 无信号重建：接受过滤（源序）→ 排序 → 反向映射
    auto *model = traceModel();
    m_proxyRows.clear();
    if (!model) {
        m_sourceToProxy.clear();
        return;
    }

    int total = model->rowCount();
    m_proxyRows.reserve(total);
    m_sourceToProxy.resize(total);
    std::fill(m_sourceToProxy.begin(), m_sourceToProxy.end(), -1);

    // 1) 接受过滤（源序）
    for (int i = 0; i < total; ++i) {
        if (!filterAcceptsRow(i))
            continue;
        m_sourceToProxy[i] = m_proxyRows.size();
        m_proxyRows.append(i);
    }

    // 1.5) 冻结中的过滤重建：显示集合已变，按新集合源序重拍增量快照
    //     （排序键与显示值同步刷新）
    if (m_deltaSortFrozen)
        refreshDeltaKeys();

    // 2) 排序
    if (m_sortColumn >= 0) {
        std::stable_sort(m_proxyRows.begin(), m_proxyRows.end(),
                         [this](int a, int b) {
                             return m_sortOrder == Qt::AscendingOrder
                                 ? lessThan(a, b) : lessThan(b, a);
                         });
    }

    // 3) 重建反向映射
    std::fill(m_sourceToProxy.begin(), m_sourceToProxy.end(), -1);
    for (int p = 0; p < m_proxyRows.size(); ++p)
        m_sourceToProxy[m_proxyRows.at(p)] = p;
}

void CanTraceProxyModel::rebuildSourceToProxy()
{
    auto *model = traceModel();
    m_sourceToProxy.resize(model ? model->rowCount() : 0);
    std::fill(m_sourceToProxy.begin(), m_sourceToProxy.end(), -1);
    for (int p = 0; p < m_proxyRows.size(); ++p)
        m_sourceToProxy[m_proxyRows.at(p)] = p;
}

void CanTraceProxyModel::rebuildMapping()
{
    withLayoutChange([this]() { buildMapping(); });
}

void CanTraceProxyModel::withLayoutChange(const std::function<void()> &mutate)
{
    // 捕获持久索引当前指向的源行
    const QModelIndexList persist = persistentIndexList();
    QVector<int> srcRows;
    srcRows.reserve(persist.size());
    for (const QModelIndex &p : persist)
        srcRows.append((p.row() >= 0 && p.row() < m_proxyRows.size())
                           ? m_proxyRows.at(p.row()) : -1);

    emit layoutAboutToBeChanged();
    mutate();

    QModelIndexList from, to;
    for (int i = 0; i < persist.size(); ++i) {
        if (!persist.at(i).isValid())
            continue;
        int p = srcRows.at(i) >= 0 ? m_sourceToProxy.value(srcRows.at(i), -1) : -1;
        from.append(persist.at(i));
        to.append(p >= 0 ? createIndex(p, persist.at(i).column()) : QModelIndex());
    }
    if (!from.isEmpty())
        changePersistentIndexList(from, to);
    emit layoutChanged();
}

void CanTraceProxyModel::forwardDataChanged(int srcTop, int srcBottom, const QVector<int> &roles)
{
    if (m_proxyRows.isEmpty() || srcBottom < srcTop || srcTop < 0)
        return;

    int pTop = -1, pBottom = -1;
    if (m_proxyRows.size() == m_sourceToProxy.size()) {
        // 快速路径：无过滤（全部行显示）
        pTop = 0;
        pBottom = m_proxyRows.size() - 1;
    } else {
        for (int r = srcTop; r <= srcBottom && r < m_sourceToProxy.size(); ++r) {
            int p = m_sourceToProxy.at(r);
            if (p >= 0) {
                if (pTop < 0)
                    pTop = p;
                pBottom = p;
            }
        }
    }
    if (pTop < 0)
        return;
    emit dataChanged(index(pTop, 0), index(pBottom, columnCount() - 1), roles);
}

// ============================================================
//  源模型信号处理
// ============================================================

void CanTraceProxyModel::onSourceRowsInserted(const QModelIndex &parent, int first, int last)
{
    Q_UNUSED(parent);
    auto *model = traceModel();
    if (!model)
        return;

    m_lastSeq = model->seqCounter();

    if (first != m_sourceToProxy.size()) {
        // 非尾部插入（CanTraceModel 理论上只尾部插入）→ 全量重建
        rebuildMapping();
        emitPacketCount();
        return;
    }

    int newCount = last - first + 1;
    m_sourceToProxy.resize(m_sourceToProxy.size() + newCount, -1);

    // 先评估新行，再通知视图（begin 前 rowCount 须保持旧值）
    QVector<int> accepted;
    accepted.reserve(newCount);
    const bool deltaFrozen = m_deltaSortFrozen;  // 新行增量快照 + 链尾维护
    if (deltaFrozen)
        m_displayDeltaKeys.resize(m_sourceToProxy.size());
    for (int r = first; r <= last; ++r) {
        if (!filterAcceptsRow(r))
            continue;
        accepted.append(r);
        if (deltaFrozen) {
            // 新行增量：与源序上一显示行的差（未排序显示序下与实时显示值
            // 一致；冻结模式下作为该行的显示/排序键）
            const double prevTs = (m_lastAcceptedSourceRow >= 0)
                ? model->frameAt(m_lastAcceptedSourceRow).timestamp : 0.0;
            m_displayDeltaKeys[r] = model->frameAt(r).timestamp - prevTs;
            m_lastAcceptedSourceRow = r;
        }
    }

    const bool appendOnly =
        (m_sortColumn < 0)
        || (m_sortColumn == CanTraceModel::ColNo && m_sortOrder == Qt::AscendingOrder);

    if (appendOnly) {
        // Capture order / ColNo ascending: O(1) tail insert (no merge / layoutChanged)
        if (!accepted.isEmpty()) {
            int insertStart = m_proxyRows.size();
            beginInsertRows({}, insertStart, insertStart + accepted.size() - 1);
            for (int r : accepted) {
                m_sourceToProxy[r] = m_proxyRows.size();
                m_proxyRows.append(r);
            }
            endInsertRows();
        }
    } else {
        // 排序模式：本批按排序键归并进 m_proxyRows（O(n+k) 单次，layoutChanged 通知）
        if (!accepted.isEmpty()) {
            std::stable_sort(accepted.begin(), accepted.end(),
                             [this](int a, int b) {
                                 return m_sortOrder == Qt::AscendingOrder
                                     ? lessThan(a, b) : lessThan(b, a);
                             });
            withLayoutChange([this, &accepted]() {
                QVector<int> merged;
                merged.resize(m_proxyRows.size() + accepted.size());
                // merge 等值时取旧集合在前 → 新行稳定排到等值行之后
                std::merge(m_proxyRows.begin(), m_proxyRows.end(),
                           accepted.begin(), accepted.end(), merged.begin(),
                           [this](int a, int b) {
                               return m_sortOrder == Qt::AscendingOrder
                                   ? lessThan(a, b) : lessThan(b, a);
                           });
                m_proxyRows = std::move(merged);
                rebuildSourceToProxy();
            });
        }
    }
    emitPacketCount();
}

void CanTraceProxyModel::onSourceRowsRemoved(const QModelIndex &parent, int first, int last)
{
    // CanTraceModel 从不删除行（环形覆盖走 dataChanged，清空走 reset），防御性重建
    Q_UNUSED(parent);
    Q_UNUSED(first);
    Q_UNUSED(last);
    rebuildMapping();
    if (auto *m = traceModel())
        m_lastSeq = m->seqCounter();
    emitPacketCount();
}

void CanTraceProxyModel::onSourceDataChanged(const QModelIndex &topLeft,
                                             const QModelIndex &bottomRight,
                                             const QVector<int> &roles)
{
    auto *model = traceModel();
    if (!model)
        return;

    int srcRows = model->rowCount();
    int srcTop = qMax(0, topLeft.row());
    int srcBottom = qMin(srcRows - 1, bottomRight.row());
    if (srcBottom < srcTop)
        return;

    quint64 seq = model->seqCounter();

    // 环形缓冲覆盖：全区间内容前移 + seqCounter 推进 + 行数不变
    if (srcTop == 0 && srcBottom == srcRows - 1 && srcRows > 0
        && seq != m_lastSeq && srcRows == m_sourceToProxy.size()) {
        int shift = static_cast<int>(qMin<quint64>(seq - m_lastSeq, static_cast<quint64>(srcRows)));
        m_lastSeq = seq;
        handleFullShift(shift, roles);
        return;
    }
    m_lastSeq = seq;

    // 小区间数据变化：逐行重评估过滤成员（内容变化可能改变接受状态）
    int span = srcBottom - srcTop + 1;
    bool mayAffectFilter = span <= 64 && (roles.isEmpty() || roles.contains(Qt::DisplayRole));
    if (mayAffectFilter) {
        bool changed = false;
        for (int r = srcTop; r <= srcBottom; ++r) {
            bool acceptedNow = filterAcceptsRow(r);
            bool acceptedBefore = m_sourceToProxy.value(r, -1) >= 0;
            if (acceptedNow != acceptedBefore) {
                changed = true;
                break;
            }
        }
        if (changed) {
            rebuildMapping();
            emitPacketCount();
            return;
        }
    }

    // 常规转发（标记/着色等仅改颜色角色的变化不会触发上面的重评估）
    forwardDataChanged(srcTop, srcBottom, roles);
}

void CanTraceProxyModel::onSourceModelAboutToBeReset()
{
    beginResetModel();
}

void CanTraceProxyModel::onSourceModelReset()
{
    if (auto *m = traceModel())
        m_lastSeq = m->seqCounter();
    buildMapping();   // 处于 reset 事务内
    endResetModel();
    emitPacketCount();
}

void CanTraceProxyModel::onSourceRingWrapped(int shift)
{
    auto *model = traceModel();
    if (!model)
        return;
    handleFullShift(shift, {Qt::DisplayRole, Qt::BackgroundRole, Qt::ForegroundRole,
                            CanTraceModel::MarkedRole});
    m_lastSeq = model->seqCounter();
}

// ============================================================
//  Ring wrap: logical rows shift forward
// ============================================================

void CanTraceProxyModel::handleFullShift(int shift, const QVector<int> &roles)
{
    auto *model = traceModel();
    if (!model)
        return;
    int n = model->rowCount();
    if (shift <= 0 || shift >= n) {
        // 一次性覆盖量超过缓冲区 → 内容整体替换，全量重建
        rebuildMapping();
        forwardDataChanged(0, n - 1, roles);
        emitPacketCount();
        return;
    }

    if (m_sortColumn >= 0) {
        // 排序模式：行身份已变，排序序无法廉价维护（与旧代理一致退化为内容更新）。
        // 仅重评估尾部 shift 行（新帧内容）的接受状态，成员翻转则全量重建。
        bool changed = false;
        for (int r = n - shift; r < n; ++r) {
            bool acceptedNow = filterAcceptsRow(r);
            bool acceptedBefore = m_sourceToProxy.value(r, -1) >= 0;
            if (acceptedNow != acceptedBefore) {
                changed = true;
                break;
            }
        }
        if (changed) {
            rebuildMapping();  // buildMapping 内含冻结快照重拍
        } else if (m_deltaSortFrozen) {
            // 冻结中的行号平移：旧行 s+shift 的键 → 新行 s；尾部新帧逐行
            // 补键（与源序上一显示行的差，链尾随平移更新）
            m_displayDeltaKeys.resize(n);
            for (int s = 0; s + shift < n; ++s)
                m_displayDeltaKeys[s] = m_displayDeltaKeys[s + shift];
            m_lastAcceptedSourceRow = (m_lastAcceptedSourceRow >= shift)
                                          ? m_lastAcceptedSourceRow - shift : -1;
            for (int r = n - shift; r < n; ++r) {
                if (m_sourceToProxy.value(r, -1) < 0)
                    continue;  // 被过滤行无需键
                const double prevTs = (m_lastAcceptedSourceRow >= 0)
                    ? model->frameAt(m_lastAcceptedSourceRow).timestamp : 0.0;
                m_displayDeltaKeys[r] = model->frameAt(r).timestamp - prevTs;
                m_lastAcceptedSourceRow = r;
            }
        }
        forwardDataChanged(0, n - 1, roles);
        emitPacketCount();
        return;
    }

    // 未排序模式：accept(i) = accept_old(i + shift)，源序保持
    // 旧行 s → 新行 s-shift；尾部 [n-shift, n) 为新帧，逐行评估
    int oldCount = m_proxyRows.size();
    QVector<int> shifted;
    shifted.reserve(oldCount);
    for (int s : m_proxyRows) {
        if (s >= shift)
            shifted.append(s - shift);
    }
    int tailStart = n - shift;
    for (int r = tailStart; r < n; ++r) {
        if (filterAcceptsRow(r))
            shifted.append(r);
    }
    int newCount = shifted.size();

    // 行数差异以尾部 insert/remove 通知（内容全部变化）
    if (newCount > oldCount) {
        beginInsertRows({}, oldCount, newCount - 1);
        m_proxyRows = std::move(shifted);
        endInsertRows();
    } else if (newCount < oldCount) {
        beginRemoveRows({}, newCount, oldCount - 1);
        m_proxyRows = std::move(shifted);
        endRemoveRows();
    } else {
        m_proxyRows = std::move(shifted);
    }

    rebuildSourceToProxy();

    if (newCount > 0)
        emit dataChanged(index(0, 0), index(newCount - 1, columnCount() - 1), roles);
    emitPacketCount();
}
