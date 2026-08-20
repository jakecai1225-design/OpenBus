# ============================================================
#  测试 — L1 核心逻辑集成测试（Qt Test）
#  方案：doc/测试验收方案.md v2.0（自动化分层验证）
#
#  设计要点：
#  - 每个套件独立可执行进程 — 隔离 core 层进程级单例状态
#    （DbcManager/PluginManager 等，见 src/CMakeLists.txt §4.2 决策 2）
#  - 链接 openbus_data（SHARED DLL：core/models/utils + 头路径
#    PUBLIC 传递 spdlog/nlohmann/concurrentqueue）；测试 exe 输出到
#    CMAKE_RUNTIME_OUTPUT_DIRECTORY（build/bin）与 DLL 同目录，自动解析
#  - L1 套件用 QTEST_GUILESS_MAIN（QCoreApplication，不加载平台插件）
#  - SIN_SOURCE_DIR 编译宏定位 test/ 测试数据（CMake 正斜杠路径）
# ============================================================
find_package(Qt6 REQUIRED COMPONENTS Test)

function(openbus_add_test name)
    add_executable(test_${name} test_${name}.cpp)
    target_compile_definitions(test_${name} PRIVATE
        SIN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(test_${name} PRIVATE
        openbus_data
        Qt6::Test
    )
    # 主程序不链接 Qt Test，windeployqt 不会部署 Qt6Test.dll；
    # 测试 exe 与主程序共用 build/bin（运行时依赖齐全），需补拷
    if(WIN32)
        add_custom_command(TARGET test_${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:Qt6::Test> $<TARGET_FILE_DIR:test_${name}>)
    endif()
    add_test(NAME ${name} COMMAND test_${name}
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endfunction()

# ---- B1 文件 I/O（TC-01/02/03/09/10/12/16/17、ST-01）----
openbus_add_test(canfileio)

# ---- B2 过滤引擎（TF-01/02/03/04/13 表达式语义部分）----
openbus_add_test(filterengine)

# ---- B3 Trace 模型（TC-11/12、PB-01、TF-07/11/12/15）----
openbus_add_test(tracecore)

# ---- B4 DBC 解析解码（DBC-01/02/03/04、TF-14）----
openbus_add_test(dbc)

# ---- B5 采集/录制/回放（SIM-01/02/03、REC-01/02、PLAY-01/02/03）----
openbus_add_test(sim_rec_play)

# ---- B6 市场驱动（MKT-01..06，依赖 build/market/market.json）----
openbus_add_test(market_driver)

# ---- B7 工程配置（PRJ-01..06）----
openbus_add_test(project)

# ============================================================
#  C L2 offscreen UI 驱动（复刻 main.cpp 初始化链，驱动真实
#  MainWindow；无法用 openbus_add_test：需链接壳 UI 静态库 +
#  4 个业务模块 DLL + 平台插件部署，见 doc/测试验收方案.md §六 C）
# ============================================================
qt_add_executable(test_ui_offscreen
    test_ui_offscreen.cpp
    ../resources/resources.qrc   # 图标/QSS 资源随测试 exe 编译（AUTORCC 全局开启）
)
target_compile_definitions(test_ui_offscreen PRIVATE
    SIN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_ui_offscreen PRIVATE
    openbus_ui          # 壳 UI 静态库（MainWindow；PUBLIC 传递 openbus_data）
    openbus_market      # 业务模块导入库：C 工厂解析同 src/main.cpp
    openbus_transceive
    openbus_dbc
    openbus_flow
    Qt6::Test
)
if(WIN32)
    # Qt6Test.dll 部署（同 openbus_add_test：主程序不链 Test）
    add_custom_command(TARGET test_ui_offscreen POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Qt6::Test> $<TARGET_FILE_DIR:test_ui_offscreen>)
    # qoffscreen 平台插件：windeployqt 只按主程序需求部署 qwindows，
    # offscreen 无头平台插件需随测试 exe 补拷（Qt 导入目标定位，免硬编码）
    if(TARGET Qt6::QOffscreenIntegrationPlugin)
        add_custom_command(TARGET test_ui_offscreen POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>
                $<TARGET_FILE_DIR:test_ui_offscreen>/platforms/)
    endif()
endif()
add_test(NAME ui_offscreen COMMAND test_ui_offscreen
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# 平台插件选择必须先于 QApplication 构造生效（offscreen = 无头）
set_property(TEST ui_offscreen PROPERTY ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen")

# 聚合目标：build.py test 只构建测试套件，
# 不触发主程序无谓重链（MinGW ld 链接大 exe 极慢）
add_custom_target(tests
    DEPENDS test_canfileio
            test_filterengine
            test_tracecore
            test_dbc
            test_sim_rec_play
            test_market_driver
            test_project
            test_ui_offscreen
    COMMENT "聚合测试目标（L1 集成 + L2 offscreen UI 驱动）"
)
