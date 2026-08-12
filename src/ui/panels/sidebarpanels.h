#ifndef SIDEBARPANELS_H
#define SIDEBARPANELS_H

#include <QWidget>
#include <QStackedWidget>
#include <QList>
#include <QVBoxLayout>
#include <QColor>
#include <QRectF>

class QTreeWidget;
class QTreeWidgetItem;
class QListWidget;
class QListWidgetItem;
class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class DbcManager;
class GraphicView;
class CanSimulator;

// ============================================================
//  基类 — 所有侧边面板共用的标题栏样式
// ============================================================
class SidePanel : public QWidget
{
    Q_OBJECT
public:
    explicit SidePanel(const QString &title, QWidget *parent = nullptr);

protected:
    void setupTitle(const QString &title);
    QVBoxLayout *contentLayout() { return m_contentLayout; }

private:
    QVBoxLayout *m_contentLayout = nullptr;
};

// ============================================================
//  工程上下文数据
// ============================================================
struct ProjectContext
{
    QString name;
    QString filePath;     ///< 工程文件路径
    QStringList dbcFiles;
    QStringList recordFiles;
    QString layoutConfig;
    QString stateJson;    ///< 工程状态 JSON 快照（未保存到文件时用于切换恢复）
};

// ============================================================
//  工程面板 — 项目列表入口
// ============================================================
class ProjectPanel : public SidePanel
{
    Q_OBJECT
public:
    explicit ProjectPanel(QWidget *parent = nullptr);

    const QList<ProjectContext> &projects() const { return m_projects; }
    QList<ProjectContext> &projectsRef() { return m_projects; }
    int currentIndex() const { return m_currentIndex; }
    void refreshList();  ///< 刷新工程列表 UI

signals:
    void projectSwitched(int index);
    void projectCreated(const QString &name);
    void openProjectRequested(const QString &filePath);
    void saveProjectRequested(const QString &filePath);
    void filePreviewRequested(const QString &filePath);  ///< 请求打开文件预览标签页

private slots:
    void onNewProject();
    void onSaveProject();
    void onDeleteProject();
    void onProjectItemClicked(QTreeWidgetItem *item, int column);
    void onProjectItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onOpenProject();
    void onOpenRecent();

private:
    QTreeWidget *m_projectTree;
    QListWidget *m_recentList = nullptr;
    QList<ProjectContext> m_projects;
    int m_currentIndex = -1;
    void refreshRecentList();
    /// 从 stateJson 解析关键文件信息列表
    QStringList extractDbcFiles(const QString &stateJson) const;
    QStringList extractRecordFiles(const QString &stateJson) const;
    QString extractPlaybackFile(const QString &stateJson) const;
};

// ============================================================
// 数据库面板 — 多协议解析文件管理（DBC / EDS / DCF / LDF / ARXML 等）
// 按协议类别以树形结构展示：CAN/CANFD · CANopen · EtherCAT · LIN · J1939 · AUTOSAR
// ============================================================
class DbcPanel : public SidePanel
{
    Q_OBJECT
public:
    explicit DbcPanel(QWidget *parent = nullptr);

    void setDbcManager(DbcManager *mgr);

    // 非协议文件信息
    struct DatabaseEntry {
        QString fileName;
        QString filePath;
        QString category;   // "CAN/CANFD", "CANopen", "EtherCAT", "LIN", "J1939", "AUTOSAR"
    };

signals:
    void dbcFileClicked(const QString &fileName);
    void databaseFileClicked(const QString &category, const QString &fileName);
    /// 请求卸载 DBC 文件（由 MainWindow 处理，清理关联标签页等）
    void dbcRemoveRequested(const QString &filePath);

private slots:
    void onImportDatabase();
    void onRemoveDatabase();
    void onItemClicked(QTreeWidgetItem *item, int column);

private:
    QTreeWidget *m_tree;
    DbcManager *m_dbcMgr = nullptr;
    QList<DatabaseEntry> m_otherDbs;  // 非 DBC 文件列表

    // 协议分类根节点
    QTreeWidgetItem *m_catCanFd    = nullptr;
    QTreeWidgetItem *m_catCanopen  = nullptr;
    QTreeWidgetItem *m_catEthercat = nullptr;
    QTreeWidgetItem *m_catLin      = nullptr;
    QTreeWidgetItem *m_catJ1939    = nullptr;
    QTreeWidgetItem *m_catAutosar  = nullptr;

    static QString categoryForFile(const QString &fileName);
    void initCategoryNodes();
    void refreshTree();
};

// ============================================================
//  Trace 面板 — Trace 标签页列表 + 新建按钮
// ============================================================
class TracePanel : public SidePanel
{
    Q_OBJECT
public:
    explicit TracePanel(QWidget *parent = nullptr);

    void refreshList(const QStringList &names);

signals:
    void openTraceRequested();
    void tracePageSelected(int row);
    /// 请求删除指定行对应的 Trace 实例
    void traceDeleteRequested(int row);

private slots:
    void onTraceClicked();
    void onPageSelected(int row);
    void onDeleteTrace();
    void onContextMenu(const QPoint &pos);

private:
    QListWidget *m_traceList;
    QPushButton *m_delBtn = nullptr;
};

// ============================================================
//  Graphic 配置面板 — Graphic 页面列表
// ============================================================
class GraphicConfigPanel : public SidePanel
{
    Q_OBJECT
public:
    explicit GraphicConfigPanel(QWidget *parent = nullptr);

    void setGraphicView(GraphicView *view);
    void refreshList(const QStringList &names);

signals:
    void graphicPageSelected(int index);
    void newGraphicRequested();
    /// 请求删除指定行对应的 Graphic 实例
    void graphicDeleteRequested(int row);

private slots:
    void onNewGraphic();
    void onPageSelected(int row);
    void onDeleteGraphic();
    void onContextMenu(const QPoint &pos);

private:
    QListWidget *m_pageList;
    QPushButton *m_delBtn = nullptr;
    GraphicView *m_graphicView = nullptr;
};

// ============================================================
//  设备连接面板 — 仅显示设备系列树（点击跳转标签页配置）
// ============================================================

class QTreeWidget;
class QTreeWidgetItem;
class CanDeviceManager;

class DevicePanel : public SidePanel
{
    Q_OBJECT
public:
    explicit DevicePanel(QWidget *parent = nullptr);

    void setSimulator(CanSimulator *sim);
    void setDeviceManager(CanDeviceManager *mgr);
    /// 刷新设备列表（调用 CanDeviceManager::enumerateDevices）
    void refreshDevices();

signals:
    /// 请求打开设备连接标签页
    /// @param deviceKind 0=模拟器, 1=ZLG
    /// @param devIndex 设备序号
    /// @param deviceName 设备显示名称
    /// @param deviceType 厂商设备子类型（如 ZLG DEV_USBCANFD_200U=41）
    void deviceOpenRequested(int deviceKind, int devIndex, const QString &deviceName, int deviceType);

private slots:
    void onItemClicked(QTreeWidgetItem *item, int column);
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onScanClicked();

private:
    QTreeWidget *m_deviceTree;
    QPushButton *m_scanBtn = nullptr;
    CanSimulator *m_simulator = nullptr;
    CanDeviceManager *m_deviceMgr = nullptr;

    void populateTree();
};

// ============================================================
//  收发面板 — 发送 / 回放 / 录制 三个入口
// ============================================================
class TransceivePanel : public SidePanel
{
    Q_OBJECT
public:
    explicit TransceivePanel(QWidget *parent = nullptr);

signals:
    void openSendRequested();
    void openPlaybackRequested();
    void openRecordRequested();

private slots:
    void onSendClicked();
    void onPlaybackClicked();
    void onRecordClicked();
};

// ============================================================
//  配置面板 — 设置入口
// ============================================================
class SettingsPanel : public SidePanel
{
    Q_OBJECT
public:
    explicit SettingsPanel(QWidget *parent = nullptr);

signals:
    void settingsRequested(const QString &section);
    void themeChanged(const QString &themeName);

private slots:
    void onItemClicked(QListWidgetItem *item);
    void onThemeItemClicked(QListWidgetItem *item);

private:
    QListWidget *m_list;
    QListWidget *m_themeList;
};

// ============================================================
//  协议面板 — 上层协议列表（UDS/CANopen/J1939/ISO-TP 等）
// ============================================================
class ProtocolPanel : public SidePanel
{
    Q_OBJECT
public:
    explicit ProtocolPanel(QWidget *parent = nullptr);

signals:
    void protocolOpened(const QString &protocolName);

private slots:
    void onItemClicked(QListWidgetItem *item);

private:
    QListWidget *m_list;
};

// ============================================================
//  分析配置面板 — 侧边栏入口（点击打开 flow 标签页）
// ============================================================
class MeasurementSetupPanel : public SidePanel
{
    Q_OBJECT
public:
    explicit MeasurementSetupPanel(QWidget *parent = nullptr);

signals:
    /// 请求打开 flow 标签页
    void openMeasurementSetupRequested();

private slots:
    void onItemClicked(QListWidgetItem *item);

private:
    QListWidget *m_list;
};

// ============================================================
//  工具集面板 — 总线分析工具列表入口
// ============================================================
class ToolsPanel : public SidePanel
{
    Q_OBJECT
public:
    explicit ToolsPanel(QWidget *parent = nullptr);

signals:
    /// 请求打开工具标签页，toolKey 为工具唯一标识
    void toolOpened(const QString &toolKey);

private slots:
    void onItemClicked(QListWidgetItem *item);

private:
    QListWidget *m_list;
};

// ============================================================
//  SideBar — 侧边栏容器（QStackedWidget 切换面板）
//  索引必须与 ActivityBar::Activity 枚举一致
//  0=Project 1=Analysis(Flow) 2=Device 3=Trace 4=Graphic
//  5=Dbc 6=Transceive 7=Protocol 8=Tools 9=Settings
// ============================================================
class SideBar : public QStackedWidget
{
    Q_OBJECT
public:
    explicit SideBar(QWidget *parent = nullptr);

    ProjectPanel *projectPanel() const { return m_project; }
    TracePanel *tracePanel() const { return m_trace; }
    GraphicConfigPanel *graphicConfigPanel() const { return m_graphicConfig; }
    DbcPanel *dbcPanel() const { return m_dbc; }
    TransceivePanel *transceivePanel() const { return m_transceive; }
    DevicePanel *devicePanel() const { return m_device; }
    ProtocolPanel *protocolPanel() const { return m_protocol; }
    MeasurementSetupPanel *analysisPanel() const { return m_analysis; }
    ToolsPanel *toolsPanel() const { return m_tools; }
    SettingsPanel *settingsPanel() const { return m_settings; }

    void showPanel(int index);
    void togglePanel(int index);

private:
    ProjectPanel *m_project;
    TracePanel *m_trace;
    GraphicConfigPanel *m_graphicConfig;
    DbcPanel *m_dbc;
    TransceivePanel *m_transceive;
    DevicePanel *m_device;
    ProtocolPanel *m_protocol;
    MeasurementSetupPanel *m_analysis;
    ToolsPanel *m_tools;
    SettingsPanel *m_settings;
    int m_lastIndex = 0;
};

#endif // SIDEBARPANELS_H
