#ifndef SIGNALSENDTAB_H
#define SIGNALSENDTAB_H

#include <QWidget>
#include <QList>

class QTableWidget;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;
class QLineEdit;
class QComboBox;
class QScrollArea;
class QVBoxLayout;
class QLabel;
class DbcManager;
struct DbcMessage;

/**
 * @brief 信号发送标签页 — 中央区域
 *
 * 上半部分：发送帧列表（列表发送/列表停止/清空）
 * 下半部分：信号级编辑 + Raw 帧编辑（从DBC导入/手动添加）
 *
 * 上下联动：点击发送列表中的某行，底部编辑器自动显示该帧的信号编辑列表。
 * 信号编辑支持 QDoubleSpinBox（数值）和 QComboBox（DBC 枚举值）。
 */
class SignalSendTab : public QWidget
{
    Q_OBJECT

public:
    explicit SignalSendTab(QWidget *parent = nullptr);
    void setDbcManager(DbcManager *mgr);

signals:
    void sendAllRequested();
    void stopAllRequested();
    void sendSingleRequested(quint32 id, const QByteArray &data);
    void sendRowRequested(int row, quint32 id, const QByteArray &data, int period, int count);
    void stopRowRequested(int row);

private slots:
    void onIdEditingFinished();
    void onAddToList();
    void onSendSingle();
    void onSendAll();
    void onStopAll();
    void onClearList();
    void onImportFromDbc();
    void onSignalValueChanged();
    void onDataEditFinished();
    void onRowSend(int row);
    void onRowStop(int row);
    void onRowDelete(int row);
    void onRowMoveUp(int row);
    void onRowMoveDown(int row);
    void onSendTableRowChanged();

private:
    // 顶部工具栏按钮
    QPushButton *m_sendAllBtn;
    QPushButton *m_stopAllBtn;
    QPushButton *m_clearListBtn;

    // 发送列表表格 (10 columns)
    QTableWidget *m_sendTable;

    // 底部编辑区 — Raw 帧编辑
    QPushButton *m_importDbcBtn;
    QLineEdit *m_idEdit;
    QSpinBox *m_dlcSpin;
    QLineEdit *m_dataEdit;
    QSpinBox *m_periodSpin;
    QSpinBox *m_countSpin;
    QPushButton *m_addToListBtn;
    QPushButton *m_sendSingleBtn;

    // 底部编辑区 — 信号级编辑
    QScrollArea *m_signalScroll;
    QVBoxLayout *m_signalLayout;
    QLabel *m_signalHintLabel;
    QList<QWidget *> m_signalWidgets;     ///< 每个信号的控件（QDoubleSpinBox 或 QComboBox）
    const DbcMessage *m_currentDbcMsg = nullptr;

    DbcManager *m_dbcManager = nullptr;
    bool m_updatingSignals = false;
    int m_selectedRow = -1;               ///< 当前选中的发送列表行（-1=未选中）

    // Helper methods
    void rebuildSignalEditors();
    void updateDataFromSignals();
    void updateSignalsFromData();
    void renumberRows();
    int findRowOfButton(QWidget *btn) const;
    QWidget *createOpWidget();
    static QByteArray parseHexData(const QString &text);
    static QString formatDataHex(const QByteArray &data);
    static QString formatIdHex(quint32 id);
    void loadRowToEditor(int row);
};

#endif // SIGNALSENDTAB_H
