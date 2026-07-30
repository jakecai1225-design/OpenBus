#ifndef CANTRACEMODEL_H
#define CANTRACEMODEL_H

#include <QAbstractTableModel>
#include <QVector>
#include <QHash>
#include "core/canframe.h"

/**
 * @brief CAN 报文追踪数据模型
 *
 * 为 TraceView 提供 QAbstractTableModel 实现。
 * 支持 appendFrame（实时捕获）、clear、随机访问。
 */
class CanTraceModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Columns {
        ColTime = 0,
        ColChannel,
        ColDirection,
        ColId,
        ColDlc,
        ColData,
        ColFlags,
        ColFrameCount,   ///< 每个 CAN ID 的帧计数
        ColCount
    };

    enum CustomRoles {
        FrameRole = Qt::UserRole + 1,  ///< 返回完整 CanFrame
    };

    explicit CanTraceModel(QObject *parent = nullptr);

    // ---- QAbstractTableModel ----
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    // ---- 数据操作 ----

    /// 追加单帧（实时捕获场景）
    void appendFrame(const CanFrame &frame);

    /// 批量追加（回放场景）
    void appendFrames(const QVector<CanFrame> &frames);

    /// 清空所有帧
    void clear();

    /// 设置最大帧数限制（超过后从头部丢弃）
    void setMaxFrames(int max) { m_maxFrames = max; }
    int maxFrames() const { return m_maxFrames; }

    /// 获取指定行帧（const 引用）
    const CanFrame &frameAt(int row) const;
    const CanFrame &frameAt(const QModelIndex &index) const { return frameAt(index.row()); }

    /// 当前帧总数
    int frameCount() const { return m_frames.size(); }

    /// 获取所有帧（const 引用，用于 GraphicView 等）
    const QVector<CanFrame> &frames() const { return m_frames; }

    // ---- 覆盖模式 ----

    /// 设置覆盖模式：同 CAN ID 的帧只保留一行，刷新数据和帧数
    void setOverwriteMode(bool mode);
    bool isOverwriteMode() const { return m_overwriteMode; }

    /// 获取指定 CAN ID 的累计帧数
    int frameCountForId(quint32 id) const { return m_idCount.value(id, 0); }

private:
    QVector<CanFrame> m_frames;
    int m_maxFrames = 100000; ///< 默认最多保留 10 万帧

    bool m_overwriteMode = false;                 ///< 覆盖模式开关
    QHash<quint32, int> m_idToRow;                ///< CAN ID → 源模型行号（覆盖模式）
    QHash<quint32, int> m_idCount;                ///< CAN ID → 累计帧数
};

#endif // CANTRACEMODEL_H
