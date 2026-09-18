# ============================================================
#  driver_ixxat — IXXAT VCI4 external driver plugin
#
#  Same CanDeviceIxxat sources (built-in backend kept as fallback).
#  Vendor DLL (vcinpl2.dll) is NOT bundled.
#  Output: build/bin/drivers/ixxat/driver_ixxat.dll
# ============================================================

qt_add_plugin(driver_ixxat)

target_sources(driver_ixxat PRIVATE
    ixxat_driver_plugin.h
    ixxat_driver_plugin.cpp
    ${CMAKE_SOURCE_DIR}/src/core/candevice_ixxat.h
    ${CMAKE_SOURCE_DIR}/src/core/candevice_ixxat.cpp
)

target_include_directories(driver_ixxat PRIVATE
    ${DRIVER_COMMON_INCLUDES}
)

target_link_libraries(driver_ixxat PRIVATE
    Qt6::Core
    Qt6::Gui
)

set_target_properties(driver_ixxat PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/ixxat
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/ixxat
)

deploy_driver_manifest(driver_ixxat)
