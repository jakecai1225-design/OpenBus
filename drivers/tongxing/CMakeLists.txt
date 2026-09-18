# ============================================================
#  driver_tongxing — TOSUN libTSCAN external driver plugin
#
#  Same CanDeviceTongXing sources (built-in backend kept as fallback).
#  Vendor DLLs are NOT bundled.
#  Output: build/bin/drivers/tongxing/driver_tongxing.dll
# ============================================================

qt_add_plugin(driver_tongxing)

target_sources(driver_tongxing PRIVATE
    tongxing_driver_plugin.h
    tongxing_driver_plugin.cpp
    ${CMAKE_SOURCE_DIR}/src/core/candevice_tongxing.h
    ${CMAKE_SOURCE_DIR}/src/core/candevice_tongxing.cpp
)

target_include_directories(driver_tongxing PRIVATE
    ${DRIVER_COMMON_INCLUDES}
)

target_link_libraries(driver_tongxing PRIVATE
    Qt6::Core
    Qt6::Gui
)

set_target_properties(driver_tongxing PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/tongxing
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/tongxing
)

deploy_driver_manifest(driver_tongxing)
