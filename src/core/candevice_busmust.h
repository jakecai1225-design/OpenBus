#ifndef CANDEVICE_BUSMUST_H
#define CANDEVICE_BUSMUST_H

#include "core/candevice.h"

#include <QString>
#include <vector>

/**
 * @brief BUSMUST USB-CAN(FD) backend via official BMAPI (BMAPI64.dll)
 *
 * Device Manager shows "BUSMUST USB-CAN(FD) Family" (e.g. VID_0810&PID_E122).
 * Lineage is proprietary BMAPI / python-can `bmcan` — not ZLG, PEAK, or candle/gs_usb.
 *
 * Runtime: load BMAPI64.dll from drivers/busmust/vendor or next to openbus.exe.
 * SDK: third_party/bmapi-sdk (headers + win64 DLL from busmust/bmapi-sdk releases).
 */
class CanDeviceBusmust : public ICanDevice
{
public:
    explicit CanDeviceBusmust(int channelIndex = 0);
    ~CanDeviceBusmust() override;

    Brand brand() const override { return Brand::Busmust; }
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
    void *m_handle = nullptr;   ///< BM_ChannelHandle
    int m_channelIndex = 0;
    bool m_canFd = false;
    bool m_opened = false;
    QString m_name;
};

#endif // CANDEVICE_BUSMUST_H
