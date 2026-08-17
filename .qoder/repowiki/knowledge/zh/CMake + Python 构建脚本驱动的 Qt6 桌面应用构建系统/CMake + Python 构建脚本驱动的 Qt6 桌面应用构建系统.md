---
kind: build_system
name: CMake + Python 构建脚本驱动的 Qt6 桌面应用构建系统
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - third_party/Dependencies.cmake
    - scripts/build.py
    - src/main.cpp
    - resources/resources.qrc
---

## 1. 使用的系统与工具

项目采用 **CMake 3.21+** 作为核心构建系统，配合一个自研的 **Python 包装脚本 `scripts/build.py`** 提供统一的开发体验。目标平台为 **Windows (MinGW)**，使用 **Qt6**（Widgets、PrintSupport、Svg）构建桌面应用程序。

- 生成器：优先检测本地 `tools/ninja/ninja.exe`，存在则使用 **Ninja** 生成器；否则回退到 **MinGW Makefiles**。
- 编译器：通过环境变量 `SIN_MINGW_DIR` 或默认路径 `C:/Qt/Tools/mingw1310_64` 指定 MinGW g++/gcc。
- CMake 路径：通过 `SIN_CMAKE_DIR` 或默认 `C:/tools/cmake-3.30.3-windows-x86_64` 指定。
- Qt 路径：通过 `SIN_QT_DIR` 或默认 `C:/Qt/6.8.3/mingw_64` 指定。
- 部署：使用 Qt 提供的 `windeployqt.exe` 打包运行时依赖，并手动补充 `Qt6PrintSupport.dll`（qcustomplot 静态库的传递依赖）。
- 调试：内置 GDB 支持，自动启动已编译的可执行文件。

## 2. 关键文件

- `CMakeLists.txt`（根）：定义项目元信息（VERSION 0.1.0）、C++17 标准、Qt6 查找与自动化处理（AUTOMOC/AUTOUIC/AUTORCC）、输出目录 `build/bin`、包含第三方依赖配置、添加 `src` 子目录。
- `src/CMakeLists.txt`：核心构建逻辑，将源码划分为四个层次并分别构建为静态库/可执行文件。
- `third_party/Dependencies.cmake`：声明第三方依赖（spdlog 接口库、nlohmann/json 头文件、qcustomplot 静态库、vector_blf 子模块）。
- `scripts/build.py`：统一入口脚本，封装 configure/build/run/debug/deploy/rebuild/status/open 等命令。
- `src/main.cpp`：程序入口，由 `qt_add_executable(sin ...)` 构建。
- `resources/resources.qrc`：Qt 资源文件，被 CMake 的 AUTORCC 处理。

## 3. 架构与约定

### 3.1 分层静态库拆分
`src/CMakeLists.txt` 将源码按职责拆分为四层，并通过静态库隔离编译依赖，显著减少增量编译时间：
- `sin_core`（STATIC）：核心业务层（core/）、数据模型（models/）、工具（utils/），链接 Qt6::Widgets、zlib、可选 vector_blf/pugixml。
- `sin_ui`（STATIC）：UI 层（ui/），仅依赖 `sin_core` 和 Qt6::Widgets/Svg/qcustomplot。
- `sin`（EXECUTABLE）：最终可执行文件，仅链接 `sin_ui`。

这种设计使修改 UI 代码时不会触发 core 层的重新编译。

### 3.2 预编译头（PCH）优化
两个静态库分别配置了精简的 `target_precompile_headers`：
- `sin_core`：仅包含无 Widget 的轻量 Qt 头（QObject、QString、QVariant、QList、QTimer、QThread、QFile 等）。
- `sin_ui`：包含完整的 Qt Widget 头列表（QWidget、QMainWindow、QMenu、QDialog 等）。

### 3.3 第三方依赖管理
所有第三方库以源码形式存放在 `third_party/` 目录下：
- 纯头文件库：spdlog、nlohmann/json、concurrentqueue、pugixml —— 通过 `INTERFACE_INCLUDE_DIRECTORIES` 暴露。
- 源码库：qcustomplot —— 直接 `add_library(qcustomplot STATIC ...)` 编译进工程。
- 子模块：vector_blf —— 通过 `add_subdirectory` 引入其自带 CMakeLists。
- 驱动 DLL：`driver/` 目录下的 ZLG CAN SDK 二进制 DLL（zlgcan.dll、USBCANFD800U.dll 等）在 POST_BUILD 阶段复制到输出目录。

### 3.4 构建产物布局
- 构建目录：`build/`（out-of-source 构建）
- 可执行文件：`build/bin/sin.exe`
- 驱动 DLL：编译后自动拷贝至 `build/bin/driver/`（与 exe 同级）
- 安装规则：`install(TARGETS sin RUNTIME DESTINATION bin)`，驱动 DLL 也随 install 安装。

### 3.5 构建脚本约定
`scripts/build.py` 提供以下子命令：
- `configure [--build-type Debug|Release|RelWithDebInfo|MinSizeRel] [--clean]`：CMake 配置
- `build [-j N] [--target TARGET]`：增量编译（首次自动 configure）
- `run [--args "..."]`：运行程序（自动 build，若不存在）
- `debug [--args "..."]`：GDB 调试
- `clean`：删除 `build/` 目录
- `rebuild [--build-type ...]`：clean + configure + build
- `deploy`：调用 windeployqt 并手动复制 Qt6PrintSupport.dll
- `all [--build-type ...] [--args "..."]`：完整流程（configure → build → deploy → run）
- `status`：显示环境与构建状态
- `open`：打开构建输出目录

## 4. 约定与约束

- **C++ 标准**：强制 C++17（`CMAKE_CXX_STANDARD 17`，`REQUIRED ON`，关闭扩展）。
- **Qt 自动化**：启用 AUTOMOC/AUTOUIC/AUTORCC，无需手写 moc/uic/rcc 命令。
- **链接器选择**：显式注释不使用 ccache（与 MinGW g++ 13 的 PCH 不兼容，会静默崩溃）和不使用 LLD（Windows 上导致文件锁问题），使用默认 ld.bfd。
- **GUI 模式**：Windows 下设置 `WIN32_EXECUTABLE TRUE`，不弹出控制台窗口。
- **并行构建**：默认使用 CPU 核心数 `-j` 并行编译，Ninja 和 MinGW Makefiles 均支持。
- **进程保护**：build/run/debug 前自动调用 `taskkill /F /IM sin.exe` 终止正在运行的实例，避免文件锁导致链接失败。
- **PATH 注入**：构建脚本在子进程执行前将 cmake/mingw/qt/bin 注入 PATH，确保 cc1plus.exe 能找到 libstdc++/libwinpthread 等依赖。
- **版本策略**：项目版本在根 `CMakeLists.txt` 中定义为 `VERSION 0.1.0`，未看到 CI/发布流水线中的版本号自动递增机制。
- **无 CI/CD**：仓库中未发现 GitHub Actions、GitLab CI、Jenkins 等持续集成配置文件，构建主要面向本地开发者。
- **无 Docker**：仓库中无 Dockerfile 或容器化配置。
- **插件系统**：`plugins/` 目录包含 Python 插件（main.py + plugin.json），但未见构建脚本中对插件的打包/分发逻辑，推测为运行时动态加载。
- **资源文件**：图标、样式表等资源通过 Qt `.qrc` 资源文件集中管理，由 AUTORCC 处理。