cmake_minimum_required(VERSION 3.21)

project(openbus
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
#  - Dev 构建档（日常开发）：-O1 -g1，独立 build-dev/ 目录，
#    与全信息 Debug（build/）并存，见 doc/拆分应用实施方案.md §3.1
# ============================================================
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(STATUS "使用默认 ld.bfd 链接器")

    # Dev 档 — 日常开发：O1 + 行号级调试信息，链接体积/耗时数量级下降
    set(CMAKE_CXX_FLAGS_DEV "-O1 -g1")

    # thin archive：只记录对象路径不复制内容，静态库重打包降为毫秒级。
    # 静态库仅供本构建树内部链接使用，不出库/不安装，thin 引用始终有效。
    # 回退方式：删除下面一行即恢复 ar qc 实档案。
    set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> qcT <TARGET> <LINK_FLAGS> <OBJECTS>")

    # 链接器结论（2026-08-19 实测，勿再尝试）：
    #  - ld.bfd（默认）：唯一可靠选择
    #  - ld.gold：MinGW 发行版内为 ELF-only 构建，链接 PE 时报
    #    "--subsystem: unknown option" / "--enable-auto-image-base: unknown option"
    #  - LLD：曾产出损坏的可执行文件
endif()

# ============================================================
#  输出目录
# ============================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ============================================================
#  查找 Qt6
# ============================================================
find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg Network)

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

# 驱动插件（.odp 外置驱动，输出到 build/bin/drivers/<id>/）
add_subdirectory(drivers)

# ============================================================
#  安装规则
# ============================================================
install(TARGETS openbus
    RUNTIME DESTINATION bin
)
