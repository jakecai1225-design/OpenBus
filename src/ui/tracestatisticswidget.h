#ifndef TRACESTATISTICSWIDGET_H
#define TRACESTATISTICSWIDGET_H

#include <QWidget>
#include "core/canframe.h"

class QTreeWidget;
class QLabel;
class QTimer;
class DbcManager;

/**
 * @brief 选中帧统计视图（CANoe Statistics 对标，设计文档 §九 G-F1 / T9）
 *
 *   时间组：帧数 N、时间跨度、相邻帧 Δt min/max/avg/σ（σ 为增强项）
 *   信号组：DBC 解码后逐信号 min/max/avg/σ/首值/末值
 *           未加载 DBC 或选中帧无 DBC 定义时按字节位置统计
 *
 * 数据源 = TraceView 当前选中行集合（TraceTab::onSelectionChanged 喂入），
 * 内部 100ms 防抖后重算；集合上限 MaxFrames，超出截断并在摘要中提示。
 */
class TraceStatisticsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TraceStatisticsWidget(QWidget *parent = nullptr);

    /// 统计集合上限（G-F1 约定：超出截断并提示）
    static constexpr int MaxFrames = 10000;

    void setDbcManager(DbcManager *mgr) { m_dbcMgr = mgr; }

    /// 提交选中帧集合（按时间排序在内部完成；防抖后重算）
    /// truncated = 原始选中数超过 MaxFrames 已截断
    void setFrames(const QVector<CanFrame> &frames, bool truncated = false);

private:
    void rebuild();

    DbcManager *m_dbcMgr = nullptr;
    QLabel *m_summary = nullptr;
    QTreeWidget *m_tree = nullptr;
    QTimer *m_debounce = nullptr;
    QVector<CanFrame> m_pending;
    bool m_pendingTruncated = false;
};

#endif // TRACESTATISTICSWIDGET_H
