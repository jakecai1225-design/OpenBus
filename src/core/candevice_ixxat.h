#ifndef CANDEVICE_IXXAT_H
#define CANDEVICE_IXXAT_H

#include "core/candevice.h"

#include <QString>
#include <cstdint>
#include <vector>

/**
 * @brief IXXAT VCI4 backend (vcinpl2.dll)
 *
 * One VCI device = one DeviceInfo. deviceType is the enumeration index.
 * Channel is selected at open() (0-based).
 *
 * Vendor DLL is NOT bundled. Load order:
 *   drivers/ixxat/vendor/vcinpl2.dll -> app dir -> PATH
 *
 * Classic and ISO CAN FD both use vcinpl2 (CANMSG2 + CANBTP).
 * USB-to-CAN V2 / compact / FD share this API.
 */
class CanDeviceIxxat : public ICanDevice
{
public:
    /// @param deviceIndex scan index from enumerate().deviceType, or -1 = resolve on open
    explicit CanDeviceIxxat(int deviceIndex = -1);
    ~CanDeviceIxxat() override;

    Brand brand() const override { return Brand::Ixxat; }
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
    using Hr = unsigned long;
    using Handle = void *;

    struct Api {
        bool ok = false;
        Hr (__stdcall *Initialize)() = nullptr;
        void (__stdcall *FormatError)(Hr, char *, unsigned) = nullptr;
        Hr (__stdcall *EnumOpen)(Handle *) = nullptr;
        Hr (__stdcall *EnumClose)(Handle) = nullptr;
        Hr (__stdcall *EnumNext)(Handle, void *) = nullptr;
        Hr (__stdcall *DeviceOpen)(void *, Handle *) = nullptr;
        Hr (__stdcall *DeviceClose)(Handle) = nullptr;
        Hr (__stdcall *ChannelOpen)(Handle, unsigned, long, Handle *) = nullptr;
        Hr (__stdcall *ChannelInit)(Handle, unsigned short, unsigned short,
                                    unsigned short, unsigned short, unsigned,
                                    unsigned char) = nullptr;
        Hr (__stdcall *ChannelActivate)(Handle, long) = nullptr;
        Hr (__stdcall *ChannelClose)(Handle) = nullptr;
        Hr (__stdcall *ReadMessage)(Handle, unsigned, void *) = nullptr;
        Hr (__stdcall *TxPost)(Handle, void *) = nullptr;
        Hr (__stdcall *ControlOpen)(Handle, unsigned, Handle *) = nullptr;
        Hr (__stdcall *ControlInit)(Handle, unsigned char, unsigned char, unsigned char,
                                    unsigned char, unsigned, unsigned, void *, void *) = nullptr;
        Hr (__stdcall *ControlClose)(Handle) = nullptr;
        Hr (__stdcall *ControlStart)(Handle, long) = nullptr;
        Hr (__stdcall *ControlGetCaps)(Handle, void *) = nullptr;
        Hr (__stdcall *WaitRx)(Handle, unsigned) = nullptr;
    };

    struct Found {
        bool ok = false;
        unsigned char objectId[8] = {};
        QString name;
        bool fdCapable = false;
        int channels = 1;
    };

    static Api &api();
    static QString statusText(Hr hr);
    static bool quietRead(Hr hr);
    static Found findDevice(int index);

    int m_preferredIndex = -1;
    Handle m_device = nullptr;
    Handle m_channelH = nullptr;
    Handle m_control = nullptr;
    int m_channel = 0;
    bool m_opened = false;
    bool m_canFd = false;
    double m_tickHz = 0;
    QString m_deviceName;
};

#endif // CANDEVICE_IXXAT_H
