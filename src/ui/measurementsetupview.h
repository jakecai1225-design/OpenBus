#ifndef MEASUREMENTSETUPVIEW_H
#define MEASUREMENTSETUPVIEW_H

#include <QWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMap>
#include <QString>
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
 */
class MeasurementSetupView : public QWidget
{
    Q_OBJECT

public:
    enum class Source { Hardware, File };

    explicit MeasurementSetupView(QWidget *parent = nullptr);

    QSize sizeHint() const override { return {900, 600}; }
    Source currentSource() const { return m_source; }

    /// 设置当前已加载的 DBC 文件列表（用于右键菜单显示）
    void setDbcFiles(const QStringList &files) { m_dbcFiles = files; }
    /// 设置最近打开的文件列表
    void setRecentFiles(const QStringList &files) { m_recentFiles = files; }

public slots:
    void setSource(Source src);
    void setFilePath(const QString &path);
    void onFrame(const class CanFrame &frame);

signals:
    void sourceChanged(int source);
    void fileBrowseRequested();
    void measurementToggled(bool running);
    void moduleToggled(const QString &moduleName, bool enabled);
    /// 请求打开对应模块的标签页
    void moduleOpened(const QString &moduleName);
    /// 请求选择 DBC 文件（由 MainWindow 弹出选择对话框）
    void dbcSelectRequested();
    /// 请求配置通道过滤条件
    void channelFilterRequested(const QString &channelId);

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

    // ---- 记录文件列表 & REAL 设备 ----
    QStringList m_recentFiles;           ///< 最近打开的文件
    QStringList m_dbcFiles;              ///< 已加载的 DBC 文件名列表
    quint8 m_channel = 1;                ///< CAN 通道

    // ---- 画布块 ----
public:
    struct BlockItem {
        QString id;             ///< 唯一标识
        QString title;          ///< 显示标题
        QString icon;           ///< emoji 图标
        QString category;       ///< "source" | "channel" | "database" | "module"
        QRectF rect;            ///< 位置和大小
        bool enabled = true;    ///< 是否启用
        QColor color;           ///< 主题色
        QGraphicsRectItem *gfxItem = nullptr;
        QGraphicsTextItem *textItem = nullptr;
    };
private:
    QMap<QString, BlockItem> m_blocks;

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
    void rebuildScene();
    void updateBlockVisual(const QString &id);
    void updateConnections();
    BlockItem *blockAt(const QPointF &scenePos);
    void toggleBlock(const QString &id);

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

    // ---- 右键弹窗对话框 ----
    void showSourceConfigDialog();
    void showChannelFilterDialog(const QString &channelId);
    void showDbcSelectDialog();
};

#endif // MEASUREMENTSETUPVIEW_H
