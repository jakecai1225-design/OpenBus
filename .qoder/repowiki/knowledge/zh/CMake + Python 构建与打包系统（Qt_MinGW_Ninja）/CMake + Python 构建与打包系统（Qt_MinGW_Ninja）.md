---
kind: build_system
name: CMake + Python 构建与打包系统（Qt/MinGW/Ninja）
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - drivers/CMakeLists.txt
    - tests/CMakeLists.txt
    - third_party/Dependencies.cmake
    - scripts/build.py
    - scripts/package.py
    - installer/openbus.iss
---

## 1. 构建系统与工具链

- **构建系统**：CMake 3.21+，顶层 `CMakeLists.txt` 声明项目版本 `0.1.0`，统一入口；子目录 `src/CMakeLists.txt`、`drivers/CMakeLists.txt`、`tests/CMakeLists.txt` 分别组织主程序、驱动插件和测试。
- **编译器与工具链**：Windows MinGW (g++ 13.x) + Qt6 (Widgets/Svg/Network/Test)。默认使用 `ld.bfd` 链接器（避免 LLD/gold 在 Windows PE 上的文件锁与未知选项问题），禁用 ccache（与 MinGW g++ 13 PCH 不兼容）。可选 Ninja 生成器（`tools/ninja/ninja.exe`），否则回退到 MinGW Makefiles。
- **Python 编排脚本**：`scripts/build.py` 是开发者入口，封装 configure/build/deploy/run/debug/test/clean/rebuild/status/open 等子命令；`scripts/package.py` 是发布流水线，串联 Release 构建 → windeployqt → staging 装配 → Python 运行时嵌入 → 依赖校验 → zip/Inno Setup 打包。

## 2. 关键文件与职责

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层工程配置：C++17、AUTOMOC/UIC/RCC、Qt6 find_package、子目录注册、install 规则 |
| `src/CMakeLists.txt` | 将源码拆分为 5 个共享库 (`openbus_core/data/ui/market/transceive/dbc/flow/trace/graphic`) + 静态库 `openbus_ui_shell` + 可执行 `openbus`，并定义 DLL 间依赖链 |
| `drivers/CMakeLists.txt` | 为 zlg/peak/kvaser/slcan/candle 各生成独立 `.odp` 驱动插件，POST_BUILD 复制 `driver.json` |
| `tests/CMakeLists.txt` | 用 `openbus_add_test()` 宏批量注册 L1 (Qt Test, QTEST_GUILESS_MAIN) 与 L2 offscreen UI 测试，统一通过 `ctest` 运行 |
| `third_party/Dependencies.cmake` | 以 INTERFACE IMPORTED / STATIC 方式引入 spdlog、qcustomplot、vector_blf 等第三方库 |
| `scripts/build.py` | 自动探测 Qt/MinGW/CMake 路径，调用 CMake 配置与编译，支持 `--build-dir build-dev` Dev 构建类型 (`-O1 -g1`) |
| `scripts/package.py` | 完整发布流水线：Release 构建、windeployqt、staging 拷贝、Python runtime + PyQt6 嵌入、objdump 依赖检查、zip/Inno Setup 输出 |
| `installer/openbus.iss` | Inno Setup 安装脚本（由 package.py 调用 iscc 编译） |

## 3. 架构与约定

- **DLL 分层拆分**：`src/CMakeLists.txt` 将原单体 `openbus_data` 拆为三层——`openbus_core`（纯 C++17 无 Qt Widgets）、`openbus_data`（DBC/Protocol/Project，依赖 core + Qt Widgets）、`openbus_ui`（Theme/Market/Shell，依赖 data + Qt Widgets/Svg/Network），业务模块再按功能拆为 `openbus_market`、`openbus_transceive`、`openbus_dbc`、`openbus_flow`、`openbus_trace`、`openbus_graphic`，最终 `openbus` 可执行文件链接全部模块。这种分层使增量编译从“分钟级”降至“秒级”。
- **构建产物布局**：所有目标输出到 `${CMAKE_BINARY_DIR}/bin`；驱动插件输出到 `build/bin/drivers/<id>/` 并附带 `driver.json`；`driver/` 下的 ZLG SDK DLL 在 POST_BUILD 复制到 exe 目录。
- **Dev/Debug/Release 多构建目录**：通过 `--build-dir` 参数支持 `build`（Debug）与 `build-dev`（Dev: `-O1 -g1`）共存；`build.py` 的 `cmake_needs_reconfigure()` 基于 `Makefile`/`build.ninja` 时间戳判断是否需要重新配置。
- **测试体系**：L1 测试使用 `QTEST_GUILESS_MAIN` 直接运行；L2 UI 测试通过 `QT_QPA_PLATFORM=offscreen` 在无头模式下运行 `test_ui_offscreen`，并通过 `add_custom_target(tests)` 聚合所有测试目标。
- **预编译头 (PCH)**：已显式禁用（注释掉 `target_precompile_headers`），原因是 MinGW g++ 13 的 PCH 支持不稳定，会引发静默崩溃。
- **静态库归档优化**：MinGW 下重写 `CMAKE_CXX_ARCHIVE_CREATE/APPEND` 使用 `ar qcT/qT` thin archive，仅记录对象路径，大幅降低静态库重打包时间。

## 4. 约定与约束

- **工具链环境变量**：必须设置 `SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR`（或走 `scripts/build.py` 内置探测逻辑），否则构建失败。
- **构建类型**：仅允许 `Dev`、`Debug`、`Release`、`RelWithDebInfo`、`MinSizeRel` 五种，Dev 专用 `-O1 -g1` 用于日常开发。
- **Ninja 优先**：若 `tools/ninja/ninja.exe` 存在则使用 Ninja 生成器，否则回退 MinGW Makefiles；两者均受 `--jobs` 控制并行度。
- **发布流程强制步骤**：`package.py` 严格按 8 步顺序执行（Release 构建 → windeployqt → staging → Python runtime → 附加文件 → objdump 依赖校验 → 报告 → zip/Inno Setup），任何一步失败即中止。
- **依赖校验**：发布前通过 `objdump -p` 扫描 staging 内所有 `.exe/.dll/.pyd`，排除已知系统 DLL（`SYSTEM_DLLS` 集合）后，缺失依赖视为错误并退出。
- **Qt 部署补充**：`windeployqt --release --no-translations --compiler-runtime` 后手动补拷 `Qt6PrintSupport.dll`（qcustomplot 需要）、`translations/qtbase_zh_CN.qm`、`lib/fonts` 目录以避免字体数据库警告。
- **驱动插件契约**：每个驱动目录必须提供 `driver.json` 与 `driver_<id>.dll`，由 `driver_tool.py` 打包为 `.odp`，被 `DriverRegistry` 在 `<exe>/drivers/<id>/` 下发现加载。
- **Python 插件生态**：`plugins/*/plugin.json` + `main.py` 构成 Python 插件，由 `plugin_tool.py` 打包为 `.opk`，与 C++ 驱动共同构成市场分发单元。
- **版本来源**：版本号来自顶层 `CMakeLists.txt` 的 `project(... VERSION 0.1.0)`，`package.py` 通过读取 `CMakeCache.txt` 中的 `openbus_VERSION` 或正则解析源文件获取，作为 zip 与 installer 文件名后缀。