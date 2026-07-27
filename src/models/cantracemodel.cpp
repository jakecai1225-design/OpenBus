#include "cantracemodel.h"
#include "utils/canutils.h"
#include <QColor>

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
        case ColTime:      return CanUtils::formatTime(f.timestamp);
        case ColChannel:   return QString::number(f.channel);
        case ColDirection: return f.direction == CanFrame::Rx ? "Rx" : "Tx";
        case ColId:        return CanUtils::formatId(f.id, f.extended);
        case ColDlc:       return CanUtils::formatDlc(f.dlc, f.fd);
        case ColData:      return CanUtils::formatData(f.data);
        case ColFlags:     return CanUtils::formatFlags(f);
        }
    }

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case ColTime:
        case ColId:
        case ColDlc:
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
    case ColTime:      return QStringLiteral("Time");
    case ColChannel:   return QStringLiteral("Ch");
    case ColDirection: return QStringLiteral("Dir");
    case ColId:        return QStringLiteral("ID");
    case ColDlc:       return QStringLiteral("DLC");
    case ColData:      return QStringLiteral("Data");
    case ColFlags:     return QStringLiteral("Flags");
    }
    return {};
}

void CanTraceModel::appendFrame(const CanFrame &frame)
{
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
    int first = m_frames.size();
    int last = first + frames.size() - 1;
    beginInsertRows({}, first, last);
    m_frames.append(frames);
    endInsertRows();
}

void CanTraceModel::clear()
{
    if (m_frames.isEmpty())
        return;
    beginResetModel();
    m_frames.clear();
    endResetModel();
}

const CanFrame &CanTraceModel::frameAt(int row) const
{
    return m_frames.at(row);
}
