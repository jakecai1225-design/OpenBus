#ifndef CANDEVICE_PEAK_H
#define CANDEVICE_PEAK_H

#include "core/candevice.h"
#include <QLibrary>
#include <QString>
#include <atomic>
#include <memory>

/**
 * @brief PEAK PCAN CAN/CAN FD 设备后端
 *
 * 动态加载 PCANUSB.dll，封装 PCAN-Basic API。
 * 支持 PCAN-USB、PCAN-USB FD、PCAN-USB Pro FD 等设备。
 *
 * 架构：ICanDevice → CanDevicePEAK → PCANUSB.dll (运行时加载)
 * DLL 缺失时 isAvailable() 返回 false，不影响其他后端。
 *
 * 时间戳：PCAN 硬件时间戳（微秒精度）转为纳秒填充 timestampNs。
 */
class CanDevicePEAK : public ICanDevice
{
public:
    /// PEAK 设备通道号（与 PCAN-Basic API 定义一致）
    enum DeviceType {
        PCAN_USB        = 0x51,   ///< PCAN-USB (Classic CAN)
        PCAN_USBFD      = 0x54,   ///< PCAN-USB FD
        PCAN_USBPROFD   = 0x56,   ///< PCAN-USB Pro FD
    };

    explicit CanDevicePEAK(DeviceType devType = PCAN_USBFD);
    ~CanDevicePEAK() override;

    // ---- ICanDevice ----
    Brand brand() const override { return Brand::PEAK; }
    bool open(int devIndex, int channel, int arbBaud, int dataBaud, bool canFd) override;
    void close() override;
    int send(const CanFrame &frame) override;
    int recv(int timeoutMs, std::vector<CanFrame> &outFrames) override;
    int pendingCount() const override;
    bool isOpen() const override;
    QString deviceName() const override;

    /// 检查 PCANUSB.dll 是否可加载
    static bool isAvailable();

    /// 枚举 PEAK 设备
    static std::vector<DeviceInfo> enumerate();

private:
    // ---- PCAN-Basic 类型定义 (与 PCANBasic.h 对齐) ----
    using TPCANStatus  = unsigned int;
    using TPCANHandle  = unsigned short;
    using TPCANBaudrate = unsigned short;
    using TPCANParameter = unsigned char;

    // PCAN 消息结构 (Classic CAN)
    struct TPCANMsg {
        unsigned int    ID;          // 11/29 bit ID
        unsigned char   MSGTYPE;     // 消息类型
        unsigned char   LEN;         // 数据长度
        unsigned char   DATA[8];     // 数据
    };

    // PCAN 消息结构 (CAN FD)
    struct TPCANMsgFD {
        unsigned int    ID;
        unsigned char   MSGTYPE;
        unsigned char   DLC;
        unsigned char   DATA[64];
    };

    // PCAN 时间戳
    struct TPCANTimestamp {
        unsigned int  millis;          // 毫秒
        unsigned int  millis_overflow; // 毫秒溢出
        unsigned short micros;         // 微秒
    };

    // ---- 函数指针类型 (__stdcall) ----
    using fn_Initialize    = TPCANStatus (__stdcall *)(TPCANHandle, TPCANBaudrate, int, int, int);
    using fn_InitializeFD  = TPCANStatus (__stdcall *)(TPCANHandle, const char*);
    using fn_Read          = TPCANStatus (__stdcall *)(TPCANHandle, TPCANMsg*, TPCANTimestamp*);
    using fn_ReadFD        = TPCANStatus (__stdcall *)(TPCANHandle, TPCANMsgFD*, TPCANTimestamp*);
    using fn_Write         = TPCANStatus (__stdcall *)(TPCANHandle, TPCANMsg*);
    using fn_WriteFD       = TPCANStatus (__stdcall *)(TPCANHandle, TPCANMsgFD*);
    using fn_Uninitialize  = TPCANStatus (__stdcall *)(TPCANHandle);
    using fn_GetValue      = TPCANStatus (__stdcall *)(TPCANHandle, TPCANParameter, void*, int);
    using fn_GetStatus     = TPCANStatus (__stdcall *)(TPCANHandle);

    // ---- DLL 加载 ----
    bool loadDll();
    void unloadDll();

    // ---- SDK 函数指针 ----
    fn_Initialize   m_fn_init    = nullptr;
    fn_InitializeFD m_fn_initFD  = nullptr;
    fn_Read         m_fn_read    = nullptr;
    fn_ReadFD       m_fn_readFD  = nullptr;
    fn_Write        m_fn_write   = nullptr;
    fn_WriteFD      m_fn_writeFD = nullptr;
    fn_Uninitialize m_fn_uninit  = nullptr;
    fn_GetValue     m_fn_getVal  = nullptr;
    fn_GetStatus    m_fn_status  = nullptr;

    // ---- 设备状态 ----
    QLibrary m_dll;
    TPCANHandle m_pcanHandle = 0;  ///< PCAN 通道句柄 (DeviceType + channel)
    DeviceType m_devType;
    int m_devIndex = 0;
    int m_channel = 0;             ///< 逻辑通道号 (0-based)
    bool m_opened = false;
    bool m_canFd = false;
    QString m_deviceName;

    /// 将 PCAN 时间戳转为纳秒
    quint64 pcanTsToNs(const TPCANTimestamp &ts) const;

    /// 毫秒溢出计数累计（PCAN 硬件时间戳以设备启动为起点）
    std::chrono::steady_clock::time_point m_startClock;
};

#endif // CANDEVICE_PEAK_H
