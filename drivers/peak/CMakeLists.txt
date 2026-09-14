# ============================================================
#  driver_peak — PEAK external driver plugin (stage 3 migration)
#
#  Same CanDevicePEAK sources (built-in backend kept as fallback;
#  external v1.0.0 matches built-in for Registry load/override tests).
#  Output matches .odp: build/bin/drivers/peak/driver_peak.dll
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

deploy_driver_manifest(driver_peak)
