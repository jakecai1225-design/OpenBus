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
 * 对应 UI 文档 Section 4.6: CAN 信号发送界面
 * - 工具栏: 全部发送 / 全部停止 / 从DBC导入 / 清空列表
 * - 发送列表表格: ✓, #, ID, 名称, DLC, 数据(Hex), 周期, 次数, 状态, 操作
 *   操作列: ▶发送 ⏹停止 ✕删除 ↑上移 ↓下移
 * - 编辑区 (较高):
 *   帧级: ID, DLC, 数据(Hex), 周期, 次数, DBC消息选择, 添加到列表, 发送单帧
 *   信号级: 根据DBC自动生成信号编辑器 (值/单位/范围)
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
    void onDbcMsgSelected(int index);
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

private:
    // Toolbar buttons
    QPushButton *m_sendAllBtn;
    QPushButton *m_stopAllBtn;
    QPushButton *m_importDbcBtn;
    QPushButton *m_clearListBtn;

    // Send list table (10 columns)
    QTableWidget *m_sendTable;

    // Frame-level edit area
    QLineEdit *m_idEdit;
    QSpinBox *m_dlcSpin;
    QLineEdit *m_dataEdit;
    QSpinBox *m_periodSpin;
    QSpinBox *m_countSpin;
    QComboBox *m_dbcMsgCombo;
    QPushButton *m_addToListBtn;
    QPushButton *m_sendSingleBtn;

    // Signal-level edit area
    QScrollArea *m_signalScroll;
    QVBoxLayout *m_signalLayout;
    QLabel *m_signalHintLabel;
    QList<QDoubleSpinBox *> m_signalSpinBoxes;
    const DbcMessage *m_currentDbcMsg = nullptr;

    DbcManager *m_dbcManager = nullptr;
    bool m_updatingSignals = false;

    // Helper methods
    void rebuildSignalEditors();
    void updateDataFromSignals();
    void updateSignalsFromData();
    void renumberRows();
    int findRowOfButton(QWidget *btn) const;
    QWidget *createOpWidget();
    void refreshDbcMessages();
    static QByteArray parseHexData(const QString &text);
    static QString formatDataHex(const QByteArray &data);
    static QString formatIdHex(quint32 id);
};

#endif // SIGNALSENDTAB_H
