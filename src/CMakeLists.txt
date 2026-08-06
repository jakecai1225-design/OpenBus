# ============================================================
#  源文件列表
# ============================================================

# core 层 — 数据结构、录制、回放、模拟器、DBC、文件格式 I/O
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
    core/candevice.h
    core/candevice.cpp
    core/candevice_zlg.h
    core/candevice_zlg.cpp
    core/candevicemanager.h
    core/candevicemanager.cpp
    core/dbcdata.h
    core/dbcmanager.h
    core/dbcmanager.cpp
    core/dbc/dbc_adapter.h
    core/dbc/dbc_adapter.cpp
    core/dbc/dbc_writer.h
    core/dbc/dbc_writer.cpp
    core/filter_engine.h
    core/filter_engine.cpp
    core/appconfig.h
    core/appconfig.cpp
    core/file_import/file_importer.h
    core/file_import/file_importer.cpp
    core/file_import/asc_importer.h
    core/file_import/asc_importer.cpp
    core/file_import/csv_importer.h
    core/file_import/csv_importer.cpp
    core/file_import/blf_importer.h
    core/file_import/blf_importer.cpp
    core/projectmanager.h
    core/projectmanager.cpp
    # 文件格式 I/O 层
    core/canfileio/canfileio.h
    core/canfileio/canfileio.cpp
    core/canfileio/canfileio_factory.h
    core/canfileio/canfileio_factory.cpp
    core/canfileio/blf.h
    core/canfileio/blf.cpp
    core/canfileio/asc.h
    core/canfileio/asc.cpp
    core/canfileio/csv.h
    core/canfileio/csv.cpp
    core/canfileio/pcap_reader.h
    core/canfileio/pcap_reader.cpp
    core/canfileio/trc_reader.h
    core/canfileio/trc_reader.cpp
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
    utils/logging.h
)

# ui 层 — 界面组件
set(SRC_UI
    ui/mainwindow.h
    ui/mainwindow.cpp
    ui/traceview.h
    ui/traceview.cpp
    ui/filterheaderview.h
    ui/filterheaderview.cpp
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
    ui/measurementsetupview.h
    ui/measurementsetupview.cpp
    ui/deviceconnectiontab.h
    ui/deviceconnectiontab.cpp
    # 工具集 — 总线分析工具
    ui/tools/blfasconverter.h
    ui/tools/blfasconverter.cpp
    ui/tools/dbctoolview.h
    ui/tools/dbctoolview.cpp
    ui/tools/loganalysisview.h
    ui/tools/loganalysisview.cpp
    ui/tools/dbcsignallistview.h
    ui/tools/dbcsignallistview.cpp
)

# ============================================================
#  sin_core — 核心静态库 (数据结构、录制、回放、DBC、过滤引擎)
#  拆分为静态库后，改 UI 代码不会触发 core 重编译
# ============================================================
add_library(sin_core STATIC
    ${SRC_CORE}
    ${SRC_MODELS}
    ${SRC_UTILS}
)

# PUBLIC: UI 层通过链接 sin_core 获得这些头文件路径
#   - spdlog: core/logging.h (公共头) 包含 <spdlog/spdlog.h>
#   - nlohmann_json: core/appconfig.h (公共头) 包含 <nlohmann/json.hpp>
#   - concurrentqueue: core/cansimulator.h (公共头) → utils/message_queue.h → <concurrentqueue.h>
target_include_directories(sin_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/spdlog/include")
    target_include_directories(sin_core PUBLIC ${CMAKE_SOURCE_DIR}/third_party/spdlog/include)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/nlohmann_json/nlohmann/json.hpp")
    target_include_directories(sin_core PUBLIC ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/concurrentqueue/concurrentqueue.h")
    target_include_directories(sin_core PUBLIC ${CMAKE_SOURCE_DIR}/third_party/concurrentqueue)
endif()

target_link_libraries(sin_core PUBLIC Qt6::Widgets)

# zlib: blf.cpp 手动解析 BLF 文件需要 zlib 解压 (MinGW 自带 libz.a)
target_link_libraries(sin_core PRIVATE
    z
)

# PCH — 核心层使用精简的 Qt 头（无 Widget），编译更快
target_precompile_headers(sin_core PRIVATE
    <QObject>
    <QString>
    <QStringList>
    <QVariant>
    <QList>
    <QHash>
    <QVector>
    <QPair>
    <QMetaType>
    <QByteArray>
    <QTimer>
    <QThread>
    <QFile>
    <QDataStream>
    <QDir>
    <QFileInfo>
    <QRegularExpression>
    <QStandardPaths>
    <QTextStream>
    <QColor>
    <QRandomGenerator>
    <QStringConverter>
    <QAbstractTableModel>
    <QSortFilterProxyModel>
    <vector>
    <string>
    <memory>
    <functional>
)

# ============================================================
#  sin_ui — UI 静态库 (所有界面组件)
#  改 UI 代码时只需重编译此库 + 最终链接，不影响 core
# ============================================================
add_library(sin_ui STATIC
    ${SRC_UI}
)

# PUBLIC: 可执行文件通过链接 sin_ui 获得 sin_core 的头文件路径
target_link_libraries(sin_ui PUBLIC
    sin_core
    Qt6::Widgets
)

# qcustomplot: 仅 graphicview.cpp 使用，graphicview.h 用前向声明 → PRIVATE
# Qt6::Svg: SVG 线条图标渲染 (activitybar / sidebarpanels)
target_link_libraries(sin_ui PRIVATE
    qcustomplot
    Qt6::Svg
)

# PCH — UI 层使用完整的 Qt Widget 头列表
target_precompile_headers(sin_ui PRIVATE
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
#  sin — 可执行目标
# ============================================================
qt_add_executable(sin
    main.cpp
    ../resources/resources.qrc
)

target_link_libraries(sin PRIVATE
    sin_ui
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
