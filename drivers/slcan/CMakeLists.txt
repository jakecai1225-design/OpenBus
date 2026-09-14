# ============================================================
#  driver_slcan — SLCAN (Lawicel serial text protocol) plugin
#
#  One driver for the SLCAN family (Taobao adapters / Lawicel CANUSB /
#  CANable slcan / USBtin / ESP32·Arduino DIY). Same CanDeviceSlcan
#  sources; serial via Win32; no vendor SDK. Output layout matches .odp:
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
