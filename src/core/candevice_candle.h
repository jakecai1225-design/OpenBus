#ifndef CANDEVICE_CANDLE_H
#define CANDEVICE_CANDLE_H

#include "core/candevice.h"

#include <QByteArray>
#include <QString>
#include <vector>

/**
 * @brief Candle / GS_USB open-source USB CAN backend (plan §14.4 P0-B)
 *
 * One driver for the GS_USB family: CANable (candle firmware) / candleLight /
 * Cantact / CANnectivity / CES CANext FD / ABE CANDebugger / etc.
 * Protocol follows Linux gs_usb.c and candleLight_fw gs_usb.h:
 * - Control: bmRequestType 0x41(OUT)/0xC1(IN), wValue=channel, wIndex=iface 0
 * - Open: HOST_FORMAT(0x0000beef LE) → DEVICE_CONFIG → BT_CONST → BITTIMING
 *   → (FD: DATA_BITTIMING) → MODE START
 * - Frame: 12 B header + data[8|64] + optional timestamp_us;
 *   classic 24 B (ts@20), FD 80 B (ts@76); echo_id 0xffffffff = Rx
 * - libusb-1.0.dll loaded dynamically (drivers/candle/vendor → app dir → PATH),
 *   process-wide singleton, never unloaded
 *
 * HW timestamp when feature bit HW_TIMESTAMP is set (1 MHz free-running).
 * Device must be bound to WinUSB (CANable WCID firmware usually is).
 * Stock CANable with SLCAN firmware is CDC serial — use the slcan driver.
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
        void *(*ref_device)(void *) = nullptr;
        void (*unref_device)(void *) = nullptr;
        int  (*claim_interface)(void *, int) = nullptr;
        int  (*release_interface)(void *, int) = nullptr;
        int  (*auto_detach)(void *, int) = nullptr;
        int  (*control)(void *, quint8, quint8, quint16, quint16, quint8 *, quint16, unsigned) = nullptr;
        int  (*bulk)(void *, quint8, quint8 *, int, int *, unsigned) = nullptr;
    };

    /// Load libusb-1.0 (drivers/candle/vendor → app dir → system PATH)
    static LibUsb &usb();

    // ---- USB helpers ----
    /// Whitelist match; `dev` is ref'd — caller must releaseMatches()
    struct UsbMatch {
        void *dev = nullptr;
        RawDeviceDescriptor desc;
        QString model;
        quint8 bus = 0;
        quint8 addr = 0;
        quint8 channels = 0;  ///< set when DEVICE_CONFIG probe succeeds
    };
    /// VID/PID whitelist matches with libusb_ref_device (stable across free_device_list)
    static std::vector<UsbMatch> collectWhitelisted();
    /// Same index space as enumerate(): whitelist ∩ openable DEVICE_CONFIG
    static std::vector<UsbMatch> collectOpenable();
    static void releaseMatches(std::vector<UsbMatch> &matches);
    /// Probe DEVICE_CONFIG; on success fills m.channels and returns true
    static bool probeDeviceConfig(const UsbMatch &m, quint8 *channelsOut);
    static bool devCtrlOut(void *handle, quint8 breq, quint16 wValue, const void *data, quint16 len);
    static bool devCtrlIn(void *handle, quint8 breq, quint16 wValue, void *data, quint16 len);
    bool ctrlOut(quint8 breq, quint16 wValue, const void *data, quint16 len);
    bool ctrlIn(quint8 breq, quint16 wValue, void *data, quint16 len);
    bool readBtConst(int channel, bool extended, BtConstRaw *arb, BtConstRaw *data);
    static bool calcBittiming(const BtConstRaw &c, int bitrate, BtRaw *out);
    quint64 hwTsToNs(quint32 rawUs);
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
