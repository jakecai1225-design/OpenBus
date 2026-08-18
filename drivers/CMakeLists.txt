# ============================================================
#  驱动插件（.odp 安装单元）— QPluginLoader 原生驱动
#
#  每个 target 输出到 build/bin/drivers/<id>/（与 .odp 包内结构一致），
#  pack 时用 scripts/driver_tool.py 组包（driver.json + driver_<id>.dll
#  + 可选 icon.svg/assets/vendor → .odp）。
#  ABI 契约：与主程序同 Qt 版本 + 同编译器（MinGW 13.1 x64, C++17）。
# ============================================================

# 驱动插件公共头路径（复用 src/core 后端源码，需与 openbus_core 的
# PUBLIC include 保持一致：spdlog 头由 core/logging.h 公共头引入）
set(DRIVER_COMMON_INCLUDES ${CMAKE_SOURCE_DIR}/src)
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/spdlog/include")
    list(APPEND DRIVER_COMMON_INCLUDES ${CMAKE_SOURCE_DIR}/third_party/spdlog/include)
endif()

# 部署包内 driver.json 到驱动输出目录——开发态构建树即 .odp 安装布局
# （DriverRegistry 扫描 <exe>/drivers/<id>/，清单不在场则插件不加载；
# pack 打 .odp 时仍以包源目录清单为准）。各包 target 定义后调用。
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
