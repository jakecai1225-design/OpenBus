#include "signalconfigdialog.h"

#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QLabel>
#include "utils/canutils.h"

SignalConfigDialog::SignalConfigDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("添加信号");
    setMinimumWidth(320);

    auto *form = new QFormLayout;

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText("例如: EngineRPM");

    m_idEdit = new QLineEdit(this);
    m_idEdit->setPlaceholderText("0x100");
    m_idEdit->setText("0x100");

    m_extCheck = new QCheckBox("扩展帧 (29-bit ID)", this);

    m_offsetSpin = new QSpinBox(this);
    m_offsetSpin->setRange(0, 63);
    m_offsetSpin->setValue(0);

    m_bitLenCombo = new QComboBox(this);
    m_bitLenCombo->addItem("8 bit (1 byte)", 8);
    m_bitLenCombo->addItem("16 bit (2 bytes)", 16);
    m_bitLenCombo->addItem("32 bit (4 bytes)", 32);

    m_beCheck = new QCheckBox("大端序 (Big Endian)", this);

    form->addRow("信号名称:", m_nameEdit);
    form->addRow("CAN ID:", m_idEdit);
    form->addRow("", m_extCheck);
    form->addRow("字节偏移:", m_offsetSpin);
    form->addRow("位长:", m_bitLenCombo);
    form->addRow("", m_beCheck);

    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addStretch();
    layout->addWidget(btnBox);

    connect(btnBox, &QDialogButtonBox::accepted, this, [this]() {
        if (m_nameEdit->text().trimmed().isEmpty()) {
            m_nameEdit->setText(QString("Sig_0x%1")
                .arg(canId(), 0, 16).toUpper());
        }
        accept();
    });
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString SignalConfigDialog::signalName() const
{
    return m_nameEdit->text().trimmed();
}

quint32 SignalConfigDialog::canId() const
{
    return CanUtils::parseHex(m_idEdit->text());
}

bool SignalConfigDialog::isExtended() const
{
    return m_extCheck->isChecked();
}

int SignalConfigDialog::byteOffset() const
{
    return m_offsetSpin->value();
}

int SignalConfigDialog::bitLength() const
{
    return m_bitLenCombo->currentData().toInt();
}

bool SignalConfigDialog::isBigEndian() const
{
    return m_beCheck->isChecked();
}
