#ifndef CANDEVICE_TONGXING_H
#define CANDEVICE_TONGXING_H

#include "core/candevice.h"

#include <QString>
#include <cstdint>
#include <vector>

/**
 * @brief TOSUN / TongXing backend (libTSCAN.dll + libTSH.dll)
 *
 * One TSMaster device = one DeviceInfo. deviceType is the scan index.
 * Channel is selected at open() (0-based).
 *
 * Vendor DLLs are NOT bundled. Load order:
 *   drivers/tongxing/vendor/libTSCAN.dll -> app dir -> PATH
 * libTSH.dll must sit next to libTSCAN.dll (or on PATH).
 *
 * Classic: tscan_config_can_by_baudrate + tscan_transmit_can_async
 * ISO FD:  tscan_config_canfd_by_baudrate + tscan_transmit_canfd_async
 */
class CanDeviceTongXing : public ICanDevice
{
public:
    /// @param deviceIndex scan index from enumerate().deviceType, or -1 = resolve on open
    explicit CanDeviceTongXing(int deviceIndex = -1);
    ~CanDeviceTongXing() override;

    Brand brand() const override { return Brand::TongXing; }
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
    using Handle = std::uintptr_t;

    struct Api {
        bool ok = false;
        void (__stdcall *Initialize)(unsigned char, unsigned char, unsigned char) = nullptr;
        void (__stdcall *Finalize)() = nullptr;
        unsigned (__stdcall *ScanDevices)(unsigned *) = nullptr;
        unsigned (__stdcall *GetDeviceInfo)(unsigned, const char **, const char **, const char **) = nullptr;
        unsigned (__stdcall *Connect)(const char *, Handle *) = nullptr;
        unsigned (__stdcall *Disconnect)(Handle) = nullptr;
        unsigned (__stdcall *GetCanChannelCount)(Handle, int *, unsigned char *) = nullptr;
        unsigned (__stdcall *ConfigCan)(Handle, unsigned, double, unsigned) = nullptr;
        unsigned (__stdcall *ConfigCanFd)(Handle, int, double, double, int, int, int) = nullptr;
        unsigned (__stdcall *TxCanAsync)(Handle, const void *) = nullptr;
        unsigned (__stdcall *TxCanFdAsync)(Handle, const void *) = nullptr;
        unsigned (__stdcall *RxCan)(Handle, void *, int *, unsigned char, unsigned char) = nullptr;
        unsigned (__stdcall *RxCanFd)(Handle, void *, int *, unsigned char, unsigned char) = nullptr;
        unsigned (__stdcall *ClearCan)(Handle, int) = nullptr;
        unsigned (__stdcall *ClearCanFd)(Handle, int) = nullptr;
        int (__stdcall *CanFrameCount)(Handle, int, int *) = nullptr;
        unsigned (__stdcall *ErrorText)(unsigned, const char **) = nullptr;
    };

    static Api &api();
    static QString statusText(unsigned code);

    int m_preferredIndex = -1;
    Handle m_handle = 0;
    int m_channel = 0;
    bool m_opened = false;
    bool m_canFd = false;
    bool m_fdCapable = false;
    QString m_deviceName;
    QString m_serial;
};

#endif // CANDEVICE_TONGXING_H
