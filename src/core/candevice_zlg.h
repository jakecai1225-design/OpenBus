#ifndef CANDEVICE_ZLG_H
#define CANDEVICE_ZLG_H

#include "core/candevice.h"
#include <QLibrary>
#include <QString>
#include <atomic>
#include <memory>

// ZLG SDK 使用 __stdcall 调用约定 (WINAPI) — 在 32-bit Windows 上至关重要
#ifdef _WIN32
#define ZCAN_CALL __stdcall
#else
#define ZCAN_CALL
#endif

/**
 * @brief ZLG 致远电子 CAN/CAN FD 设备后端
 *
 * 动态加载 zlgcan.dll，封装全部原生 API。
 * 支持 USBCAN-1/2、USBCANFD-200U 等 ZLG 设备。
 *
 * 架构：ICanDevice → CanDeviceZLG → zlgcan.dll (运行时加载)
 * DLL 缺失时 open() 返回 false，不影响其他后端。
 */
class CanDeviceZLG : public ICanDevice
{
public:
    /// ZLG 设备类型枚举（与 zlgcan.h 一致）
    enum DeviceType {
        DEV_USBCAN_1     = 4,   ///< USBCAN-1 (8路)
        DEV_USBCAN_2     = 5,   ///< USBCAN-2 (2路)
        DEV_CANET_TCP    = 7,   ///< CANET-200E
        DEV_PCI_CANAL    = 20,  ///< PCI-CANal
        DEV_USBCANFD_200U = 35,  ///< USBCANFD-200U
        DEV_USBCANFD_100U = 36,  ///< USBCANFD-100U
        DEV_USBCAN_4E    = 31,  ///< USBCAN-4E
    };

    /// Vendor 扩展指令
    enum VendorCmd {
        CMD_GET_DEV_INFO    = 0x01, ///< 获取设备信息
        CMD_RESET_CAN       = 0x02, ///< 复位 CAN 控制器
        CMD_SET_FILTER      = 0x03, ///< 设置硬件滤波
        CMD_GET_BUS_STATUS  = 0x04, ///< 读取总线状态
    };

    explicit CanDeviceZLG(DeviceType devType = DEV_USBCANFD_200U);
    ~CanDeviceZLG() override;

    // ---- ICanDevice ----
    bool open(int devIndex, int channel, int arbBaud, int dataBaud, bool canFd) override;
    void close() override;
    int send(const CanFrame &frame) override;
    int recv(int timeoutMs, std::vector<CanFrame> &outFrames) override;
    int pendingCount() const override;
    bool isOpen() const override;
    QString deviceName() const override;
    bool vendorCtrl(int cmd, void *param) override;

    /// 检查 zlgcan.dll 是否可加载（不打开设备）
    static bool isAvailable();

    /// 枚举 ZLG 设备
    static std::vector<DeviceInfo> enumerate();

private:
    // ---- ZLG SDK 函数指针类型 (ZLG SDK 使用 __stdcall / WINAPI 调用约定) ----
    // ZCAN_InitCAN 返回 CHANNEL_HANDLE，后续操作使用 channelHandle 而非 devHandle+channel
    using fn_OpenDevice  = void* (ZCAN_CALL *)(int deviceType, int deviceIndex, int reserved);
    using fn_CloseDevice = int   (ZCAN_CALL *)(void* devHandle);
    using fn_InitCan     = void* (ZCAN_CALL *)(void* devHandle, unsigned int channel, void* config);
    using fn_StartCan    = int   (ZCAN_CALL *)(void* channelHandle);
    using fn_Transmit    = int   (ZCAN_CALL *)(void* channelHandle, void* sendBuf, unsigned int len);
    using fn_GetRecvNum  = unsigned int (ZCAN_CALL *)(void* channelHandle);
    using fn_Receive     = int   (ZCAN_CALL *)(void* channelHandle, void* recvBuf, unsigned int len, int timeout);
    using fn_ResetCan    = int   (ZCAN_CALL *)(void* channelHandle);
    using fn_GetDevInfo  = int   (ZCAN_CALL *)(void* devHandle, unsigned char* info, unsigned int len);
    using fn_SetValue    = int   (ZCAN_CALL *)(void* devHandle, const char* valueKey, const char* value);

    // ---- DLL 加载 ----
    bool loadDll();
    void unloadDll();

    // ---- SDK 函数指针 ----
    fn_OpenDevice  m_fn_open    = nullptr;
    fn_CloseDevice m_fn_close   = nullptr;
    fn_InitCan     m_fn_init    = nullptr;
    fn_StartCan    m_fn_start   = nullptr;
    fn_Transmit    m_fn_send    = nullptr;
    fn_GetRecvNum  m_fn_recvNum = nullptr;
    fn_Receive     m_fn_recv   = nullptr;
    fn_ResetCan    m_fn_reset  = nullptr;
    fn_GetDevInfo  m_fn_devInfo = nullptr;
    fn_SetValue    m_fn_setVal = nullptr;

    // ---- 设备状态 ----
    QLibrary m_dll;
    void *m_devHandle = nullptr;    ///< ZCAN_OpenDevice 返回的设备句柄
    void *m_channelHandle = nullptr;  ///< ZCAN_InitCAN 返回的通道句柄
    DeviceType m_devType;
    int m_devIndex = 0;
    int m_channel = 0;
    bool m_opened = false;
    double m_startTime = 0.0;    ///< 接收起始时间（用于计算相对时间戳）
};

#endif // CANDEVICE_ZLG_H
