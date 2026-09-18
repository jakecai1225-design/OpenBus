#ifndef CANDEVICE_INTREPID_H
#define CANDEVICE_INTREPID_H

#include "core/candevice.h"

#include <QString>
#include <cstdint>
#include <vector>

/**
 * @brief Intrepid ValueCAN / neoVI backend (libicsneo C API — icsneoc)
 *
 * One hardware box = one DeviceInfo. deviceType is the scan index from
 * icsneo_findAllDevices(). Channel at open() maps to the Nth CAN network
 * via icsneo_getNetworkByNumber (1-based).
 *
 * Vendor shared library is NOT bundled. Load order:
 *   drivers/intrepid/vendor/icsneoc -> app dir -> PATH / ld cache
 *
 * Classic + ISO CAN FD via setBaudrate / setFDBaudrate + settingsApplyTemporary.
 */
class CanDeviceIntrepid : public ICanDevice
{
public:
    /// @param deviceIndex scan index from enumerate().deviceType, or -1 = resolve on open
    explicit CanDeviceIntrepid(int deviceIndex = -1);
    ~CanDeviceIntrepid() override;

    Brand brand() const override { return Brand::Intrepid; }
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
#pragma pack(push, 1)
    struct NeoDevice {
        void *device = nullptr;
        int32_t handle = 0;
        uint32_t type = 0;
        char serial[7] = {};
    };
#pragma pack(pop)

    struct Api {
        bool ok = false;
        void (*FindAllDevices)(NeoDevice *, size_t *) = nullptr;
        void (*FreeUnconnected)() = nullptr;
        bool (*OpenDevice)(const NeoDevice *) = nullptr;
        bool (*CloseDevice)(const NeoDevice *) = nullptr;
        bool (*GoOnline)(const NeoDevice *) = nullptr;
        bool (*GoOffline)(const NeoDevice *) = nullptr;
        bool (*EnablePolling)(const NeoDevice *) = nullptr;
        bool (*DisablePolling)(const NeoDevice *) = nullptr;
        bool (*GetMessages)(const NeoDevice *, void *, size_t *, uint64_t) = nullptr;
        bool (*SetPollingLimit)(const NeoDevice *, size_t) = nullptr;
        uint16_t (*GetNetworkByNumber)(const NeoDevice *, uint8_t, unsigned) = nullptr;
        bool (*GetProductName)(const NeoDevice *, char *, size_t *) = nullptr;
        bool (*DescribeDevice)(const NeoDevice *, char *, size_t *) = nullptr;
        bool (*SettingsRefresh)(const NeoDevice *) = nullptr;
        bool (*SettingsApplyTemporary)(const NeoDevice *) = nullptr;
        bool (*SetBaudrate)(const NeoDevice *, uint16_t, int64_t) = nullptr;
        bool (*SetFDBaudrate)(const NeoDevice *, uint16_t, int64_t) = nullptr;
        bool (*Transmit)(const NeoDevice *, const void *) = nullptr;
    };

    static Api &api();
    static int countCanNetworks(const NeoDevice &dev);
    static QString productLabel(const NeoDevice &dev);
    static bool looksFdCapable(uint32_t deviceType);
    bool resolveDevice(int preferIndex, NeoDevice *out);

    int m_preferredIndex = -1;
    QString m_preferredSerial;
    NeoDevice m_device;
    uint16_t m_netId = 0;
    int m_channel = 0;
    bool m_opened = false;
    bool m_canFd = false;
    QString m_deviceName;
};

#endif // CANDEVICE_INTREPID_H
