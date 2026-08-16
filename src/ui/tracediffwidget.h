#ifndef TRACEDIFFWIDGET_H
#define TRACEDIFFWIDGET_H

#include <QWidget>
#include "core/canframe.h"

class QTreeWidget;
class QCheckBox;
class QTimer;
class DbcManager;

/**
 * @brief 选中帧差异对比视图（CANoe Difference 对标，设计文档 §九 G-F2 / T10）
 *
 *   选中 ≥2 帧 → 对比首帧(A)与末帧(B)（按时间排序）；恰好 2 帧即 A/B 对比
 *   概览：  A/B 时间、报文名、CAN ID、DLC、首末 Δt
 *   字节级：双列 Hex 对照，不等字节高亮
 *   信号级：DBC 解码后逐信号 首值 → 末值，默认仅列变化项，可切换显示全部
 *
 * 数据源 = TraceView 当前选中行集合（TraceTab::onSelectionChanged 喂入），
 * 内部 100ms 防抖后重算。
 */
class TraceDiffWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TraceDiffWidget(QWidget *parent = nullptr);

    void setDbcManager(DbcManager *mgr) { m_dbcMgr = mgr; }

    /// 提交选中帧集合（<2 帧显示占位提示）
    void setFrames(const QVector<CanFrame> &frames);

private:
    void rebuild();

    DbcManager *m_dbcMgr = nullptr;
    QCheckBox *m_showChangedOnly = nullptr;
    QTreeWidget *m_tree = nullptr;
    QTimer *m_debounce = nullptr;
    QVector<CanFrame> m_pending;
};

#endif // TRACEDIFFWIDGET_H
