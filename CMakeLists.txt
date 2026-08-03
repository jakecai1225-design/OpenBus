cmake_minimum_required(VERSION 3.21)

project(sin
    VERSION 0.1.0
    DESCRIPTION "报文解析、分析、回放、录制、trace、graphic 桌面软件"
    LANGUAGES CXX
)

# ============================================================
#  C++ 标准
# ============================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
#  Qt 自动化处理 (MOC / UIC / RCC)
# ============================================================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ============================================================
#  构建配置说明
#  - PCH (预编译头) 在 src/CMakeLists.txt 中配置
#  - 使用默认 ld.bfd 链接器（兼容性最好，无文件锁问题）
#  - 不使用 ccache（与 MinGW g++ 13 的 PCH 不兼容，会静默崩溃）
#  - 不使用 LLD 链接器（在 Windows 上会导致文件锁问题）
# ============================================================
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(STATUS "使用默认 ld.bfd 链接器")
endif()

# ============================================================
#  输出目录
# ============================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ============================================================
#  查找 Qt6
# ============================================================
find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport)

# 启用 Qt 标准项目设置
qt_standard_project_setup()

# ============================================================
#  第三方依赖
# ============================================================
include(${CMAKE_SOURCE_DIR}/third_party/Dependencies.cmake)

# ============================================================
#  子目录
# ============================================================
add_subdirectory(src)

# ============================================================
#  安装规则
# ============================================================
install(TARGETS sin
    RUNTIME DESTINATION bin
)
