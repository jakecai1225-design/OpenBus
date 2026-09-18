# ============================================================
#  driver_intrepid — Intrepid ValueCAN / neoVI plugin (libicsneo)
#
#  Same CanDeviceIntrepid sources (built-in backend kept as fallback).
#  Vendor shared library (icsneoc) is NOT bundled.
#  Output: build/bin/drivers/intrepid/driver_intrepid.dll|.so
# ============================================================

qt_add_plugin(driver_intrepid)

target_sources(driver_intrepid PRIVATE
    intrepid_driver_plugin.h
    intrepid_driver_plugin.cpp
    ${CMAKE_SOURCE_DIR}/src/core/candevice_intrepid.h
    ${CMAKE_SOURCE_DIR}/src/core/candevice_intrepid.cpp
)

target_include_directories(driver_intrepid PRIVATE
    ${DRIVER_COMMON_INCLUDES}
)

target_link_libraries(driver_intrepid PRIVATE
    Qt6::Core
    Qt6::Gui
)

set_target_properties(driver_intrepid PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/intrepid
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/intrepid
)

deploy_driver_manifest(driver_intrepid)
