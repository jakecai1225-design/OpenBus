#ifndef CANDEVICE_PEAK_H
#define CANDEVICE_PEAK_H

#include "core/candevice.h"

#include <QString>
#include <vector>

/**
 * @brief PEAK PCAN backend via official PCAN-Basic API (PCANBasic.dll)
 *
 * Same userland stack as PCAN-View / Cangaroo / modern PEAK samples:
 *   Device Manager "PCAN_USB" (KMDF)  →  PCANBasic.dll  →  this class
 *
 * Do NOT load PCANUSB.dll / talk to the kernel driver directly.
 * BusMaster's legacy CanApi2 path is obsolete; we reuse the vendored
 * third_party/PCAN-Basic package (x64/PCANBasic.dll).
 *
 * Channel handles (TPCANHandle) are the real device identity, e.g.
 * PCAN_USBBUS1 = 0x51 for a classic PCAN-USB. Enumerate via
 * PCAN_ATTACHED_CHANNELS; open with that handle.
 */
class CanDevicePEAK : public ICanDevice
{
public:
    /// Well-known USB channel handles (PCANBasic.h); also used as deviceType
    enum ChannelHandle : int {
        PCAN_USBBUS1    = 0x51,
        PCAN_USBBUS2    = 0x52,
        PCAN_USBBUS3    = 0x53,
        PCAN_USBBUS4    = 0x54,
        PCAN_USBBUS5    = 0x55,
        PCAN_USBBUS6    = 0x56,
        // Legacy aliases kept for Call sites that still say "DeviceType"
        PCAN_USB        = PCAN_USBBUS1,
        PCAN_USBFD      = PCAN_USBBUS4,   // NOT an FD type — USBBUS4 handle
        PCAN_USBPROFD   = PCAN_USBBUS6,
    };

    /// @param channelHandle TPCANHandle from enumerate (deviceType), or 0 = resolve on open
    explicit CanDevicePEAK(int channelHandle = 0);
    ~CanDevicePEAK() override;

    Brand brand() const override { return Brand::PEAK; }
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
    using TPCANStatus = unsigned int;
    using TPCANHandle = unsigned short;
    using TPCANBaudrate = unsigned short;
    using TPCANParameter = unsigned char;

    // Layout matches MSVC PCANBasic.h (natural alignment — no pack(1))
    struct TPCANMsg {
        unsigned int  ID;
        unsigned char MSGTYPE;
        unsigned char LEN;
        unsigned char DATA[8];
    };
    struct TPCANMsgFD {
        unsigned int  ID;
        unsigned char MSGTYPE;
        unsigned char DLC;
        unsigned char DATA[64];
    };
    struct TPCANTimestamp {
        unsigned int   millis;
        unsigned short millis_overflow;
        unsigned short micros;
    };
    struct TPCANChannelInformation {
        TPCANHandle   channel_handle;
        unsigned char device_type;
        unsigned char controller_number;
        unsigned int  device_features;
        char          device_name[33];
        unsigned int  device_id;
        unsigned int  channel_condition;
    };

    struct Api {
        bool ok = false;
        TPCANStatus (__stdcall *Initialize)(TPCANHandle, TPCANBaudrate, unsigned char, unsigned int, unsigned short) = nullptr;
        TPCANStatus (__stdcall *InitializeFD)(TPCANHandle, const char *) = nullptr;
        TPCANStatus (__stdcall *Uninitialize)(TPCANHandle) = nullptr;
        TPCANStatus (__stdcall *Read)(TPCANHandle, TPCANMsg *, TPCANTimestamp *) = nullptr;
        TPCANStatus (__stdcall *ReadFD)(TPCANHandle, TPCANMsgFD *, unsigned long long *) = nullptr;
        TPCANStatus (__stdcall *Write)(TPCANHandle, TPCANMsg *) = nullptr;
        TPCANStatus (__stdcall *WriteFD)(TPCANHandle, TPCANMsgFD *) = nullptr;
        TPCANStatus (__stdcall *GetValue)(TPCANHandle, TPCANParameter, void *, unsigned int) = nullptr;
        TPCANStatus (__stdcall *SetValue)(TPCANHandle, TPCANParameter, void *, unsigned int) = nullptr;
        TPCANStatus (__stdcall *GetStatus)(TPCANHandle) = nullptr;
    };

    static Api &api();
    static TPCANBaudrate baudToBtr(int bitrate);
    static QByteArray buildFdBitrate(int arbBaud, int dataBaud);
    static quint64 classicTsToNs(const TPCANTimestamp &ts);

    int m_preferredHandle = 0;   ///< from ctor / deviceType
    TPCANHandle m_handle = 0;
    int m_channel = 0;           ///< 0-based logical channel for CanFrame::channel
    bool m_opened = false;
    bool m_canFd = false;
    bool m_fdCapable = false;
    QString m_deviceName;
};

#endif // CANDEVICE_PEAK_H
