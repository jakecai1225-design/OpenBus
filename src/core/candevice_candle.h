#ifndef CANDEVICE_CANDLE_H
#define CANDEVICE_CANDLE_H

#include "core/candevice.h"

#include <QByteArray>
#include <QString>
#include <vector>

/**
 * @brief Candle / GS_USB 开源 USB CAN 设备后端（方案 §14.4 P0-B）
 *
 * 一份驱动覆盖 GS_USB 协议全家：CANable (candle 固件) / candleLight DIY /
 * Cantact / CANnectivity / CES CANext FD / ABE CANDebugger 等。
 * 协议以 Linux 内核 gs_usb.c 与 candleLight_fw gs_usb.h 为权威参照：
 * - 控制传输：bmRequestType 0x41(OUT)/0xC1(IN)，wValue=通道号，wIndex=接口 0
 * - 打开序列：HOST_FORMAT(0x0000beef LE) → DEVICE_CONFIG(通道数) →
 *   BT_CONST(位时序参数) → BITTIMING →(FD: DATA_BITTIMING) → MODE START
 * - 帧：12 字节头 + data[8]/[64] + 可选 timestamp_us；
 *   经典帧 24 字节（ts@20），FD 帧 80 字节（ts@76）；echo_id 0xffffffff=接收帧
 * - libusb-1.0.dll 动态加载（drivers/candle/vendor → 应用目录 → PATH），
 *   进程内共享单例、永不卸载（libusb 全局状态不可安全回收）
 *
 * 硬件时间戳：设备特征含 HW_TIMESTAMP(bit4) 时启用（1MHz 自由计数器），
 * 首帧与 steady_clock 对齐后换算纳秒并处理 32 位回绕（约 71.6 分钟）。
 * 设备需绑定 WinUSB/libusb-win32 驱动（CANable 等新版固件为 WinUSB WCID）。
 */
class CanDeviceCandle : public ICanDevice
{
public:
    explicit CanDeviceCandle(int subType = 0);
    ~CanDeviceCandle() override;

    // ---- ICanDevice ----
    Brand brand() const override { return Brand::Candle; }
    bool open(int devIndex, int channel, int arbBaud, int dataBaud, bool canFd) override;
    void close() override;
    int send(const CanFrame &frame) override;
    int recv(int timeoutMs, std::vector<CanFrame> &outFrames) override;
    int pendingCount() const override;
    bool isOpen() const override { return m_opened; }
    QString deviceName() const override;

    // ---- 枚举 ----

    /// libusb-1.0.dll 是否可加载
    static bool isAvailable();

    /// 枚举 VID/PID 白名单设备并经 DEVICE_CONFIG 探测通道数
    static std::vector<DeviceInfo> enumerate();

private:
    /// 与 libusb_device_descriptor 等价的 18 字节布局（避免引入 libusb 头）
    struct RawDeviceDescriptor {
        quint8  bLength;
        quint8  bDescriptorType;
        quint16 bcdUSB;
        quint8  bDeviceClass;
        quint8  bDeviceSubClass;
        quint8  bDeviceProtocol;
        quint8  bMaxPacketSize0;
        quint16 idVendor;
        quint16 idProduct;
        quint16 bcdDevice;
        quint8  iManufacturer;
        quint8  iProduct;
        quint8  iSerialNumber;
        quint8  bNumConfigurations;
    } __attribute__((packed));

    /// GS_USB bt_const（小端 u32 × 10：设备特征 / CAN 时钟 / tseg·brp 范围）
    struct BtConstRaw {
        quint32 feature;
        quint32 fclk;
        quint32 tseg1Min, tseg1Max;
        quint32 tseg2Min, tseg2Max;
        quint32 sjwMax;
        quint32 brpMin, brpMax, brpInc;
    };

    /// GS_USB 位时序寄存器值（u32 × 5）
    struct BtRaw {
        quint32 propSeg;
        quint32 phaseSeg1;
        quint32 phaseSeg2;
        quint32 sjw;
        quint32 brp;
    };

    // ---- libusb 函数表（共享单例，进程内只加载一次、永不卸载） ----
    struct LibUsb {
        bool ok = false;
        int  (*init)(void **) = nullptr;
        void (*exit)(void *) = nullptr;
        long (*get_device_list)(void *, void ***) = nullptr;
        void (*free_device_list)(void **, int) = nullptr;
        int  (*get_device_descriptor)(void *, RawDeviceDescriptor *) = nullptr;
        quint8 (*bus_number)(void *) = nullptr;
        quint8 (*dev_address)(void *) = nullptr;
        int  (*open)(void *, void **) = nullptr;
        void (*close)(void *) = nullptr;
        int  (*claim_interface)(void *, int) = nullptr;
        int  (*release_interface)(void *, int) = nullptr;
        int  (*auto_detach)(void *, int) = nullptr;
        int  (*control)(void *, quint8, quint8, quint16, quint16, quint8 *, quint16, unsigned) = nullptr;
        int  (*bulk)(void *, quint8, quint8 *, int, int *, unsigned) = nullptr;
    };

    /// 加载 libusb-1.0（drivers/candle/vendor → 应用目录 → 系统搜索路径）
    static LibUsb &usb();

    // ---- USB 传输辅助 ----
    /// 白名单匹配结果（未打开的 libusb_device 引用，仅设备表遍历期间有效）
    struct UsbMatch {
        void *dev = nullptr;
        RawDeviceDescriptor desc;
        QString model;
        quint8 bus = 0;
        quint8 addr = 0;
    };
    /// 遍历 libusb 设备表，返回 VID/PID 白名单匹配项（顺序稳定，枚举与打开共用）
    static std::vector<UsbMatch> collectWhitelisted();
    static bool devCtrlOut(void *handle, quint8 breq, quint16 wValue, const void *data, quint16 len);
    static bool devCtrlIn(void *handle, quint8 breq, quint16 wValue, void *data, quint16 len);
    bool ctrlOut(quint8 breq, quint16 wValue, const void *data, quint16 len);
    bool ctrlIn(quint8 breq, quint16 wValue, void *data, quint16 len);
    /// 读取通道位时序参数；extended=true 时额外取 FD 数据段参数
    bool readBtConst(int channel, bool extended, BtConstRaw *arb, BtConstRaw *data);
    /// 由 bt_const 与目标波特率计算位时序（87.5% 采样点，仅接受精确分频）
    static bool calcBittiming(const BtConstRaw &c, int bitrate, BtRaw *out);
    /// 设备硬件时间戳（µs）→ steady_clock 纳秒（首帧对齐 + 32 位回绕补偿）
    quint64 hwTsToNs(quint32 rawUs);
    /// VID/PID 白名单显示名（如 "CANable (candle)"），非白名单返回空
    static QString modelForVidPid(quint16 vid, quint16 pid);

    void *m_handle = nullptr;      ///< libusb_device_handle
    int m_channel = 0;             ///< 通道号（0-based）
    bool m_opened = false;
    bool m_canFd = false;
    bool m_hwTs = false;           ///< 设备支持硬件时间戳
    quint32 m_echoCounter = 0;     ///< 发送帧 echo_id（回环帧据此跳过）
    QString m_deviceName;

    // ---- 硬件时间戳对齐状态 ----
    bool m_tsSynced = false;
    qint64 m_tsOffsetNs = 0;
    quint32 m_lastRawTsUs = 0;
    quint64 m_tsWrapUs = 0;
};

#endif // CANDEVICE_CANDLE_H
