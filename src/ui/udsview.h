#ifndef UDSVIEW_H
#define UDSVIEW_H

#include <QWidget>

class QComboBox;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class QTextEdit;
class QPushButton;
class QCheckBox;
class QLabel;

/**
 * @brief UDS 诊断视图 — 参考 CANoe 诊断窗口
 *
 * 功能：
 *  - 诊断服务请求构建器（服务 ID / 子功能 / 数据）
 *  - CAN ID 配置（请求/响应）
 *  - 诊断事务日志（时间/方向/服务/数据/状态/NRC）
 *  - 响应解析与 NRC 描述
 */
class UdsView : public QWidget
{
    Q_OBJECT
public:
    explicit UdsView(QWidget *parent = nullptr);

private slots:
    void onServiceChanged(int index);
    void onSendRequest();
    void onClearLog();
    void onSimulateResponse();

private:
    void setupUi();
    void populateServices();
    void populateSubFunctions(int serviceId);
    void appendLog(const QString &direction, int serviceId,
                   int subFunc, const QByteArray &data,
                   const QString &status, const QString &nrcDesc);
    static QString serviceName(int sid);
    static QString nrcName(int nrc);
    static QString bytesToHex(const QByteArray &data, bool withSpace = true);
    static QByteArray parseHex(const QString &text);

    // 请求构建器
    QComboBox *m_serviceCombo = nullptr;
    QComboBox *m_subFuncCombo = nullptr;
    QLineEdit *m_dataEdit = nullptr;
    QSpinBox *m_reqIdSpin = nullptr;
    QSpinBox *m_rspIdSpin = nullptr;
    QCheckBox *m_autoRespCheck = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;

    // 日志表
    QTableWidget *m_logTable = nullptr;

    // 响应详情
    QTextEdit *m_detailEdit = nullptr;
};

#endif // UDSVIEW_H
