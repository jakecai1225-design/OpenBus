#ifndef TRACEVIEW_H
#define TRACEVIEW_H

#include <QTableView>
#include <QWidget>
#include <QPlainTextEdit>
#include <QList>
#include <QPixmap>
#include "core/canframe.h"

class CanTraceModel;
class CanTraceProxyModel;
class ViewportProxyModel;
class FilterBar;
class BookmarkManager;
class QSplitter;
class QLabel;
class QActionGroup;
class DbcManager;
class QTimer;
class QTabWidget;
class QTreeWidget;
class TraceStatisticsWidget;
class TraceDiffWidget;

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

    /// 获取当前选中的源模型行号列表（供统计/差异视图取帧集合）
    QList<int> selectedSourceRows() const;

    /// 重写 setModel，自动将代理模型传递给 FilterHeaderView
    void setModel(QAbstractItemModel *model) override;

    // ---- 列布局持久化 ----

    /// 保存列布局（宽度、顺序、可见性）到 QSettings
    void saveColumnLayout();
    /// 从 QSettings 恢复列布局
    void restoreColumnLayout();

    // ---- 字体缩放（持久化） ----

    /// 应用字体大小（7-24 磅，行高随之缩放，写回 QSettings）
    void applyFontSize(int pointSize);
    void zoomFontIn();
    void zoomFontOut();
    void resetFontSize();

    // ---- Trace 文件导出 ----

    /// 导出模式
    enum ExportMode {
        ExportAll,       ///< 所有帧
        ExportFiltered,  ///< 过滤后帧
        ExportSelected,  ///< 选中帧
        ExportMarked     ///< 标记帧
    };

    /// 导出帧到文件
    void exportFrames(ExportMode mode);

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
    /// 跳转到下一个标记行（标记/着色/标签）
    void goToNextMark();
    /// 跳转到上一个标记行
    void goToPrevMark();

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
    void wheelEvent(QWheelEvent *event) override;

private:
    /// 获取视窗代理模型 (如有)
    ViewportProxyModel *viewportProxy() const;
    /// 获取过滤代理模型 (穿越视窗代理层)
    CanTraceProxyModel *filterProxy() const;
    /// 获取源数据模型 (穿越代理层)
    CanTraceModel *traceSource() const;

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

    // ---- 3-state 排序状态 ----
    int m_sortColumn = -1;              ///< 当前排序列（-1 = 未排序）
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;  ///< 当前排序方向

    void setupAppearance();
    void showHeaderMenu(int column, const QPoint &pos);
    QString columnFilterHint(int column) const;
    /// 将代理模型行号映射到源模型行号
    int toSourceRow(const QModelIndex &proxyIndex) const;
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
 * @brief Signal decode panel — selectable DBC signal list for the current frame.
 *
 * Right-click / double-click a signal to add it to Graphic (via shell).
 */
class SignalDecodeWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SignalDecodeWidget(QWidget *parent = nullptr);

    void setDbcManager(DbcManager *mgr) { m_dbcMgr = mgr; }
    void setFrame(const CanFrame &frame);
    void clear();

signals:
    /// Request adding one decoded signal to the Graphic page.
    void signalAddToGraphic(quint32 canId, const QString &signalName);

private slots:
    void onContextMenu(const QPoint &pos);
    void onItemDoubleClicked();

private:
    void emitAddSelected();

    QLabel *m_msgLabel = nullptr;
    QTreeWidget *m_tree = nullptr;
    DbcManager *m_dbcMgr = nullptr;
    quint32 m_canId = 0;
    bool m_extended = false;
    bool m_hasFrame = false;
};

/**
 * @brief CANoe 风格视窗缩略图控件
 *
 *   ┌──┐
 *   │  │ ← 全部数据缩略图 (Rx/Tx 密度)
 *   │██│ ← 高亮视窗区域 (可拖拽)
 *   │██│
 *   │  │
 *   └──┘
 *
 * 不是标准滚动条 — 是一个可视化数据概览:
 *   - 显示全部帧的密度分布 (Rx=绿色, Tx=蓝色)
 *   - 高亮矩形表示当前视窗位置
 *   - 拖拽高亮区域移动视窗
 *   - 点击任意位置跳转视窗
 */
class ViewportOverview : public QWidget
{
    Q_OBJECT

public:
    explicit ViewportOverview(QWidget *parent = nullptr);

    void setViewportProxy(ViewportProxyModel *proxy);
    void setFilterProxy(CanTraceProxyModel *proxy);
    void setTraceSource(CanTraceModel *model);

    /// 标记缓存需要重建
    void markCacheDirty();

signals:
    /// 用户拖拽或点击导致视窗位置变化
    void viewportMoved(int start);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    QSize sizeHint() const override { return {60, 100}; }
    QSize minimumSizeHint() const override { return {60, 50}; }

private:
    ViewportProxyModel *m_proxy = nullptr;
    CanTraceProxyModel *m_filterProxy = nullptr;
    CanTraceModel *m_traceModel = nullptr;

    bool m_dragging = false;
    int m_dragStartGlobalY = 0;
    int m_dragStartViewport = 0;
    QTimer *m_dragTimer = nullptr;
    void onDragTimer();

    // 密度缓存
    QPixmap m_cachePixmap;
    bool m_cacheDirty = true;
    int m_cachedTotal = 0;
    int m_cachedHeight = 0;
    QTimer *m_rebuildTimer = nullptr;

    void scheduleRebuild();
    void rebuildCache();
    QRect viewportRect() const;
    int yToViewportStart(int y) const;
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
 *   ├────────────────────────────────┤
 *   │ Trace Explorer 底部标签          │
 *   │ (详情/信号/统计/差异)            │
 *   └────────────────────────────────┘
 *
 * 每个 TraceTab 拥有独立的 CanTraceModel + CanTraceProxyModel，
 * 通过 Start/Stop 按钮控制是否接收帧数据。
 */
class TraceTab : public QWidget
{
    Q_OBJECT

public:
    explicit TraceTab(QWidget *parent = nullptr);
    ~TraceTab();

    TraceView *traceView() const { return m_traceView; }
    FilterBar *filterBar() const { return m_filterBar; }
    FrameInfoWidget *frameInfo() const { return m_frameInfo; }
    SignalDecodeWidget *signalDecode() const { return m_signalDecode; }
    CanTraceModel *traceModel() const { return m_traceModel; }
    CanTraceProxyModel *proxyModel() const { return m_proxyModel; }
    ViewportProxyModel *viewportProxy() const { return m_viewportProxy; }

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

    /// M1 预埋：协议 / 形态身份（doc/flow.md §十三）——多协议就绪前恒为 can/framelist
    QString protocolId() const { return m_protocolId; }
    void setProtocolId(const QString &id) { m_protocolId = id; }
    QString formId() const { return m_formId; }
    void setFormId(const QString &id) { m_formId = id; }

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
    /// 更新视窗缩略图控件
    void updateViewportOverview();
    FilterBar *m_filterBar = nullptr;
    TraceView *m_traceView = nullptr;
    QSplitter *m_vSplitter = nullptr;
    QTabWidget *m_explorerTabs = nullptr;           ///< T8: 底部 Trace Explorer 标签外壳
    FrameInfoWidget *m_frameInfo = nullptr;
    SignalDecodeWidget *m_signalDecode = nullptr;
    TraceStatisticsWidget *m_statistics = nullptr;  ///< T9: 选中帧统计
    TraceDiffWidget *m_diff = nullptr;              ///< T10: 选中帧差异对比

    CanTraceModel *m_traceModel = nullptr;
    CanTraceProxyModel *m_proxyModel = nullptr;
    ViewportProxyModel *m_viewportProxy = nullptr;
    BookmarkManager *m_bookmarkManager = nullptr;
    ViewportOverview *m_viewportOverview = nullptr;
    bool m_autoScrollViewport = true;  ///< 视窗自动跟随新数据
    bool m_running = false;

    // ---- 设置菜单 ----
    QActionGroup *m_timeFormatGroup = nullptr;  ///< 时间格式互斥动作组
    QTimer *m_packetCountTimer = nullptr;      ///< 分组计数防抖定时器
    bool m_packetCountDirty = false;           ///< 分组计数待更新标记

    // ---- M1 预埋：协议 / 形态身份（doc/flow.md §十三）----
    QString m_protocolId = QStringLiteral("can");
    QString m_formId = QStringLiteral("framelist");
};

#endif // TRACEVIEW_H
