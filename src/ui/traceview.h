#ifndef TRACEVIEW_H
#define TRACEVIEW_H

#include <QTableView>
#include "core/canframe.h"

class CanTraceModel;

/**
 * @brief CANoe 风格 Trace 报文列表视图
 *
 * 基于 QTableView，显示 CanTraceModel 数据。
 * 特性：等宽字体、列宽自适应、自动滚动到底部、右键菜单。
 */
class TraceView : public QTableView
{
    Q_OBJECT

public:
    explicit TraceView(QWidget *parent = nullptr);

    void setAutoScrollEnabled(bool enabled) { m_autoScroll = enabled; }
    bool autoScrollEnabled() const { return m_autoScroll; }

    /// 获取当前选中帧（若无选中返回 nullptr）
    const CanFrame *selectedFrame() const;

public slots:
    void scrollToBottom();

signals:
    void frameDoubleClicked(const CanFrame &frame);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    bool m_autoScroll = true;

    void setupAppearance();
};

#endif // TRACEVIEW_H
