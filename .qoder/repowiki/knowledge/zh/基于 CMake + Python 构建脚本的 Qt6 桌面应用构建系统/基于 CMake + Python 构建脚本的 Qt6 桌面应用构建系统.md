---
kind: build_system
name: 基于 CMake + Python 构建脚本的 Qt6 桌面应用构建系统
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

项目采用 **CMake 3.21+** 作为核心构建系统，配合自研的 **Python 构建脚本** (`scripts/build.py`) 封装配置、编译、部署、调试等流程。目标平台为 **Windows (MinGW)**，使用 MinGW g++/gcc 13 与 Qt6.8.3 工具链。

- **生成器选择**：优先检测 `tools/ninja/ninja.exe`（本地内置），使用 Ninja 生成器；未找到时回退到 MinGW Makefiles，并通过 `-DCMAKE_MAKE_PROGRAM` 指定 make 路径。
- **Qt 自动化**：启用 `CMAKE_AUTOMOC/AUTOUIC/AUTORCC`，通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg)` 查找 Qt6，并使用 `qt_standard_project_setup()`。
- **C++ 标准**：强制 C++17，关闭扩展，要求严格模式。
- **链接器约束**：注释明确禁止使用 ccache（与 MinGW g++ 13 PCH 不兼容）、LLD（Windows 文件锁问题），默认使用 `ld.bfd` 以保证兼容性。

## 2. 关键构建文件

| 文件 | 职责 |
|---|---|
| `CMakeLists.txt` | 顶层工程定义、Qt6 查找、第三方依赖引入、子目录组织 |
| `src/CMakeLists.txt` | 源文件分组、静态库拆分、PCH 配置、可执行目标、安装规则、驱动 DLL 拷贝 |
| `third_party/Dependencies.cmake` | 第三方库声明（spdlog 接口库、qcustomplot 静态库、vector_blf 子目录） |
| `scripts/build.py` | 构建编排脚本：环境检测、CMake 配置、增量编译、windeployqt 部署、GDB 调试 |
| `resources/resources.qrc` | Qt 资源文件（图标、样式表） |

## 3. 架构与约定

### 3.1 分层静态库拆分
`src/CMakeLists.txt` 将源码按层次拆分为三个目标，实现修改隔离与增量编译优化：
- `sin_core`（STATIC）：core/models/utils 层，仅依赖 Qt 基础模块和 zlib，**不包含 Widget 头**，因此 UI 改动不会触发 core 重编译。
- `sin_ui`（STATIC）：ui 层，依赖 `sin_core` 和 `Qt6::Widgets`、`Qt6::Svg`、`qcustomplot`。
- `sin`（EXECUTABLE）：入口 `main.cpp` + `resources.qrc`，仅链接 `sin_ui`。

### 3.2 预编译头（PCH）策略
两个静态库分别配置精简的 `target_precompile_headers`：
- `sin_core`：仅包含 `<QObject>/<QString>/<QVariant>` 等轻量 Qt 头，避免 Widget 开销。
- `sin_ui`：包含完整 Widget 头列表（`QWidget/QMainWindow/QTabWidget` 等）。

### 3.3 第三方依赖管理
- **源码内嵌**：`third_party/` 下直接存放 spdlog、nlohmann_json、concurrentqueue、qcustomplot、vector_blf、dbcppp 等源码或单头文件。
- **条件构建**：通过 `if(EXISTS ...)` 判断依赖是否存在再添加目标，使项目可在无第三方源码时仍被 CMake 识别。
- **zlib**：BLF 解析需要 zlib，MinGW 自带 `libz.a`，通过 `target_link_libraries(sin_core PRIVATE z)` 链接。

### 3.4 驱动 DLL 分发
`driver/` 目录存放 ZLG SDK 的 `zlgcan.dll` 及各类设备 DLL。构建后通过 `add_custom_command(TARGET sin POST_BUILD)` 将整个 `driver/` 目录复制到输出目录（exe 同级），并定义 `install(DIRECTORY ... FILES_MATCHING PATTERN "*.dll")` 供打包使用。

### 3.5 构建脚本能力
`scripts/build.py` 提供统一入口：
- `configure`：自动检测 Ninja/MinGW，设置 `CMAKE_PREFIX_PATH`、编译器路径、构建类型。
- `build`：增量编译，首次运行自动 configure，支持 `-j` 并行。
- `run/debug`：自动终止已运行的 `sin.exe`（避免 Windows 文件锁），启动程序或 GDB。
- `deploy`：调用 `windeployqt` 打包 Qt 运行时，并手动补充 `Qt6PrintSupport.dll`（qcustomplot 静态库传递依赖未被 windeployqt 发现）。
- `clean/rebuild/all/status/open`：完整生命周期管理。

## 4. 约定与约束

- **输出目录**：所有产物位于 `build/bin/`，由 `CMAKE_RUNTIME_OUTPUT_DIRECTORY` 控制。
- **Windows GUI 模式**：通过 `WIN32_EXECUTABLE TRUE` 属性隐藏控制台窗口。
- **环境变量覆盖工具路径**：`SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR` 可覆盖默认路径。
- **禁止使用的工具**：明确禁用 ccache（PCH 崩溃）、LLD（文件锁冲突），这些是硬性约束而非建议。
- **版本信息**：在顶层 `CMakeLists.txt` 中通过 `project(... VERSION 0.1.0)` 声明版本号。
- **安装规则**：顶层 `install(TARGETS sin RUNTIME DESTINATION bin)`，驱动 DLL 通过 `FILES_MATCHING *.dll` 安装。
- **构建类型**：支持 Debug / Release / RelWithDebInfo / MinSizeRel，默认 Debug。

## 5. 缺失项

仓库中未发现 CI/CD 配置文件（如 GitHub Actions `.github/workflows/`、Jenkinsfile、Azure Pipelines 等），也没有 Dockerfile 或容器化脚本。构建完全依赖本地 `scripts/build.py` 脚本与手工环境配置。