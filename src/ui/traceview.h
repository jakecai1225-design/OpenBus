#ifndef TRACEVIEW_H
#define TRACEVIEW_H

#include <QTableView>
#include <QWidget>
#include <QPlainTextEdit>
#include "core/canframe.h"

class CanTraceModel;
class CanFilterProxyModel;
class FilterBar;
class QSplitter;
class QLabel;
class DbcManager;

/**
 * @brief CANoe 风格 Trace 报文列表视图
 */
class TraceView : public QTableView
{
    Q_OBJECT

public:
    explicit TraceView(QWidget *parent = nullptr);

    void setAutoScrollEnabled(bool enabled) { m_autoScroll = enabled; }
    bool autoScrollEnabled() const { return m_autoScroll; }

    const CanFrame *selectedFrame() const;

public slots:
    void scrollToBottom();

signals:
    void frameDoubleClicked(const CanFrame &frame);
    void frameSelected(const CanFrame &frame);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private slots:
    void onHeaderClicked(int column);
    void onHeaderContextMenu(const QPoint &pos);
    void onColumnFilter(int column);
    void onClearColumnFilter(int column);
    void onClearAllFilters();

private:
    bool m_autoScroll = true;
    void setupAppearance();
    void showHeaderMenu(int column, const QPoint &pos);
    QString columnFilterHint(int column) const;
};

/**
 * @brief 帧结构面板 — 紧凑文本显示选中帧的字段
 *
 *   Time:       12.345678
 *   Channel:    1
 *   Direction:  Rx
 *   ID:         0x123
 *   DLC:        8
 *   Data:       01 02 03 04 05 06 07 08
 *   Flags:      ---
 */
class FrameInfoWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FrameInfoWidget(QWidget *parent = nullptr);

    void setFrame(const CanFrame &frame);
    void clear();

private:
    QPlainTextEdit *m_edit;
};

/**
 * @brief 信号解析面板 — 紧凑文本显示 DBC 信号解码值
 *
 *   EngineRPM:     1234.500 rpm
 *   ThrottlePos:   45.200 %
 *   coolantTemp:   89.000 C
 */
class SignalDecodeWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SignalDecodeWidget(QWidget *parent = nullptr);

    void setDbcManager(DbcManager *mgr) { m_dbcMgr = mgr; }
    void setFrame(const CanFrame &frame);
    void clear();

private:
    QPlainTextEdit *m_edit;
    DbcManager *m_dbcMgr = nullptr;
};

/**
 * @brief Wireshark 风格 Trace 页面 — 整体三栏
 *
 *   ┌────────────────────────────────┐
 *   │ FilterBar                       │
 *   ├────────────────────────────────┤
 *   │ TraceView (报文列表)            │
 *   ├──────────────┬─────────────────┤
 *   │ 帧结构        │ 信号解析         │
 *   └──────────────┴─────────────────┘
 */
class TraceTab : public QWidget
{
    Q_OBJECT

public:
    explicit TraceTab(QWidget *parent = nullptr);

    TraceView *traceView() const { return m_traceView; }
    FilterBar *filterBar() const { return m_filterBar; }
    FrameInfoWidget *frameInfo() const { return m_frameInfo; }
    SignalDecodeWidget *signalDecode() const { return m_signalDecode; }

    void setDbcManager(DbcManager *mgr);
    void setProxyModel(CanFilterProxyModel *proxy);

private slots:
    void onSelectionChanged();

private:
    FilterBar *m_filterBar = nullptr;
    TraceView *m_traceView = nullptr;
    QSplitter *m_vSplitter = nullptr;
    QSplitter *m_hSplitter = nullptr;
    FrameInfoWidget *m_frameInfo = nullptr;
    SignalDecodeWidget *m_signalDecode = nullptr;
};

#endif // TRACEVIEW_H
