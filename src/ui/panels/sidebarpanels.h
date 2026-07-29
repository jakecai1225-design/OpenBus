#ifndef SIDEBARPANELS_H
#define SIDEBARPANELS_H

#include <QWidget>
#include <QStackedWidget>
#include <QList>
#include <QVBoxLayout>

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
    QStringList dbcFiles;
    QStringList recordFiles;
    QString layoutConfig;
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
    int currentIndex() const { return m_currentIndex; }

signals:
    void projectSwitched(int index);
    void projectCreated(const QString &name);

private slots:
    void onNewProject();
    void onSaveProject();
    void onDeleteProject();
    void onProjectSelected(int row);

private:
    QListWidget *m_projectList;
    QList<ProjectContext> m_projects;
    int m_currentIndex = -1;
    void refreshList();
};

// ============================================================
// DBC 面板 — DBC 文件列表（点击在右侧标签页展开详情）
// ============================================================
class DbcPanel : public SidePanel
{
    Q_OBJECT
public:
    explicit DbcPanel(QWidget *parent = nullptr);

    void setDbcManager(DbcManager *mgr);

signals:
    void dbcFileClicked(const QString &fileName);

private slots:
    void onImportDbc();
    void onItemClicked(QTreeWidgetItem *item, int column);

private:
    QTreeWidget *m_tree;
    DbcManager *m_dbcMgr = nullptr;
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

private slots:
    void onTraceClicked();
    void onPageSelected(int row);

private:
    QListWidget *m_traceList;
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

private slots:
    void onNewGraphic();
    void onPageSelected(int row);

private:
    QListWidget *m_pageList;
    GraphicView *m_graphicView = nullptr;
};

// ============================================================
//  设备面板 — 仅设备连接
// ============================================================
class DevicePanel : public SidePanel
{
    Q_OBJECT
public:
    explicit DevicePanel(QWidget *parent = nullptr);

    void setSimulator(CanSimulator *sim);

signals:
    void deviceConnectRequested(const QString &device, int baudrate);
    void deviceDisconnectRequested();

private slots:
    void onConnect();
    void onDisconnect();

private:
    QComboBox *m_deviceCombo;
    QComboBox *m_baudCombo;
    QComboBox *m_channelCombo;
    QComboBox *m_fdCombo;
    QPushButton *m_connectBtn;
    QPushButton *m_disconnectBtn;
    QLabel *m_statusLabel;
    CanSimulator *m_simulator = nullptr;
};

// ============================================================
//  发送面板 — 仅入口
// ============================================================
class SendPanel : public SidePanel
{
    Q_OBJECT
public:
    explicit SendPanel(QWidget *parent = nullptr);

signals:
    void openSendRequested();
    void openPlaybackRequested();

private slots:
    void onSendClicked();
    void onPlaybackClicked();
};

// ============================================================
//  录制面板 — 仅入口
// ============================================================
class RecordPanel : public SidePanel
{
    Q_OBJECT
public:
    explicit RecordPanel(QWidget *parent = nullptr);

signals:
    void openRecordRequested();

private slots:
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
//  SideBar — 侧边栏容器（QStackedWidget 切换面板）
//  索引必须与 ActivityBar::Activity 枚举一致
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
    SendPanel *sendPanel() const { return m_send; }
    RecordPanel *recordPanel() const { return m_record; }
    DevicePanel *devicePanel() const { return m_device; }
    SettingsPanel *settingsPanel() const { return m_settings; }

    void showPanel(int index);
    void togglePanel(int index);

private:
    ProjectPanel *m_project;
    TracePanel *m_trace;
    GraphicConfigPanel *m_graphicConfig;
    DbcPanel *m_dbc;
    SendPanel *m_send;
    RecordPanel *m_record;
    DevicePanel *m_device;
    SettingsPanel *m_settings;
    int m_lastIndex = 0;
};

#endif // SIDEBARPANELS_H
