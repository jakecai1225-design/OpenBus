#ifndef FILTERBAR_H
#define FILTERBAR_H

#include <QWidget>
#include <QVector>
#include <QPair>
#include <QString>

class QLineEdit;
class QToolButton;
class QLabel;
class QMenu;
class QHBoxLayout;
class QScrollArea;
class FilterPresetManager;

/**
 * @brief Wireshark-style display filter bar
 *
 * Left: status + expanding filter edit + apply/clear;
 * then Trace action icons; right: presets / help / settings.
 */
class FilterBar : public QWidget
{
    Q_OBJECT

public:
    explicit FilterBar(QWidget *parent = nullptr);

    QString filterText() const;
    bool filterActive() const;
    void setFilterText(const QString &text);

    void setPresetManager(FilterPresetManager *mgr);
    QToolButton *settingsButton() const { return m_settingsBtn; }
    void setPacketCountText(const QString &text);
    void retranslateUi();

    /// Slot for Trace action icons (Find / Follow / …) — after apply/clear
    QHBoxLayout *actionsLayout() const { return m_actionsLay; }

signals:
    void filterApplied(const QString &filter);
    void filterCleared();
    void refreshRateChanged(int intervalMs);
    void clearListRequested();

private slots:
    void onApply();
    void onClear();
    void showHelp();
    void onTextChanged();
    void onPresetMenu();
    void onSaveAsPreset();

private:
    QLineEdit *m_edit = nullptr;
    QToolButton *m_applyBtn = nullptr;
    QToolButton *m_clearBtn = nullptr;
    QToolButton *m_helpBtn = nullptr;
    QToolButton *m_presetBtn = nullptr;
    QToolButton *m_settingsBtn = nullptr;
    QToolButton *m_clearListBtn = nullptr;
    QLabel *m_statusIcon = nullptr;
    QLabel *m_packetCountLabel = nullptr;
    QWidget *m_actionsHost = nullptr;
    QHBoxLayout *m_actionsLay = nullptr;
    FilterPresetManager *m_presetMgr = nullptr;

    void refreshPresets();
};

/**
 * @brief U2: active-filter chip strip (main expr + column filters)
 *
 * Each chip is id + label; clicking × emits chipDismissed(id).
 * "Clear all" emits clearAllRequested when any chip is present.
 */
class FilterChipBar : public QWidget
{
    Q_OBJECT
public:
    explicit FilterChipBar(QWidget *parent = nullptr);

    void setChips(const QVector<QPair<QString, QString>> &chips); // id, label
    int chipCount() const { return m_chipCount; }

signals:
    void chipDismissed(const QString &id);
    void clearAllRequested();

private:
    QScrollArea *m_scroll = nullptr;
    QWidget *m_inner = nullptr;
    QHBoxLayout *m_lay = nullptr;
    QToolButton *m_clearAllBtn = nullptr;
    int m_chipCount = 0;

    void rebuild(const QVector<QPair<QString, QString>> &chips);
};

#endif // FILTERBAR_H
