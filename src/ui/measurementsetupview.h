#ifndef MEASUREMENTSETUPVIEW_H
#define MEASUREMENTSETUPVIEW_H

#include <QWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMap>
#include <QHash>
#include <QSet>
#include <QString>
#include <QList>
#include <QMenu>
#include <QAction>

class QGraphicsRectItem;
class QGraphicsTextItem;
class QGraphicsPathItem;
class QTimer;
class QToolBar;
class QAction;
class QToolButton;
class QLabel;

/**
 * @brief CANoe Measurement Setup 风格的可视化测量配置画布
 *
 * 以标签页形式展示，包含：
 *   - 工具栏：数据源切换 (硬件/文件) | 开始/停止测量 | 文件选择
 *   - 大画布：QGraphicsScene 绘制流程拓扑
 *     数据源 → Filter 过滤（多 CAN 通道块收编为单块）→ DBC 数据库
 *     → [Trace, Graphic, Watcher 观测，录制]
 *   - 块交互：未启用块单击 = 启用；已启用块单击/双击 = 进入配置；
 *     右键菜单 = 配置 / 启停 / 增删（数据流过滤统一在 Filter 块配置）
 *   - 模块块内展示已打开的实例列表，单击实例跳转对应标签页
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
    /// 清除所有 Trace/Graphic 实例块（切换工程时调用）
    void clearTraceGraphicInstances();

    /// Query module block enable (default true if unknown)
    bool isBlockEnabled(const QString &blockId) const;
    /// All blockId → enabled (for project snapshot)
    QHash<QString, bool> blockEnabledMap() const;
    /// Set block enable (source real/file excluded; emits moduleToggled)
    void setBlockEnabled(const QString &id, bool enabled);

    /// Mark/clear block runtime error (error blinks red; clears when recovered.
    /// Applies to source_real and function blocks)
    void setBlockError(const QString &blockId, bool on);

public slots:
    void setSource(Source src);
    void setFilePath(const QString &path);
    /// 复位启停按钮状态（离线回放结束/未真正启动时由壳经 flow 模块调用；
    /// 纯状态设置，不发 measurementToggled）
    void setRunning(bool running);
    void onFrame(const class CanFrame &frame);

signals:
    void sourceChanged(int source);
    void fileBrowseRequested();
    void measurementToggled(bool running);
    /// Clear Trace/Graphic and restart offline (or hardware) measurement from the beginning.
    void measurementReplayRequested();
    void moduleToggled(const QString &blockId, const QString &moduleName, bool enabled);
    /// 请求打开/跳转模块实例（instanceId 为空表示新建）
    void moduleOpened(const QString &moduleName, const QString &instanceId);
    /// 请求关闭指定模块实例
    void moduleInstanceClosed(const QString &moduleName, const QString &instanceId);
    /// 请求选择 DBC 文件（由 MainWindow 弹出选择对话框）
    void dbcSelectRequested();
    /// 请求卸载指定 DBC 文件（由 MainWindow 调用 DbcManager::unloadDbc）
    void dbcRemoveRequested(const QString &fileName);
    /// Filter 块过滤规则变更（规则摘要列表；通道块收编后替代 channelFilterRequested）
    void filterRulesChanged(const QStringList &rules);
    /// 请求跳转到设备连接界面（点击 Real 块时触发）
    void realBlockClicked();
    /// 请求跳转到离线分析标签页（双击离线分析块时触发）
    void fileBlockClicked();
    /// Open signal send page (double-click Signal Generator)
    void sendPageOpened();
    /// Open file playback page (double-click File Playback)
    void playbackPageOpened();

public slots:
    // ---- 右键弹窗对话框（需要外部调用） ----
    void showFileConfigDialog();
    void showFilterConfigDialog();
    void showDbcSelectDialog();
    /// Filter 块当前规则摘要列表（块内实例行标题）
    QStringList filterRules() const;
    void clearFilterRules();
    /// Replace filter rule titles on the Filter block (project restore).
    void setFilterRules(const QStringList &rules);

private:
    // ---- UI ----
    QToolBar *m_toolbar = nullptr;
    QGraphicsScene *m_scene = nullptr;
    QGraphicsView *m_view = nullptr;

    // 工具栏控件
    QToolButton *m_hwBtn = nullptr;
    QToolButton *m_fileBtn = nullptr;
    // Action / button pointers for Start / Replay / Stop (canvas overlay)
    QAction *m_startAct = nullptr;
    QAction *m_replayAct = nullptr;
    QAction *m_stopAct = nullptr;
    QToolButton *m_startBtn = nullptr;
    QToolButton *m_replayBtn = nullptr;
    QToolButton *m_stopBtn = nullptr;
    QAction *m_browseAct = nullptr;

    // ---- 状态 ----
    Source m_source = Source::Hardware;
    bool m_running = false;
    QString m_filePath;
    QRectF m_switchRect;  ///< 数据源切换开关区域
    
    // ---- 双击判断 ----
    qint64 m_lastClickTime = 0;          ///< 上次点击时刻（毫秒）
    static constexpr qint64 DOUBLE_CLICK_INTERVAL = 300;  ///< 双击时间窗口 (ms)

    // ---- 数据流/异常指示（功能块状态灯） ----
    QTimer *m_blinkTimer = nullptr;  ///< 灯闪烁相位驱动（500ms，仅有流/异常时运转）
    bool m_blinkOn = false;          ///< 当前闪烁相位（true = 亮）
    qint64 m_lastFrameMs = 0;        ///< 最近一帧到达时刻（数据流活跃判定）
    QSet<QString> m_blockErrors;    ///< 处于异常态的块 ID 集合

    // ---- 记录文件列表 & REAL 设备 ----
    QStringList m_recentFiles;           ///< 最近打开的文件
    QStringList m_dbcFiles;              ///< 已加载的 DBC 文件名列表
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
        QString category;       ///< "source" | "filter" | "database" | "module"
        QString moduleName;     ///< 模块类型名（如 "trace"），用于区分独立块
        /// M1 预埋：块所属协议身份（doc/flow.md §十三）——多协议就绪前恒为 "can"；
        /// 多协议画布落地后由建块的协议角色写入，驱动 Parser/Adapter 管线选择
        QString protocolId = QStringLiteral("can");
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

    // ---- 连线 ----
    struct Connection {
        QString fromId;
        QString toId;
        QGraphicsPathItem *pathItem = nullptr;
    };
    QList<Connection> m_connections;

    // ---- 方法 ----
    void setupUi();
    void buildTopology();
    void updateBlockVisual(const QString &id);
    /// 全量刷新功能块指示灯（未使能=不亮；使能待命=常亮绿；
    /// 数据流活跃=绿闪；异常=红闪；数据源块仅异常亮灯）
    void updateBlockLamps();
    void updateConnections();
    BlockItem *blockAt(const QPointF &scenePos);
    void toggleBlock(const QString &id);
    /// 块配置统一入口（已启用块单击/双击/右键「配置」共用）
    void openBlockConfig(const QString &blockId);

    /// 查找点击位置所在的实例
    /// @param scenePos 场景坐标
    /// @param moduleId 输出：所属模块块 ID
    /// @param instanceId 输出：实例 ID
    /// @return true 如果点击了某个实例
    bool instanceAt(const QPointF &scenePos, QString &moduleId, QString &instanceId);

    // ---- 动态增删 ----
    void removeModuleBlock(const QString &blockId);
    /// 重新排列所有模块块（Trace / Graphic / Watcher+Record 分行水平排列）
    void relayoutModuleBlocks();

    // ---- 工具栏按钮槽 ----
    void onStartClicked();
    void onReplayClicked();
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
};

#endif // MEASUREMENTSETUPVIEW_H
