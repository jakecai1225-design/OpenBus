# ============================================================
#  源文件列表
# ============================================================

# core 层 — 数据结构、录制、回放、模拟器
set(SRC_CORE
    core/canframe.h
    core/recorder.h
    core/recorder.cpp
    core/player.h
    core/player.cpp
    core/cansimulator.h
    core/cansimulator.cpp
)

# models 层 — Qt 数据模型
set(SRC_MODELS
    models/cantracemodel.h
    models/cantracemodel.cpp
    models/canfilterproxymodel.h
    models/canfilterproxymodel.cpp
)

# utils 层 — 工具函数
set(SRC_UTILS
    utils/canutils.h
    utils/canutils.cpp
)

# ui 层 — 界面组件
set(SRC_UI
    ui/mainwindow.h
    ui/mainwindow.cpp
    ui/traceview.h
    ui/traceview.cpp
    ui/graphicview.h
    ui/graphicview.cpp
    ui/filterbar.h
    ui/filterbar.cpp
    ui/signalconfigdialog.h
    ui/signalconfigdialog.cpp
)

# ============================================================
#  可执行目标
# ============================================================
qt_add_executable(sin
    main.cpp
    ${SRC_CORE}
    ${SRC_MODELS}
    ${SRC_UTILS}
    ${SRC_UI}
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
