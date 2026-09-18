# ============================================================
#  Driver plugins (.odp install units) — QPluginLoader native drivers
#
#  Each target writes to build/bin/drivers/<id>/ (same layout as .odp).
#  Pack with scripts/driver_tool.py (driver.json + driver_<id>.dll
#  + optional icon.svg/assets/vendor -> .odp).
#  ABI: same Qt version + same compiler as the main app (MSYS2 UCRT64, C++17).
# ============================================================

# Shared include paths for driver plugins (reuse src/core; keep in sync with
# openbus_data PUBLIC includes: spdlog via core/logging.h).
set(DRIVER_COMMON_INCLUDES ${CMAKE_SOURCE_DIR}/src)
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/spdlog/include")
    list(APPEND DRIVER_COMMON_INCLUDES ${CMAKE_SOURCE_DIR}/third_party/spdlog/include)
endif()

# Copy driver.json next to the plugin DLL so the build tree matches .odp layout
# (DriverRegistry scans <exe>/drivers/<id>/; missing manifest => not loaded).
# Pack still uses the package source manifest.
function(deploy_driver_manifest target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/driver.json"
                "$<TARGET_FILE_DIR:${target}>/driver.json"
        COMMENT "Deploying driver.json (${target})")
endfunction()

add_subdirectory(zlg)
add_subdirectory(peak)
add_subdirectory(kvaser)
add_subdirectory(slcan)
add_subdirectory(candle)
add_subdirectory(busmust)
