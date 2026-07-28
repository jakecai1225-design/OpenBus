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
class QSpinBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;
class DbcManager;
class GraphicView;
class CanSimulator;

// ============================================================
//  CollapsibleSection — 可折叠的分组容器
// ============================================================
class CollapsibleSection : public QWidget
{
    Q_OBJECT
public:
    explicit CollapsibleSection(const QString &title, QWidget *parent = nullptr);

    void setContent(QWidget *widget);
    void setExpanded(bool expanded);
    bool isExpanded() const { return m_expanded; }

private slots:
    void onToggle();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    QLabel *m_toggleBtn;
    QWidget *m_content;
    bool m_expanded = true;
};

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
//  工程面板 — 项目上下文管理器
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
    void fileOpenRequested(const QString &filePath);

private slots:
    void onNewProject();
    void onSaveProject();
    void onDeleteProject();
    void onProjectSelected(int row);
    void onItemDoubleClicked(QListWidgetItem *item);

private:
    QListWidget *m_projectList;
    QList<ProjectContext> m_projects;
    int m_currentIndex = -1;
    void refreshList();
};

// ============================================================
//  DBC 面板 — DBC 文件 + 信号树
// ============================================================
class DbcPanel : public SidePanel
{
    Q_OBJECT
public:
    explicit DbcPanel(QWidget *parent = nullptr);

    void setDbcManager(DbcManager *mgr);

signals:
    void signalDoubleClicked(quint32 canId, const QString &signalName);

private slots:
    void onImportDbc();
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);

private:
    QTreeWidget *m_tree;
    DbcManager *m_dbcMgr = nullptr;
    void refreshTree();
};

// ============================================================
//  Trace 配置面板（含回放控制折叠区）
// ============================================================
class TraceConfigPanel : public SidePanel
{
    Q_OBJECT
public:
    explicit TraceConfigPanel(QWidget *parent = nullptr);

    void setPlayerLoaded(bool loaded, bool playing);

signals:
    void columnsChanged();
    void filterPresetApplied(const QString &filter);
    void autoScrollChanged(bool enabled);
    void playRequested();
    void pauseRequested();
    void stopRequested();
    void speedChanged(double speed);
    void seekChanged(double ratio);   // 0.0 ~ 1.0

private:
    QCheckBox *m_chkTime, *m_chkCh, *m_chkDir, *m_chkId, *m_chkDlc, *m_chkData, *m_chkFlags;
    QListWidget *m_presets;
    QPushButton *m_playBtn, *m_pauseBtn, *m_stopBtn;
    QSlider *m_seekSlider;
    QComboBox *m_speedCombo;
};

// ============================================================
//  Graphic 配置面板
// ============================================================
class GraphicConfigPanel : public SidePanel
{
    Q_OBJECT
public:
    explicit GraphicConfigPanel(QWidget *parent = nullptr);

    void setGraphicView(GraphicView *view);

private slots:
    void onAddSignal();
    void onRemoveSignal();
    void onClearSignals();

private:
    GraphicView *m_graphicView = nullptr;
    QListWidget *m_signalList;
    void refreshList();
};

// ============================================================
//  设备连接面板（含录制控制折叠区）
// ============================================================
class DevicePanel : public SidePanel
{
    Q_OBJECT
public:
    explicit DevicePanel(QWidget *parent = nullptr);

    void setSimulator(CanSimulator *sim);
    void setRecording(bool recording);

signals:
    void deviceConnectRequested(const QString &device, int baudrate);
    void deviceDisconnectRequested();
    void recordToggled(bool on);
    void clearRequested();
    void autoScrollToggled(bool on);

private slots:
    void onConnect();
    void onDisconnect();
    void onRecord();
    void onClear();
    void onAutoScroll(int state);

private:
    QComboBox *m_deviceCombo;
    QComboBox *m_baudCombo;
    QComboBox *m_channelCombo;
    QPushButton *m_connectBtn;
    QPushButton *m_disconnectBtn;
    QLabel *m_statusLabel;
    CanSimulator *m_simulator = nullptr;

    // 录制控制
    QPushButton *m_recordBtn;
    QCheckBox *m_autoScrollChk;
};

// ============================================================
//  SideBar — 侧边栏容器（QStackedWidget 切换面板）
// ============================================================
class SideBar : public QStackedWidget
{
    Q_OBJECT
public:
    explicit SideBar(QWidget *parent = nullptr);

    ProjectPanel *projectPanel() const { return m_project; }
    DbcPanel *dbcPanel() const { return m_dbc; }
    TraceConfigPanel *traceConfigPanel() const { return m_traceConfig; }
    GraphicConfigPanel *graphicConfigPanel() const { return m_graphicConfig; }
    DevicePanel *devicePanel() const { return m_device; }

    void showPanel(int index);
    void togglePanel(int index);

private:
    ProjectPanel *m_project;
    DbcPanel *m_dbc;
    TraceConfigPanel *m_traceConfig;
    GraphicConfigPanel *m_graphicConfig;
    DevicePanel *m_device;
    int m_lastIndex = 0;
};

#endif // SIDEBARPANELS_H
