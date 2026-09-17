# ============================================================
#  driver_peak — PEAK PCAN external plugin (PCAN-Basic)
#
#  Same CanDevicePEAK sources as the built-in backend.
#  Runtime loads PCANBasic.dll (official PEAK API used by PCAN-View /
#  Cangaroo), NOT the KMDF PCAN_USB.sys / legacy PCANUSB.dll.
#  Layout: build/bin/drivers/peak/{driver_peak.dll,driver.json,vendor/}
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

# Bundle official x64 PCANBasic.dll next to the plugin
set(_PEAK_BASIC "${CMAKE_SOURCE_DIR}/third_party/PCAN-Basic/x64/PCANBasic.dll")
if(EXISTS "${_PEAK_BASIC}")
    add_custom_command(TARGET driver_peak POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:driver_peak>/vendor"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_PEAK_BASIC}"
                "$<TARGET_FILE_DIR:driver_peak>/vendor/PCANBasic.dll"
        COMMENT "Deploying PCANBasic.dll for driver_peak")
    # Also stage into source tree vendor/ for docs / smoke scripts
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
    configure_file("${_PEAK_BASIC}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/vendor/PCANBasic.dll" COPYONLY)
else()
    message(STATUS
        "PEAK: third_party/PCAN-Basic/x64/PCANBasic.dll missing")
endif()
