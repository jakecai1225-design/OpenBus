#include "cantracemodel.h"
#include "utils/canutils.h"
#include "core/filter_engine.h"
#include <QColor>
#include <QList>
#include <algorithm>
#include <memory>

CanTraceModel::CanTraceModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int CanTraceModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_frames.size();
}

int CanTraceModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColCount;
}

QVariant CanTraceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_frames.size())
        return {};

    const CanFrame &f = m_frames.at(index.row());

    if (role == FrameRole)
        return QVariant::fromValue(f);

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColNo:         return index.row() + 1;
        case ColTime:       return CanUtils::formatTime(f.timestamp);
        case ColDelta: {
            double prev = (index.row() > 0) ? m_frames.at(index.row() - 1).timestamp : f.timestamp;
            return CanUtils::formatTime(f.timestamp - prev);
        }
        case ColChannel:    return QString::number(f.channel);
        case ColDirection:  return f.direction == CanFrame::Rx ? "Rx" : "Tx";
        case ColId:         return CanUtils::formatId(f.id, f.extended);
        case ColDlc:        return CanUtils::formatDlc(f.dlc, f.fd);
        case ColData:       return CanUtils::formatData(f.data);
        case ColFlags:      return CanUtils::formatFlags(f);
        case ColFrameCount: return m_idCount.value(f.id, 0);
        }
    }

    if (role == MarkedRole)
        return m_markedRows.contains(index.row());

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case ColNo:
        case ColTime:
        case ColDelta:
        case ColId:
        case ColDlc:
        case ColFrameCount:
            return int(Qt::AlignRight | Qt::AlignVCenter);
        case ColData:
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        default:
            return int(Qt::AlignCenter);
        }
    }

    if (role == Qt::ForegroundRole) {
        // Tx 帧蓝色，错误帧红色
        if (f.isErrorFrame())
            return QColor(0xD0, 0x20, 0x20);
        if (f.direction == CanFrame::Tx)
            return QColor(0x10, 0x50, 0xD0);
        return QColor(0x20, 0x20, 0x20);
    }

    if (role == Qt::BackgroundRole) {
        // 优先使用用户自定义行颜色
        auto colorIt = m_rowColors.find(index.row());
        if (colorIt != m_rowColors.end())
            return colorIt.value();
        // 着色规则求值
        if (!m_colorFilters.isEmpty()) {
            QColor ruleColor = evaluateColorRules(f);
            if (ruleColor.isValid())
                return ruleColor;
        }
        // 标记行用浅黄色高亮
        if (m_markedRows.contains(index.row()))
            return QColor(0xFF, 0xF3, 0xB0);
        // CAN FD 帧浅绿底色
        if (f.fd)
            return QColor(0xE8, 0xF5, 0xE8);
        return {};
    }

    return {};
}

QVariant CanTraceModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    switch (section) {
    case ColNo:         return QStringLiteral("No.");
    case ColTime:       return QStringLiteral("Time");
    case ColDelta:      return QStringLiteral("Delta");
    case ColChannel:    return QStringLiteral("Ch");
    case ColDirection:  return QStringLiteral("Dir");
    case ColId:         return QStringLiteral("ID");
    case ColDlc:        return QStringLiteral("DLC");
    case ColData:       return QStringLiteral("Data");
    case ColFlags:      return QStringLiteral("Flags");
    case ColFrameCount: return QStringLiteral("Count");
    }
    return {};
}

void CanTraceModel::appendFrame(const CanFrame &frame)
{
    // 累计每个 CAN ID 的帧数（两种模式都维护）
    m_idCount[frame.id]++;

    if (m_overwriteMode) {
        // 覆盖模式：同 CAN ID 刷新已有行
        auto it = m_idToRow.find(frame.id);
        if (it != m_idToRow.end()) {
            int row = it.value();
            m_frames[row] = frame;
            // 通知视图整行数据变化
            emit dataChanged(index(row, 0), index(row, ColCount - 1));
            return;
        }
        // 新 CAN ID：追加一行
        int row = m_frames.size();
        beginInsertRows({}, row, row);
        m_frames.append(frame);
        m_idToRow[frame.id] = row;
        endInsertRows();
        return;
    }

    // 滚动模式（原始行为）：每帧一行
    int row = m_frames.size();

    // 达到上限时从头部移除
    if (m_maxFrames > 0 && row >= m_maxFrames) {
        beginRemoveRows({}, 0, 0);
        m_frames.removeFirst();
        endRemoveRows();
        row = m_frames.size();
    }

    beginInsertRows({}, row, row);
    m_frames.append(frame);
    endInsertRows();
}

void CanTraceModel::appendFrames(const QVector<CanFrame> &frames)
{
    if (frames.isEmpty())
        return;

    if (m_overwriteMode) {
        // 覆盖模式：逐帧处理
        for (const auto &f : frames)
            appendFrame(f);
        return;
    }

    // 滚动模式：批量追加（性能更优）
    for (const auto &f : frames)
        m_idCount[f.id]++;

    int first = m_frames.size();
    int last = first + frames.size() - 1;
    beginInsertRows({}, first, last);
    m_frames.append(frames);
    endInsertRows();
}

void CanTraceModel::clear()
{
    beginResetModel();
    m_frames.clear();
    m_idToRow.clear();
    m_idCount.clear();
    m_markedRows.clear();
    m_rowColors.clear();
    m_seqCounter = 0;
    endResetModel();
}

const CanFrame &CanTraceModel::frameAt(int row) const
{
    return m_frames.at(row);
}

// ============================================================
//  行标记与着色
// ============================================================

void CanTraceModel::toggleMark(int row)
{
    if (row < 0 || row >= m_frames.size())
        return;
    if (m_markedRows.contains(row))
        m_markedRows.remove(row);
    else
        m_markedRows.insert(row);
    emit dataChanged(index(row, 0), index(row, ColCount - 1),
                     {Qt::BackgroundRole, MarkedRole});
}

void CanTraceModel::setMarked(int row, bool marked)
{
    if (row < 0 || row >= m_frames.size())
        return;
    if (marked)
        m_markedRows.insert(row);
    else
        m_markedRows.remove(row);
    emit dataChanged(index(row, 0), index(row, ColCount - 1),
                     {Qt::BackgroundRole, MarkedRole});
}

bool CanTraceModel::isMarked(int row) const
{
    return m_markedRows.contains(row);
}

void CanTraceModel::clearMarks()
{
    if (m_markedRows.isEmpty())
        return;
    // 通知所有行更新
    auto rows = m_markedRows.values();
    m_markedRows.clear();
    for (int r : rows)
        emit dataChanged(index(r, 0), index(r, ColCount - 1),
                         {Qt::BackgroundRole, MarkedRole});
}

QList<int> CanTraceModel::markedRows() const
{
    auto result = m_markedRows.values();
    std::sort(result.begin(), result.end());
    return result;
}

void CanTraceModel::setRowColor(int row, const QColor &color)
{
    if (row < 0 || row >= m_frames.size())
        return;
    if (color.isValid())
        m_rowColors[row] = color;
    else
        m_rowColors.remove(row);
    emit dataChanged(index(row, 0), index(row, ColCount - 1),
                     {Qt::BackgroundRole});
}

QColor CanTraceModel::rowColor(int row) const
{
    return m_rowColors.value(row);
}

void CanTraceModel::clearColors()
{
    if (m_rowColors.isEmpty())
        return;
    auto rows = m_rowColors.keys();
    m_rowColors.clear();
    for (int r : rows)
        emit dataChanged(index(r, 0), index(r, ColCount - 1),
                         {Qt::BackgroundRole});
}

void CanTraceModel::setOverwriteMode(bool mode)
{
    if (m_overwriteMode == mode)
        return;
    // 切换模式时清空数据，避免行号映射混乱
    beginResetModel();
    m_overwriteMode = mode;
    m_frames.clear();
    m_idToRow.clear();
    m_idCount.clear();
    m_markedRows.clear();
    m_rowColors.clear();
    endResetModel();
}

// ============================================================
//  着色规则
// ============================================================

void CanTraceModel::setColorRules(const QVector<ColorRule> &rules)
{
    // 清理旧的 FilterEngine
    for (auto *fe : m_colorFilters)
        delete fe;
    m_colorFilters.clear();

    m_colorRules = rules;

    // 为每条启用的规则编译 FilterEngine
    for (const auto &rule : m_colorRules) {
        if (!rule.enabled || rule.expr.trimmed().isEmpty()) {
            m_colorFilters.append(nullptr);
            continue;
        }
        auto *fe = new FilterEngine();
        fe->compile(rule.expr);
        m_colorFilters.append(fe);
    }

    // 刷新所有可见行的背景色
    if (!m_frames.isEmpty())
        emit dataChanged(index(0, 0), index(m_frames.size() - 1, ColCount - 1),
                         {Qt::BackgroundRole, Qt::ForegroundRole});
}

void CanTraceModel::clearColorRules()
{
    for (auto *fe : m_colorFilters)
        delete fe;
    m_colorFilters.clear();
    m_colorRules.clear();

    if (!m_frames.isEmpty())
        emit dataChanged(index(0, 0), index(m_frames.size() - 1, ColCount - 1),
                         {Qt::BackgroundRole, Qt::ForegroundRole});
}

QColor CanTraceModel::evaluateColorRules(const CanFrame &frame) const
{
    for (int i = 0; i < m_colorFilters.size() && i < m_colorRules.size(); ++i) {
        const auto &rule = m_colorRules[i];
        if (!rule.enabled) continue;

        const auto *fe = m_colorFilters[i];
        if (!fe) continue;

        if (fe->evaluate(frame))
            return rule.background;
    }
    return QColor();  // 无匹配
}
