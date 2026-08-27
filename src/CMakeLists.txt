# ============================================================
#  源文件列表
# ============================================================

# core 层 — 数据结构、录制、回放、模拟器、DBC、文件格式 I/O
set(SRC_CORE
    core/canframe.h
    core/logging.h
    core/logging.cpp
    core/signalrelay.h
    core/signalrelay.cpp
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
    core/candevice_peak.h
    core/candevice_peak.cpp
    core/candevice_kvaser.h
    core/candevice_kvaser.cpp
    core/candevice_slcan.h
    core/candevice_slcan.cpp
    core/candevice_candle.h
    core/candevice_candle.cpp
    core/candevicemanager.h
    core/candevicemanager.cpp
    core/driver/candriverplugin.h
    core/driver/driverregistry.h
    core/driver/driverregistry.cpp
    core/driver/marketindex.h
    core/driver/marketindex.cpp
    core/dbcdata.h
    core/dbcmanager.h
    core/dbcmanager.cpp
    core/dbc/dbc_adapter.h
    core/dbc/dbc_adapter.cpp
    core/dbc/dbc_writer.h
    core/dbc/dbc_writer.cpp
    core/dbc/arxml_importer.h
    core/dbc/arxml_importer.cpp
    core/dbc/arxml_exporter.h
    core/dbc/arxml_exporter.cpp
    # M2 预埋：多协议数据层接口（doc/flow.md §十三）——纯新增不接线
    core/busmessage.h
    core/protocol/busdefinition.h
    core/protocol/iprotocoladapter.h
    core/protocol/protocolregistry.h
    core/protocol/protocolregistry.cpp
    core/protocol/canprotocoladapter.h
    core/protocol/canprotocoladapter.cpp
    core/protocol/ibusparser.h
    core/protocol/parserregistry.h
    core/protocol/parserregistry.cpp
    core/protocol/dbcparser.h
    core/protocol/dbcparser.cpp
    core/protocol/busdefinitionstore.h
    core/protocol/busdefinitionstore.cpp
    core/busstatistics.h
    core/busstatistics.cpp
    core/logsplitter.h
    core/logsplitter.cpp
    core/triggerrecorder.h
    core/triggerrecorder.cpp
    core/filterpresetmanager.h
    core/filterpresetmanager.cpp
    core/bookmarkmanager.h
    core/bookmarkmanager.cpp
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
    core/resourceresolver.h
    core/resourceresolver.cpp
    core/sessionmanager.h
    core/sessionmanager.cpp
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
    # QML MenuBar Backend (独立 DLL)
    core/qmlmenulibrary.h
    core/qmlmenulibrary.cpp
    core/canfileio/pcap_reader.h
    core/canfileio/pcap_reader.cpp
    core/canfileio/trc_reader.h
    core/canfileio/trc_reader.cpp
    # 插件系统
    core/plugin/plugininfo.h
    core/plugin/plugininfo.cpp
    core/plugin/pluginhost.h
    core/plugin/pluginhost.cpp
    core/plugin/pluginconvertjob.h
    core/plugin/pluginconvertjob.cpp
    core/plugin/pluginmanager.h
    core/plugin/pluginmanager.cpp
    # 市场数据聚合（B5-5 自 ui/ 迁入：侧栏/壳/市场页共用，聚合源全在 core）
    core/marketmodel.h
    core/marketmodel.cpp
    # 业务模块接口（拆分方案 §4.2 — 壳与业务 DLL 的唯一契约）
    core/module/imodule.h
    core/module/moduleregistry.h
    core/module/moduleregistry.cpp
)

# models 层 — Qt 数据模型
set(SRC_MODELS
    models/cantracemodel.h
    models/cantracemodel.cpp
    models/cantraceproxymodel.h
    models/cantraceproxymodel.cpp
    models/viewportproxy.h
    models/viewportproxy.cpp
)

# utils 层 — 工具函数
set(SRC_UTILS
    utils/canutils.h
    utils/canutils.cpp
    utils/ringbuffer.h
    utils/message_queue.h
    utils/logging.h
)

# ui 层 — 界面组件
# （trace/graphic 文件簇已迁出（拆分方案 B5）：见 openbus_trace / openbus_graphic）
set(SRC_UI
    ui/mainwindow.h
    ui/mainwindow.cpp
    # B6 瘦身：mainwindow 同一类拆为部分实现文件（doc/拆分应用实施方案.md §4.5）
    ui/mainwindow_setup.cpp      # 构造分阶段装配（服务/插件/数据管线/侧栏/工程接线）
    ui/mainwindow_chrome.cpp     # 菜单栏/窗口按钮/布局/状态栏/dock 切换/eventFilter
    ui/mainwindow_actions.cpp    # 录制/回放/导入/快捷按钮/终端命令/统计
    ui/mainwindow_frameflow.cpp  # 帧管线/回放进度/DBC 信号联动/测量门控编排
    ui/mainwindow_pages.cpp      # 页面打开槽 + 面板列表刷新
    ui/mainwindow_project.cpp    # 工程与会话生命周期 + closeEvent
    ui/mainwindow_dialogs.cpp    # 帮助对话框 + 插件集成槽
    ui/mainwindow_qmlmenu.cpp    # ✅ QML MenuBar 集成（新架构）
    # traceview/tracestatisticswidget/tracediffwidget/filterheaderview/
    # columnfilterpopup/filterbar/colorruleeditor 迁入 openbus_trace.dll（B5）
    # graphicview/graphic/downsample/signalconfigdialog/datawindow 迁入
    # openbus_graphic.dll（B5）
    # dbcimportdialog 迁入 openbus_transceive.dll（拆分方案 B2：
    # 唯一使用者 signalsendtab；B3 openbus_dbc 落地时可再评估归属）
    # settingsdialog 已改造为 settingspage（QWidget 标签页形态，侧栏设置条目
    # 不再弹窗）；shortcutspage 为快捷键参考标签页
    ui/settingspage.h
    ui/settingspage.cpp
    ui/shortcutspage.h
    ui/shortcutspage.cpp
    ui/activitybar.h
    ui/activitybar.cpp
    ui/bottompanel.h
    ui/bottompanel.cpp
    ui/rightpanel.h
    ui/rightpanel.cpp
    ui/panels/sidebarpanels.h
    ui/panels/sidebarpanels.cpp
    # Graphic 侧栏「添加信号」弹窗（壳编排：DbcManager 数据源 → graphic 模块 addSignals）
    ui/dbcsignalpickerdialog.h
    ui/dbcsignalpickerdialog.cpp
    # Watcher 观测页（doc/Watcher方案.md 方案 A：壳侧自持，直连 BusStatistics/DbcManager）
    ui/watcherview.h
    ui/watcherview.cpp
    ui/spliteditorarea.h
    ui/spliteditorarea.cpp
    # signalsendtab/playbacktab/offlineanalysistab/recordtab 迁入
    # openbus_transceive.dll（拆分方案 B2）
    # dbcdetailtab/tools/dbcsignallistview 迁入 openbus_dbc.dll（拆分方案 B3）
    # thememanager 迁入 openbus_data（拆分方案 §4.3：被全部业务使用，须与单例们同 DLL）
    # markettab/marketmodule 迁入 openbus_market.dll（拆分方案 B1）；
    # marketmodel 迁入 openbus_data（B5-5：侧栏/壳/市场页共用）
    # measurementsetupview/deviceconnectiontab 迁入 openbus_flow.dll（拆分方案 B4）
    # 工具集 — 内置分析工具（原 blfasconverter/dbctoolview/loganalysisview 已插件化，见 plugins/）
    # dbcsignallistview 迁入 openbus_dbc.dll（拆分方案 B3）
    ui/tools/iographview.h
    ui/tools/iographview.cpp
)

# ============================================================
#  openbus_data — 公共底座 DLL（拆分方案 §4.1/B1）
#  = 原 openbus_core（core/models/utils）+ thememanager（自 ui 迁入）
#  全部 core 单例（AppConfig/DbcManager/PluginManager/ThemeManager/
#  ModuleRegistry…）在此保持全进程唯一实例（§4.2 决策 2）。
#  MinGW GNU ld 默认全符号导出，无需逐类 export 宏（§4.2 决策 1）。
# ============================================================
add_library(openbus_data SHARED
    ${SRC_CORE}
    ${SRC_MODELS}
    ${SRC_UTILS}
    ui/thememanager.h
    ui/thememanager.cpp
)

# ============================================================
#  openbus_qml_menu — QML 菜单栏独立 DLL
# ===========================================================
add_library(openbus_qml_menu SHARED
    core/qmlmenulibrary.h
    core/qmlmenulibrary.cpp
)

# PUBLIC: Qt6::Qml 是必需的（QML_ELEMENT macro）
#        Qt6::Quick 是必需的（QQmlEngine）
target_link_libraries(openbus_qml_menu PUBLIC
    Qt6::Qml
    Qt6::Quick
    Qt6::Widgets
)

# PCH - QML 组件需要完整的 Qt 头
# target_precompile_headers(openbus_qml_menu PRIVATE
#     <QObject>
#     <QString>
#     <QStringList>
#     <QVariant>
#     <QVariantList>
# )

# PUBLIC: 业务 DLL / 壳通过链接 openbus_data 获得这些头文件路径
#   - spdlog: core/logging.h (公共头) 包含 <spdlog/spdlog.h>
#   - nlohmann_json: core/appconfig.h (公共头) 包含 <nlohmann/json.hpp>
#   - concurrentqueue: core/cansimulator.h (公共头) → utils/message_queue.h → <concurrentqueue.h>
target_include_directories(openbus_data PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/spdlog/include")
    target_include_directories(openbus_data PUBLIC ${CMAKE_SOURCE_DIR}/third_party/spdlog/include)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/nlohmann_json/nlohmann/json.hpp")
    target_include_directories(openbus_data PUBLIC ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/concurrentqueue/concurrentqueue.h")
    target_include_directories(openbus_data PUBLIC ${CMAKE_SOURCE_DIR}/third_party/concurrentqueue)
endif()

# pugixml include path (for arxml_importer.h which includes "pugixml.hpp")
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/pugixml/pugixml.hpp")
    target_include_directories(openbus_data PUBLIC ${CMAKE_SOURCE_DIR}/third_party/pugixml)
endif()

target_link_libraries(openbus_data PUBLIC Qt6::Widgets)

# Linker optimization for MinGW (simplified)
# Use --gc-sections to remove unused code sections
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_target_properties(openbus_data PROPERTIES
        LINK_FLAGS "-Wl,--gc-sections"
    )
endif()

# Qml: MenuController (QQmlElement macro requires Qt6::Qml)
target_link_libraries(openbus_data PUBLIC Qt6::Qml)

# Svg: utils/svg_icon.h（公共头，thememanager/market/侧栏均用）→ PUBLIC
target_link_libraries(openbus_data PUBLIC Qt6::Svg)

# Network: MarketIndex 设备市场索引（market.json / 图片 / 驱动包下载）
target_link_libraries(openbus_data PRIVATE Qt6::Network)

# zlib: blf.cpp 手动解析 BLF 文件需要 zlib 解压 (MinGW 自带 libz.a)
target_link_libraries(openbus_data PRIVATE
    z
)
if(TARGET vector_blf)
    target_link_libraries(openbus_data PRIVATE vector_blf)
    target_compile_definitions(openbus_data PRIVATE HAS_VECTOR_BLF)
endif()

# pugixml: arxml_importer.cpp 使用 (如果存在 pugixml 目标)
if(TARGET pugixml)
    target_link_libraries(openbus_data PRIVATE pugixml)
endif()

# PCH — 核心层使用精简的 Qt 头（无 Widget），编译更快。
# thememanager.cpp 所需的 QApplication/QPalette 由其自身 include 引入，
# 不拖累其余 ~90 个 core 对象的 PCH 体积。
target_precompile_headers(openbus_data PRIVATE
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
#  openbus_market — 插件市场业务 DLL（拆分方案 B1，模块模板）
#  markettab / marketmodule + 唯一导出的 C 工厂
#  openbus_createMarketModule()（§4.2 决策 1：最小 ABI 面）
#  依赖：openbus_data（单例经 DLL 共享；marketmodel 聚合层 B5-5 已迁 data）
#  + Qt Widgets/Network/Svg
# ============================================================
add_library(openbus_market SHARED
    ui/markettab.h
    ui/markettab.cpp
    # 流式布局 — 市场首页卡片网格换行（VS Code marketplace 网页版版式）
    ui/flowlayout.h
    ui/flowlayout.cpp
    ui/marketmodule.h
    ui/marketmodule.cpp
)

# PUBLIC openbus_data：模块头（marketmodel.h 等）引用 core 类型与 thememanager
target_link_libraries(openbus_market PUBLIC
    openbus_data
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
)

# PCH — 复制壳的 Qt Widget 头清单（markettab.cpp Widget 用量与主界面同级）
target_precompile_headers(openbus_market PRIVATE
    <QObject>
    <QWidget>
    <QFrame>
    <QVBoxLayout>
    <QHBoxLayout>
    <QLabel>
    <QPushButton>
    <QToolButton>
    <QTableWidget>
    <QHeaderView>
    <QLineEdit>
    <QMenu>
    <QMessageBox>
    <QFileDialog>
    <QProgressBar>
    <QScrollArea>
    <QSplitter>
    <QJsonDocument>
    <QJsonArray>
    <QJsonObject>
    <QDir>
    <QFile>
    <QFileInfo>
    <QStandardPaths>
    <QTemporaryFile>
    <QProcess>
    <QPointer>
    <QNetworkAccessManager>
    <QNetworkReply>
    <QNetworkRequest>
    <QSvgRenderer>
    <QCryptographicHash>
    <QTimer>
    <vector>
    <string>
    <memory>
    <functional>
    <algorithm>
)

# ============================================================
#  openbus_transceive — 收发业务 DLL（拆分方案 B2）
#  发送/回放/离线分析/录制 四页面 + TransceiveModule 适配器，
#  唯一导出 C 工厂 openbus_createTransceiveModule()。
#  依赖：openbus_data（player/recorder/deviceManager/dbcManager 单例
#  经 ShellContext 指针使用）+ Qt Widgets/Svg
# ============================================================
add_library(openbus_transceive SHARED
    ui/signalsendtab.h
    ui/signalsendtab.cpp
    ui/playbacktab.h
    ui/playbacktab.cpp
    ui/offlineanalysistab.h
    ui/offlineanalysistab.cpp
    ui/recordtab.h
    ui/recordtab.cpp
    ui/dbcimportdialog.h
    ui/dbcimportdialog.cpp
    ui/transceivemodule.h
    ui/transceivemodule.cpp
)

target_link_libraries(openbus_transceive PUBLIC
    openbus_data
    Qt6::Widgets
    Qt6::Svg
)

# PCH — 收发四页 Widget 用量与市场页同级（表格/滑条/组合框等）
target_precompile_headers(openbus_transceive PRIVATE
    <QObject>
    <QWidget>
    <QFrame>
    <QVBoxLayout>
    <QHBoxLayout>
    <QGridLayout>
    <QGroupBox>
    <QLabel>
    <QPushButton>
    <QToolButton>
    <QTableWidget>
    <QTableWidgetItem>
    <QHeaderView>
    <QLineEdit>
    <QComboBox>
    <QCheckBox>
    <QSpinBox>
    <QDoubleSpinBox>
    <QSlider>
    <QSplitter>
    <QMenu>
    <QMessageBox>
    <QFileDialog>
    <QDateTime>
    <QTimer>
    <QDir>
    <QFile>
    <QFileInfo>
    <QSvgRenderer>
    <vector>
    <string>
    <memory>
    <functional>
    <algorithm>
)

# ============================================================
#  openbus_dbc — DBC 业务 DLL（拆分方案 B3）
#  DBC 详情页（多实例，每文件一页）+ 信号清单工具 + DbcModule 适配器，
#  唯一导出 C 工厂 openbus_createDbcModule()。
#  依赖：openbus_data（dbcManager 单例经 ShellContext 指针使用）+ Qt Widgets/Svg
#  注：dbcimportdialog 留在 openbus_transceive（唯一使用者发送页，见 B2 备注）
# ============================================================
add_library(openbus_dbc SHARED
    ui/dbcdetailtab.h
    ui/dbcdetailtab.cpp
    ui/tools/dbcsignallistview.h
    ui/tools/dbcsignallistview.cpp
    ui/dbcmodule.h
    ui/dbcmodule.cpp
)

target_link_libraries(openbus_dbc PUBLIC
    openbus_data
    Qt6::Widgets
    Qt6::Svg
)

# PCH — DBC 页以树/表 Widget 为主
target_precompile_headers(openbus_dbc PRIVATE
    <QObject>
    <QWidget>
    <QFrame>
    <QVBoxLayout>
    <QHBoxLayout>
    <QSplitter>
    <QLabel>
    <QPushButton>
    <QLineEdit>
    <QTreeWidget>
    <QTreeWidgetItem>
    <QTableWidget>
    <QTableWidgetItem>
    <QHeaderView>
    <QStackedWidget>
    <QScrollArea>
    <QGroupBox>
    <QMenu>
    <QAction>
    <QMessageBox>
    <QFileDialog>
    <QFile>
    <QFileInfo>
    <QDir>
    <QTimer>
    <vector>
    <string>
    <memory>
    <functional>
    <algorithm>
)

# ============================================================
#  openbus_flow — 测量流程业务 DLL（拆分方案 B4）
#  Flow 测量配置页（MeasurementSetupView）+ 设备连接页（DeviceConnectionTab）
#  + FlowModule 适配器，唯一导出 C 工厂 openbus_createFlowModule()。
#  依赖：openbus_data（player/simulator/deviceManager/dbcManager 单例
#  经 ShellContext 指针使用）+ Qt Widgets/Svg
# ============================================================
add_library(openbus_flow SHARED
    ui/measurementsetupview.h
    ui/measurementsetupview.cpp
    ui/deviceconnectiontab.h
    ui/deviceconnectiontab.cpp
    ui/flowmodule.h
    ui/flowmodule.cpp
)

target_link_libraries(openbus_flow PUBLIC
    openbus_data
    Qt6::Widgets
    Qt6::Svg
)

# PCH — Flow 页为 QGraphicsScene/View + 表单组合
target_precompile_headers(openbus_flow PRIVATE
    <QObject>
    <QWidget>
    <QFrame>
    <QVBoxLayout>
    <QHBoxLayout>
    <QGridLayout>
    <QFormLayout>
    <QGroupBox>
    <QLabel>
    <QPushButton>
    <QToolButton>
    <QAction>
    <QToolBar>
    <QComboBox>
    <QCheckBox>
    <QLineEdit>
    <QListWidget>
    <QGraphicsScene>
    <QGraphicsView>
    <QGraphicsRectItem>
    <QGraphicsTextItem>
    <QGraphicsPathItem>
    <QPainterPath>
    <QLinearGradient>
    <QPen>
    <QBrush>
    <QMenu>
    <QMessageBox>
    <QFileDialog>
    <QInputDialog>
    <QDialog>
    <QTimer>
    <QScrollBar>
    <QFileInfo>
    <QMouseEvent>
    <cmath>
    <vector>
    <string>
    <memory>
    <functional>
    <algorithm>
)

# ============================================================
#  openbus_trace — Trace 业务 DLL（拆分方案 B5）
#  TraceTab（Wireshark 风格三栏：过滤栏 + 报文列表 + Explorer 底栏）及其
#  组件簇（TraceView/统计/差异/列过滤/FilterBar/着色规则编辑器）
#  + TraceModule 适配器，唯一导出 C 工厂 openbus_createTraceModule()。
#  依赖：openbus_data（dbcManager 单例经 ShellContext 指针使用）+ Qt Widgets/Svg
# ============================================================
add_library(openbus_trace SHARED
    ui/traceview.h
    ui/traceview.cpp
    ui/tracestatisticswidget.h
    ui/tracestatisticswidget.cpp
    ui/tracediffwidget.h
    ui/tracediffwidget.cpp
    ui/filterheaderview.h
    ui/filterheaderview.cpp
    ui/columnfilterpopup.h
    ui/columnfilterpopup.cpp
    ui/filterbar.h
    ui/filterbar.cpp
    ui/colorruleeditor.h
    ui/colorruleeditor.cpp
    ui/tracemodule.h
    ui/tracemodule.cpp
)

target_link_libraries(openbus_trace PUBLIC
    openbus_data
    Qt6::Widgets
    Qt6::Svg
)

# PCH — Trace 页以表格视图为主
target_precompile_headers(openbus_trace PRIVATE
    <QObject>
    <QWidget>
    <QFrame>
    <QVBoxLayout>
    <QHBoxLayout>
    <QSplitter>
    <QLabel>
    <QPushButton>
    <QToolButton>
    <QTableView>
    <QHeaderView>
    <QLineEdit>
    <QMenu>
    <QAction>
    <QTabWidget>
    <QComboBox>
    <QCheckBox>
    <QMessageBox>
    <QFileDialog>
    <QTimer>
    <QSettings>
    <vector>
    <string>
    <memory>
    <functional>
    <algorithm>
)

# ============================================================
#  openbus_graphic — Graphic 业务 DLL（拆分方案 B5）
#  GraphicView（qcustomplot 曲线多实例）+ 降采样 + 信号配置对话框 +
#  DataWindow 实时信号表格 + GraphicModule 适配器，唯一导出 C 工厂
#  openbus_createGraphicModule()。
#  依赖：openbus_data + qcustomplot + Qt Widgets/Svg/PrintSupport
# ============================================================
add_library(openbus_graphic SHARED
    ui/graphicview.h
    ui/graphicview.cpp
    ui/graphic/downsample.h
    ui/graphic/downsample.cpp
    ui/signalconfigdialog.h
    ui/signalconfigdialog.cpp
    ui/dbcsignalpickerdialog.h
    ui/dbcsignalpickerdialog.cpp
    ui/datawindow.h
    ui/datawindow.cpp
    ui/graphicmodule.h
    ui/graphicmodule.cpp
)

target_link_libraries(openbus_graphic PUBLIC
    openbus_data
    Qt6::Widgets
    Qt6::Svg
)

# qcustomplot: 仅 graphicview.cpp 使用（前向声明）→ PRIVATE；
# qcustomplot 传递依赖 Qt6::PrintSupport
target_link_libraries(openbus_graphic PRIVATE
    qcustomplot
)

# PCH — Graphic 页以 qcustomplot 自绘为主
target_precompile_headers(openbus_graphic PRIVATE
    <QObject>
    <QWidget>
    <QVBoxLayout>
    <QHBoxLayout>
    <QLabel>
    <QPushButton>
    <QToolButton>
    <QComboBox>
    <QCheckBox>
    <QSpinBox>
    <QDoubleSpinBox>
    <QMenu>
    <QAction>
    <QDialog>
    <QDialogButtonBox>
    <QTableWidget>
    <QTableWidgetItem>
    <QHeaderView>
    <QMessageBox>
    <QFileDialog>
    <QTimer>
    <QSettings>
    <QPen>
    <QBrush>
    <QPainter>
    <vector>
    <string>
    <memory>
    <functional>
    <algorithm>
)

# ============================================================
#  openbus_ui — 壳 UI 静态库 (剩余界面组件；thin archive 打包)
#  改 UI 代码时只需重编译此库 + 最终链接，不影响 data/market
# ============================================================
add_library(openbus_ui STATIC
    ${SRC_UI}
)

# ✅ QML MenuBar 支持（PRIVATE - 仅编译期需要）:
#   openbus_ui 静态库包含 MainWindow.h 中的 QML 成员变量和函数声明
#   Qt6::Qml + Qt6::Quick 用于支持 QQmlEngine 类型（头文件解析）
# ⚠️ NOTE: QQuickWidget header in QtQuickwidgets module is included via manual include path below
#           (MinGW Qt missing cmake config, so can't use find_package(QuickWidgets))
# IMPORTANT: Use PRIVATE for link libraries to avoid propagating dependencies to consumers!
target_link_libraries(openbus_ui PRIVATE
    openbus_data         # ✅ Needed for core types headers (canframe.h, dbcdata.h, etc.)
    Qt6::Widgets
    Qt6::Qml          # Required for QML_ELEMENT macro in MenuController
    Qt6::Quick        # Required for QQmlEngine type resolution
)

# ⚠️ Manually add QuickWidgets include path (MinGW Qt6.8.3 lacks CMake targets)
# ✅ Also need to link Qt6QuickWidgets library for runtime
set(QUICKWIDGETS_INCLUDE_DIR "D:/Qt/6.8.3/mingw_64/include/QtQuickWidgets")
target_include_directories(openbus_ui PUBLIC ${QUICKWIDGETS_INCLUDE_DIR})

# Link Qt6QuickWidgets DLL explicitly for MinGW toolchain
# ⚠️ Note: MinGW Qt6.8.3 has no prebuilt qtquickwidgetsTargets.cmake!
# Use actual import library filename: libQt6QuickWidgets.a or Qt6QuickWidgets.dll.a
if(WIN32)
    # Find and use the actual import library file in Qt bin directory
    find_library(QT_QUICKWIDGETS_IMPORT_LIB NAMES Qt6QuickWidgets Qt6QuickWidgets.lib libQt6QuickWidgets.a 
        PATHS "D:/Qt/6.8.3/mingw_64/bin" "D:/Qt/6.8.3/mingw_64/lib" NO_DEFAULT_PATH)
    if(QT_QUICKWIDGETS_IMPORT_LIB)
        message(STATUS "Found Qt6QuickWidgets import library: ${QT_QUICKWIDGETS_IMPORT_LIB}")
        target_link_libraries(openbus_ui PRIVATE ${QT_QUICKWIDGETS_IMPORT_LIB})
    else()
        message(FATAL_ERROR "CRITICAL: Qt6QuickWidgets import library is missing from D:/Qt/6.8.3/mingw_64/bin/!\n\nYour MinGW Qt6.8.3 installation does NOT include the import library (.a/.dll.a).\nThis means you MUST generate it manually or switch to MSVC Qt.\n\nTo generate (requires windll.exe from MinGW-w64 toolchain):\n  cd D:/Qt/6.8.3/mingw_64/bin\n  dlltool -d qtquickwidgets.def -l libQt6QuickWidgets.a -D Qt6QuickWidgets.dll\n\nOr download/install a complete MinGW Qt build.")
    endif()
endif()

target_link_libraries(openbus_qml_menu PRIVATE
    Qt6::Qml
    Qt6::Quick
)

# Qt6::Svg: SVG 线条图标渲染 (activitybar / sidebarpanels)
# Qt6::Network: 侧栏 ExtensionsPanel 图标异步加载（marketmodel B5-5 已迁 data）
# qcustomplot: I/O Graph 工具页 (ui/tools/iographview)——主窗口工具页而非
# Graphic 模块页面，B5 拆分后 qcustomplot 虽集中于 openbus_graphic，壳侧
# 此一处使用仍留在 openbus_ui（静态库仅编译期依赖，无 DLL 分发影响）
target_link_libraries(openbus_ui PRIVATE
    Qt6::Svg
    qcustomplot
)

# Windows: psapi 用于进程资源监控 (ExtensionsTab)
if(WIN32)
    target_link_libraries(openbus_ui PRIVATE psapi)
endif()

# PCH — UI 层使用完整的 Qt Widget + QML 头列表（含 QObject 供模块适配器使用）
# ✅ QML MenuBar: MainWindow.h 需要 Qt Quick 支持，必须包含在 PCH 中
target_precompile_headers(openbus_ui PRIVATE
    <QObject>
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
    # QML/Qt Quick support for QQuickWidget type in MainWindow.h
    <QQuickWidget>
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
#  openbus — 可执行目标
# ============================================================
qt_add_executable(openbus
    main.cpp
    ../resources/resources.qrc
)

target_link_libraries(openbus PRIVATE
    openbus_ui
    # 过渡（B5-5 已解耦）：marketmodel 已迁 openbus_data，壳不再链接
    # openbus_market 亦可解析 MarketItem/MarketModel 符号；保留链接仅为
    # main() 经 C 工厂注册模块（§4.2 决策 1）
    openbus_market
    # 收发模块（B2）：main() 经 C 工厂 openbus_createTransceiveModule 注册，
    # 壳经 ModuleRegistry::createPage/invoke/query 操控四页面
    openbus_transceive
    # DBC 模块（B3）：main() 经 C 工厂 openbus_createDbcModule 注册，
    # 壳经 createPage("detail", 文件名，ctx) 打开 DBC 详情页
    openbus_dbc
    # flow 模块（B4）：main() 经 C 工厂 openbus_createFlowModule 注册，
    # shell 经 createPage/invoke/query 操控 Flow/设备连接页与测量编排
    openbus_flow
    # trace 模块（B5）：main() 经 C 工厂 openbus_createTraceModule 注册，
    # 壳经 createPage/invoke/query 操控 Trace 页与帧分发/过滤/着色
    openbus_trace
    # graphic 模块（B5）：main() 经 C 工厂 openbus_createGraphicModule 注册，
    # 壳经 createPage/invoke/query 操控 Graphic 页/DataWindow 与信号添加
    openbus_graphic
    # QML 菜单栏独立 DLL
    openbus_qml_menu
)

# ============================================================
#  Windows: 以 GUI 程序方式运行 (不弹出控制台窗口)
# ============================================================
if(WIN32)
    set_target_properties(openbus PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# ============================================================
#  驱动 DLL — 编译后拷贝 driver/ 下的 DLL 直接到输出目录
#  ZLG SDK 的 zlgcan.dll 必须与 exe 在同一目录才能找到 USB 驱动
#  driver/ 目录是项目中的存储位置，编译时拷贝到 exe 同级目录
# ============================================================
if(EXISTS "${CMAKE_SOURCE_DIR}/driver")
    add_custom_command(TARGET openbus POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/driver"
                "$<TARGET_FILE_DIR:openbus>"
        COMMENT "Copying driver DLLs to output directory"
    )
    # 安装规则 — 安装包包含驱动 DLL
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/driver/"
            DESTINATION bin
            FILES_MATCHING PATTERN "*.dll"
    )
endif()

# ============================================================
# 安装规则
# ============================================================
install(TARGETS openbus
    RUNTIME DESTINATION bin
)
