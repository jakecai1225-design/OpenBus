#ifndef EXTENSIONSTAB_H
#define EXTENSIONSTAB_H

#include <QWidget>

class QTableWidget;
class QTableWidgetItem;
class QLabel;
class QPushButton;
class QTimer;
class PluginManager;

/**
 * @brief 扩展标签页 — 插件管理中心
 *
 * 顶部显示宿主进程实时资源信息（PID、CPU、内存、磁盘 I/O），
 * 下方表格展示所有已发现插件的状态与操作按钮。
 */
class ExtensionsTab : public QWidget
{
    Q_OBJECT
public:
    explicit ExtensionsTab(QWidget *parent = nullptr);
    ~ExtensionsTab();

    /// 刷新插件列表和宿主信息
    void refresh();

signals:
    /// 请求激活插件（双击或点击启动按钮）
    void pluginActivateRequested(const QString &name);
    /// 请求停用插件
    void pluginDeactivateRequested(const QString &name);
    /// 请求启用/禁用插件
    void pluginToggleRequested(const QString &name, bool enable);
    /// 请求停止宿主进程
    void hostStopRequested();

private slots:
    void onRefreshClicked();
    void onItemDoubleClicked(int row, int col);
    void onResourceTimer();

private:
    void setupUi();
    void populateTable();
    void updateHostInfo();
    void sampleProcessResources();

    PluginManager *m_pm;

    // ---- 宿主进程信息栏 ----
    QLabel *m_hostStatus   = nullptr;
    QLabel *m_hostPid      = nullptr;
    QLabel *m_cpuLabel     = nullptr;
    QLabel *m_memLabel    = nullptr;
    QLabel *m_diskReadLabel  = nullptr;
    QLabel *m_diskWriteLabel = nullptr;
    QLabel *m_pythonPath   = nullptr;
    QPushButton *m_stopHostBtn = nullptr;

    // ---- 插件表格 ----
    QLabel *m_pluginCount;
    QTableWidget *m_table;
    QPushButton *m_refreshBtn;

    // ---- 资源采样状态 ----
    QTimer *m_resourceTimer = nullptr;
    qint64 m_prevCpuTime = 0;    ///< 上次采样的 CPU 时间（100ns 单位）
    qint64 m_prevWallTime = 0;   ///< 上次采样的墙钟时间（100ns 单位）
    quint64 m_prevDiskRead = 0;
    quint64 m_prevDiskWrite = 0;
    bool m_firstSample = true;
};

#endif // EXTENSIONSTAB_H
