#ifndef CANDEVICE_VECTOR_H
#define CANDEVICE_VECTOR_H

#include "core/candevice.h"

#include <QString>
#include <cstdint>
#include <vector>

/**
 * @brief Vector XL Driver Library backend (vxlapi64.dll / vxlapi.dll)
 *
 * One XL CAN-capable channel = one DeviceInfo.
 * deviceType stores the global XL channelIndex used with channelMask = 1ull << index.
 *
 * Vendor DLL is NOT bundled (Vector license). Load order:
 *   drivers/vector/vendor/vxlapi64.dll → app dir → PATH / Vector Driver Setup
 *
 * Classic CAN uses xlReceive / xlCanTransmit (INTERFACE_VERSION_V3).
 * ISO CAN FD uses xlCanReceive / xlCanTransmitEx (INTERFACE_VERSION_V4).
 * Mainstream VN16xx / VN56xx / VirtualCAN share this API.
 */
class CanDeviceVector : public ICanDevice
{
public:
    /// @param channelIndex XL global channel index from enumerate().deviceType, or -1 = resolve on open
    explicit CanDeviceVector(int channelIndex = -1);
    ~CanDeviceVector() override;

    Brand brand() const override { return Brand::Vector; }
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
    using XlStatus = short;
    using XlPortHandle = long;
    using XlAccess = std::uint64_t;
    using XlHandle = void *; // HANDLE

    struct Api {
        bool ok = false;
        XlStatus (__stdcall *OpenDriver)() = nullptr;
        XlStatus (__stdcall *CloseDriver)() = nullptr;
        XlStatus (__stdcall *GetDriverConfig)(void *) = nullptr;
        XlStatus (__stdcall *OpenPort)(XlPortHandle *, char *, XlAccess, XlAccess *,
                                       unsigned, unsigned, unsigned) = nullptr;
        XlStatus (__stdcall *ClosePort)(XlPortHandle) = nullptr;
        XlStatus (__stdcall *ActivateChannel)(XlPortHandle, XlAccess, unsigned, unsigned) = nullptr;
        XlStatus (__stdcall *DeactivateChannel)(XlPortHandle, XlAccess) = nullptr;
        XlStatus (__stdcall *CanSetChannelBitrate)(XlPortHandle, XlAccess, unsigned long) = nullptr;
        XlStatus (__stdcall *CanFdSetConfiguration)(XlPortHandle, XlAccess, void *) = nullptr;
        XlStatus (__stdcall *CanSetChannelMode)(XlPortHandle, XlAccess, int, int) = nullptr;
        XlStatus (__stdcall *CanSetChannelOutput)(XlPortHandle, XlAccess, int) = nullptr;
        XlStatus (__stdcall *SetNotification)(XlPortHandle, XlHandle *, int) = nullptr;
        XlStatus (__stdcall *FlushReceiveQueue)(XlPortHandle) = nullptr;
        XlStatus (__stdcall *GetReceiveQueueLevel)(XlPortHandle, int *) = nullptr;
        XlStatus (__stdcall *Receive)(XlPortHandle, unsigned *, void *) = nullptr;
        XlStatus (__stdcall *CanTransmit)(XlPortHandle, XlAccess, unsigned *, void *) = nullptr;
        XlStatus (__stdcall *CanReceive)(XlPortHandle, void *) = nullptr;
        XlStatus (__stdcall *CanTransmitEx)(XlPortHandle, XlAccess, unsigned, unsigned *, void *) = nullptr;
        char *(__stdcall *GetErrorString)(XlStatus) = nullptr;
    };

    static Api &api();
    static QString statusText(XlStatus st);
    static bool lookupChannel(int channelIndex, XlAccess *maskOut, QString *nameOut,
                              bool *fdCapableOut);

    int m_preferredChannel = -1; ///< XL channelIndex from ctor / deviceType
    XlPortHandle m_port = -1;
    XlAccess m_accessMask = 0;
    XlHandle m_notifyEvent = nullptr;
    int m_channel = 0;           ///< 0-based logical channel for CanFrame::channel
    bool m_opened = false;
    bool m_canFd = false;
    bool m_fdCapable = false;
    QString m_deviceName;
};

#endif // CANDEVICE_VECTOR_H
