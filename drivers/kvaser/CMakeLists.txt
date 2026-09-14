# ============================================================
#  driver_kvaser — Kvaser external driver plugin (stage 3 migration)
#
#  Same CanDeviceKvaser sources (built-in backend kept as fallback;
#  external v1.0.0 matches built-in for Registry load/override tests).
#  Output matches .odp: build/bin/drivers/kvaser/driver_kvaser.dll
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

deploy_driver_manifest(driver_kvaser)
