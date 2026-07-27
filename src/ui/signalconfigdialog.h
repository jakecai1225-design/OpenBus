#ifndef SIGNALCONFIGDIALOG_H
#define SIGNALCONFIGDIALOG_H

#include <QDialog>

class QLineEdit;
class QCheckBox;
class QComboBox;
class QSpinBox;

/**
 * @brief 信号配置对话框
 *
 * 用于在 GraphicView 中添加信号监控：
 * - 信号名称
 * - CAN ID（十六进制）
 * - 扩展帧选项
 * - 字节偏移
 * - 位长（8/16/32）
 * - 字节序
 */
class SignalConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SignalConfigDialog(QWidget *parent = nullptr);

    QString signalName() const;
    quint32 canId() const;
    bool isExtended() const;
    int byteOffset() const;
    int bitLength() const;
    bool isBigEndian() const;

private:
    QLineEdit *m_nameEdit;
    QLineEdit *m_idEdit;
    QCheckBox *m_extCheck;
    QSpinBox *m_offsetSpin;
    QComboBox *m_bitLenCombo;
    QCheckBox *m_beCheck;
};

#endif // SIGNALCONFIGDIALOG_H
