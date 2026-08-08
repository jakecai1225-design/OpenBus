#ifndef DATAWINDOW_H
#define DATAWINDOW_H

#include <QWidget>
#include <QVector>
#include <QHash>
#include <QTimer>
#include "core/canframe.h"

class QTableWidget;
class QToolBar;
class QToolButton;
class DbcManager;

/**
 * @brief Data Window — 信号实时表格（对标 CANoe Data Window）
 *
 * 以表格形式实时展示多个信号的当前值、原始值、物理值、最小值、最大值。
 * 刷新策略：100ms 定时器轮询最新帧数据，避免高频报文导致 UI 卡顿。
 */
class DataWindow : public QWidget
{
    Q_OBJECT

public:
    /// 信号配置（从 DBC 信号树添加）
    struct SignalEntry {
        QString name;
        quint32 canId = 0;
        bool extended = false;
        double factor = 1.0;
        double offset = 0.0;
        QString unit;
        // 运行时统计
        double currentValue = 0.0;
        quint64 rawValue = 0;
        double minValue = 0.0;
        double maxValue = 0.0;
        bool hasValue = false;
        double lastTimestamp = 0.0;
    };

    explicit DataWindow(QWidget *parent = nullptr);

    void setDbcManager(DbcManager *mgr) { m_dbc = mgr; }

    void addSignal(const QString &name, quint32 canId, bool extended,
                   double factor, double offset, const QString &unit);
    void removeSignal(int index);
    void clearSignals();

public slots:
    void onFrame(const CanFrame &frame);
    void clearData();

private slots:
    void onRefreshTimer();
    void onAddSignal();
    void onRemoveSignal();
    void onClearAll();

private:
    QTableWidget *m_table = nullptr;
    QToolBar *m_toolbar = nullptr;
    DbcManager *m_dbc = nullptr;
    QVector<SignalEntry> m_entries;
    QTimer m_refreshTimer;

    /// 每个信号对应的最新帧数据缓存（canId → frame）
    QHash<quint32, CanFrame> m_latestFrames;

    void setupUi();
    void refreshTable();
};

#endif // DATAWINDOW_H
