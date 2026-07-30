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
    core/appconfig.h
    core/appconfig.cpp
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
    utils/message_queue.h
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
    ui/dbcimportdialog.h
    ui/dbcimportdialog.cpp
    ui/settingsdialog.h
    ui/settingsdialog.cpp
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
    ui/udsview.h
    ui/udsview.cpp
    ui/canopenview.h
    ui/canopenview.cpp
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
#  链接库
# ============================================================
target_link_libraries(sin PRIVATE
    Qt6::Widgets
    qcustomplot
)

# spdlog 头文件路径（直接添加，不通过 IMPORTED target）
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/spdlog/include")
    target_include_directories(sin PRIVATE ${CMAKE_SOURCE_DIR}/third_party/spdlog/include)
endif()

# nlohmann/json 头文件路径
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/nlohmann_json/nlohmann/json.hpp")
    target_include_directories(sin PRIVATE ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json)
endif()

# moodycamel::ConcurrentQueue 头文件路径
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/concurrentqueue/concurrentqueue.h")
    target_include_directories(sin PRIVATE ${CMAKE_SOURCE_DIR}/third_party/concurrentqueue)
endif()

# # 将来引入的库（已下载但暂未使用）
# - moodycamel::ConcurrentQueue (高性能无锁队列)

# qcustomplot 已下载到 third_party/qcustomplot/

# ============================================================
#  头文件搜索路径
# ============================================================
target_include_directories(sin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ============================================================
#  预编译头（PCH）— 显著加速编译（避免每个 .cpp 重复解析 Qt 头文件）
# ============================================================
target_precompile_headers(sin PRIVATE
    <QWidget>
    <QMainWindow>
    <QVBoxLayout>
    <QHBoxLayout>
    <QSplitter>
    <QToolBar>
    <QAction>
    <QMenu>
    <QMenuBar>
    <QStatusBar>
    <QLabel>
    <QPushButton>
    <QToolButton>
    <QTreeWidget>
    <QTableWidget>
    <QListWidget>
    <QComboBox>
    <QLineEdit>
    <QCheckBox>
    <QHeaderView>
    <QDockWidget>
    <QTabWidget>
    <QFileDialog>
    <QMessageBox>
    <QCloseEvent>
    <QMouseEvent>
    <QApplication>
    <QSettings>
    <QJsonDocument>
    <QJsonObject>
    <QJsonArray>
    <QDateTime>
    <QTimer>
    <QThread>
    <QFileInfo>
    <QDebug>
    <vector>
    <string>
    <memory>
    <functional>
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
