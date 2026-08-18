#ifndef CANDEVICE_H
#define CANDEVICE_H

#include "core/canframe.h"
#include <QString>
#include <functional>
#include <memory>
#include <vector>

/**
 * @brief CAN 设备抽象接口
 *
 * 定义统一的 CAN/CAN FD 设备操作契约（打开、关闭、发送、接收）。
 * 各厂商后端（ZLG、PEAK、Kvaser、同星等）分别实现此接口。
 * 厂商特有功能通过 vendorCtrl() 扩展，不丢失硬件能力。
 *
 * 架构位置：src/core/candevice.h
 * 使用方：CanDeviceManager (QObject 桥接层) 持有 ICanDevice 实例
 *
 * 时间戳约定：
 *   - 设备后端可选择填充 CanFrame::timestampNs（硬件纳秒时间戳）
 *   - CanDeviceManager 统一用 steady_clock 补零并派生 timestamp（秒）
 *   - 确保 ZLG/PEAK/Kvaser 混用时时间轴不发生偏移
 */
class ICanDevice
{
public:
    virtual ~ICanDevice() = default;

    // ---- 品牌标识 ----

    /// 设备品牌（用于工厂创建、UI 展示、日志标识）
    enum class Brand {
        ZLG       = 0,   ///< 致远电子 ZLG (USBCANFD-200U 等)
        PEAK      = 1,   ///< PEAK PCAN (PCAN-USB / PCAN-USB FD)
        Kvaser    = 2,   ///< Kvaser (canlib32)
        TongXing  = 3,   ///< 同星科技 (TSMCAN)
        SLCAN     = 4,   ///< 开源 SLCAN / serial-CAN 固件
        Candle    = 5,   ///< Candle / GS_USB 开源 USB CAN (CANable 等)
    };

    /// 品牌显示名
    static QString brandName(Brand b);

    /// 本设备品牌
    virtual Brand brand() const = 0;

    // ---- 设备枚举 ----

    struct DeviceInfo {
        Brand brand = Brand::ZLG;   ///< 设备品牌（兼容字段，新驱动以 driverId 为主）
        QString name;               ///< 显示名（如 "USBCANFD-200U #0"）
        int deviceType = 0;         ///< 厂商设备类型 ID
        int deviceIndex = 0;        ///< 设备序号（0-based）
        int channels = 1;           ///< 通道数
        QString driverId;           ///< 所属驱动 id（DriverRegistry 填充，如 "zlg"）
        bool hasHwTimestamp = true; ///< 是否具备硬件时间戳（方案 §14.6；false=软件补齐）
    };

    /// 枚举所有品牌的所有可用设备
    static std::vector<DeviceInfo> enumerateAll();

    /// 工厂方法：按品牌创建设备实例
    /// @param brand 品牌
    /// @param subType 厂商设备子类型（如 ZLG 的 DEV_USBCANFD_200U，PEAK 的 PCAN_USBFD）
    /// @return 设备实例，DLL 不可用时返回 nullptr
    static std::unique_ptr<ICanDevice> create(Brand brand, int subType = 0);

    // ---- 设备操作 ----

    /// 打开设备并初始化通道
    /// @param devIndex 设备序号
    /// @param channel 通道号（0-based）
    /// @param arbBaud 仲裁段波特率
    /// @param dataBaud 数据段波特率（CAN FD）
    /// @param canFd 是否 CAN FD 模式
    /// @return true 成功
    virtual bool open(int devIndex, int channel, int arbBaud, int dataBaud, bool canFd) = 0;

    /// 关闭设备
    virtual void close() = 0;

    /// 发送单帧
    /// @return 实际发送帧数（0=失败）
    virtual int send(const CanFrame &frame) = 0;

    /// 接收帧（阻塞或带超时）
    /// @param timeoutMs 超时毫秒（-1=阻塞, 0=非阻塞）
    /// @param outFrames 输出帧列表
    /// @return 接收帧数
    virtual int recv(int timeoutMs, std::vector<CanFrame> &outFrames) = 0;

    /// 查询接收队列中待读帧数
    virtual int pendingCount() const = 0;

    /// 是否已打开
    virtual bool isOpen() const = 0;

    /// 设备显示名
    virtual QString deviceName() const = 0;

    // ---- 厂商扩展 ----

    /// 厂商特有功能扩展接口（硬件滤波、错误帧、ISO 切换等）
    /// @param cmd 厂商指令（由各后端定义）
    /// @param param 参数指针
    /// @return true 成功
    virtual bool vendorCtrl(int cmd, void *param) { (void)cmd; (void)param; return false; }

    // ---- 硬件接收滤波器 ----

    /// 设置硬件接收滤波器
    /// @param code 滤波码（frame_id & mask == code & mask → 接收）
    /// @param mask 滤波掩码（1=比较, 0=忽略对应位）
    /// @param extended 是否扩展帧模式
    /// @return true 成功（不支持时返回 false）
    virtual bool setAcceptanceFilter(quint32 code, quint32 mask, bool extended) {
        (void)code; (void)mask; (void)extended; return false;
    }

    /// 清除硬件接收滤波器（接收所有帧）
    /// @return true 成功
    virtual bool clearAcceptanceFilter() { return false; }
};

#endif // CANDEVICE_H
