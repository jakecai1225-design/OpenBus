---
kind: build_system
name: CMake + Python 构建打包流水线（Qt6/MinGW/Ninja）
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

## 1. 系统概览

本项目采用 **CMake 3.21+** 作为核心构建系统，配合自研 Python 脚本 `scripts/build.py` 与 `scripts/package.py` 完成配置、编译、测试、部署与打包全流程。目标平台为 **Windows (MinGW g++ 13.1 x64)**，使用 **Qt6 (Widgets/PrintSupport/Svg/Network/Test)** 框架，构建工具优先选择 **Ninja**（本地 `tools/ninja/ninja.exe`），回退到 MinGW Makefiles。

顶层 `CMakeLists.txt` 定义项目版本 `0.1.0`、启用 C++17、开启 Qt AUTOMOC/AUTOUIC/AUTORCC，并通过 `third_party/Dependencies.cmake` 聚合第三方依赖（spdlog 头文件库、nlohmann/json 单头、qcustomplot 静态库、vector_blf 子工程）。链接器强制使用默认 `ld.bfd`（禁用 LLD/gold，避免 Windows PE 文件锁问题），并针对 MinGW 启用 thin archive (`ar qcT/qT`) 以将静态库重打包降为毫秒级。

## 2. 关键文件与目录

- `CMakeLists.txt`：顶层工程入口，定义版本、Qt 查找、子目录、安装规则。
- `src/CMakeLists.txt`：核心构建逻辑——按拆分方案 B1–B5 定义多个共享 DLL 目标：`openbus_data`（公共底座）、`openbus_market`、`openbus_transceive`、`openbus_dbc`、`openbus_flow`、`openbus_trace`、`openbus_graphic`，以及静态库 `openbus_ui` 和可执行目标 `openbus`；每个 DLL 通过 `target_precompile_headers` 精确声明 PCH 头清单以加速编译。
- `drivers/CMakeLists.txt`：驱动插件 `.odp` 包构建，输出布局 `build/bin/drivers/<id>/driver.json + driver_<id>.dll`，由 `deploy_driver_manifest` 函数统一拷贝清单。
- `tests/CMakeLists.txt`：L1 集成测试（Qt Test，`QTEST_GUILESS_MAIN`）与 L2 offscreen UI 测试（`test_ui_offscreen` 链接壳 UI + 全部业务模块 DLL），通过 `add_custom_target(tests)` 聚合，由 `build.py test` 调用 ctest 执行。
- `third_party/Dependencies.cmake`：第三方依赖的 CMake 接口封装。
- `scripts/build.py`：开发者入口，支持 `configure/build/run/debug/clean/rebuild/deploy/all/status/open/test` 子命令；自动检测 Ninja/MinGW/Qt 路径（环境变量 `SIN_QT_DIR` / `SIN_MINGW_DIR` / `SIN_CMAKE_DIR` 优先），支持 Dev 档（`--build-type Dev --build-dir build-dev`）与全量 Debug 并存。
- `scripts/package.py`：发布流水线，实现 doc/打包安装方案.md §5 的 8 步流程：Release 构建 → windeployqt → staging 组装 → Python 运行时捆绑 → 附加文件 → objdump 依赖校验 → 体积报告 → zip 便携版 + Inno Setup 安装器。
- `installer/openbus.iss`：Inno Setup 安装脚本（可选，缺失时跳过不阻塞）。

## 3. 架构与约定

### 多 DLL 模块化拆分（B1–B5）
主程序 `openbus.exe` 仅负责装配，业务功能拆入独立 DLL，通过 C 工厂函数（如 `openbus_createTraceModule()`）动态注册，降低耦合与链接时间。每个 DLL 有独立的 PCH 头清单，减少不必要的 Qt 头包含。

### 驱动插件外置（`.odp`）
厂商驱动（zlg/peak/kvaser/slcan/candle）编译为独立 DLL 并附带 `driver.json`，由 `DriverRegistry` 在 `<exe>/drivers/<id>/` 扫描加载，不随主程序静态链接。

### 构建产物布局
- `build/bin/`：可执行文件、业务 DLL、驱动 `.odp` 目录。
- `build-rel/bin/`：Release 构建产物（打包专用，与开发 Debug 分离）。
- `dist/stage/openbus/`：staging 目录，白名单拷贝 exe/DLL/Qt 插件/Python runtime/sdK/scripts/market。
- `dist/`：最终产物 `openbus-<ver>-win64-portable.zip` 与 `openbus-<ver>-win64-setup.exe`。

### 构建类型约定
| 类型 | 用途 | 优化/调试 | 目录 |
|---|---|---|---|
| `Dev` | 日常开发 | `-O1 -g1`，快速增量 | `build-dev/` |
| `Debug` | 全信息调试 | 完整符号 | `build/` |
| `Release` | 发布构建 | 无调试信息 | `build-rel/` |
| `RelWithDebInfo` | 发布带符号 | — | 任意 |
| `MinSizeRel` | 最小体积 | — | 任意 |

### 测试体系
- L1：纯 core 逻辑测试，`QTEST_GUILESS_MAIN`，进程隔离单例状态。
- L2：offscreen UI 测试，设置 `QT_QPA_PLATFORM=offscreen`，复刻 main.cpp 初始化链。
- 聚合目标 `tests` 由 `build.py test` 构建后通过 ctest 执行，失败时 `--output-on-failure` 打印完整日志。

## 4. 约定与约束

- **链接器约束**：Windows MinGW 必须使用默认 `ld.bfd`，禁用 LLD/gold（PE 子系统选项不支持或产出损坏可执行）。
- **缓存约束**：禁用 ccache（与 MinGW g++ 13 的 PCH 不兼容，会静默崩溃）。
- **PCH 策略**：每个 DLL 目标显式声明 `target_precompile_headers` 私有头清单，禁止隐式包含大 Qt 头。
- **thin archive**：MinGW 下 `ar qcT/qT` 用于静态库归档，避免 Windows 命令行长度限制导致的分批归档失败。
- **构建前清理**：`cmd_build` 自动 `taskkill openbus.exe` 释放文件锁，再执行编译。
- **打包白名单**：`package.py` 中 `BIN_FILES` / `BIN_DIRS` 严格限定拷贝范围，`driver/` 与 `drivers/` 均不预装（用户从市场安装）。
- **依赖完整性门禁**：步骤 6 用 `objdump -p` 解析 staging 内所有 exe/dll/pyd 的 import 表，逐一核对是否在 staging 或系统 DLL 白名单（含 `api-ms-*` / `ext-ms-*`），任一缺失即 FAIL。
- **Python 运行时隔离**：捆绑 Python 解释器时使用 `pythonXY._pth` 隔离 sys.path，剔除 tkinter/tk 等 GUI 依赖，仅保留 uds-diagnostic 所需的 PyQt6 QtCore/Gui/Widgets/Svg 子集。
- **版本来源**：版本号来自 `CMakeLists.txt` 的 `project(... VERSION ...)`，`package.py` 读取 `CMakeCache.txt` 中的 `openbus_VERSION` 或正则匹配源码行。
- **安装规则**：顶层 `install(TARGETS openbus RUNTIME DESTINATION bin)`，驱动 DLL 通过 `POST_BUILD` 拷贝至输出目录，安装时一并复制。