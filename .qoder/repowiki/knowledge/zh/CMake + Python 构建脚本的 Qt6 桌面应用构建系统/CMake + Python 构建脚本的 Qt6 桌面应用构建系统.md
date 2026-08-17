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

## 1. 使用的系统与工具

- **构建系统**: CMake (最低版本 3.21)，顶层 `CMakeLists.txt` 定义项目元信息，`src/CMakeLists.txt` 组织源码与目标。
- **生成器**: 优先使用本地内置的 Ninja (`tools/ninja/ninja.exe`)，回退到 MinGW Makefiles；由 `scripts/build.py` 自动检测并选择。
- **编译器**: MinGW g++/gcc（默认路径 `C:/Qt/Tools/mingw1310_64/bin`），通过环境变量 `SIN_MINGW_DIR`、`SIN_QT_DIR`、`SIN_CMAKE_DIR` 覆盖。
- **打包部署**: `windeployqt` 用于复制 Qt 运行时依赖；驱动 DLL 通过 CMake `POST_BUILD` 命令复制到输出目录。
- **辅助脚本**: `scripts/build.py` 提供 `configure / build / run / debug / clean / rebuild / deploy / all / status / open` 子命令，封装环境检查、Ninja/MinGW 切换、并行编译、进程锁释放等流程。

## 2. 关键文件

- `CMakeLists.txt`：项目根配置，声明 Qt6 组件（Widgets、PrintSupport、Svg）、启用 AUTOMOC/AUTOUIC/AUTORCC、包含第三方依赖、添加 `src` 子目录。
- `src/CMakeLists.txt`：核心构建逻辑——定义 `openbus_core`（静态库）、`openbus_ui`（静态库）、`openbus`（可执行）三个目标，管理 PCH、链接库、Windows GUI 属性、驱动 DLL 拷贝与安装规则。
- `third_party/Dependencies.cmake`：集中声明第三方依赖（spdlog 接口库、qcustomplot 静态库、vector_blf 子项目、nlohmann_json/pugixml 头文件）。
- `scripts/build.py`：统一入口脚本，管理工具链路径、生成器选择、增量构建、部署、调试。
- `resources/resources.qrc`：Qt 资源文件，随 `qt_add_executable` 一并编译进二进制。
- `driver/`：ZLG 等硬件驱动 DLL 及配置文件，构建后自动复制到 exe 同级目录。

## 3. 架构与约定

### 3.1 分层静态库拆分
`src/CMakeLists.txt` 将源码按层拆分为三个目标：
- `openbus_core`：core/models/utils 三层，仅依赖轻量 Qt 头（QObject/QString/QVariant 等），通过 `target_precompile_headers` 预编译加速。
- `openbus_ui`：ui 层，依赖 `openbus_core` 和完整 Qt Widgets，同样配置了 PCH。
- `openbus`：可执行入口，仅链接 `openbus_ui`。

这种拆分使得修改 UI 代码时只重编译 `openbus_ui` + 最终链接，不触发 core 重编译，显著缩短增量构建时间。

### 3.2 第三方依赖管理
- 头文件型库（spdlog、nlohmann_json、pugixml、concurrentqueue）以源码形式放在 `third_party/`，通过 `target_include_directories(... PUBLIC ...)` 暴露给使用者。
- 源码型库（qcustomplot）直接 `add_library(STATIC ...)` 编译进工程。
- vector_blf 若存在则作为子项目 `add_subdirectory` 引入，并通过 `HAS_VECTOR_BLF` 宏条件编译 BLF 读写分支。
- zlib 通过 MinGW 自带 `libz.a` 链接（BLF 解压需要）。

### 3.3 构建产物与安装
- 输出目录固定为 `${CMAKE_BINARY_DIR}/bin`。
- Windows 下设置 `WIN32_EXECUTABLE TRUE` 隐藏控制台窗口。
- `POST_BUILD` 阶段将整个 `driver/` 目录复制到 exe 输出目录，确保 ZLG SDK 的 `zlgcan.dll` 等驱动 DLL 与程序同目录。
- `install(TARGETS openbus RUNTIME DESTINATION bin)` 配合 `install(DIRECTORY driver/ FILES_MATCHING PATTERN "*.dll")` 支持 `make install` 打包。

### 3.4 构建优化约定
- 启用 C++17 且禁止扩展（`CMAKE_CXX_STANDARD 17`, `CMAKE_CXX_EXTENSIONS OFF`）。
- 启用 Qt 自动化（AUTOMOC/AUTOUIC/AUTORCC）。
- 显式禁用 ccache（注释说明与 MinGW g++ 13 的 PCH 不兼容，会静默崩溃）。
- 显式禁用 LLD（Windows 上会导致文件锁问题），使用默认 `ld.bfd` 链接器。
- 使用 `target_precompile_headers` 为 core 和 ui 分别配置精简/完整的 Qt 头列表，加速编译。

## 4. 约定与约束

| 约定/约束 | 来源/证据 |
|---|---|
| 必须通过 `scripts/build.py` 或 CMake 配置，Qt 路径通过 `--qt-dir` 或 `SIN_QT_DIR` 指定 | `scripts/build.py` 中 `Environment` 类与命令行参数 |
| 构建类型限定为 Debug / Release / RelWithDebInfo / MinSizeRel 四种 | `BUILD_TYPES` 列表限制 `--build-type` 选项 |
| 构建目录固定为根级 `build/` | `BUILD_DIR = PROJECT_ROOT / "build"` |
| 新增源文件需手动加入 `src/CMakeLists.txt` 对应 `SRC_CORE/SRC_MODELS/SRC_UI` 列表 | 所有源文件均以显式列表方式注册 |
| 新增第三方头文件库需在 `third_party/Dependencies.cmake` 中声明 `INTERFACE_INCLUDE_DIRECTORIES` | 现有 spdlog/nlohmann_json/pugixml 均遵循此模式 |
| 新增 Qt 模块需在顶层 `find_package(Qt6 REQUIRED COMPONENTS ...)` 中添加 | 当前已声明 Widgets/PrintSupport/Svg |
| 新增驱动 DLL 应放入 `driver/`，构建时自动复制 | `POST_BUILD copy_directory` 规则 |
| 不得启用 ccache 或 LLD 链接器 | 多处注释明确禁止原因 |
| 增量构建前会自动终止正在运行的 `openbus.exe` 避免文件锁 | `kill_running_executable()` 在 `cmd_build`/`cmd_run`/`cmd_debug` 中调用 |
| 部署时需手动补充 `Qt6PrintSupport.dll`（qcustomplot 静态库传递依赖未被 windeployqt 识别） | `cmd_deploy` 中的特殊处理逻辑 |

## 5. CI / 发布

仓库中未发现 GitHub Actions、GitLab CI、Jenkinsfile 等 CI 配置文件。发布流程目前依赖本地 `scripts/build.py deploy` 调用 `windeployqt` 完成 Qt 运行时打包，再结合 CMake `install` 规则导出驱动 DLL。