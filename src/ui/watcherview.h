#ifndef WATCHERVIEW_H
#define WATCHERVIEW_H

#include <QWidget>
#include <QVector>
#include <QHash>
#include <QTimer>
#include "core/canframe.h"
#include "core/dbcdata.h"

class QTableWidget;
class QToolBar;
class QToolButton;
class QLabel;
class DbcManager;
class BusStatistics;

/**
 * @brief Watcher 观测窗口 — 调试器 Watch 风格的观测页（doc/Watcher方案.md）
 *
 * 对标 Lauterbach/IAR/Keil 的 Watch 窗口，两个标签页：
 *   - 变量观测：DBC 信号（= 总线"变量"）实时列表——当前值/原始值/最小/
 *     最大/单位/报文；懒解码自最新帧缓存，500ms 刷新，可暂停冻结；
 *   - 总线统计：BusStatistics 快照——摘要（总帧数/ID 数/负载/时长/错误帧）
 *     + 逐报文频率/周期/抖动 + 错误帧分类（Stuff/Form/ACK/Bit0/Bit1/CRC）。
 *
 * 数据来源：壳侧直连（onFrameReceived 喂帧；统计走引擎快照轮询，
 * 不连 statisticsUpdated 信号——DEF-08 跨库 PMF connect 规避）。
 */
class WatcherView : public QWidget
{
    Q_OBJECT

public:
    /// 观测变量条目（自带 DbcSignal 拷贝，解码不回查 DbcManager）
    struct WatchEntry {
        QString name;          ///< 信号名
        QString messageName;   ///< 所属报文名（显示用）
        quint32 canId = 0;
        bool extended = false;
        DbcSignal sig;         ///< 信号定义拷贝（含 factor/offset/unit/位定义）
        // 运行时统计
        double currentValue = 0.0;
        double minValue = 0.0;
        double maxValue = 0.0;
        bool hasValue = false;
    };

    explicit WatcherView(DbcManager *dbc, BusStatistics *stats,
                         QWidget *parent = nullptr);

public slots:
    /// 最新帧缓存（壳 onFrameReceived 直调，同 m_ioGraph 先例）
    void onFrame(const CanFrame &frame);
    /// 复位变量统计与帧缓存（新测量会话由壳调用）
    void clearData();

private slots:
    void onAddVariables();      ///< DBC 信号选择弹窗批量添加（搜索/树形/多选）
    void onRemoveSelected();
    void onClearVariables();
    void onResetStats();        ///< 引擎 clear() + 变量统计复位
    void onRefreshTimer();      ///< 500ms：变量表 + 统计页刷新（暂停时跳过）

private:
    DbcManager *m_dbc = nullptr;
    BusStatistics *m_stats = nullptr;

    // ---- UI ----
    QToolBar *m_toolbar = nullptr;
    QToolButton *m_pauseBtn = nullptr;     ///< 暂停刷新（冻结显示，checkable）
    QTableWidget *m_watchTable = nullptr;  ///< 变量观测表（7 列）
    QLabel *m_summaryLabel = nullptr;      ///< 统计摘要行（5 项卡片式文本）
    QTableWidget *m_idTable = nullptr;     ///< 逐报文统计表（9 列）
    QTableWidget *m_errorTable = nullptr;  ///< 错误分类表（1 行 7 列）

    // ---- 状态 ----
    QVector<WatchEntry> m_entries;
    QHash<quint32, CanFrame> m_latestFrames;   ///< canId → 最新帧（懒解码源）
    QTimer m_refreshTimer;
    bool m_paused = false;

    void setupUi();
    void refreshWatchTable();
    void refreshStatistics();
};

#endif // WATCHERVIEW_H
