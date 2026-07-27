# ============================================================
#  可执行目标
# ============================================================
qt_add_executable(sin
    main.cpp
    ui/mainwindow.h
    ui/mainwindow.cpp
    ui/mainwindow.ui
    ../resources/resources.qrc
)

# ============================================================
#  链接库
# ============================================================
target_link_libraries(sin PRIVATE
    Qt6::Widgets
)

# ============================================================
#  头文件搜索路径
#  后续扩展模块 (core / models / services / utils) 时
#  在此添加对应的 include 目录即可
# ============================================================
target_include_directories(sin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ============================================================
#  Windows: 以 GUI 程序方式运行 (不弹出控制台窗口)
# ============================================================
if(WIN32)
    set_target_properties(sin PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# ============================================================
#  安装规则
# ============================================================
install(TARGETS sin
    RUNTIME DESTINATION bin
)
