#ifndef WATCHERVIEW_H
#define WATCHERVIEW_H

#include <QWidget>
#include <QVector>
#include <QHash>
#include <QTimer>
#include <QShowEvent>
#include <QHideEvent>
#include <QElapsedTimer>
#include "core/canframe.h"
#include "core/dbcdata.h"

class QTableWidget;
class QToolBar;
class QToolButton;
class QLabel;
class QComboBox;
class QSplitter;
class QTabWidget;
class DbcManager;
class BusStatistics;

/**
 * @brief Watcher — debugger-style signal watch (Ozone / IAR / Keil flavored).
 *
 * Tab 1: variable watch — DBC signals with adjustable UI refresh, VAL_
 *         symbolic names, change highlight, per-signal ring-buffer recording
 *         and CSV export.
 * Tab 2: bus statistics (unchanged snapshot polling of BusStatistics).
 */
class WatcherView : public QWidget
{
    Q_OBJECT

public:
    static constexpr int kRingCapacity = 2000;
    static constexpr int kHighlightMs = 400;

    struct Sample {
        double tMs = 0.0;   ///< wall or bus time (ms) for CSV
        double phys = 0.0;
        quint64 raw = 0;
    };

    struct WatchEntry {
        QString name;
        QString messageName;
        quint32 canId = 0;
        bool extended = false;
        DbcSignal sig;

        double currentValue = 0.0;
        double minValue = 0.0;
        double maxValue = 0.0;
        bool hasValue = false;
        double lastPhys = 0.0;
        bool recording = false;
        qint64 changedUntilMs = 0;   ///< QElapsedTimer ms epoch for highlight end
        qint64 lastSampleMs = 0;     ///< last ring push (wall ms)
        QVector<Sample> history;     ///< ring (oldest→newest, capped)
    };

    explicit WatcherView(DbcManager *dbc, BusStatistics *stats,
                         QWidget *parent = nullptr);

    QVector<WatchEntry> watchEntries() const { return m_entries; }
    void loadWatchEntries(const QVector<WatchEntry> &entries);
    void retranslateUi();

public slots:
    void onFrame(const CanFrame &frame);
    void clearData();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private slots:
    void onAddVariables();
    void onRemoveSelected();
    void onClearVariables();
    void onResetStats();
    void onRefreshTimer();
    void onRefreshRateChanged(int index);
    void onSampleModeChanged(int index);
    void onRecordSelected();
    void onStopRecordSelected();
    void onExportCsv();
    void onWatchSelectionChanged();
    void onRecCellChanged(int row, int column);

private:
    DbcManager *m_dbc = nullptr;
    BusStatistics *m_stats = nullptr;

    QToolBar *m_toolbar = nullptr;
    QTabWidget *m_tabs = nullptr;
    QToolButton *m_addBtn = nullptr;
    QToolButton *m_removeBtn = nullptr;
    QToolButton *m_clearBtn = nullptr;
    QToolButton *m_pauseBtn = nullptr;
    QToolButton *m_resetBtn = nullptr;
    QToolButton *m_recBtn = nullptr;
    QToolButton *m_stopRecBtn = nullptr;
    QToolButton *m_exportBtn = nullptr;
    QLabel *m_rateLbl = nullptr;
    QLabel *m_sampleLbl = nullptr;
    QComboBox *m_rateCombo = nullptr;
    QComboBox *m_sampleCombo = nullptr;
    QTableWidget *m_watchTable = nullptr;
    QTableWidget *m_historyTable = nullptr;
    QLabel *m_historyHint = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QTableWidget *m_idTable = nullptr;
    QTableWidget *m_errorTable = nullptr;
    QSplitter *m_watchSplitter = nullptr;

    QVector<WatchEntry> m_entries;
    QHash<quint32, CanFrame> m_latestFrames;
    QHash<quint32, qint64> m_frameWallMs;  ///< canId → wall ms of last frame
    QTimer m_refreshTimer;
    QElapsedTimer m_clock;
    bool m_paused = false;
    bool m_recGuard = false;
    int m_refreshMs = 200;
    bool m_sampleOnChange = false;  ///< false = sample every refreshMs

    enum Col {
        ColName = 0,
        ColValue,
        ColSymbolic,
        ColRaw,
        ColUnit,
        ColMessage,
        ColAge,
        ColMin,
        ColMax,
        ColRec,
        ColCount
    };

    void setupUi();
    void applyRefreshMs(int ms);
    void refreshWatchTable();
    void refreshStatistics();
    void refreshHistoryPane();
    void pushSample(WatchEntry &e, double phys, quint64 raw, double tMs);
    void maybeSampleFromFrame(quint32 canId, const CanFrame &frame);
    QString formatAge(qint64 lastWallMs) const;
    QString symbolicFor(const WatchEntry &e, quint64 raw) const;
    int selectedWatchRow() const;
};

#endif // WATCHERVIEW_H
