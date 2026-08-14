#ifndef CANTRACEMODEL_H
#define CANTRACEMODEL_H

#include <QAbstractTableModel>
#include <QHash>
#include <QSet>
#include <QColor>
#include <QTimer>
#include <QVector>
#include <QDateTime>
#include "core/canframe.h"
#include "utils/ringbuffer.h"

class FilterEngine;
class DbcManager;

/**
 * @brief CAN 报文追踪数据模型（高性能版）
 *
 * 集成 4 项优化：
 * - 延迟格式化 + 可见行缓存（Phase 1）
 * - 批量更新 + 可调刷新率（Phase 2）
 * - 环形缓冲区存储（Phase 4）
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
        ColSignal,       ///< 内联信号值列
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

    /// 追加单帧（实时捕获场景，缓冲到 pending 队列）
    void appendFrame(const CanFrame &frame);

    /// 批量追加（回放场景，直接写入不经过 pending 队列）
    void appendFrames(const QVector<CanFrame> &frames);

    /// 清空所有帧
    void clear();

    /// 设置最大帧数限制（环形缓冲区容量）
    void setMaxFrames(int max);
    int maxFrames() const { return m_maxFrames; }

    /// 获取指定行帧（const 引用）
    const CanFrame &frameAt(int row) const;
    const CanFrame &frameAt(const QModelIndex &index) const { return frameAt(index.row()); }

    /// 当前帧总数
    int frameCount() const { return m_ringBuffer.size(); }

    /// 捕获起始的 wall-clock 时间（用于 DateTimeOfDay / SecondsSinceEpoch 时间戳模式）
    QDateTime captureStartTime() const { return m_captureStartDateTime; }

    /// 设置 DBC 管理器（用于内联信号值列）
    void setDbcManager(DbcManager *mgr);

    /// 获取所有帧（返回临时 QVector，用于 GraphicView 等）
    QVector<CanFrame> frames() const;

    /// 收集指定列的唯一值及其出现次数（用于 Excel 风格筛选面板）
    QList<QPair<QString, int>> uniqueValues(int column) const;

    // ---- 行标记与着色 ----

    void toggleMark(int row);
    void setMarked(int row, bool marked);
    bool isMarked(int row) const;
    void clearMarks();
    QList<int> markedRows() const;

    void setRowColor(int row, const QColor &color);
    QColor rowColor(int row) const;
    void clearColors();

    // ---- 行标签（Notepad++ 风格书签） ----

    void setRowLabel(int row, const QString &label);
    QString rowLabel(int row) const;
    /// 获取所有已标记/着色/标签的行及其标签文本（用于跳转菜单）
    QList<QPair<int, QString>> labeledMarks() const;
    void clearLabels();

    // ---- 着色规则 ----

    struct ColorRule {
        QString expr;
        QColor background;
        QColor foreground;
        bool enabled = true;
    };

    void setColorRules(const QVector<ColorRule> &rules);
    const QVector<ColorRule> &colorRules() const { return m_colorRules; }
    void clearColorRules();

    // ---- 覆盖模式 ----

    void setOverwriteMode(bool mode);
    bool isOverwriteMode() const { return m_overwriteMode; }

    // ---- 错误帧高亮 ----

    void setErrorFrameHighlight(bool enabled);
    bool errorFrameHighlight() const { return m_errorFrameHighlight; }

    // ---- 时间参考点 ----

    /// 设置时间参考点（指定行号的帧作为 t=0）
    void setTimeReference(int row);
    /// 清除时间参考点
    void clearTimeReference();
    /// 是否已设置时间参考点
    bool hasTimeReference() const { return m_hasTimeRef; }
    /// 时间参考点的行号（-1 表示未设置或已超出范围）
    int timeReferenceRow() const;
    /// 时间参考点的时间戳
    double timeReferenceTimestamp() const { return m_timeRefTimestamp; }

    int frameCountForId(quint32 id) const { return m_idCount.value(id, 0); }

    // ---- Phase 2: 刷新率控制 ----

    enum RefreshRate {
        High = 50,      ///< 50ms — 高刷新率
        Medium = 100,   ///< 100ms — 中刷新率
        Low = 200,      ///< 200ms — 低刷新率
        Paused = 0      ///< 0 — 暂停刷新
    };

    void setRefreshRate(RefreshRate rate);
    RefreshRate refreshRate() const { return m_refreshRate; }

    /// 手动 flush pending 帧（暂停刷新时调用以一次性提交）
    void flushPending();

    // ---- Phase 1: 行缓存管理 ----

    /// 通知可见行范围变化，淘汰不可见行缓存
    void setVisibleRange(int first, int last);
    /// 清除所有行缓存（时间格式切换、着色规则变化时调用）
    void invalidateRowCache();

private:
    // ---- Phase 4: 环形缓冲区存储 ----
    // 注意: m_maxFrames 必须在 m_ringBuffer 之前声明，
    // 因为 C++ 按声明顺序初始化成员，m_ringBuffer 构造依赖 m_maxFrames 的值。
    int m_maxFrames = 1000000;
    RingBuffer<CanFrame> m_ringBuffer;
    quint64 m_seqCounter = 0;   ///< 帧序列号（永不回退，用于 No. 列）
    QDateTime m_captureStartDateTime;  ///< 捕获起始 wall-clock 时间（首次提交帧时设置）

    bool m_overwriteMode = false;
    bool m_errorFrameHighlight = true;  ///< 错误帧整行高亮（默认开启）

    // ---- 时间参考点 ----
    bool m_hasTimeRef = false;        ///< 是否已设置时间参考点
    quint64 m_timeRefSeq = 0;         ///< 参考帧的序列号
    double m_timeRefTimestamp = 0.0;  ///< 参考帧的时间戳

    QHash<quint32, int> m_idToRow;   ///< CAN ID → 逻辑行号（覆盖模式）
    QHash<quint32, quint64> m_idCount;  ///< CAN ID → 累计帧数

    // 行标记/着色 key 改用 seqCounter（Phase 4）
    QSet<quint64> m_markedRows;
    QHash<quint64, QColor> m_rowColors;
    QHash<quint64, QString> m_rowLabels;  ///< 行标签（自定义文字标记）

    // ---- 着色规则 ----
    QVector<ColorRule> m_colorRules;
    QVector<FilterEngine *> m_colorFilters;
    QColor evaluateColorRules(const CanFrame &frame) const;

    // ---- Phase 1: 行缓存 ----
    struct RowCache {
        QString cols[ColCount];      ///< 各列格式化字符串
        QColor bgColor;              ///< 背景色（着色规则结果缓存）
        bool bgValid = false;        ///< 背景色缓存是否有效
        bool valid = false;          ///< 整个缓存是否有效
    };
    mutable QHash<int, RowCache> m_rowCache;
    int m_cacheFirst = -1;  ///< 当前缓存的可见行起始
    int m_cacheLast = -1;   ///< 当前缓存的可见行结束
    void formatCell(int row, int col, const CanFrame &f, QString &out) const;

    // ---- Phase 2: 批量更新 ----
    QVector<CanFrame> m_pendingFrames;
    QTimer m_flushTimer;
    RefreshRate m_refreshRate = High;

    DbcManager *m_dbcManager = nullptr;  ///< DBC 管理器（用于内联信号值列）

    /// 实际将帧写入环形缓冲区（flush 时调用）
    void commitFrame(const CanFrame &frame);
    /// 提交一批 pending 帧
    void commitBatch(const QVector<CanFrame> &frames);

signals:
    /// 新帧已提交到模型（flush 后发出，用于刷新统计）
    void framesCommitted(int count);
};

#endif // CANTRACEMODEL_H
