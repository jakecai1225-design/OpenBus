---
kind: build_system
name: 基于 CMake + MinGW/Ninja 的 Windows Qt6 桌面应用构建系统
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - scripts/build.py
    - third_party/Dependencies.cmake
    - .gitignore
---

## 1. 构建系统与工具链

项目采用 **CMake 3.21+** 作为核心构建系统，目标平台为 **Windows (MinGW)**，使用 **Qt6** 框架（Widgets、PrintSupport、Svg）。顶层 `CMakeLists.txt` 声明项目版本 `0.1.0`，强制启用 C++17 (`CMAKE_CXX_STANDARD 17`, `REQUIRED ON`, `EXTENSIONS OFF`)。

构建脚本位于 `scripts/build.py`，封装了 configure / build / run / debug / clean / rebuild / deploy / all / status / open 等子命令，统一入口。该脚本自动检测并优先使用本地 `tools/ninja/ninja.exe`（Ninja 生成器），回退到 `MinGW Makefiles`；通过环境变量 `SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR` 覆盖默认路径（默认指向 `C:/Qt/6.8.3/mingw_64`、`C:/Qt/Tools/mingw1310_64`、`C:/tools/cmake-3.30.3-windows-x86_64`）。

## 2. 关键文件与目录

- `CMakeLists.txt`：顶层工程定义、Qt6 查找、第三方依赖引入、输出目录 `build/bin`、安装规则。
- `src/CMakeLists.txt`：源文件分组（core/models/utils/ui）、静态库拆分、PCH 配置、可执行目标 `sin`、驱动 DLL 拷贝后处理。
- `scripts/build.py`：构建流程编排、环境校验、windeployqt 部署、进程锁释放。
- `third_party/Dependencies.cmake`：将 spdlog、nlohmann/json、qcustomplot、vector_blf 以 INTERFACE IMPORTED 或 add_subdirectory 方式集成。
- `driver/`：ZLG CAN 设备 SDK 预编译 DLL（zlgcan.dll 等），编译后通过 `POST_BUILD` 命令复制到 exe 同级目录。
- `resources/resources.qrc`：Qt 资源文件，由 `CMAKE_AUTORCC` 自动生成。
- `.gitignore`：忽略 `build/`、`tools/`、moc/qrc 中间产物、*.exe/*.dll 等。

## 3. 架构与约定

### 3.1 分层静态库拆分
`src/CMakeLists.txt` 将代码拆分为三个目标：
- `sin_core`（STATIC）：core + models + utils，暴露公共头（spdlog、nlohmann_json、concurrentqueue 的 include 路径通过 PUBLIC 传递）。
- `sin_ui`（STATIC）：ui 层，仅 PRIVATE 依赖 qcustomplot 和 Qt6::Svg，改 UI 不触发 core 重编译。
- `sin`（可执行）：链接 sin_ui，Windows 下设为 `WIN32_EXECUTABLE TRUE` 隐藏控制台。

### 3.2 预编译头（PCH）
两个库分别配置精简 PCH：`sin_core` 仅包含无 Widget 的 Qt 基础头（QObject/QString/QVariant 等），`sin_ui` 包含完整 QWidget 系列头，加速增量编译。

### 3.3 第三方依赖策略
- **头文件型**：spdlog、nlohmann/json 通过 `INTERFACE_INCLUDE_DIRECTORIES` 暴露。
- **源码型**：qcustomplot 直接 `add_library(qcustomplot STATIC ...)` 编译进项目。
- **子工程型**：vector_blf 通过 `add_subdirectory` 集成（需存在其自身 CMakeLists.txt）。
- **外部库**：BLF 解析需要 zlib，链接 MinGW 自带的 `libz.a`（`target_link_libraries(sin_core PRIVATE z)`）。

### 3.4 运行时依赖与部署
- 驱动 DLL：`POST_BUILD` 命令将整个 `driver/` 目录复制到 `$<TARGET_FILE_DIR:sin>`，确保 zlgcan.dll 与 exe 同目录。
- Qt 运行时：通过 `scripts/build.py deploy` 调用 `windeployqt.exe`，并手动补充 qcustomplot 静态库对 `Qt6PrintSupport.dll` 的传递依赖。
- 安装规则：顶层 `install(TARGETS sin RUNTIME DESTINATION bin)`，driver DLL 通过 `install(DIRECTORY ... FILES_MATCHING PATTERN "*.dll")` 一并打包。

### 3.5 构建类型与生成器
支持 Debug / Release / RelWithDebInfo / MinSizeRel。生成器选择逻辑：若 `tools/ninja/ninja.exe` 存在则用 Ninja，否则用 MinGW Makefiles。注释明确禁用 ccache（与 MinGW g++ 13 的 PCH 不兼容）和 LLD（Windows 上文件锁问题）。

## 4. 约定与约束

- **Qt 自动化开关**：`CMAKE_AUTOMOC`、`AUTOUIC`、`AUTORCC` 全部开启，无需手写 moc/uic/rcc 规则。
- **Qt 标准项目设置**：调用 `qt_standard_project_setup()` 启用 Qt 推荐的 CMake 行为。
- **输出目录集中**：所有产物输出至 `${CMAKE_BINARY_DIR}/bin`，便于定位。
- **驱动必须随包分发**：`driver/` 下的 DLL 是 ZLG USB 设备运行必需，构建脚本通过 POST_BUILD 自动复制，安装规则也包含这些 DLL。
- **构建前进程清理**：`cmd_build` 在每次编译前尝试 `taskkill /F /IM sin.exe` 并轮询释放文件锁，避免链接失败。
- **路径优先级**：构建脚本将 `cmake/bin`、`mingw/bin`、`qt/bin` 前置到 PATH，确保 cc1plus 能找到 libgcc/libstdc++/libwinpthread 等运行时 DLL。
- **无 CI/CD**：仓库未包含 GitHub Actions、GitLab CI、Jenkinsfile 等持续集成配置，构建完全依赖本地 `scripts/build.py`。
- **版本管理**：版本号集中在顶层 `project(... VERSION 0.1.0)`，由 CMake 管理，未见独立 version 文件或语义化发布脚本。