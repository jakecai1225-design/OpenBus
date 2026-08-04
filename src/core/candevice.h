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
 * 各厂商后端（ZLG、PEAK、CandleLight、SLCAN）分别实现此接口。
 * 厂商特有功能通过 vendorCtrl() 扩展，不丢失硬件能力。
 *
 * 架构位置：src/core/candevice.h
 * 使用方：CanDeviceManager (QObject 桥接层) 持有 ICanDevice 实例
 */
class ICanDevice
{
public:
    virtual ~ICanDevice() = default;

    // ---- 设备枚举 ----

    struct DeviceInfo {
        QString name;        ///< 显示名（如 "USBCANFD-200U #0"）
        int deviceType = 0;  ///< 厂商设备类型 ID
        int deviceIndex = 0; ///< 设备序号（0-based）
        int channels = 1;    ///< 通道数
    };

    /// 枚举所有可用设备（纯函数，不持有状态）
    static std::vector<DeviceInfo> enumerate();

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
};

#endif // CANDEVICE_H
