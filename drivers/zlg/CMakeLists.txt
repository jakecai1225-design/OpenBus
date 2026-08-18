# ============================================================
#  driver_zlg — ZLG 外置驱动插件（首个 .odp 样例）
#
#  同一份 CanDeviceZLG 源码编入本 target（阶段 3 前内置后端保留，
#  外置 v1.0.0 与内置同版本，用于验证 Registry 覆盖/加载链路）。
#  输出目录与 .odp 包内布局一致：build/bin/drivers/zlg/driver_zlg.dll
# ============================================================

qt_add_plugin(driver_zlg)

target_sources(driver_zlg PRIVATE
    zlg_driver_plugin.h
    zlg_driver_plugin.cpp
    ${CMAKE_SOURCE_DIR}/src/core/candevice_zlg.h
    ${CMAKE_SOURCE_DIR}/src/core/candevice_zlg.cpp
)

target_include_directories(driver_zlg PRIVATE
    ${DRIVER_COMMON_INCLUDES}
)

target_link_libraries(driver_zlg PRIVATE
    Qt6::Core
    Qt6::Gui
)

set_target_properties(driver_zlg PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/zlg
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/zlg
)

deploy_driver_manifest(driver_zlg)
