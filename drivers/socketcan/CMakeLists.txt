# ============================================================
#  driver_socketcan — Linux SocketCAN plugin
#
#  Same CanDeviceSocketCan sources. On Windows the backend is a stub
#  (isAvailable=false). On Linux it uses PF_CAN SOCK_RAW.
#  Output: build/bin/drivers/socketcan/driver_socketcan.dll|.so
# ============================================================

qt_add_plugin(driver_socketcan)

target_sources(driver_socketcan PRIVATE
    socketcan_driver_plugin.h
    socketcan_driver_plugin.cpp
    ${CMAKE_SOURCE_DIR}/src/core/candevice_socketcan.h
    ${CMAKE_SOURCE_DIR}/src/core/candevice_socketcan.cpp
)

target_include_directories(driver_socketcan PRIVATE
    ${DRIVER_COMMON_INCLUDES}
)

target_link_libraries(driver_socketcan PRIVATE
    Qt6::Core
    Qt6::Gui
)

set_target_properties(driver_socketcan PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/socketcan
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/socketcan
)

deploy_driver_manifest(driver_socketcan)
