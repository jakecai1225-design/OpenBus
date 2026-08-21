#ifndef SIDEBARPANELS_H
#define SIDEBARPANELS_H

#include <QWidget>
#include <QStackedWidget>
#include <QList>
#include <QVBoxLayout>
#include <QColor>
#include <QRectF>

#include "core/marketmodel.h" // MarketItem / FrameRow / MarketEntryData（§13.10，B5 迁 openbus_data）

class QTreeWidget;
class QTreeWidgetItem;
class QListWidget;
class QListWidgetItem;
class QScrollArea;
class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QLineEdit;
class QToolButton;
class DbcManager;
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
//  Trace 面板 — 形态模板平铺 + 已打开实例列表
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
    void onTemplateClicked(QListWidgetItem *item);
    void onPageSelected(int row);
    void onDeleteTrace();
    void onContextMenu(const QPoint &pos);

private:
    QListWidget *m_templateList = nullptr;  ///< 形态模板平铺行（doc/flow.md §7.2 平铺修订）
    QListWidget *m_traceList;
    QPushButton *m_delBtn = nullptr;
};

// ============================================================
//  Graphic 配置面板 — 形态模板平铺 + 已打开页面列表
// ============================================================
class GraphicConfigPanel : public SidePanel
{
    Q_OBJECT
public:
    explicit GraphicConfigPanel(QWidget *parent = nullptr);

    void refreshList(const QStringList &names);

signals:
    void graphicPageSelected(int index);
    void newGraphicRequested();
    /// 请求删除指定行对应的 Graphic 实例
    void graphicDeleteRequested(int row);

private slots:
    void onTemplateClicked(QListWidgetItem *item);
    void onPageSelected(int row);
    void onDeleteGraphic();
    void onContextMenu(const QPoint &pos);

private:
    QListWidget *m_templateList = nullptr;  ///< 形态模板平铺行（doc/flow.md §7.2 平铺修订）
    QListWidget *m_pageList;
    QPushButton *m_delBtn = nullptr;
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

public slots:
    /// 刷新设备列表（调用 CanDeviceManager::enumerateDevices；DEF-08 字符串槽）
    void refreshDevices();

signals:
    /// 请求打开设备连接标签页
    /// @param deviceKind 0=模拟器, 1=ZLG
    /// @param devIndex 设备序号
    /// @param deviceName 设备显示名称
    /// @param deviceType 厂商设备子类型（如 ZLG DEV_USBCANFD_200U=41）
    void deviceOpenRequested(int deviceKind, int devIndex, const QString &deviceName, int deviceType);

    /// 请求打开「新增设备」标签页（设备市场：搜索/详情/安装驱动）
    void addDeviceRequested();

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
//  收发面板 — 发送 / 回放 / 离线分析 / 录制 入口
// ============================================================
class TransceivePanel : public SidePanel
{
    Q_OBJECT
public:
    explicit TransceivePanel(QWidget *parent = nullptr);

signals:
    void openSendRequested();
    void openPlaybackRequested();
    void openOfflineAnalysisRequested();
    void openRecordRequested();

private slots:
    void onSendClicked();
    void onPlaybackClicked();
    void onOfflineAnalysisClicked();
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

private slots:
    void onItemClicked(QListWidgetItem *item);

private:
    QListWidget *m_list;
};

// ============================================================
//  分析配置面板 — 协议流模板平铺（doc/flow.md §7.2）
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
    void onTemplateClicked(QListWidgetItem *item);
    /// 重灌模板行：注册表适配器变化 / 主题切换（DEF-08 字符串槽经 SignalRelay 桥接）
    void rebuildTemplates();

private:
    QListWidget *m_templateList = nullptr;  ///< 协议流模板平铺行
};

// ============================================================
//  扩展面板 — 迷你市场（与插件市场页同数据源同行风格，方案 §13.10）
//  搜索栏 + 右上角 "…" 菜单（离线安装 .odp/.opk / 打开市场页 / 刷新）
//  + 三分组条目（已安装 = 驱动+插件 / 驱动市场 / 插件市场，点击跳
//  市场页定位详情）+ 行内齿轮菜单（启停/启禁/卸载）+ 命令列表
// ============================================================
class ExtensionsPanel : public SidePanel
{
    Q_OBJECT
public:
    explicit ExtensionsPanel(QWidget *parent = nullptr);

public slots:
    /// 重新聚合四源并重建三分组（数据源信号已自动连接；DEF-08 字符串槽）
    void refreshEntries();

public:
    void addCommand(const QString &id, const QString &title);
    void clearCommands();

signals:
    void commandTriggered(const QString &id);
    /// 行点击 → 请求打开插件市场页并定位该条目详情
    void itemActivated(const MarketItem &item);
    void pluginToggleRequested(const QString &name, bool enable);  // 启用/禁用
    void pluginActivated(const QString &name);                     // 启动（重启）
    void pluginDeactivateRequested(const QString &name);           // 停止
    void pluginUninstallRequested(const QString &name);            // 卸载
    void driverToggleRequested(const QString &driverId, bool enable);  // 禁用/启用
    void driverUninstallRequested(const QString &driverId);            // 卸载
    void installFromFileRequested();   // "…" 菜单：离线安装 .odp / .opk
    void openMarketRequested();        // "…" 菜单：打开插件市场页

private slots:
    void onSearchChanged();
    void onMenuClicked();   // "…" 按钮
    void onCommandClicked(QListWidgetItem *item);

private:
    QLineEdit *m_searchEdit;
    QToolButton *m_menuBtn;
    QScrollArea *m_listArea = nullptr;
    QVBoxLayout *m_listLay = nullptr;    // 三分组条目（尾 stretch）
    QLabel *m_cmdHeader = nullptr;       // 命令分组标题（无命令时隐藏）
    QListWidget *m_cmdList = nullptr;    // 插件命令入口

    void rebuild();                      // 依据搜索词 + 四源重建三分组
    FrameRow *makeRow(const MarketEntryData &e);
    void addSectionLabel(const QString &title);
    void showGearMenu(const MarketEntryData &e, const QPoint &globalPos);
};

// ============================================================
//  SideBar — 侧边栏容器（QStackedWidget 切换面板）
//  索引必须与 ActivityBar::Activity 枚举一致
//  0=Project 1=Analysis(Flow) 2=Device 3=Trace 4=Graphic
//  5=Dbc 6=Transceive 7=Extensions 8=Settings
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
    MeasurementSetupPanel *analysisPanel() const { return m_analysis; }
    ExtensionsPanel *extensionsPanel() const { return m_extensions; }
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
    MeasurementSetupPanel *m_analysis;
    ExtensionsPanel *m_extensions;
    SettingsPanel *m_settings;
    int m_lastIndex = 0;
};

#endif // SIDEBARPANELS_H
