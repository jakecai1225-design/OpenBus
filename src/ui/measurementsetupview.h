#ifndef MEASUREMENTSETUPVIEW_H
#define MEASUREMENTSETUPVIEW_H

#include <QWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMap>
#include <QString>
#include <QList>
#include <QMenu>
#include <QAction>

class QGraphicsRectItem;
class QGraphicsTextItem;
class QGraphicsPathItem;
class QToolBar;
class QAction;
class QToolButton;
class QLabel;

/**
 * @brief CANoe Measurement Setup 风格的可视化测量配置画布
 *
 * 以标签页形式展示，包含：
 *   - 工具栏：数据源切换(硬件/文件) | 开始/停止测量 | 文件选择
 *   - 大画布：QGraphicsScene 绘制流程拓扑
 *     数据源 → 通道 → DBC数据库 → [Trace, Graphic, Data, 录制]
 *   - 每个块可点击切换启用/禁用，双击可配置
 *   - 模块块内展示已打开的实例列表，单击实例跳转对应标签页
 *   - 右键菜单支持增删通道块和模块实例
 */
class MeasurementSetupView : public QWidget
{
    Q_OBJECT

public:
    enum class Source { Hardware, File };

    explicit MeasurementSetupView(QWidget *parent = nullptr);

    QSize sizeHint() const override { return {900, 600}; }
    Source currentSource() const { return m_source; }
    QString filePath() const { return m_filePath; }
    QString activeSourceId() const;  ///< 返回当前活跃数据源块 ID ("source_real" 或 "source_file")

    /// 设置当前已加载的 DBC 文件列表（用于右键菜单显示）
    void setDbcFiles(const QStringList &files) { m_dbcFiles = files; }
    /// 设置最近打开的文件列表
    void setRecentFiles(const QStringList &files) { m_recentFiles = files; }

    /// 添加模块实例（由 MainWindow 在创建新标签页后调用）
    void addModuleInstance(const QString &moduleName, const QString &instanceId, const QString &title);
    /// 移除模块实例（由 MainWindow 在关闭标签页后调用）
    void removeModuleInstance(const QString &moduleName, const QString &instanceId);

    /// CAN 硬件配置参数（参考 CANoe 硬件参数配置）
    struct CanHwConfig {
        int channel = 1;
        bool canFd = false;           // CAN FD 模式
        int arbBaudrate = 500000;     // 仲裁段波特率
        int dataBaudrate = 2000000;   // 数据段波特率（CAN FD）
        int samplePoint = 75;        // 采样点 (%)
        int sjw = 1;                  // 同步跳转宽度 (TQ)
        int tseg1 = 12;              // 时间段 1 (TQ)
        int tseg2 = 3;               // 时间段 2 (TQ)
        int dataSamplePoint = 75;    // 数据段采样点 (%)
        int dataSjw = 1;             // 数据段 SJW (TQ)
        int dataTseg1 = 12;          // 数据段 TSEG1 (TQ)
        int dataTseg2 = 3;           // 数据段 TSEG2 (TQ)
        int intervalMs = 5;          // 帧生成间隔 (ms)
    };

public slots:
    void setSource(Source src);
    void setFilePath(const QString &path);
    void onFrame(const class CanFrame &frame);

signals:
    void sourceChanged(int source);
    void fileBrowseRequested();
    void measurementToggled(bool running);
    void moduleToggled(const QString &blockId, const QString &moduleName, bool enabled);
    /// 请求打开/跳转模块实例（instanceId 为空表示新建）
    void moduleOpened(const QString &moduleName, const QString &instanceId);
    /// 请求关闭指定模块实例
    void moduleInstanceClosed(const QString &moduleName, const QString &instanceId);
    /// 请求选择 DBC 文件（由 MainWindow 弹出选择对话框）
    void dbcSelectRequested();
    /// 请求配置通道过滤条件
    void channelFilterRequested(const QString &channelId);
    /// Real 硬件参数变更
    void realConfigChanged(const MeasurementSetupView::CanHwConfig &config);

private:
    // ---- UI ----
    QToolBar *m_toolbar = nullptr;
    QGraphicsScene *m_scene = nullptr;
    QGraphicsView *m_view = nullptr;
    QLabel *m_statusLabel = nullptr;

    // 工具栏控件
    QToolButton *m_hwBtn = nullptr;
    QToolButton *m_fileBtn = nullptr;
    QAction *m_startAct = nullptr;
    QAction *m_stopAct = nullptr;
    QAction *m_browseAct = nullptr;

    // ---- 状态 ----
    Source m_source = Source::Hardware;
    bool m_running = false;
    QString m_filePath;
    QRectF m_switchRect;  ///< 数据源切换开关区域

    // ---- 记录文件列表 & REAL 设备 ----
    QStringList m_recentFiles;           ///< 最近打开的文件
    QStringList m_dbcFiles;              ///< 已加载的 DBC 文件名列表
    CanHwConfig m_hwConfig;              ///< 硬件参数
    QStringList m_loadedFiles;           ///< 已加载的回放文件列表

    // ---- 画布块 ----
public:
    /// 模块实例子项（模块块内的一个标签页对应项）
    struct InstanceItem {
        QString id;       ///< "trace1", "trace2", "graphic1"
        QString title;    ///< "Trace1", "Trace2"
        QRectF subRect;   ///< 在父块内的子区域（scene 坐标）
    };

    struct BlockItem {
        QString id;             ///< 唯一标识
        QString title;          ///< 显示标题
        QString icon;           ///< emoji 图标
        QString category;       ///< "source" | "channel" | "database" | "module"
        QString moduleName;     ///< 模块类型名（如 "trace"），用于区分独立块
        QRectF rect;            ///< 位置和大小
        bool enabled = true;    ///< 是否启用
        QColor color;           ///< 主题色
        QGraphicsRectItem *gfxItem = nullptr;
        QGraphicsTextItem *textItem = nullptr;
        QList<InstanceItem> instances;  ///< 模块块内的实例列表（仅 module 类别）
    };

    void rebuildScene();
private:
    QMap<QString, BlockItem> m_blocks;
    int m_nextChannelNum = 3;   ///< 下一个通道块的编号

    // ---- 连线 ----
    struct Connection {
        QString fromId;
        QString toId;
        QGraphicsPathItem *pathItem = nullptr;
    };
    QList<Connection> m_connections;

    // ---- 统计 ----
    int m_frameCount = 0;

    // ---- 方法 ----
    void setupUi();
    void buildTopology();
    void updateBlockVisual(const QString &id);
    void updateConnections();
    BlockItem *blockAt(const QPointF &scenePos);
    void toggleBlock(const QString &id);

    /// 查找点击位置所在的实例
    /// @param scenePos 场景坐标
    /// @param moduleId 输出：所属模块块 ID
    /// @param instanceId 输出：实例 ID
    /// @return true 如果点击了某个实例
    bool instanceAt(const QPointF &scenePos, QString &moduleId, QString &instanceId);

    // ---- 动态增删 ----
    void addChannelBlock();
    void removeChannelBlock(const QString &blockId);
    void removeModuleBlock(const QString &blockId);
    /// 重新排列所有模块块（Trace / Graphic / Data+Record 分行水平排列）
    void relayoutModuleBlocks();

    // 工具栏
    void onStartClicked();
    void onStopClicked();
    void onBrowseClicked();

    // 画布事件
    void onSceneClicked(const QPointF &scenePos);
    void onSceneDoubleClicked(const QPointF &scenePos);
    void onSceneRightClicked(const QPointF &scenePos);

    // ---- 右键菜单 ----
    QMenu *m_rightMenu = nullptr;
    void buildContextMenu(BlockItem *block, const QPointF &scenePos);
    void buildEmptyAreaMenu(const QPointF &scenePos);

    // ---- 右键弹窗对话框 ----
    void showFileConfigDialog();
    void showRealConfigDialog();
    void showChannelFilterDialog(const QString &channelId);
    void showDbcSelectDialog();
};

#endif // MEASUREMENTSETUPVIEW_H
