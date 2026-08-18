# ============================================================
#  driver_kvaser — Kvaser 外置驱动插件（阶段 3 迁移）
#
#  同一份 CanDeviceKvaser 源码编入本 target（内置后端保留保底，
#  外置 v1.0.0 与内置同版本，用于验证 Registry 覆盖/加载链路）。
#  输出目录与 .odp 包内布局一致：build/bin/drivers/kvaser/driver_kvaser.dll
# ============================================================

qt_add_plugin(driver_kvaser)

target_sources(driver_kvaser PRIVATE
    kvaser_driver_plugin.h
    kvaser_driver_plugin.cpp
    ${CMAKE_SOURCE_DIR}/src/core/candevice_kvaser.h
    ${CMAKE_SOURCE_DIR}/src/core/candevice_kvaser.cpp
)

target_include_directories(driver_kvaser PRIVATE
    ${DRIVER_COMMON_INCLUDES}
)

target_link_libraries(driver_kvaser PRIVATE
    Qt6::Core
    Qt6::Gui
)

set_target_properties(driver_kvaser PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/kvaser
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/kvaser
)
