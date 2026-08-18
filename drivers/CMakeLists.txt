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

add_subdirectory(zlg)
add_subdirectory(peak)
add_subdirectory(kvaser)
