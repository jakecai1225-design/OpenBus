---
kind: build_system
name: CMake + Qt6 构建系统（含 Python 构建脚本与第三方依赖集成）
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

- **构建系统**: CMake 3.21+，顶层 `CMakeLists.txt` 定义项目元信息（VERSION 0.1.0），并通过 `add_subdirectory(src)` 引入源码子工程。
- **编译器/工具链**: Windows 平台使用 MinGW (g++ 13) + Ninja（优先）或 MinGW Makefiles（回退）。Ninja 位于 `tools/ninja/ninja.exe`，若存在则自动选用；否则回退到 `mingw32-make.exe` / `make.exe`。
- **Qt 版本**: Qt6（Widgets、PrintSupport、Svg），通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg)` 查找，并启用 `qt_standard_project_setup()`。
- **自动化处理**: 开启 `CMAKE_AUTOMOC`、`AUTOUIC`、`AUTORCC`，使 `.ui`、`.qrc`、`.h` 中的 `Q_OBJECT` 等由 CMake 自动生成代码。
- **构建脚本**: `scripts/build.py` 提供 `configure / build / run / debug / clean / rebuild / deploy / all / status / open` 子命令，封装 CMake 配置、增量编译、`windeployqt` 部署、GDB 调试、进程锁释放等流程。
- **输出目录**: `CMAKE_RUNTIME_OUTPUT_DIRECTORY = ${CMAKE_BINARY_DIR}/bin`，最终产物为 `build/bin/sin.exe`。

## 2. 关键文件

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层工程：声明 C++17、Qt6 查找、包含 `third_party/Dependencies.cmake`、添加 `src/` 子目录、安装规则 |
| `src/CMakeLists.txt` | 核心构建逻辑：拆分 `sin_core`（静态库）、`sin_ui`（静态库）、`sin`（可执行目标），配置 PCH、链接库、驱动 DLL 拷贝 |
| `scripts/build.py` | Python 构建编排器：环境检测、Ninja/MinGW 生成器选择、`windeployqt` 部署、GDB 调试、并行编译 `-j` |
| `third_party/Dependencies.cmake` | 第三方依赖聚合：spdlog（INTERFACE IMPORTED）、nlohmann/json（单头）、qcustomplot（静态库）、vector_blf（subdir） |
| `resources/resources.qrc` | Qt 资源清单（图标、样式表） |
| `driver/` | ZLG CAN 驱动 DLL 集合，构建后通过 `POST_BUILD` 命令整体拷贝至 exe 同级目录 |

## 3. 架构与约定

### 3.1 分层静态库拆分
`src/CMakeLists.txt` 将源码按职责拆分为三个目标：
- `sin_core`：核心业务（CAN 设备、DBC、文件 I/O、过滤器、模拟器、录制/回放、日志、会话管理），仅依赖 Qt Core（通过预编译头限定最小头集），对外以 `PUBLIC include_directories` 暴露头路径。
- `sin_ui`：所有 UI 组件（MainWindow、各 Tab、Panel、工具视图），依赖 `sin_core` 和 `Qt6::Widgets`，额外私有链接 `qcustomplot`、`Qt6::Svg`。
- `sin`：可执行入口 `main.cpp` + `resources.qrc`，仅链接 `sin_ui`。

这种拆分使得修改 UI 代码时只重编译 `sin_ui` 和最终链接，不触发 core 层重新编译，提升增量构建速度。

### 3.2 预编译头（PCH）策略
两个静态库分别配置了精简的 `target_precompile_headers`：
- `sin_core`：仅包含 `<QObject>`、`<QString>`、`<QVector>`、`<QFile>` 等轻量 Qt 头，避免 Widget 头膨胀。
- `sin_ui`：包含完整 Widget 头列表（`QWidget`、`QMainWindow`、`QTabWidget`、`QDialog` 等）。

### 3.3 第三方依赖集成模式
- **头文件-only**：spdlog、nlohmann/json、concurrentqueue、pugixml 通过 `if(EXISTS ...)` 条件判断后直接 `target_include_directories` 暴露。
- **源码静态库**：qcustomplot 在 `third_party/Dependencies.cmake` 中以 `add_library(qcustomplot STATIC qcustomplot.cpp)` 形式编译，并传递依赖 `Qt6::Widgets`、`Qt6::PrintSupport`。
- **子目录构建**：vector_blf 若自带 `CMakeLists.txt` 则通过 `add_subdirectory` 纳入。
- **外部二进制**：ZLG 驱动 DLL（`zlgcan.dll`、`USBCANFD800U.dll` 等）作为预编译二进制随项目分发，通过 `POST_BUILD copy_directory` 复制到输出目录。

### 3.4 平台相关约定
- Windows GUI 程序：`WIN32_EXECUTABLE TRUE` 防止弹出控制台窗口。
- 链接器：注释明确“使用默认 ld.bfd 链接器（兼容性最好，无文件锁问题）”，禁用 LLD。
- 运行时依赖：`windeployqt` 部署 Qt 动态库，并手动补充 `Qt6PrintSupport.dll`（因为 qcustomplot 是静态库，`windeployqt` 无法识别其传递依赖）。
- 驱动 DLL：构建后把整个 `driver/` 目录拷贝到 `build/bin/`，确保 `zlgcan.dll` 与 `sin.exe` 同目录。

### 3.5 构建脚本约定
`scripts/build.py` 通过环境变量覆盖工具路径：`SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR`。构建前自动终止正在运行的 `sin.exe`（`taskkill /F /IM sin.exe`）以避免文件锁导致链接失败。支持 `-j` 并行编译，默认使用 CPU 核心数。

## 4. 约定与约束

- **C++ 标准固定为 C++17**：`CMAKE_CXX_STANDARD 17`，且 `REQUIRED ON`、`EXTENSIONS OFF`。
- **禁止 ccache**：脚本注释明确指出“与 MinGW g++ 13 的 PCH 不兼容，会静默崩溃”。
- **禁止 LLD 链接器**：Windows 上会导致文件锁问题，强制使用默认 `ld.bfd`。
- **Qt 组件显式声明**：仅请求 `Widgets`、`PrintSupport`、`Svg`，避免引入多余模块。
- **驱动 DLL 必须随包分发**：`install(DIRECTORY driver/ ... FILES_MATCHING PATTERN "*.dll")` 确保安装包包含全部 ZLG 驱动。
- **构建类型**：支持 Debug / Release / RelWithDebInfo / MinSizeRel，默认 Debug。
- **Ninja 优先**：检测到 `tools/ninja/ninja.exe` 即使用 Ninja 生成器，否则回退到 MinGW Makefiles 并显式设置 `CMAKE_MAKE_PROGRAM`。
- **资源文件集中管理**：所有图标、样式表放入 `resources/` 并通过 `resources.qrc` 注册，UI 通过 `:/` 路径引用。
- **版本号来源单一**：项目版本 `0.1.0` 仅在顶层 `project(sin VERSION 0.1.0)` 中声明，未在其他位置重复维护。

## 5. 缺失项说明

- 未发现 CI/CD 配置文件（如 GitHub Actions、Jenkinsfile、Azure Pipelines 等）。
- 未发现 Dockerfile 或容器化脚本。
- 未发现独立的发布打包脚本（`scripts/build.py` 仅提供 `deploy` 调用 `windeployqt`，不包含安装包制作）。
- 未发现跨平台交叉编译配置（当前脚本硬编码 Windows 路径与 `windeployqt`）。
