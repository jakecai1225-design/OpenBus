# ============================================================
#  源文件列表
# ============================================================

# core 层 — 数据结构、录制、回放、模拟器、DBC
set(SRC_CORE
    core/canframe.h
    core/logging.h
    core/logging.cpp
    core/recorder.h
    core/recorder.cpp
    core/player.h
    core/player.cpp
    core/cansimulator.h
    core/cansimulator.cpp
    core/dbcdata.h
    core/dbcmanager.h
    core/dbcmanager.cpp
    core/dbc/dbc_adapter.h
    core/dbc/dbc_adapter.cpp
    core/filter_engine.h
    core/filter_engine.cpp
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
    ui/activitybar.h
    ui/activitybar.cpp
    ui/bottompanel.h
    ui/bottompanel.cpp
    ui/rightpanel.h
    ui/rightpanel.cpp
    ui/panels/sidebarpanels.h
    ui/panels/sidebarpanels.cpp
    ui/spliteditorarea.h
    ui/spliteditorarea.cpp
    ui/signalsendtab.h
    ui/signalsendtab.cpp
    ui/playbacktab.h
    ui/playbacktab.cpp
    ui/recordtab.h
    ui/recordtab.cpp
    ui/dbcdetailtab.h
    ui/dbcdetailtab.cpp
    ui/thememanager.h
    ui/thememanager.cpp
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
#  exprtk 头文件极大，MinGW 需启用 big-obj 避免节区溢出
# ============================================================
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # exprtk.hpp 是 ~1.7MB 纯头文件，模板实例化产生巨型目标文件
    # 需关闭调试信息并优化以避免 COFF 节区/字符串表溢出
    set_source_files_properties(core/filter_engine.cpp PROPERTIES
        COMPILE_OPTIONS "-Wa,-mbig-obj;-g0;-O2")
endif()

# ============================================================
#  链接库
# ============================================================
target_link_libraries(sin PRIVATE
    Qt6::Widgets
    spdlog::spdlog
    exprtk
    qcustomplot
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
