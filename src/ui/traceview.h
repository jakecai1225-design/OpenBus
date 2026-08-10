#ifndef TRACEVIEW_H
#define TRACEVIEW_H

#include <QTableView>
#include <QWidget>
#include <QPlainTextEdit>
#include <QList>
#include "core/canframe.h"

class CanTraceModel;
class CanFilterProxyModel;
class FilterBar;
class QSplitter;
class QLabel;
class QActionGroup;
class DbcManager;
class QTimer;

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

    /// 重写 setModel，自动将代理模型传递给 FilterHeaderView
    void setModel(QAbstractItemModel *model) override;

    // ---- 选中行保持（过滤变化后恢复定位） ----

    /// 在过滤/排序变化前记录当前选中的源模型行号
    void pinSelection();
    /// 在过滤/排序变化后恢复选中并滚动到该行
    void restoreSelection();

    // ---- Wireshark 风格导航 ----

    /// 跳转到指定帧编号（1-based）
    void goToPacket(int frameNumber);
    /// 查找下一匹配帧（从当前选中行之后开始）
    bool findNext(const QString &text);
    /// 查找上一匹配帧（从当前选中行之前开始）
    bool findPrevious(const QString &text);
    /// 跳转到下一个相同 CAN ID 的帧
    void goToNextSameId();
    /// 跳转到上一个相同 CAN ID 的帧
    void goToPrevSameId();

public slots:
    void scrollToBottom();

signals:
    void frameDoubleClicked(const CanFrame &frame);
    void frameSelected(const CanFrame &frame);
    /// 请求将选中帧的信号发送到 Graphic
    void frameAddToGraphic(const CanFrame &frame);
    /// 请求清除当前过滤
    void clearFilterRequested();

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private slots:
    void onHeaderClicked(int column);
    void onHeaderContextMenu(const QPoint &pos);
    void onColumnFilter(int column);
    void onClearColumnFilter(int column);
    void onClearAllFilters();
    void onFilterIconClicked(int column);
    void onToggleMarkSelected();
    void onColorSelected();
    void onClearMarks();
    void onClearColors();
    void onGoToPacket();
    void onFind();
    void onFindNext();
    void onFindPrevious();

private:
    bool m_autoScroll = true;
    int m_pinnedSourceRow = -1;  ///< 过滤变化前锁定的源模型行号
    QString m_lastFindText;      ///< 上次查找文本

    void setupAppearance();
    void showHeaderMenu(int column, const QPoint &pos);
    QString columnFilterHint(int column) const;
    /// 将代理模型行号映射到源模型行号
    int toSourceRow(const QModelIndex &proxyIndex) const;
    /// 获取当前选中的源模型行号列表
    QList<int> selectedSourceRows() const;
    /// 选中并滚动到指定源模型行
    void selectSourceRow(int sourceRow);
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
 *   │ FilterBar (Start/Stop + Filter) │
 *   ├────────────────────────────────┤
 *   │ 工具条 (时间戳模式 + 分组统计)    │
 *   ├────────────────────────────────┤
 *   │ TraceView (报文列表)            │
 *   ├──────────────┬─────────────────┤
 *   │ 帧结构        │ 信号解析         │
 *   └──────────────┴─────────────────┘
 *
 * 每个 TraceTab 拥有独立的 CanTraceModel + CanFilterProxyModel，
 * 通过 Start/Stop 按钮控制是否接收帧数据。
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
    CanTraceModel *traceModel() const { return m_traceModel; }
    CanFilterProxyModel *proxyModel() const { return m_proxyModel; }

    void setDbcManager(DbcManager *mgr);

    bool isRunning() const { return m_running; }
    void setRunning(bool running);

    bool isOverwriteMode() const;

    void appendFrame(const CanFrame &frame);
    void appendFrames(const QVector<CanFrame> &frames);
    void clearTrace();
    int frameCount() const;
    bool setFilterExpression(const QString &expr);
    /// 仅清除主过滤表达式（不影响列过滤）
    void clearFilter();
    /// 清除主过滤表达式 + 所有列过滤
    void clearAllFilters();
    QString filterExpression() const;

    /// 从外部文件加载帧数据（BLF/ASC/CSV）
    void loadFile(const QString &path);

    /// 更新分组统计显示
    void updatePacketCount();

signals:
    /// 文件拖放后加载完成
    void fileLoaded(int frameCount);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onSelectionChanged();
    void onPacketCountTimer();

private:
    FilterBar *m_filterBar = nullptr;
    TraceView *m_traceView = nullptr;
    QSplitter *m_vSplitter = nullptr;
    QSplitter *m_hSplitter = nullptr;
    FrameInfoWidget *m_frameInfo = nullptr;
    SignalDecodeWidget *m_signalDecode = nullptr;

    CanTraceModel *m_traceModel = nullptr;
    CanFilterProxyModel *m_proxyModel = nullptr;
    bool m_running = false;

    // ---- 设置菜单 ----
    QActionGroup *m_timeFormatGroup = nullptr;  ///< 时间格式互斥动作组
    QTimer *m_packetCountTimer = nullptr;  ///< 分组计数防抖定时器
    bool m_packetCountDirty = false;       ///< 分组计数待更新标记
};

#endif // TRACEVIEW_H
