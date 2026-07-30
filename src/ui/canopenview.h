#ifndef CANOPENVIEW_H
#define CANOPENVIEW_H

#include <QWidget>

class QTabWidget;
class QSpinBox;
class QComboBox;
class QLineEdit;
class QTableWidget;
class QTextEdit;
class QPushButton;
class QLabel;
class QCheckBox;

/**
 * @brief CANopen 协议视图 — 参考 CANoe CANopen Monitor / ZLG CANopen Studio
 *
 * 功能：
 *  - NMT 命令（Start/Stop/Pre-Op/Reset/Reset Comm）
 *  - SDO 读写（Index/Subindex/数据类型/数据）
 *  - Emergency 对象显示
 *  - Heartbeat 监控
 *  - 事务日志
 */
class CanOpenView : public QWidget
{
    Q_OBJECT
public:
    explicit CanOpenView(QWidget *parent = nullptr);

private slots:
    // NMT
    void onNmtCommand(int cmd);
    // SDO
    void onSdoRead();
    void onSdoWrite();
    // 通用
    void onClearLog();

private:
    void setupUi();
    QWidget *createNmtTab();
    QWidget *createSdoTab();
    QWidget *createEmergencyTab();
    QWidget *createHeartbeatTab();

    void appendLog(const QString &type, const QString &desc,
                   const QString &data, const QString &status);

    // NMT
    QSpinBox *m_nmtNodeId = nullptr;
    QLabel *m_nmtStatusLabel = nullptr;

    // SDO
    QSpinBox *m_sdoNodeId = nullptr;
    QSpinBox *m_sdoIndex = nullptr;
    QSpinBox *m_sdoSubIndex = nullptr;
    QComboBox *m_sdoDataType = nullptr;
    QLineEdit *m_sdoDataEdit = nullptr;
    QLabel *m_sdoResultLabel = nullptr;

    // Emergency 表
    QTableWidget *m_emergencyTable = nullptr;

    // Heartbeat
    QTableWidget *m_heartbeatTable = nullptr;

    // 日志
    QTableWidget *m_logTable = nullptr;
};

#endif // CANOPENVIEW_H
