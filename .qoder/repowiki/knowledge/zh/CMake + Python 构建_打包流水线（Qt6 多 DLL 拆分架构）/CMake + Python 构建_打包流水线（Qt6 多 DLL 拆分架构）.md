---
kind: build_system
name: CMake + Python 构建/打包流水线（Qt6 多 DLL 拆分架构）
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - scripts/build.py
    - scripts/package.py
    - third_party/Dependencies.cmake
    - installer/openbus.iss
    - drivers/zlg/CMakeLists.txt
    - drivers/peak/CMakeLists.txt
    - drivers/kvaser/CMakeLists.txt
    - drivers/slcan/CMakeLists.txt
    - drivers/candle/CMakeLists.txt
    - tests/CMakeLists.txt
---

## 1. 系统概览

本项目采用 **CMake (≥3.21) + Qt6** 作为核心构建系统，配合自研的 **Python 编排脚本** (`scripts/build.py`、`scripts/package.py`) 完成配置、编译、部署、测试与产物打包。目标平台为 Windows (MinGW g++ 13)，通过 `windeployqt` 实现免依赖分发。

- 构建工具链：CMake → Ninja（优先，位于 `tools/ninja/`）或 MinGW Makefiles；编译器为 MinGW g++ 13；链接器强制使用默认 `ld.bfd`（禁用 LLD/gold，避免 PE 文件锁与子系统选项问题）。
- C++ 标准：C++17，关闭扩展；启用 `AUTOMOC/AUTOUIC/AUTORCC`。
- 预编译头：每个共享库/静态库独立定义精简 PCH（`target_precompile_headers`），避免全量 Qt 头膨胀。
- 构建类型：内置 `Dev`（`-O1 -g1`，用于日常开发目录 `build-dev/`）、`Debug`、`Release`、`RelWithDebInfo`、`MinSizeRel`。

## 2. 关键文件与包

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 根工程：版本 `0.1.0`、Qt6 查找、第三方依赖引入、子目录装配 |
| `src/CMakeLists.txt` | 核心构建逻辑：定义 `openbus_data` / `openbus_market` / `openbus_transceive` / `openbus_dbc` / `openbus_flow` / `openbus_trace` / `openbus_graphic` 等共享库及 `openbus_ui` 静态库，最终链接到 `openbus` 可执行 |
| `drivers/*/CMakeLists.txt` | 5 个外置驱动插件（zlg/peak/kvaser/slcan/candle）各自编译为 `driver_<id>.dll`，输出至 `build/bin/drivers/<id>/` |
| `third_party/Dependencies.cmake` | 声明 spdlog、nlohmann/json、qcustomplot（静态库）、vector_blf 等三方依赖 |
| `scripts/build.py` | 统一入口：configure/build/run/debug/clean/rebuild/deploy/all/test/status/open，自动探测 Qt/MinGW/CMake/Ninja，管理 `--build-dir`（支持 build 与 build-dev 并存） |
| `scripts/package.py` | 完整发布流水线：Release 构建 → windeployqt → staging 组装 → 捆绑 Python+PyQt6 运行时 → 依赖完整性校验（objdump import 表）→ zip 便携版 → Inno Setup 安装器 |
| `installer/openbus.iss` | Inno Setup 安装脚本（双语向导、覆盖安装、ZCANPRO/PCANUSB/canlib32 缺失提示） |
| `tests/CMakeLists.txt` | 启用 ctest，聚合所有单元测试目标 |

## 3. 架构与约定

### 3.1 多 DLL 拆分架构（B1–B5 方案）

`src/CMakeLists.txt` 将应用拆分为多个共享库，每个业务模块暴露唯一 C 工厂函数（如 `openbus_createMarketModule()`、`openbus_createTransceiveModule()` 等），由主程序 `main.cpp` 动态加载。分层如下：

- `openbus_data`（公共底座 DLL）：core/models/utils + thememanager，持有全部单例（AppConfig/DbcManager/PluginManager/ThemeManager…），被其他 DLL 以 PUBLIC 方式链接。
- `openbus_market` / `openbus_transceive` / `openbus_dbc` / `openbus_flow` / `openbus_trace` / `openbus_graphic`：各业务模块 DLL，仅依赖 `openbus_data` 和对应 Qt 组件。
- `openbus_ui`（静态库）：壳 UI 剩余组件，编译期依赖 qcustomplot（不随 DLL 分发）。
- `openbus`（GUI 可执行，`WIN32_EXECUTABLE TRUE`）：链接所有模块 DLL。

### 3.2 驱动插件体系

`drivers/` 下每个厂商一个子目录，包含 `CMakeLists.txt`、`driver.json`、`*_driver_plugin.{cpp,h}`，编译为 `driver_<id>.dll` 并复制到 `build/bin/drivers/<id>/`。打包时通过 `package.py` 校验 5 个驱动布局完整性（`driver.json` + `driver_*.dll`）。驱动不预装到最终安装包，用户从市场安装。

### 3.3 构建目录策略

- 默认构建目录 `build/`（全量 Debug）。
- 开发快速档 `build-dev/`（`--build-type Dev --build-dir build-dev`），与全量 Debug 并存，避免切换档位触发全量重编。
- Release 构建固定使用 `build-rel/`（`package.py` 步骤 1 调用 `build.py configure --build-type Release --build-dir build-rel`）。

### 3.4 打包流水线（`scripts/package.py`）

8 步幂等流程：
1. 配置并构建 Release（复用 `build.py`）。
2. `windeployqt --release --no-translations --compiler-runtime` 部署 Qt 运行时，手动补拷 `Qt6PrintSupport.dll`（qcustomplot 静态库传递依赖）。
3. 白名单拷贝至 `dist/stage/openbus/`（`BIN_FILES`/`BIN_DIRS` 列表控制）。
4. 捆绑 Python 运行时（精简拷贝 + `pythonXY._pth` 隔离 + PyQt6 裁剪子集：QtCore/Gui/Widgets/Svg）。
5. 附加 `THIRD_PARTY_NOTICES.md` / `README-PORTABLE.txt` / `LICENSE.txt`。
6. objdump 导入表校验：逐一解析 exe/dll/pyd 的依赖，不在 staging 内且非系统白名单即失败。
7. 生成体积报告 `dist/package-report.txt`。
8a. 压缩为 `openbus-<ver>-win64-portable.zip`。
8b. 可选编译 Inno Setup 安装器（`iscc` 不存在则跳过，不阻塞便携版）。

### 3.5 测试

通过 `enable_testing()` + `add_subdirectory(tests)` 集成 ctest。`build.py test` 仅构建 `tests` 聚合目标（避免主程序重链），再执行 `ctest --output-on-failure`。

## 4. 约定与约束

- **链接器约束**：Windows MinGW 必须使用默认 `ld.bfd`，禁止 LLD/gold（注释中明确说明 LLD 曾产出损坏可执行、gold 不支持 PE 子系统选项）。
- **缓存约束**：不使用 ccache（与 MinGW g++ 13 的 PCH 不兼容，会静默崩溃）。
- **thin archive**：MinGW 下启用 `ar qcT/qT` thin archive，仅记录对象路径，使静态库重打包降为毫秒级（APPEND 必须带 T 以兼容已存在的 thin archive）。
- **进程锁保护**：`build.py` 在编译前自动 `taskkill /F openbus.exe`，等待文件锁释放后再链接，避免链接失败。
- **Qt 字体目录**：`windeployqt` 后需创建空 `lib/fonts/` 目录，否则 `QFontDatabase` 告警（Qt 6 Windows 不再自带字体）。
- **驱动不随包**：`driver/`（ZLG SDK）与 `drivers/`（.odp 外置驱动）均不预装，用户按 `README-PORTABLE.txt` 引导安装 ZCANPRO，驱动一律从市场安装。
- **版本来源**：版本号来自根 `CMakeLists.txt` 的 `project(... VERSION 0.1.0)`，`package.py` 优先读取 `CMakeCache.txt` 中的 `openbus_VERSION`，回退正则解析源码。
- **Ninja 优先**：若 `tools/ninja/ninja.exe` 存在则使用 Ninja 生成器，否则回退 MinGW Makefiles；Ninja 未检测到时打印警告但不阻断构建。
- **Qt 组件最小化**：仅请求 `Widgets/PrintSupport/Svg/Network`，并通过 PCH 精确限定每个库所需的 Qt 头集合，减少编译时间。
- **资源复制**：构建后自动复制 `driver/` 目录（含 `zlgcan.dll` 等）到 exe 同级目录；同时复制 `sdk/` 与宿主脚本 `sin_host.py`/`plugin_tool.py`/`driver_tool.py` 到输出目录供插件系统使用。