#ifndef CANDEVICE_ZLG_H
#define CANDEVICE_ZLG_H

#include "core/candevice.h"
#include <QLibrary>
#include <QString>
#include <atomic>
#include <memory>

// ZLG SDK 使用 __stdcall 调用约定 (FUNC_CALL)
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
 *
 * 基于 ZLG 官方 zlgcan.h (2025-08-15 版本) 对齐函数签名和结构体布局。
 */
class CanDeviceZLG : public ICanDevice
{
public:
    /// ZLG 设备类型枚举（与 zlgcan.h 设备类型号表一致）
    /// 参考: http://www.zlgcan.com/zlgcan-2/1216/
    enum DeviceType {
        DEV_USBCAN_1      = 3,   ///< USBCAN-1 (8路)
        DEV_USBCAN_2      = 4,   ///< USBCAN-2 (2路)
        DEV_USBCAN_E_U    = 20,  ///< USBCAN-E-U
        DEV_USBCAN_2E_U   = 21,  ///< USBCAN-2E-U
        DEV_USBCAN_4E_U   = 31,  ///< USBCAN-4E-U
        DEV_USBCANFD_200U = 41,  ///< USBCANFD-200U
        DEV_USBCANFD_100U = 42,  ///< USBCANFD-100U
        DEV_USBCANFD_MINI = 43,  ///< USBCANFD-mini
        DEV_USBCANFD_800U = 59,  ///< USBCANFD-800U
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
    // ---- ZLG SDK 函数指针类型 (与 zlgcan.h 官方签名一致) ----
    // DEVICE_HANDLE = void*, CHANNEL_HANDLE = void*
    // INVALID_DEVICE_HANDLE = 0, INVALID_CHANNEL_HANDLE = 0
    using fn_OpenDevice  = void* (ZCAN_CALL *)(unsigned int deviceType, unsigned int deviceIndex, unsigned int reserved);
    using fn_CloseDevice = unsigned int (ZCAN_CALL *)(void* devHandle);
    using fn_InitCan     = void* (ZCAN_CALL *)(void* devHandle, unsigned int canIndex, void* pInitConfig);
    using fn_StartCan    = unsigned int (ZCAN_CALL *)(void* channelHandle);
    using fn_Transmit    = unsigned int (ZCAN_CALL *)(void* channelHandle, void* pTransmit, unsigned int len);
    using fn_TransmitFD  = unsigned int (ZCAN_CALL *)(void* channelHandle, void* pTransmit, unsigned int len);
    using fn_GetRecvNum  = unsigned int (ZCAN_CALL *)(void* channelHandle, unsigned char type);
    using fn_Receive     = unsigned int (ZCAN_CALL *)(void* channelHandle, void* pReceive, unsigned int len, int waitTime);
    using fn_ReceiveFD   = unsigned int (ZCAN_CALL *)(void* channelHandle, void* pReceive, unsigned int len, int waitTime);
    using fn_ResetCan    = unsigned int (ZCAN_CALL *)(void* channelHandle);
    using fn_GetDevInfo  = unsigned int (ZCAN_CALL *)(void* devHandle, void* pInfo);
    using fn_SetValue    = unsigned int (ZCAN_CALL *)(void* devHandle, const char* path, const void* value);
    using fn_GetAvailDev = unsigned int (ZCAN_CALL *)(unsigned int deviceType, void* pInfo);
    using fn_IsDevOnline = unsigned int (ZCAN_CALL *)(void* devHandle);

    // ---- DLL 加载 ----
    bool loadDll();
    void unloadDll();

    // ---- SDK 函数指针 ----
    fn_OpenDevice  m_fn_open     = nullptr;
    fn_CloseDevice m_fn_close    = nullptr;
    fn_InitCan     m_fn_init     = nullptr;
    fn_StartCan    m_fn_start    = nullptr;
    fn_Transmit    m_fn_send     = nullptr;
    fn_TransmitFD  m_fn_sendFD   = nullptr;
    fn_GetRecvNum  m_fn_recvNum  = nullptr;
    fn_Receive     m_fn_recv     = nullptr;
    fn_ReceiveFD   m_fn_recvFD   = nullptr;
    fn_ResetCan    m_fn_reset    = nullptr;
    fn_GetDevInfo  m_fn_devInfo  = nullptr;
    fn_SetValue    m_fn_setVal   = nullptr;
    fn_GetAvailDev m_fn_getAvail = nullptr;
    fn_IsDevOnline m_fn_isOnline = nullptr;

    // ---- 设备状态 ----
    QLibrary m_dll;
    void *m_devHandle = nullptr;    ///< ZCAN_OpenDevice 返回的设备句柄
    void *m_channelHandle = nullptr;  ///< ZCAN_InitCAN 返回的通道句柄
    DeviceType m_devType;
    int m_devIndex = 0;
    int m_channel = 0;
    bool m_opened = false;
    bool m_canFd = false;           ///< 通道是否为 CAN FD 模式
    double m_startTime = 0.0;    ///< 接收起始时间（用于计算相对时间戳）
};

#endif // CANDEVICE_ZLG_H
