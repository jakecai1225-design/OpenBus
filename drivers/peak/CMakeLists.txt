# ============================================================
#  driver_peak — PEAK 外置驱动插件（阶段 3 迁移）
#
#  同一份 CanDevicePEAK 源码编入本 target（内置后端保留保底，
#  外置 v1.0.0 与内置同版本，用于验证 Registry 覆盖/加载链路）。
#  输出目录与 .odp 包内布局一致：build/bin/drivers/peak/driver_peak.dll
# ============================================================

qt_add_plugin(driver_peak)

target_sources(driver_peak PRIVATE
    peak_driver_plugin.h
    peak_driver_plugin.cpp
    ${CMAKE_SOURCE_DIR}/src/core/candevice_peak.h
    ${CMAKE_SOURCE_DIR}/src/core/candevice_peak.cpp
)

target_include_directories(driver_peak PRIVATE
    ${DRIVER_COMMON_INCLUDES}
)

target_link_libraries(driver_peak PRIVATE
    Qt6::Core
    Qt6::Gui
)

set_target_properties(driver_peak PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/peak
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/peak
)
