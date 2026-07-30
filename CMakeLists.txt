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
#  构建优化：精简调试信息 + LLD 链接器
#  -g1: 仅函数级调试信息（比默认 -g 小很多，链接速度提升 5x+）
#  -fuse-ld=lld: 使用 LLD 链接器（多线程，比 ld.bfd 快 3-5x）
#  注意:
#   - gold 链接器不支持 --subsystem（Windows GUI 程序需要），不可用
#   - --gc-sections 在 ld.bfd 上反而增加链接时间（需扫描所有 section），不使用
#   - PCH (预编译头) 在 src/CMakeLists.txt 中配置
#   - LLD 从项目 tools/lld/ 目录自动检测，不存在则回退 ld.bfd
# ============================================================
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g1)
        message(STATUS "Debug 构建使用 -g1（精简调试信息，加速编译和链接）")
    endif()

    # 检测 LLD 链接器（项目本地 tools/lld/ld.lld.exe）
    set(_lld_path "${CMAKE_SOURCE_DIR}/tools/lld/ld.lld.exe")
    if(EXISTS "${_lld_path}")
        add_compile_options(-fuse-ld=lld)
        add_link_options(-fuse-ld=lld)
        message(STATUS "使用 LLD 链接器 (${_lld_path})")
    else()
        message(STATUS "未检测到 LLD，使用默认 ld.bfd 链接器")
    endif()
endif()

# ============================================================
#  输出目录
# ============================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ============================================================
#  查找 Qt6
# ============================================================
find_package(Qt6 REQUIRED COMPONENTS Widgets)

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
