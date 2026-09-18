# ============================================================
#  driver_vector — Vector XL external driver plugin
#
#  Same CanDeviceVector sources (built-in backend kept as fallback).
#  Vendor DLL (vxlapi64.dll) is NOT bundled — install Vector Driver Setup.
#  Output: build/bin/drivers/vector/driver_vector.dll
# ============================================================

qt_add_plugin(driver_vector)

target_sources(driver_vector PRIVATE
    vector_driver_plugin.h
    vector_driver_plugin.cpp
    ${CMAKE_SOURCE_DIR}/src/core/candevice_vector.h
    ${CMAKE_SOURCE_DIR}/src/core/candevice_vector.cpp
)

target_include_directories(driver_vector PRIVATE
    ${DRIVER_COMMON_INCLUDES}
)

target_link_libraries(driver_vector PRIVATE
    Qt6::Core
    Qt6::Gui
)

set_target_properties(driver_vector PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/vector
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/vector
)

deploy_driver_manifest(driver_vector)
