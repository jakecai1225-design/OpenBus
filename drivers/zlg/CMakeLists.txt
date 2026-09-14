# ============================================================
#  driver_zlg — ZLG external driver plugin (first .odp sample)
#
#  Same CanDeviceZLG sources (built-in backend kept until stage 3;
#  external v1.0.0 matches built-in for Registry load/override tests).
#  Output layout matches .odp: build/bin/drivers/zlg/driver_zlg.dll
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
