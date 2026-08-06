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
    - resources/resources.qrc
---

## 1. 使用的系统与工具
- **构建系统**: CMake 3.21+，采用单工程（single-project）结构，顶层 `CMakeLists.txt` 管理项目元信息，`src/CMakeLists.txt` 组织源码与目标。
- **编译器链**: MinGW (g++/gcc)，通过环境变量或命令行参数指定路径，默认指向 `D:/Qt/Tools/mingw1310_64`。
- **GUI 框架**: Qt6（Widgets、Charts），启用 AUTOMOC/AUTOUIC/AUTORCC 自动处理信号槽、UI 文件和资源。
- **构建编排**: Python 脚本 `scripts/build.py` 封装 configure/build/run/debug/clean/rebuild/deploy/all/status/open 等子命令，统一入口。
- **部署工具**: `windeployqt` 打包 Qt 运行时依赖。
- **调试器**: GDB（可选）。

## 2. 关键文件与包
- `CMakeLists.txt` — 项目定义、Qt6 查找、子目录引入、安装规则。
- `src/CMakeLists.txt` — 源文件分组（core/models/ui/utils）、可执行目标 `sin`、链接库、包含路径、Windows GUI 属性、安装规则。
- `scripts/build.py` — 构建脚本，负责环境检测、CMake 配置、增量编译、运行、GDB 调试、清理、重新构建、windeployqt 部署、状态查看与打开输出目录。
- `resources/resources.qrc` — Qt 资源文件，被 RCC 自动处理。
- `build/` — 外部构建目录（out-of-source build），由 CMake 生成。

## 3. 架构与约定
- **分层源码组织**: `src/core`（CAN 帧、录制、回放、仿真、DBC）、`src/models`（Qt 数据模型）、`src/ui`（界面组件）、`src/utils`（工具函数），每层在 `src/CMakeLists.txt` 中以变量分组声明。
- **单可执行目标**: 所有源码最终链接为单一 `sin.exe`，无静态/动态库拆分。
- **Qt 自动化**: 通过 `CMAKE_AUTOMOC/AUTOUIC/AUTORCC ON` 和 `qt_standard_project_setup()` 启用 Qt 标准项目设置，MOC/UIC/RCC 自动生成。
- **输出目录**: 可执行文件输出到 `${CMAKE_BINARY_DIR}/bin`，即 `build/bin/sin.exe`。
- **平台特定**: Windows 下通过 `WIN32_EXECUTABLE TRUE` 隐藏控制台窗口。
- **安装规则**: 顶层与 `src/CMakeLists.txt` 均定义了 `install(TARGETS sin RUNTIME DESTINATION bin)`，但实际发布流程由 `windeployqt` 接管。

## 4. 约定与约束
- **C++ 标准**: 强制 C++17，禁止扩展（`CMAKE_CXX_STANDARD 17`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`）。
- **构建类型**: 支持 Debug / Release / RelWithDebInfo / MinSizeRel，默认 Debug。
- **并行编译**: 构建脚本默认使用 CPU 核心数作为 `-j` 参数，可通过 `--jobs` 覆盖。
- **环境发现**: 工具路径优先从环境变量 `SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR` 读取，否则回退到硬编码默认值。
- **增量构建**: `build` 命令若检测到 `CMakeCache.txt` 不存在则自动先执行 `configure`；后续仅编译变更文件。
- **自动依赖部署**: `deploy` 子命令调用 `windeployqt` 将 Qt 运行时 DLL 复制到可执行文件同级目录。
- **完整流水线**: `all` 子命令串联 configure → build → deploy → run，提供一键构建运行体验。
- **清理策略**: `clean` 直接删除整个 `build/` 目录；`rebuild` 先 clean 再 configure + build。
- **Qt 版本要求**: `find_package(Qt6 REQUIRED COMPONENTS Widgets Charts)` 表明必须安装 Qt6 且至少包含 Widgets 与 Charts 模块。
- **MinGW Makefiles 生成器**: 固定使用 `-G "MinGW Makefiles"`，不兼容 MSVC 或其他生成器。
- **资源文件**: 通过 `../resources/resources.qrc` 引入，需确保路径相对 `src/CMakeLists.txt` 正确。
