# ============================================================
#  driver_candle — Candle / GS_USB external driver plugin
#
#  One driver for the GS_USB family (CANable / candleLight /
#  CANnectivity / CES CANext FD / ABE / Xylanta Saint3).
#  Same CanDeviceCandle sources; libusb-1.0.dll loaded at runtime
#  (drivers/candle/vendor -> app dir -> PATH; scripts/download_libusb.py
#  can place it under driver/). Output matches .odp:
#  build/bin/drivers/candle/driver_candle.dll
# ============================================================

qt_add_plugin(driver_candle)

target_sources(driver_candle PRIVATE
    candle_driver_plugin.h
    candle_driver_plugin.cpp
    ${CMAKE_SOURCE_DIR}/src/core/candevice_candle.h
    ${CMAKE_SOURCE_DIR}/src/core/candevice_candle.cpp
)

target_include_directories(driver_candle PRIVATE
    ${DRIVER_COMMON_INCLUDES}
)

target_link_libraries(driver_candle PRIVATE
    Qt6::Core
    Qt6::Gui
)

set_target_properties(driver_candle PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/candle
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/candle
)

deploy_driver_manifest(driver_candle)
