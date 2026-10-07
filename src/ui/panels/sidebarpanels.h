#ifndef SIDEBARPANELS_H
#define SIDEBARPANELS_H

#include <QWidget>
#include <QStackedWidget>
#include <QList>
#include <QVBoxLayout>
#include <QHBoxLayout>
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
class QHBoxLayout;
class QEvent;
class QShowEvent;
class QEnterEvent;
class QMouseEvent;
class QLabel;
class QIcon;
class DbcManager;
class CanSimulator;

// ============================================================
//  VS Code–style list/tree row with trailing hover actions
// ============================================================
class ExplorerItemRow : public QWidget
{
    Q_OBJECT
public:
    explicit ExplorerItemRow(const QString &text, QWidget *parent = nullptr);

    void setText(const QString &text);
    void setLeadingIcon(const QIcon &icon);
    QToolButton *addAction(const QString &iconPath, const QString &tooltip,
                           int iconSize = 12);
    void setActionsVisible(bool on);
    void refreshTheme();

signals:
    void activated();

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void updateActionVisibility();

    QLabel *m_iconLabel = nullptr;
    QLabel *m_textLabel = nullptr;
    QWidget *m_actionsHost = nullptr;
    QHBoxLayout *m_actionsLay = nullptr;
    QList<QToolButton *> m_actions;
    QStringList m_actionIconPaths;
    bool m_hovered = false;
};

/// Buttons-only host for QTreeWidget action column (hover-visible).
class ExplorerItemActions : public QWidget
{
    Q_OBJECT
public:
    explicit ExplorerItemActions(QWidget *parent = nullptr);

    QToolButton *addAction(const QString &iconPath, const QString &tooltip,
                           int iconSize = 12);
    void setActionsVisible(bool on);
    void refreshTheme();

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QHBoxLayout *m_lay = nullptr;
    QList<QToolButton *> m_actions;
    QStringList m_actionIconPaths;
    bool m_hovered = false;
};

// ============================================================
//  基类 — 所有侧边面板共用的标题栏样式
// ============================================================
class SidePanel : public QWidget
{
    Q_OBJECT
public:
    explicit SidePanel(const QString &title, QWidget *parent = nullptr);
    void setTitle(const QString &title);

protected:
    void setupTitle(const QString &title);
    QVBoxLayout *contentLayout() { return m_contentLayout; }

private:
    QVBoxLayout *m_contentLayout = nullptr;
    QLabel *m_titleLabel = nullptr;
};

// ============================================================
//  VS Code–style collapsible explorer section (twistie + body)
// ============================================================
class ExplorerSection : public QWidget
{
    Q_OBJECT
public:
    explicit ExplorerSection(const QString &title, QWidget *parent = nullptr);

    void setTitle(const QString &title);
    QString title() const;
    QVBoxLayout *bodyLayout() const { return m_bodyLayout; }
    QWidget *bodyWidget() const { return m_body; }

    bool isExpanded() const { return m_expanded; }
    void setExpanded(bool expanded);
    void refreshTheme();
    /// Trailing green status dot (plain title + green ● only)
    void setStatusDotVisible(bool on);

    /// Icon-only action on the header trailing edge.
    /// Visible while the section is expanded; hidden when collapsed.
    QToolButton *addHeaderAction(const QString &iconPath, const QString &tooltip,
                                 int iconSize = 14);

    /// Re-pack sibling ExplorerSections like VS Code views:
    /// all collapsed → top; expanded share middle; trailing collapsed → bottom.
    void rebalanceSiblings();

signals:
    void expandedChanged(bool expanded);

public slots:
    void toggle();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void updateHeaderChrome();
    void applyExpandPolicy();
    void updateHeaderActionsVisibility();
    void refreshActionIcons();

    QWidget *m_headerRow = nullptr;
    QHBoxLayout *m_headerLay = nullptr;
    QToolButton *m_header = nullptr;
    QLabel *m_statusDot = nullptr;
    QWidget *m_actionsHost = nullptr;
    QHBoxLayout *m_actionsLay = nullptr;
    QList<QToolButton *> m_actions;
    QStringList m_actionIconPaths;
    QWidget *m_body = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;
    bool m_expanded = true;
};

// ============================================================
//  Project context
// ============================================================
struct ProjectContext
{
    QString name;
    QString filePath;     ///< project file path
    QStringList dbcFiles;
    QStringList recordFiles;
    QString layoutConfig;
    QString stateJson;    ///< state snapshot when unsaved
};

// ============================================================
//  Project panel — explorer sections: active project + recent
// ============================================================
class ProjectPanel : public SidePanel
{
    Q_OBJECT
public:
    explicit ProjectPanel(QWidget *parent = nullptr);

    const QList<ProjectContext> &projects() const { return m_projects; }
    QList<ProjectContext> &projectsRef() { return m_projects; }
    int currentIndex() const { return m_currentIndex; }
    void refreshList();  ///< refresh project tree UI
    /// Activate / insert project by path and sync display name (Recent / Open)
    void activateProject(const QString &filePath, const QString &name = QString());

signals:
    void projectSwitched(int index);
    void projectCreated(const QString &name);
    void openProjectRequested(const QString &filePath);
    void saveProjectRequested(const QString &filePath);
    void filePreviewRequested(const QString &filePath);

private slots:
    void onNewProject();
    void onSaveProject();
    void onDeleteProject();
    void onProjectItemClicked(QTreeWidgetItem *item, int column);
    void onProjectItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onProjectTreeContextMenu(const QPoint &pos);
    void onRecentListContextMenu(const QPoint &pos);
    void onOpenProject();
    void onOpenRecent();

private:
    ExplorerSection *m_projectSection = nullptr;
    ExplorerSection *m_recentSection = nullptr;
    QTreeWidget *m_projectTree = nullptr;
    QListWidget *m_recentList = nullptr;
    QList<ProjectContext> m_projects;
    int m_currentIndex = -1;
    void refreshRecentList();
    void updateProjectSectionTitle();
    static void revealInFileManager(const QString &path);
    QString pathForTreeItem(QTreeWidgetItem *item) const;
    QStringList extractDbcFiles(const QString &stateJson) const;
    QStringList extractRecordFiles(const QString &stateJson) const;
    QStringList extractOfflineFiles(const QString &stateJson) const;
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
    ExplorerSection *m_dbSection = nullptr;
    QTreeWidget *m_tree = nullptr;
    DbcManager *m_dbcMgr = nullptr;
    QList<DatabaseEntry> m_otherDbs;
    QToolButton *m_importBtn = nullptr;
    QToolButton *m_removeBtn = nullptr;

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
    void removeDatabaseItem(QTreeWidgetItem *item);
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
    QListWidget *m_templateList = nullptr;
    QListWidget *m_traceList = nullptr;
    ExplorerSection *m_openedSection = nullptr;
    ExplorerSection *m_newSection = nullptr;
    QToolButton *m_delBtn = nullptr;
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
    /// Request deleting the Graphic page at the given list row
    void graphicDeleteRequested(int row);

private slots:
    void onTemplateClicked(QListWidgetItem *item);
    void onPageSelected(int row);
    void onContextMenu(const QPoint &pos);

private:
    QListWidget *m_templateList = nullptr;
    QListWidget *m_pageList = nullptr;
    ExplorerSection *m_openedSection = nullptr;
    ExplorerSection *m_newSection = nullptr;
    QToolButton *m_delBtn = nullptr;
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
    void onAddDeviceClicked();

private:
    ExplorerSection *m_devicesSection = nullptr;
    QTreeWidget *m_deviceTree = nullptr;
    QToolButton *m_scanBtn = nullptr;
    QToolButton *m_addBtn = nullptr;
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
    void retranslateUi();

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

private:
    QPushButton *m_sendBtn = nullptr;
    QPushButton *m_playbackBtn = nullptr;
    QPushButton *m_offlineBtn = nullptr;
    QPushButton *m_recordBtn = nullptr;
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

    /// 刷新「已打开」列表（壳 refreshPanelLists 收集 Flow 标签页喂入；
    /// VS Code 版式：已打开在上、新建模板与操作按钮在下）
    void refreshOpenList(const QStringList &names);

signals:
    /// 请求打开 flow 标签页
    void openMeasurementSetupRequested();

private slots:
    void onTemplateClicked(QListWidgetItem *item);
    /// 「已打开」行点击 → 打开/聚焦对应画布页（F1 多实例前 = 单画布）
    void onOpenedClicked(QListWidgetItem *item);
    /// 重灌模板行：注册表适配器变化 / 主题切换（DEF-08 字符串槽经 SignalRelay 桥接）
    void rebuildTemplates();

private:
    ExplorerSection *m_openedSection = nullptr;
    ExplorerSection *m_newSection = nullptr;
    QListWidget *m_openedList = nullptr;
    QListWidget *m_templateList = nullptr;
};

// ============================================================
//  Extensions panel — mini market (Installed / Running sections)
// ============================================================
class ExtensionsPanel : public SidePanel
{
    Q_OBJECT
public:
    explicit ExtensionsPanel(QWidget *parent = nullptr);

public slots:
    void refreshEntries();

public:
    /// Legacy no-ops (commands moved to gear menu / market tab).
    void addCommand(const QString &id, const QString &title);
    void clearCommands();

signals:
    void commandTriggered(const QString &id);  // unused; kept for signal ABI
    void itemActivated(const MarketItem &item);
    void pluginToggleRequested(const QString &name, bool enable);
    void pluginActivated(const QString &name);
    void pluginDeactivateRequested(const QString &name);
    void pluginUninstallRequested(const QString &name);
    void driverToggleRequested(const QString &driverId, bool enable);
    void driverUninstallRequested(const QString &driverId);
    void installFromFileRequested();
    void openMarketRequested();

private slots:
    void onSearchChanged();
    void onMenuClicked();

private:
    QLineEdit *m_searchEdit = nullptr;
    QToolButton *m_menuBtn = nullptr;
    QWidget *m_sectionsHost = nullptr;
    QVBoxLayout *m_listLay = nullptr;
    ExplorerSection *m_installedSection = nullptr;
    ExplorerSection *m_runningSection = nullptr;

    void rebuild();
    FrameRow *makeRow(const MarketEntryData &e);
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
