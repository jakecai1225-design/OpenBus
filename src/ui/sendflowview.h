#ifndef SEND_FLOW_VIEW_H
#define SEND_FLOW_VIEW_H

#include <QWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMap>
#include <QString>
#include <QList>
#include <QColor>
#include <QTimer>
#include <QSet>

class QGraphicsRectItem;
class QGraphicsTextItem;
class QGraphicsPathItem;
class DbcManager;
class CanDeviceManager;

/**
 * @brief CAN Data Send Flow 可视化视图
 *
 * 三层级拓扑图 + 流指示器系统：
 *   Layer 1: SignalGenerator（单个信号发生器）
 *                │ sendFrame
 *   Layer 2: Trace + Graphic + Record（并行接收端）📊
 *                │
 *   Layer 3: Real（实际连接的设备显示）🔌
 *
 * 流指示器系统通过状态灯机制提供直观的视觉反馈：
 *   - 未使能/无数据流：不亮灯（块体灰化）
 *   - 待命（enabled 但 idle）：常亮绿色
 *   - 数据流活跃（有 Tx/Rx 帧到达）：绿色闪烁
 *   - 运行异常：红色闪烁
 */
class SendFlowView : public QWidget
{
    Q_OBJECT

public:
    explicit SendFlowView(QWidget *parent = nullptr);

    QSize sizeHint() const override { return {900, 600}; }

    /// 设置 DBC 管理器
    void setDbcManager(DbcManager *mgr) { m_dbcManager = mgr; }
    /// 设置设备管理器
    void setDeviceManager(CanDeviceManager *mgr) { m_deviceManager = mgr; }

public slots:
    /// 构建/刷新拓扑图
    void buildTopology();
    /// 更新设备连接状态显示
    void updateDeviceStatus();
    /// 手动刷新整个画布
    void refreshView() { buildTopology(); updateDeviceStatus(); }
    /// 收到帧（来自 Tx/Rx 回环）
    void onFrameReceived(const class CanFrame &frame);
    /// 设置测量运行状态
    void setRunning(bool running) { m_running = running; updateBlockLamps(); }

signals:
    /// 请求打开收发模块的信号发送 Tab
    void openSendTabRequested();
    /// 设备状态变化通知
    void deviceStatusChanged(bool connected, QString deviceName);
    /// 数据流活跃通知
    void flowActiveChanged(bool active);

private:
    // ---- UI ----
    QGraphicsScene *m_scene = nullptr;
    QGraphicsView *m_view = nullptr;

    // ---- 数据源 / 功能块状态 ----
    enum class BlockLamp {
        Off,      ///< 不亮灯（未使能）
        On,       ///< 常亮绿色（待命）
        BlinkOn,  ///< 闪烁亮相（数据流活跃或异常）
        BlinkOff  ///< 闪烁灭相
    };

    struct BlockItem {
        QString id;             ///< 唯一标识
        QString title;          ///< 显示标题
        QString icon;           ///< emoji 图标
        QString category;       ///< "source" | "module" | "device"
        QRectF rect;            ///< 位置和大小
        bool enabled = true;    ///< 是否启用
        QColor color;           ///< 主题色
        QGraphicsRectItem *gfxItem = nullptr;
        QGraphicsTextItem *textItem = nullptr;
        // M1: 状态灯系统
        BlockLamp lamp = BlockLamp::Off;     ///< 当前指示灯状态
        bool lampBlinkPhase = false;         ///< 闪烁相位（true=亮，false=灭）
    };

    QMap<QString, BlockItem> m_blocks;      ///< 所有块的映射
    QList<QGraphicsPathItem *> m_connections; ///< 连线列表

    // ---- M1: 数据流/异常指示（功能块状态灯） ----
    QTimer *m_blinkTimer = nullptr;           ///< 灯闪烁相位驱动（500ms，仅有流/异常时运转）
    bool m_blinkOn = false;                   ///< 当前闪烁相位（true = 亮）
    qint64 m_lastFrameMs = 0;                 ///< 最近一帧到达时刻（数据流活跃判定）
    QSet<QString> m_blockErrors;              ///< 处于异常态的块 ID 集合
    bool m_running = false;                   ///< 测量是否正在运行

    // ---- 方法 ----
    void setupUi();
    void initBlocks();
    void drawConnections();
    BlockItem *blockAt(const QPointF &scenePos);
    void onBlockClicked(BlockItem *block);

    // M1: 流指示器系统
    void startBlinkTimer();
    void stopBlinkTimer();
    void updateBlockLamps();                  ///< 全量刷新所有块的状态灯
    void setBlockError(const QString &blockId, bool on);  ///< 设置/清除块的错误状态
    void updateBlockLamp(const QString &blockId, BlockLamp lamp, bool blinkPhase);
    void drawBlockLamp(QGraphicsRectItem *gfxItem, BlockLamp lamp, bool blinkPhase);
    void drawLampIndicator(QPainter *painter, QRectF rect, BlockLamp lamp, bool blinkPhase);

    // ---- 事件处理 ----
    void mousePressEvent(QMouseEvent *event) override;

    // ---- 引用 ----
    DbcManager *m_dbcManager = nullptr;
    CanDeviceManager *m_deviceManager = nullptr;
};

#endif // SEND_FLOW_VIEW_H
