# ============================================================
#  Source file lists
# ============================================================

# core - data structures, record/replay, simulator, DBC, file I/O
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
    core/capturelog.h
    core/capturelog.cpp
    core/tracefilterindex.h
    core/samplestore.h
    core/samplestore.cpp
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
    core/candevice_vector.h
    core/candevice_vector.cpp
    core/candevice_tongxing.h
    core/candevice_tongxing.cpp
    core/candevice_ixxat.h
    core/candevice_ixxat.cpp
    core/candevice_socketcan.h
    core/candevice_socketcan.cpp
    core/candevice_intrepid.h
    core/candevice_intrepid.cpp
    core/candevice_slcan.h
    core/candevice_slcan.cpp
    core/candevice_candle.h
    core/candevice_candle.cpp
    core/candevice_busmust.h
    core/candevice_busmust.cpp
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
    # M2 stub: multi-protocol data interfaces (doc/flow.md) - add only, not wired
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
    core/insights.h
    core/insights.cpp
    core/translationmanager.h
    core/translationmanager.cpp
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
    # file format I/O
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
    # plugin system
    core/plugin/plugininfo.h
    core/plugin/plugininfo.cpp
    core/plugin/pluginhost.h
    core/plugin/pluginhost.cpp
    core/plugin/pluginzmq.h
    core/plugin/pluginzmq.cpp
    core/plugin/pluginconvertjob.h
    core/plugin/pluginconvertjob.cpp
    core/plugin/pluginmanager.h
    core/plugin/pluginmanager.cpp
    # market aggregate (B5-5 moved from ui/: sidebar/shell/market share; sources in core)
    core/marketmodel.h
    core/marketmodel.cpp
    # business module API (split plan §4.2 - sole contract between shell and DLLs)
    core/module/imodule.h
    core/module/moduleregistry.h
    core/module/moduleregistry.cpp
)

# models - Qt models
set(SRC_MODELS
    models/cantracemodel.h
    models/cantracemodel.cpp
    models/cantraceproxymodel.h
    models/cantraceproxymodel.cpp
    models/viewportproxy.h
    models/viewportproxy.cpp
)

# utils - helpers
set(SRC_UTILS
    utils/canutils.h
    utils/canutils.cpp
    utils/ringbuffer.h
    utils/message_queue.h
    utils/logging.h
)

# ui - UI widgets
# (trace/graphic clusters moved out in B5: see openbus_trace / openbus_graphic)
set(SRC_UI
    ui/mainwindow.h
    ui/mainwindow.cpp
    # B6 slim-down: MainWindow split into partial .cpp files
    ui/mainwindow_setup.cpp      # staged ctor wiring (services/plugins/pipeline/sidebar/project)
    ui/mainwindow_chrome.cpp     # menubar/window buttons/layout/statusbar/dock toggle/eventFilter
    ui/mainwindow_actions.cpp    # record/replay/import/quick buttons/terminal cmds/stats
    ui/mainwindow_frameflow.cpp  # frame pipeline/replay progress/DBC signal link/measure gating
    ui/mainwindow_pages.cpp      # page-open slots + panel list refresh
    ui/mainwindow_project.cpp    # project/session lifecycle + closeEvent
    ui/mainwindow_dialogs.cpp    # help dialogs + plugin integration slots
    # traceview/tracestatisticswidget/tracediffwidget/filterheaderview/
    # columnfilterpopup/filterbar/colorruleeditor moved to openbus_trace.dll (B5)
    # graphicview/graphic/downsample/signalconfigdialog/datawindow moved
    # openbus_graphic.dll(B5)
    # dbcimportdialog moved to openbus_transceive.dll (split B2:
    # sole user signalsendtab; reassess ownership when openbus_dbc lands)
    # settingsdialog became settingspage (QWidget tab; sidebar settings entry
    # no longer a dialog); shortcutspage is shortcuts reference tab
    ui/settingspage.h
    ui/settingspage.cpp
    ui/shortcutspage.h
    ui/shortcutspage.cpp
    ui/welcomepage.h
    ui/welcomepage.cpp
    ui/activitybar.h
    ui/activitybar.cpp
    ui/bottompanel.h
    ui/bottompanel.cpp
    ui/terminal/terminalsession.h
    ui/terminal/terminalsession.cpp
    ui/terminal/terminalworkspace.h
    ui/terminal/terminalworkspace.cpp
    ui/commandcenter.h
    ui/commandcenter.cpp
    ui/commandpalette.h
    ui/commandpalette.cpp
    ui/rightpanel.h
    ui/rightpanel.cpp
    ui/panels/sidebarpanels.h
    ui/panels/sidebarpanels.cpp
    # Graphic sidebar Add-Signal dialog (shell: DbcManager -> graphic addSignals)
    ui/dbcsignalpickerdialog.h
    ui/dbcsignalpickerdialog.cpp
    # Watcher page (doc Watcher plan A: shell-owned; BusStatistics/DbcManager)
    ui/watcherview.h
    ui/watcherview.cpp
    ui/spliteditorarea.h
    ui/spliteditorarea.cpp
    # signalsendtab/playbacktab/offlineanalysistab/recordtab moved
    # openbus_transceive.dll (split B2)
    # dbcdetailtab/tools/dbcsignallistview moved to openbus_dbc.dll (split B3)
    # thememanager moved to openbus_data (§4.3: used by all; same DLL as singletons)
    # markettab/marketmodule moved to openbus_market.dll (split B1);
    # marketmodel moved to openbus_data (B5-5: shared by sidebar/shell/market)
    # measurementsetupview/deviceconnectiontab moved to openbus_flow.dll (B4)
    # tools - built-in analysis (blfasconverter/dbctoolview/loganalysisview now plugins/)
    # dbcsignallistview moved to openbus_dbc.dll (split B3)
    ui/tools/iographview.h
    ui/tools/iographview.cpp
)

# ============================================================
#  openbus_data - shared foundation DLL (split §4.1/B1)
#  = former openbus_core (core/models/utils) + thememanager (from ui)
#  All core singletons (AppConfig/DbcManager/PluginManager/ThemeManager/
#  ModuleRegistry...) stay process-unique here (§4.2 decision 2).
#  MinGW GNU ld exports all symbols by default; no per-class export macros (§4.2 #1).
# ============================================================
add_library(openbus_data SHARED
    ${SRC_CORE}
    ${SRC_MODELS}
    ${SRC_UTILS}
    ui/thememanager.h
    ui/thememanager.cpp
)

# PUBLIC: business DLLs / shell get these include paths by linking openbus_data
#   - spdlog: core/logging.h (public) includes <spdlog/spdlog.h>
#   - nlohmann_json: core/appconfig.h (public) includes <nlohmann/json.hpp>
#   - concurrentqueue: core/cansimulator.h (public) -> utils/message_queue.h -> <concurrentqueue.h>
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

# BUSMUST BMAPI types (bm_usb_def.h) for CanDeviceBusmust
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/bmapi-sdk/include/bm_usb_def.h")
    target_include_directories(openbus_data PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/bmapi-sdk/include)
endif()

target_link_libraries(openbus_data PUBLIC Qt6::Widgets)

# Svg: utils/svg_icon.h (public; thememanager/market/sidebar) -> PUBLIC
target_link_libraries(openbus_data PUBLIC Qt6::Svg)

# Network: MarketIndex (market.json / images / driver package download)
target_link_libraries(openbus_data PRIVATE Qt6::Network)

# ZeroMQ: plugin host control (ROUTER/DEALER) + data (PUB) — scheme B
find_path(ZMQ_INCLUDE_DIR zmq.h)
find_library(ZMQ_LIBRARY NAMES zmq libzmq)
if(ZMQ_INCLUDE_DIR AND ZMQ_LIBRARY)
    target_include_directories(openbus_data PRIVATE ${ZMQ_INCLUDE_DIR})
    target_link_libraries(openbus_data PRIVATE ${ZMQ_LIBRARY})
    message(STATUS "ZeroMQ found: ${ZMQ_LIBRARY}")
    # Runtime DLL must sit beside the DLL (Explorer launch has no MSYS PATH).
    # openbus.exe shares the same OUTPUT dir as openbus_data.
    get_filename_component(_zmq_libdir "${ZMQ_LIBRARY}" DIRECTORY)
    get_filename_component(_zmq_prefix "${_zmq_libdir}" DIRECTORY)
    set(OPENBUS_ZMQ_DLL "${_zmq_prefix}/bin/libzmq.dll")
    if(EXISTS "${OPENBUS_ZMQ_DLL}")
        add_custom_command(TARGET openbus_data POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${OPENBUS_ZMQ_DLL}"
                    "$<TARGET_FILE_DIR:openbus_data>/libzmq.dll"
            COMMENT "Copying libzmq.dll next to openbus_data"
        )
        # libzmq pulls libsodium (SONAME libsodium-26.dll on current MSYS2)
        set(_sodium_dll "${_zmq_prefix}/bin/libsodium-26.dll")
        if(EXISTS "${_sodium_dll}")
            add_custom_command(TARGET openbus_data POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_sodium_dll}"
                        "$<TARGET_FILE_DIR:openbus_data>/libsodium-26.dll"
                COMMENT "Copying libsodium-26.dll (libzmq dependency)"
            )
        endif()
    else()
        message(WARNING "libzmq.dll not found at ${OPENBUS_ZMQ_DLL}; runtime will fail outside MSYS PATH")
        unset(OPENBUS_ZMQ_DLL)
    endif()
else()
    message(FATAL_ERROR "ZeroMQ not found (install mingw-w64-ucrt-x86_64-zeromq / cppzmq)")
endif()

# zlib: blf.cpp needs zlib inflate (MSYS2 / MinGW libz)
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    target_link_libraries(openbus_data PRIVATE ZLIB::ZLIB)
else()
    target_link_libraries(openbus_data PRIVATE z)
endif()
if(TARGET vector_blf)
    target_link_libraries(openbus_data PRIVATE vector_blf)
    target_compile_definitions(openbus_data PRIVATE HAS_VECTOR_BLF)
endif()

# pugixml: used by arxml_importer.cpp (if pugixml target exists)
if(TARGET pugixml)
    target_link_libraries(openbus_data PRIVATE pugixml)
endif()

# PCH - lean Qt headers for core (no Widget) for faster compiles.
# thememanager.cpp pulls QApplication/QPalette via its own includes,
# so the other ~90 core objects keep a smaller PCH.
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
#  openbus_market - plugin market DLL (split B1, module template)
#  markettab / marketmodule + sole exported C factory
#  openbus_createMarketModule() (§4.2 decision 1: minimal ABI)
#  deps: openbus_data (singletons shared via DLL; marketmodel in data since B5-5)
#  + Qt Widgets/Network/Svg
# ============================================================
add_library(openbus_market SHARED
    ui/markettab.h
    ui/markettab.cpp
    # flow layout - market home card wrap (VS Code marketplace-like)
    ui/flowlayout.h
    ui/flowlayout.cpp
    ui/marketmodule.h
    ui/marketmodule.cpp
)

# PUBLIC openbus_data: module headers reference core types + thememanager
target_link_libraries(openbus_market PUBLIC
    openbus_data
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
)

# PCH - same Qt Widget header set as shell (markettab Widget usage is similar)
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
#  openbus_transceive - TX/RX business DLL (split B2)
#  send/playback/offline/record pages + TransceiveModule adapter,
#  sole C factory openbus_createTransceiveModule().
#  deps: openbus_data (player/recorder/deviceManager/dbcManager singletons
#  via ShellContext pointers) + Qt Widgets/Svg
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

# PCH - TX/RX pages use Widgets at market-page level (tables/sliders/combos)
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
#  openbus_dbc - DBC business DLL (split B3)
#  DBC detail pages (multi-instance, one per file) + signal list + DbcModule,
#  sole C factory openbus_createDbcModule().
#  deps: openbus_data (dbcManager via ShellContext) + Qt Widgets/Svg
#  note: dbcimportdialog stays in openbus_transceive (send page only; see B2)
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

# PCH - DBC pages are mostly tree/table Widgets
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
#  openbus_flow - measurement flow DLL (split B4)
#  Flow setup (MeasurementSetupView) + device connection (DeviceConnectionTab)
#  + FlowModule adapter; sole C factory openbus_createFlowModule().
#  deps: openbus_data (player/simulator/deviceManager/dbcManager singletons
#  via ShellContext pointers) + Qt Widgets/Svg
# ============================================================
add_library(openbus_flow SHARED
    ui/measurementsetupview.h
    # Graphics items definition (shared internal header with Q_OBJECT)
    ui/measurementsetupview.gfx.h
    # MeasurementSetupView modular split (each file < 500 lines)
    ui/measurementsetupview.ui.cpp
    ui/measurementsetupview.render.cpp
    ui/measurementsetupview.connections.cpp
    ui/measurementsetupview.events.cpp
    ui/measurementsetupview.dialogs.cpp
    ui/measurementsetupview.utils.cpp
    ui/measurementsetupview.menu.cpp
    ui/measurementsetupview.toolbar.cpp
    ui/measurementsetupview.module.cpp
    
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

# PCH - Flow pages: QGraphicsScene/View + forms
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
#  openbus_trace - Trace business DLL (split B5)
#  TraceTab (Wireshark-like: filter bar + frame list + Explorer bottom) and
#  components (TraceView/stats/diff/column filter/FilterBar/color rules)
#  + TraceModule adapter; sole C factory openbus_createTraceModule().
#  deps: openbus_data (dbcManager via ShellContext) + Qt Widgets/Svg
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

# PCH - Trace pages are mostly table views
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
#  openbus_graphic - Graphic business DLL (split B5)
#  GraphicView (multi qcustomplot curves) + downsample + signal config +
#  DataWindow live signal table + GraphicModule; sole C factory
#  openbus_createGraphicModule()。
#  deps: openbus_data + qcustomplot + Qt Widgets/Svg/PrintSupport
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

# qcustomplot: only graphicview.cpp (fwd decl) -> PRIVATE;
# qcustomplot transitively needs Qt6::PrintSupport
target_link_libraries(openbus_graphic PRIVATE
    qcustomplot
)

# PCH - Graphic pages are mostly qcustomplot custom paint
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
#  openbus_ui - shell UI static lib (remaining widgets; thin archive)
#  UI edits rebuild this lib + final link only; not data/market
# ============================================================
add_library(openbus_ui STATIC
    ${SRC_UI}
)

# PUBLIC: exe gets openbus_data include paths by linking openbus_ui
target_link_libraries(openbus_ui PUBLIC
    openbus_data
    Qt6::Widgets
)

# Qt6::Svg: SVG line icons (activitybar / sidebarpanels)
# Qt6::Network: sidebar ExtensionsPanel async icons (marketmodel in data)
# qcustomplot: I/O Graph tool page (ui/tools/iographview) - main-window tool,
# not Graphic module; after B5 qcustomplot is mostly in openbus_graphic, but
# this one use stays in openbus_ui (static; compile-time only, no DLL ship)
target_link_libraries(openbus_ui PRIVATE
    Qt6::Svg
    qcustomplot
)

# Windows: psapi for process resource monitor (ExtensionsTab)
if(WIN32)
    target_link_libraries(openbus_ui PRIVATE psapi)
endif()

# PCH - full Qt Widget header set for UI (incl. QObject for module adapters)
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
#  openbus - executable target
# ============================================================
qt_add_executable(openbus
    main.cpp
    ../resources/resources.qrc
)

target_link_libraries(openbus PRIVATE
    openbus_ui
    # transition (B5-5 decoupled): marketmodel is in openbus_data; shell need not
    # link openbus_market to resolve MarketItem/MarketModel; keep link only so
    # main() can register the module via C factory (§4.2 decision 1)
    openbus_market
    # transceive (B2): main() registers via openbus_createTransceiveModule,
    # shell drives four pages via ModuleRegistry::createPage/invoke/query
    openbus_transceive
    # DBC (B3): main() registers via openbus_createDbcModule,
    # shell opens DBC detail via createPage("detail", fileName, ctx)
    openbus_dbc
    # flow (B4): main() registers via openbus_createFlowModule,
    # shell drives Flow/device pages and measure orchestration via createPage/invoke/query
    openbus_flow
    # trace (B5): main() registers via openbus_createTraceModule,
    # shell drives Trace page + frame dispatch/filter/color via createPage/invoke/query
    openbus_trace
    # graphic (B5): main() registers via openbus_createGraphicModule,
    # shell drives Graphic/DataWindow + signal add via createPage/invoke/query
    openbus_graphic
)

# Ensure libzmq.dll is beside openbus.exe even if only the exe is rebuilt
if(DEFINED OPENBUS_ZMQ_DLL AND EXISTS "${OPENBUS_ZMQ_DLL}")
    add_custom_command(TARGET openbus POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${OPENBUS_ZMQ_DLL}"
                "$<TARGET_FILE_DIR:openbus>/libzmq.dll"
        COMMENT "Copying libzmq.dll next to openbus.exe"
    )
endif()

# ============================================================
#  UI translations (.ts -> .qm next to exe)
#  Source language is English in code; catalogs under translations/
#  Refresh strings: run scripts/update_translations.py then rebuild
# ============================================================
set(OPENBUS_TS_FILES
    ${CMAKE_SOURCE_DIR}/translations/openbus_zh_CN.ts
    ${CMAKE_SOURCE_DIR}/translations/openbus_zh_TW.ts
    ${CMAKE_SOURCE_DIR}/translations/openbus_es.ts
    ${CMAKE_SOURCE_DIR}/translations/openbus_fr.ts
    ${CMAKE_SOURCE_DIR}/translations/openbus_de.ts
    ${CMAKE_SOURCE_DIR}/translations/openbus_ja.ts
    ${CMAKE_SOURCE_DIR}/translations/openbus_pt_BR.ts
    ${CMAKE_SOURCE_DIR}/translations/openbus_ru.ts
    ${CMAKE_SOURCE_DIR}/translations/openbus_ko.ts
)

if(Qt6LinguistTools_FOUND)
    set_source_files_properties(${OPENBUS_TS_FILES} PROPERTIES
        OUTPUT_LOCATION "${CMAKE_BINARY_DIR}/translations")
    qt6_add_translation(OPENBUS_QM_FILES ${OPENBUS_TS_FILES})
    add_custom_target(openbus_qm ALL DEPENDS ${OPENBUS_QM_FILES})
    add_dependencies(openbus openbus_qm)
    add_custom_command(TARGET openbus POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:openbus>/translations"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${OPENBUS_QM_FILES}
                "$<TARGET_FILE_DIR:openbus>/translations/"
        COMMENT "Copying openbus_*.qm to output translations/"
    )
    install(FILES ${OPENBUS_QM_FILES} DESTINATION bin/translations)
else()
    message(STATUS "Qt6 LinguistTools not found — skip .qm build; place openbus_*.qm under translations/")
    # Still copy any prebuilt .qm from source tree if present
    add_custom_command(TARGET openbus POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:openbus>/translations"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/translations"
                "$<TARGET_FILE_DIR:openbus>/translations"
        COMMENT "Copying translations/ (ts and any qm) to output"
    )
endif()

install(DIRECTORY "${CMAKE_SOURCE_DIR}/plugins/_shared/locales"
        DESTINATION bin/plugins/_shared
        OPTIONAL)

# ============================================================
#  Windows: GUI subsystem (no console window)
# ============================================================
if(WIN32)
    set_target_properties(openbus PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# ============================================================
#  Driver DLLs - copy driver/ DLLs next to the exe after build
#  ZLG SDK zlgcan.dll must sit beside the exe to find USB drivers
#  driver/ is the project store; copy beside exe at build time
# ============================================================
if(EXISTS "${CMAKE_SOURCE_DIR}/driver")
    add_custom_command(TARGET openbus POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/driver"
                "$<TARGET_FILE_DIR:openbus>"
        COMMENT "Copying driver DLLs to output directory"
    )
    # install rule - package includes driver DLLs
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/driver/"
            DESTINATION bin
            FILES_MATCHING PATTERN "*.dll"
    )
endif()

# ============================================================
#  Plugin host materials - sdk/ (+ optional host scripts) into output dir
#  Suite Python code loads from SOURCE plugins/ when openbus runs from
#  build/bin (PluginManager::resolvePluginsDir). POST_BUILD still copies
#  sdk + _shared for host bootstrap; do NOT rely on build/bin/plugins/<suite>
#  for live edits — that tree is only for .opk install testing
#  (SIN_USE_INSTALLED_PLUGINS=1).
# ============================================================
if(EXISTS "${CMAKE_SOURCE_DIR}/sdk")
    set(_openbus_host_scripts "")
    foreach(_script IN ITEMS sin_host.py plugin_tool.py driver_tool.py)
        if(EXISTS "${CMAKE_SOURCE_DIR}/scripts/${_script}")
            list(APPEND _openbus_host_scripts "${CMAKE_SOURCE_DIR}/scripts/${_script}")
        endif()
    endforeach()

    add_custom_command(TARGET openbus POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/sdk"
                "$<TARGET_FILE_DIR:openbus>/sdk"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:openbus>/scripts"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:openbus>/plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/plugins/_shared"
                "$<TARGET_FILE_DIR:openbus>/plugins/_shared"
        COMMENT "Copying plugin host materials (sdk + scripts + _shared) to output directory"
    )
    if(_openbus_host_scripts)
        add_custom_command(TARGET openbus POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${_openbus_host_scripts}
                    "$<TARGET_FILE_DIR:openbus>/scripts/"
            COMMENT "Copying available plugin host scripts"
        )
    else()
        message(STATUS "Plugin host scripts not found under scripts/; sdk copy only")
    endif()
endif()

# ============================================================
#  Install rules
# ============================================================
install(TARGETS openbus
    RUNTIME DESTINATION bin
)
