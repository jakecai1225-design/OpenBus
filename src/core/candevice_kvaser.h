#ifndef CANDEVICE_KVASER_H
#define CANDEVICE_KVASER_H

#include "core/candevice.h"

#include <QString>
#include <cstddef>
#include <vector>

/**
 * @brief Kvaser CANlib backend (canlib32.dll)
 *
 * One CANlib channel = one DeviceInfo (same model as PEAK).
 * deviceType stores the CANlib channel number used by canOpenChannel().
 *
 * Vendor DLL is NOT bundled (Kvaser license). Load order:
 *   drivers/kvaser/vendor/canlib32.dll → app dir → PATH / system install
 *
 * Supports classic CAN and ISO CAN FD via canOPEN_CAN_FD + canSetBusParamsFd.
 * Mainstream hardware (Leaf Pro HS v2, USBcan Pro, U100, PCIEcan v3, Memorator Pro,
 * Hybrid Pro, Virtual) all share this single CANlib API.
 */
class CanDeviceKvaser : public ICanDevice
{
public:
    /// @param canlibChannel CANlib channel from enumerate().deviceType, or -1 = resolve on open
    explicit CanDeviceKvaser(int canlibChannel = -1);
    ~CanDeviceKvaser() override;

    Brand brand() const override { return Brand::Kvaser; }
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
    using CanHandle = int;
    using CanStatus = int;

    struct Api {
        bool ok = false;
        void (__stdcall *InitializeLibrary)() = nullptr;
        CanStatus (__stdcall *GetNumberOfChannels)(int *) = nullptr;
        CanStatus (__stdcall *GetChannelData)(int, int, void *, size_t) = nullptr;
        CanHandle (__stdcall *OpenChannel)(int, int) = nullptr;
        CanStatus (__stdcall *Close)(CanHandle) = nullptr;
        CanStatus (__stdcall *BusOn)(CanHandle) = nullptr;
        CanStatus (__stdcall *BusOff)(CanHandle) = nullptr;
        CanStatus (__stdcall *SetBusParams)(CanHandle, long, unsigned, unsigned, unsigned, unsigned, unsigned) = nullptr;
        CanStatus (__stdcall *SetBusParamsFd)(CanHandle, long, unsigned, unsigned, unsigned) = nullptr;
        CanStatus (__stdcall *Write)(CanHandle, long, void *, unsigned, unsigned) = nullptr;
        CanStatus (__stdcall *ReadWait)(CanHandle, long *, void *, unsigned *, unsigned *, unsigned long *, unsigned long) = nullptr;
        CanStatus (__stdcall *Read)(CanHandle, long *, void *, unsigned *, unsigned *, unsigned long *) = nullptr;
        CanStatus (__stdcall *GetErrorText)(CanStatus, char *, size_t) = nullptr;
    };

    static Api &api();
    static long classicBitrate(int baud);
    static long fdBitrate(int baud);
    static QString statusText(CanStatus st);

    int m_preferredChannel = -1; ///< CANlib channel from ctor / deviceType
    CanHandle m_handle = -1;
    int m_channel = 0;           ///< 0-based logical channel for CanFrame::channel
    bool m_opened = false;
    bool m_canFd = false;
    bool m_fdCapable = false;
    QString m_deviceName;
};

#endif // CANDEVICE_KVASER_H
