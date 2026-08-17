---
kind: build_system
name: CMake + Python 构建脚本的 Qt6 桌面应用构建系统
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - scripts/build.py
    - third_party/Dependencies.cmake
    - resources/resources.qrc
---

## 1. 使用的系统与工具

- **构建系统**: CMake 3.21+，作为唯一官方构建入口；顶层 `CMakeLists.txt` 定义项目、Qt6 查找与子目录，`src/CMakeLists.txt` 组织源文件并生成目标。
- **编译器/工具链**: Windows MinGW (g++ 13 / gcc 13)，通过环境变量 `SIN_MINGW_DIR`、`SIN_QT_DIR`、`SIN_CMAKE_DIR` 或命令行 `--mingw-dir/--qt-dir/--cmake-dir` 指定；默认路径硬编码在 `scripts/build.py`。
- **生成器**: 优先 Ninja（本地 `tools/ninja/ninja.exe`），回退到 MinGW Makefiles；两种均支持 `-j` 并行编译。
- **打包/部署**: 使用 `windeployqt` 自动复制 Qt 运行时依赖；驱动 DLL 通过 `add_custom_command(TARGET openbus POST_BUILD)` 复制到输出目录。
- **辅助脚本**: `scripts/build.py` 提供 `configure/build/run/debug/clean/rebuild/deploy/all/status/open` 等子命令，封装 CMake 调用、环境 PATH 注入、运行中进程终止、Ninja/MinGW 选择等。

## 2. 关键文件

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层工程：版本 `0.1.0`、C++17、Qt6 Widgets/PrintSupport/Svg、包含 `third_party/Dependencies.cmake`、`add_subdirectory(src)`、安装规则 |
| `src/CMakeLists.txt` | 源文件分组 (`SRC_CORE`/`SRC_MODELS`/`SRC_UTILS`/`SRC_UI`)，生成三个目标：静态库 `openbus_core`、静态库 `openbus_ui`、可执行 `openbus`；配置 PCH、链接 z/vector_blf/pugixml/qcustomplot/Qt；Windows GUI 属性、驱动 DLL 拷贝 |
| `scripts/build.py` | 统一构建入口：检测 Ninja/MinGW、设置 PATH、调用 CMake、自动 kill 运行中的 `openbus.exe`、调用 `windeployqt`、补充 `Qt6PrintSupport.dll` |
| `third_party/Dependencies.cmake` | 第三方依赖声明：spdlog (INTERFACE)、qcustomplot (STATIC)、vector_blf (可选 add_subdirectory) |
| `resources/resources.qrc` | Qt 资源文件，随 `qt_add_executable` 一起编译进二进制 |
| `driver/` | ZLG CAN SDK 的 `.dll/.lib` 及 USB 设备 XML 配置，编译后整体拷贝到 exe 同级目录 |
| `plugins/*/plugin.json` | 插件元数据（非构建产物，但由运行时加载） |

## 3. 架构与约定

- **分层静态库拆分**：`openbus_core`（核心逻辑 + 数据模型 + 工具）、`openbus_ui`（界面组件，依赖 core）、`openbus`（可执行）。这样修改 UI 不会触发 core 重编译，提升增量编译速度。
- **PCH 预编译头**：core 层仅预编译精简 Qt 头（无 Widget），ui 层预编译完整 Widget 头列表，加速编译。注释明确说明不使用 ccache（与 MinGW g++ 13 的 PCH 不兼容会静默崩溃）和 LLD（Windows 上文件锁问题）。
- **Qt 自动化**：开启 `AUTOMOC/AUTOUIC/AUTORCC`，`.qrc` 资源直接嵌入；`qt_standard_project_setup()` 启用 Qt 标准项目设置。
- **依赖管理**：第三方库以源码形式放入 `third_party/`，通过 `Dependencies.cmake` 暴露为 CMake 目标；可选依赖用 `if(EXISTS ...)` 包裹，保证在没有 vendored 源码时仍可编译。
- **平台差异处理**：Windows 下设置 `WIN32_EXECUTABLE TRUE` 隐藏控制台窗口；`psapi` 仅在 WIN32 链接；驱动 DLL 仅在存在 `driver/` 目录时拷贝。
- **输出布局**：可执行文件输出到 `${CMAKE_BINARY_DIR}/bin`；驱动 DLL 与 exe 同目录；Qt 运行时通过 `windeployqt` 部署。

## 4. 约定与约束

- **C++ 标准**：强制 C++17 (`CMAKE_CXX_STANDARD 17` + `REQUIRED ON` + `EXTENSIONS OFF`)。
- **构建类型**：仅支持 `Debug` / `Release` / `RelWithDebInfo` / `MinSizeRel` 四种。
- **工具路径优先级**：环境变量 `SIN_*_DIR` > 命令行参数 > 脚本内默认路径（如 `C:/Qt/6.8.3/mingw_64`）。
- **Ninja 优先**：若 `tools/ninja/ninja.exe` 存在则使用 Ninja 生成器，否则回退 MinGW Makefiles 并显式传入 `CMAKE_MAKE_PROGRAM`。
- **禁止 ccache 与 LLD**：脚本与 CMake 注释均明确禁止——ccache 与 MinGW g++ 13 的 PCH 不兼容，LLD 在 Windows 上导致文件锁问题。
- **构建前自动 kill**：`cmd_build`/`cmd_run`/`cmd_debug` 在编译/运行前调用 `taskkill /F /IM openbus.exe`，等待文件锁释放后再写入，避免链接失败。
- **驱动 DLL 必须随 exe 分发**：ZLG SDK 的 `zlgcan.dll` 必须与 exe 在同一目录才能找到 USB 驱动，因此 `POST_BUILD` 阶段将 `driver/` 整个目录复制到输出目录。
- **Qt 运行时补充**：`windeployqt` 无法识别静态库 `qcustomplot` 对 `Qt6PrintSupport.dll` 的传递依赖，脚本手动复制该 DLL。
- **版本**：项目在 `CMakeLists.txt` 中声明 `VERSION 0.1.0`，未看到 CI/发布流水线，版本号维护方式未见自动化脚本。
- **无 Dockerfile / CI 配置**：仓库根目录未发现 Dockerfile、GitHub Actions、Jenkins 等 CI 配置文件，构建主要面向本地 MinGW+Ninja 环境。