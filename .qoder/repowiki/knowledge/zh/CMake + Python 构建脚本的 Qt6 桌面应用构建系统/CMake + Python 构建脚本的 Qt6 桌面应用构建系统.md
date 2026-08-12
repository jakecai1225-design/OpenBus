---
kind: build_system
name: CMake + Python 构建脚本的 Qt6 桌面应用构建系统
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - third_party/Dependencies.cmake
    - scripts/build.py
    - resources/resources.qrc
---

## 1. 构建系统与工具链

项目采用 **CMake 3.21+** 作为核心构建系统，配合自研的 **Python 构建脚本** (`scripts/build.py`) 封装配置、编译、部署、调试等常用流程。目标平台为 **Windows (MinGW)**，使用 **Qt6** (Widgets/PrintSupport/Svg) 开发桌面 GUI 应用。

- C++ 标准固定为 **C++17**，关闭扩展 (`CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`)。
- 编译器通过环境变量 `SIN_QT_DIR` / `SIN_MINGW_DIR` / `SIN_CMAKE_DIR` 或默认路径定位：`C:/Qt/6.8.3/mingw_64`、`C:/Qt/Tools/mingw1310_64`、`C:/tools/cmake-3.30.3-windows-x86_64`。
- 生成器优先使用内置的 **Ninja** (`tools/ninja/ninja.exe`)，未检测到时回退到 **MinGW Makefiles**；并行任务数默认等于 CPU 核心数（`-j`）。
- 明确禁用 ccache（与 MinGW g++ 13 的 PCH 不兼容，会静默崩溃）和 LLD 链接器（Windows 上导致文件锁问题），强制使用默认 `ld.bfd` 链接器。

## 2. 关键构建文件

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层工程定义，版本 `0.1.0`，查找 Qt6，包含第三方依赖与子目录 |
| `src/CMakeLists.txt` | 源文件组织、库拆分、PCH、安装规则、驱动 DLL 拷贝 |
| `third_party/Dependencies.cmake` | 第三方头文件/静态库集成（spdlog、qcustomplot、vector_blf） |
| `scripts/build.py` | 统一入口：configure/build/run/debug/clean/rebuild/deploy/all/status/open |
| `resources/resources.qrc` | Qt 资源文件，随可执行文件打包 |

## 3. 架构与约定

### 3.1 分层静态库拆分
`src/CMakeLists.txt` 将源码按职责拆分为三个目标，实现 UI 改动不影响 core 重编译：
- `sin_core`（STATIC）：core + models + utils，对外暴露公共头路径（spdlog/json/concurrentqueue/pugixml）。
- `sin_ui`（STATIC）：ui 层组件，仅 PRIVATE 依赖 qcustomplot 和 Qt6::Svg。
- `sin`（可执行）：由 `main.cpp` 启动，仅链接 `sin_ui`。

### 3.2 预编译头 (PCH)
通过 `target_precompile_headers` 为两个库分别维护精简头列表：`sin_core` 仅包含无 Widget 的 Qt 基础类型，`sin_ui` 包含完整 Widget 头，显著缩短增量编译时间。

### 3.3 第三方依赖管理
- 纯头文件库（spdlog、nlohmann_json、concurrentqueue、pugixml）以 `INTERFACE IMPORTED` 或 `add_subdirectory` 方式引入，通过 `if(EXISTS ...)` 条件判断是否启用。
- 源码型静态库（qcustomplot）直接编译进 `qcustomplot` 目标，并 PUBLIC 链接 Qt6::Widgets/PrintSupport。
- Vector BLF 解析库 (`vector_blf`) 仅在存在其 CMakeLists.txt 时加入。

### 3.4 运行时依赖处理
- Windows 下通过 `POST_BUILD` 命令将整个 `driver/` 目录（ZLG SDK 的 `zlgcan.dll` 等驱动 DLL）复制到 exe 同级目录，确保 USB 设备驱动能被找到。
- 部署阶段调用 `windeployqt` 自动复制 Qt 运行时，并手动补充 `Qt6PrintSupport.dll`（qcustomplot 静态库对 PrintSupport 的传递依赖未被 windeployqt 检测）。
- 输出目录统一为 `${CMAKE_BINARY_DIR}/bin`。

### 3.5 构建脚本工作流
`scripts/build.py` 提供子命令：
- `configure`：检查工具链 → 选择 Ninja/MinGW Makefiles → 设置 `-DCMAKE_PREFIX_PATH`、编译器、构建类型。
- `build`：若 `build/CMakeCache.txt` 不存在则自动 configure；编译前用 `taskkill /F /IM sin.exe` 终止运行中的程序避免文件锁。
- `run` / `debug`：自动触发 build，支持传参给 GDB 或程序。
- `deploy`：调用 `windeployqt` + 手动补 DLL。
- `all`：一键完成 configure → build → deploy → run。
- `status` / `open`：查看环境与打开输出目录。

## 4. 约定与约束

- **Qt 自动化**：开启 `AUTOMOC` / `AUTOUIC` / `AUTORCC`，`.qrc` 资源文件在构建时被处理。
- **GUI 模式**：Windows 下通过 `WIN32_EXECUTABLE TRUE` 属性隐藏控制台窗口。
- **安装规则**：顶层 `install(TARGETS sin RUNTIME DESTINATION bin)`，驱动 DLL 通过 `install(DIRECTORY ... FILES_MATCHING PATTERN "*.dll")` 一并安装。
- **构建类型**：支持 Debug / Release / RelWithDebInfo / MinSizeRel，默认 Debug。
- **环境隔离**：所有工具路径通过 `PATH` 前置注入，避免依赖全局环境变量。
- **无 CI/Docker**：仓库中未发现 GitHub Actions、Dockerfile 或其他持续集成配置；构建完全依赖本地 MinGW + Qt6 环境。
- **版本策略**：版本号硬编码于顶层 `CMakeLists.txt` 的 `project(... VERSION 0.1.0)`，未见自动化版本递增机制。