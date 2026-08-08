#ifndef CANTRACEMODEL_H
#define CANTRACEMODEL_H

#include <QAbstractTableModel>
#include <QVector>
#include <QHash>
#include <QSet>
#include <QColor>
#include "core/canframe.h"

class FilterEngine;

/**
 * @brief CAN 报文追踪数据模型
 *
 * 为 TraceView 提供 QAbstractTableModel 实现。
 * 支持 appendFrame（实时捕获）、clear、随机访问。
 * 支持 No.（帧编号）、Delta（相对时间）列。
 * 支持行标记与自定义着色。
 */
class CanTraceModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Columns {
        ColNo = 0,       ///< 帧编号（1-based 序列号）
        ColTime,         ///< 绝对时间戳
        ColDelta,        ///< 与上一帧的时间增量
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
        MarkedRole,                    ///< 返回该行是否被标记
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

    // ---- 行标记与着色 ----

    /// 切换指定源模型行的标记状态
    void toggleMark(int row);
    /// 设置指定行的标记状态
    void setMarked(int row, bool marked);
    /// 查询指定行是否被标记
    bool isMarked(int row) const;
    /// 清除所有标记
    void clearMarks();
    /// 获取所有标记行（排序后）
    QList<int> markedRows() const;

    /// 为指定行设置自定义背景色（QColor() 表示清除）
    void setRowColor(int row, const QColor &color);
    /// 查询指定行的自定义颜色
    QColor rowColor(int row) const;
    /// 清除所有自定义颜色
    void clearColors();

    // ---- 着色规则 ----

    /// 着色规则结构
    struct ColorRule {
        QString expr;         ///< 条件表达式 (FilterEngine 语法)
        QColor background;    ///< 背景色
        QColor foreground;    ///< 前景色
        bool enabled = true;  ///< 是否启用
    };

    /// 设置着色规则列表（规则按顺序匹配，首个命中生效）
    void setColorRules(const QVector<ColorRule> &rules);
    /// 获取当前着色规则
    const QVector<ColorRule> &colorRules() const { return m_colorRules; }
    /// 清除着色规则
    void clearColorRules();

    // ---- 覆盖模式 ----

    /// 设置覆盖模式：同 CAN ID 的帧只保留一行，刷新数据和帧数
    void setOverwriteMode(bool mode);
    bool isOverwriteMode() const { return m_overwriteMode; }

    /// 获取指定 CAN ID 的累计帧数
    int frameCountForId(quint32 id) const { return m_idCount.value(id, 0); }

private:
    QVector<CanFrame> m_frames;
    int m_maxFrames = 100000; ///< 默认最多保留 10 万帧
    int m_seqCounter = 0;     ///< 帧序列号计数器（不因滚动丢弃而回退）

    bool m_overwriteMode = false;                 ///< 覆盖模式开关
    QHash<quint32, int> m_idToRow;                ///< CAN ID → 源模型行号（覆盖模式）
    QHash<quint32, int> m_idCount;                ///< CAN ID → 累计帧数

    QSet<int> m_markedRows;        ///< 被标记的源模型行号集合
    QHash<int, QColor> m_rowColors; ///< 行号 → 自定义背景色

    // ---- 着色规则 ----
    QVector<ColorRule> m_colorRules;
    QVector<FilterEngine *> m_colorFilters;  ///< 每条规则对应的编译后的 FilterEngine

    /// 对帧执行着色规则匹配，返回背景色（无效色表示无匹配）
    QColor evaluateColorRules(const CanFrame &frame) const;
};

#endif // CANTRACEMODEL_H
