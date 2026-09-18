#ifndef CANDEVICE_SOCKETCAN_H
#define CANDEVICE_SOCKETCAN_H

#include "core/candevice.h"

#include <QString>
#include <vector>

/**
 * @brief Linux SocketCAN backend (PF_CAN / SOCK_RAW / CAN_RAW)
 *
 * One network interface (can0 / vcan0 / slcan0) = one DeviceInfo.
 * deviceType stores the kernel ifindex (stable within a boot).
 *
 * No vendor DLL. Bitrate is normally configured by the OS
 * (`ip link set can0 type can bitrate ...`). open() binds the socket;
 * when canFd is requested it enables CAN_RAW_FD_FRAMES if the iface MTU
 * is CANFD_MTU (72).
 *
 * Windows / non-Linux builds compile a stub with isAvailable() == false.
 */
class CanDeviceSocketCan : public ICanDevice
{
public:
    /// @param ifIndex kernel ifindex from enumerate().deviceType, or -1 = resolve on open
    explicit CanDeviceSocketCan(int ifIndex = -1);
    ~CanDeviceSocketCan() override;

    Brand brand() const override { return Brand::SocketCan; }
    bool open(int devIndex, int channel, int arbBaud, int dataBaud, bool canFd) override;
    void close() override;
    int send(const CanFrame &frame) override;
    int recv(int timeoutMs, std::vector<CanFrame> &outFrames) override;
    int pendingCount() const override;
    bool isOpen() const override { return m_opened; }
    QString deviceName() const override;

    static bool isAvailable();
    static std::vector<DeviceInfo> enumerate();

private:
    int m_preferredIfIndex = -1;
    int m_fd = -1;
    int m_channel = 0;
    bool m_opened = false;
    bool m_canFd = false;
    QString m_ifName;
};

#endif // CANDEVICE_SOCKETCAN_H
