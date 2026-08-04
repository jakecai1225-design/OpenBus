#include "candevice.h"
#include "candevice_zlg.h"

std::vector<ICanDevice::DeviceInfo> ICanDevice::enumerate()
{
    std::vector<DeviceInfo> list;

    // ZLG 设备
    auto zlgDevs = CanDeviceZLG::enumerate();
    list.insert(list.end(), zlgDevs.begin(), zlgDevs.end());

    // 后续扩展: PEAK, CandleLight, SLCAN
    return list;
}
