# ============================================================
#  driver_slcan — SLCAN（Lawicel 串口文本协议）外置驱动插件
#
#  方案 §14 v2.2 批次 1：一份驱动覆盖 SLCAN 协议全家（淘宝适配器 /
#  Lawicel CANUSB / CANable slcan / USBtin / ESP32·Arduino DIY）。
#  同一份 CanDeviceSlcan 源码编入本 target，串口走 Win32 系统驱动，
#  无厂商 SDK 依赖。输出目录与 .odp 包内布局一致：
#  build/bin/drivers/slcan/driver_slcan.dll
# ============================================================

qt_add_plugin(driver_slcan)

target_sources(driver_slcan PRIVATE
    slcan_driver_plugin.h
    slcan_driver_plugin.cpp
    ${CMAKE_SOURCE_DIR}/src/core/candevice_slcan.h
    ${CMAKE_SOURCE_DIR}/src/core/candevice_slcan.cpp
)

target_include_directories(driver_slcan PRIVATE
    ${DRIVER_COMMON_INCLUDES}
)

target_link_libraries(driver_slcan PRIVATE
    Qt6::Core
    Qt6::Gui
)

set_target_properties(driver_slcan PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/slcan
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/slcan
)

deploy_driver_manifest(driver_slcan)
