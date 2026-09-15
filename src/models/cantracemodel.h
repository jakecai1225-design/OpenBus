#ifndef CANTRACEMODEL_H
#define CANTRACEMODEL_H

#include <QAbstractTableModel>
#include <QHash>
#include <QSet>
#include <QColor>
#include <QTimer>
#include <QVector>
#include <QDateTime>
#include <QtGlobal>
#include "core/canframe.h"
#include "core/capturelog.h"
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
        ColName,        ///< DBC 报文名称
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

    /// Append one frame into pending (live capture).
    void appendFrame(const CanFrame &frame);

    /// Append a batch into pending without committing (live CaptureLog pull).
    void enqueueFrames(const QVector<CanFrame> &frames);

    /// Commit a batch immediately (offline import / file load).
    void appendFrames(const QVector<CanFrame> &frames);

    /// 清空所有帧
    void clear();

    /// 设置最大帧数限制（环形缓冲区容量）
    void setMaxFrames(int max);
    int maxFrames() const { return m_maxFrames; }

    /// Get frame at logical row (by value — CaptureLog camera has no stable refs).
    CanFrame frameAt(int row) const;
    CanFrame frameAt(const QModelIndex &index) const { return frameAt(index.row()); }

    /// Cheap meta for paint / filter keys (no payload copy). Prefer over frameAt in data().
    bool frameMetaAt(int row, CaptureFrameMeta *out) const;

    /// Current frame total (camera: CaptureLog view size; else local ring).
    int frameCount() const { return displaySize(); }

    /// Phase B5: live Trace mirrors CaptureLog (no per-tab frame ring).
    void setCaptureLogCamera(bool enabled);
    bool isCaptureLogCamera() const { return m_captureCamera; }

    /**
     * Sync camera rowCount/seq to CaptureLog; advance @p cursorSeq by at most
     * @p maxRows (streaming). Emits insertRows / ringWrapped. Returns frames visited.
     */
    int syncFromCaptureLog(quint64 *cursorSeq, int maxRows = 256);

    /**
     * T5: adopt CaptureLog size/seq in one reset (no incremental proxy notify).
     * Used when a background Trace tab becomes visible again.
     */
    void adoptCaptureLogSnapshot(int size, quint64 seq);

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

    // ---- 列对齐配置 ----

    void setColumnAlignment(int col, Qt::Alignment align);
    Qt::Alignment columnAlignment(int col) const;
    void clearColumnAlignment(int col);
    Qt::Alignment effectiveAlignment(int column) const;
    void resetToDefault(int col);

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
    /// 首个命中帧的着色规则下标（-1 = 无命中）；
    /// m_colorFilters 与 m_colorRules 下标一一对应（禁用/编译失败位为 nullptr）
    int matchingColorRule(const CanFrame &frame) const;

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

    /// 帧序列号计数（永不回退；No. = seqCounter - rowCount + row + 1）
    quint64 seqCounter() const { return m_seqCounter; }

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

    // ---- Phase 1 / T4: visible-row cache (format + color off paint) ----

    /// Notify visible/window row range; prunes and prefills cache (DBC/color).
    void setVisibleRange(int first, int last);
    /// Drop all row caches (time format / color rules / DBC change).
    void invalidateRowCache();

private:
    // ---- Phase 4 / B5 storage ----
    // m_maxFrames before m_ringBuffer (ctor init order).
    // Live CaptureLog camera: m_viewRows mirrors CaptureLog; local ring unused.
    // Offline / overwrite: local m_ringBuffer owns frames.
    int m_maxFrames = 1000000;
    RingBuffer<CanFrame> m_ringBuffer;
    bool m_captureCamera = false;  ///< B5: read frames from CaptureLog
    int m_viewRows = 0;            ///< camera-mode rowCount
    quint64 m_seqCounter = 0;   ///< frame sequence (never decreases; No. column)
    QDateTime m_captureStartDateTime;  ///< wall-clock at first commit

    bool m_overwriteMode = false;
    bool m_errorFrameHighlight = true;  ///< highlight error frames (default on)

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
    QVector<FilterEngine *> m_colorFilters;  ///< 与 m_colorRules 下标对齐（nullptr = 不参与求值）

    // ---- Phase 1 / T4: visible-row cache ----
    struct RowCache {
        QString cols[ColCount];
        QColor bgColor;
        QColor fgColor;
        bool bgValid = false;
        bool fgValid = false;
        bool colsFilled = false; ///< All columns formatted (incl. ColSignal)
        bool valid = false;
    };
    mutable QHash<int, RowCache> m_rowCache;
    int m_cacheFirst = -1;
    int m_cacheLast = -1;

    void formatCell(int row, int col, const CanFrame &f, QString &out) const;
    void formatCellMeta(int row, int col, const CaptureFrameMeta &m, QString &out) const;
    CanFrame frameWithPayload(int row, const CaptureFrameMeta &meta) const;
    static bool columnNeedsPayload(int col);

    /// Eagerly format columns + evaluate color rules for one row (T4).
    void fillRowCache(int row) const;
    /// Fill every row in [m_cacheFirst, m_cacheLast].
    void prefillVisibleCache() const;
    quint64 seqForRow(int row) const;

    // ---- Phase 2: 批量更新 ----
    QVector<CanFrame> m_pendingFrames;
    QTimer m_flushTimer;
    RefreshRate m_refreshRate = High;

    DbcManager *m_dbcManager = nullptr;  ///< DBC 管理器（用于内联信号值列）

    // ---- 列对齐配置 ----
    QHash<int, Qt::Alignment> m_columnAlignments;  ///< 用户自定义对齐（列号 → 对齐）

    /// Write one frame into the ring (flush path).
    void commitFrame(const CanFrame &frame);
    /// Commit a pending batch into the ring.
    void commitBatch(const QVector<CanFrame> &frames);
    /// Flush when pending grows beyond this (avoids multi-100k commits).
    int pendingSoftCap() const { return qMax(2048, m_maxFrames / 8); }
    int displaySize() const { return m_captureCamera ? m_viewRows : m_ringBuffer.size(); }
    /// Leave camera mode and use the local ring (offline import / overwrite).
    void leaveCaptureCameraForLocal();

signals:
    /// New frames flushed into the model (for stats / autoscroll)
    void framesCommitted(int count);
    /// Ring wrap: `shift` oldest rows dropped, row count unchanged.
    /// Proxies should remap without a million-row dataChanged.
    void ringWrapped(int shift);
};

#endif // CANTRACEMODEL_H
