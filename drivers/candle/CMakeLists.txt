# ============================================================
#  driver_candle — Candle / GS_USB 外置驱动插件
#
#  方案 §14 v2.2 批次 1：一份驱动覆盖 GS_USB 协议全家（CANable /
#  candleLight / CANnectivity / CES CANext FD / ABE / Xylanta Saint3）。
#  同一份 CanDeviceCandle 源码编入本 target；libusb-1.0.dll 运行时动态
#  加载（drivers/candle/vendor → 应用目录 → PATH，scripts/download_libusb.py
#  可自动放置到 driver/）。输出目录与 .odp 包内布局一致：
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
