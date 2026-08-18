#ifndef CANDEVICE_SLCAN_H
#define CANDEVICE_SLCAN_H

#include "core/candevice.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

/**
 * @brief SLCAN（Lawicel 串口文本协议）CAN 设备后端
 *
 * 一份驱动覆盖 SLCAN 协议全家（方案 §14.4 P0-A）：
 * 淘宝廉价 SLCAN 适配器 / Lawicel CANUSB / CANable (slcan 固件) /
 * ESP32·Arduino DIY / USBtin 等兼容固件。
 *
 * - 串口层：Win32 API（项目当前 Windows 平台），115200-8N1，无流控，
 *   DTR/RTS 均不置位（避免 ESP32 自动复位电路误触发）
 * - 协议：保守公共子集 C/O/V/N/F/M/S（方案 §14.5.2 方言风险对策），
 *   不承诺厂商私有扩展；CAN FD 不支持（各固件方言不兼容）
 * - 时间戳：无硬件时间戳（DeviceInfo.hasHwTimestamp = false），
 *   timestampNs 置 0 由 CanDeviceManager 用 steady_clock 软件补齐
 *
 * 架构：ICanDevice → CanDeviceSlcan → 串口（系统自带驱动，无厂商 DLL）
 */
class CanDeviceSlcan : public ICanDevice
{
public:
    /// vendorCtrl 指令：指定串口名（param 指向 QString，如 "COM3"；open 前调用）
    static constexpr int VendorCmdSetPort = 1;
    /// vendorCtrl 指令：串口波特率（param 指向 int；默认 115200，个别固件为 9600/1M）
    static constexpr int VendorCmdSetUartBaud = 2;

    explicit CanDeviceSlcan(int subType = 0);
    ~CanDeviceSlcan() override;

    // ---- ICanDevice ----
    Brand brand() const override { return Brand::SLCAN; }
    bool open(int devIndex, int channel, int arbBaud, int dataBaud, bool canFd) override;
    void close() override;
    int send(const CanFrame &frame) override;
    int recv(int timeoutMs, std::vector<CanFrame> &outFrames) override;
    int pendingCount() const override;
    bool isOpen() const override { return m_opened; }
    QString deviceName() const override;
    bool vendorCtrl(int cmd, void *param) override;
    bool setAcceptanceFilter(quint32 code, quint32 mask, bool extended) override;
    bool clearAcceptanceFilter() override;

    // ---- 枚举（无厂商 SDK，串口即设备） ----

    /// 系统串口名列表（"COM3" 形式，供连接页串口下拉）
    static QStringList availablePorts();

    /// 枚举全部串口为设备条目（零副作用：不打开串口探测，固件名在
    /// open() 阶段经 V 命令获取，避免占用他人端口 + UI 线程耗时）
    static std::vector<DeviceInfo> enumerate();

private:
    bool openSerial(const QString &portName, int uartBaud);
    void closeSerial();
    bool writeLine(const QByteArray &cmd);
    /// 读一行应答（\r/\n 结尾），超时返回空
    QByteArray readLine(int timeoutMs);
    /// 丢弃启动期杂散应答/回显字节（打开序列间隙调用）
    void drainRxAged();
    /// 仲裁段波特率 → SLCAN 标准波特率表 S0..S8 序号（-1 = 不支持）
    static int baudToSlcanIndex(int baud);

    void *m_handle = nullptr;      ///< Win32 HANDLE（跨平台占位）
    QString m_portName;            ///< 指定串口（vendorCtrl 设置；空 = 按 devIndex 取）
    int m_uartBaud = 115200;       ///< 串口波特率
    bool m_opened = false;
    bool m_busOn = false;
    QString m_deviceName;
    int m_channel = 0;             ///< 逻辑通道（0-based；SLCAN 单通道恒 0）
    QByteArray m_rxBuf;            ///< 接收行缓冲（\r / \n 分隔）
};

#endif // CANDEVICE_SLCAN_H
