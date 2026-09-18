# ============================================================
#  driver_busmust — BUSMUST USB-CAN(FD) via BMAPI64.dll
#
#  Device Manager name: "BUSMUST USB-CAN(FD) Family"
#  Lineage: proprietary BMAPI (python-can interface=bmcan), NOT candle/gs_usb.
#  Layout: build/bin/drivers/busmust/{driver_busmust.dll,driver.json,vendor/}
# ============================================================

qt_add_plugin(driver_busmust)

target_sources(driver_busmust PRIVATE
    busmust_driver_plugin.h
    busmust_driver_plugin.cpp
    ${CMAKE_SOURCE_DIR}/src/core/candevice_busmust.h
    ${CMAKE_SOURCE_DIR}/src/core/candevice_busmust.cpp
)

target_include_directories(driver_busmust PRIVATE
    ${DRIVER_COMMON_INCLUDES}
    ${CMAKE_SOURCE_DIR}/third_party/bmapi-sdk/include
)

target_link_libraries(driver_busmust PRIVATE
    Qt6::Core
    Qt6::Gui
)

set_target_properties(driver_busmust PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/busmust
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/drivers/busmust
)

deploy_driver_manifest(driver_busmust)

# Bundle BMAPI64.dll next to the plugin
set(_BMAPI_DLL "${CMAKE_SOURCE_DIR}/third_party/bmapi-sdk/bin/win64/BMAPI64.dll")
if(EXISTS "${_BMAPI_DLL}")
    add_custom_command(TARGET driver_busmust POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:driver_busmust>/vendor"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_BMAPI_DLL}"
                "$<TARGET_FILE_DIR:driver_busmust>/vendor/BMAPI64.dll"
        COMMENT "Deploying BMAPI64.dll for driver_busmust")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
    configure_file("${_BMAPI_DLL}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/vendor/BMAPI64.dll" COPYONLY)
else()
    message(STATUS
        "BUSMUST: third_party/bmapi-sdk/bin/win64/BMAPI64.dll missing")
endif()
