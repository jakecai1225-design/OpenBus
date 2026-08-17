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
    - src/main.cpp
    - resources/resources.qrc
---

## 1. 使用的系统与工具

- **构建系统**: CMake 3.21+，生成器优先使用本地 `tools/ninja/ninja.exe`（Ninja），回退到 MinGW Makefiles。
- **编译器**: MinGW g++/gcc（默认路径 `C:/Qt/Tools/mingw1310_64/bin`），通过环境变量 `SIN_MINGW_DIR`、`SIN_QT_DIR`、`SIN_CMAKE_DIR` 覆盖。
- **UI 框架**: Qt6（Widgets / PrintSupport / Svg），启用 `CMAKE_AUTOMOC/AUTOUIC/AUTORCC` 自动处理 MOC/UIC/RCC。
- **打包部署**: `windeployqt` 用于复制 Qt 运行时 DLL；驱动 DLL 通过 `add_custom_command(PRE_BUILD)` 复制到输出目录。
- **构建编排**: Python 脚本 `scripts/build.py` 封装 configure/build/run/debug/clean/rebuild/deploy/all/status/open 等子命令。
- **第三方依赖**: 以源码形式内嵌在 `third_party/`（spdlog、nlohmann_json、qcustomplot、vector_blf、pugixml、concurrentqueue、dbcppp、pugixml、spdlog）。

## 2. 关键文件

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层项目定义（版本 0.1.0）、Qt6 查找、包含 `third_party/Dependencies.cmake`、添加 `src/` 子目录 |
| `src/CMakeLists.txt` | 源文件分组（core/models/utils/ui）、构建三个目标：`openbus_core`（静态库）、`openbus_ui`（静态库）、`openbus`（可执行） |
| `third_party/Dependencies.cmake` | 声明 spdlog/qcustomplot/vector_blf 等第三方依赖的 CMake 接口 |
| `scripts/build.py` | 统一入口：检测 Ninja/MinGW、配置 CMake、增量编译、调用 windeployqt、启动 GDB |
| `driver/` | ZLG CAN 硬件驱动 DLL（zlgcan.dll 等），编译后拷贝至 exe 同级目录 |
| `resources/resources.qrc` | Qt 资源文件，被 RCC 自动嵌入 |
| `tools/ninja/` | 内置 Ninja 二进制，避免外部依赖 |

## 3. 架构与约定

### 3.1 分层静态库拆分
`src/CMakeLists.txt` 将代码拆为三层静态库：
- `openbus_core`：核心逻辑（CAN 设备、DBC、文件格式 I/O、插件、过滤器、录制/回放/模拟器、日志、会话管理），仅依赖精简 Qt 头（QObject/QString/QVariant 等），**不包含 QWidget**。
- `openbus_ui`：所有 UI 组件（MainWindow、TraceView、GraphicView、各 Tab、工具面板），链接 `openbus_core` + Qt Widgets + qcustomplot + Qt::Svg。
- `openbus`：可执行目标，仅链接 `openbus_ui`，入口 `src/main.cpp`。

这种拆分使得修改 UI 代码时只重编译 `openbus_ui`，不触发 core 层重编译，显著缩短迭代时间。

### 3.2 预编译头（PCH）策略
两个库分别配置了精简 PCH：
- `openbus_core`：仅包含非 Widget 的 Qt 头，加速核心层编译。
- `openbus_ui`：包含完整 QWidget 相关头列表。
注释明确说明不使用 ccache（与 MinGW g++ 13 的 PCH 不兼容，会静默崩溃）。

### 3.3 第三方依赖管理
- 单头文件库（spdlog、nlohmann_json、concurrentqueue、pugixml）通过 `target_include_directories` 暴露。
- 源码库（qcustomplot）直接 `add_library(STATIC ...)` 编译进工程。
- vector_blf 通过 `add_subdirectory` 引入其自身 CMakeLists。
- 条件检查 `if(EXISTS ...)` 保证部分依赖缺失时仍可配置。

### 3.4 平台特定处理
- Windows GUI 模式：`set_target_properties(openbus PROPERTIES WIN32_EXECUTABLE TRUE)`，不弹出控制台。
- Windows 驱动拷贝：`POST_BUILD` 阶段将整个 `driver/` 目录复制到 `$<TARGET_FILE_DIR:openbus>`，确保 zlgcan.dll 与 exe 同目录。
- 链接 psapi（Windows 进程监控）。

### 3.5 构建流程（Python 脚本）
`scripts/build.py` 提供统一入口：
- 自动检测 Ninja（`tools/ninja/ninja.exe`），不存在则回退 MinGW Makefiles。
- 自动设置 PATH（cmake/mingw/qt/bin），避免 cc1plus 找不到 libstdc++ 等 DLL。
- 编译前自动 `taskkill /F openbus.exe` 释放文件锁。
- 支持 `--qt-dir`、`--mingw-dir`、`--cmake-dir` 覆盖默认路径。
- `deploy` 子命令调用 `windeployqt`，并手动补充 Qt6PrintSupport.dll（qcustomplot 静态库传递依赖未被检测到）。

## 4. 约定与约束

- **C++ 标准**：强制 C++17，关闭扩展（`CMAKE_CXX_STANDARD_REQUIRED ON`，`CMAKE_CXX_EXTENSIONS OFF`）。
- **构建类型**：支持 Debug / Release / RelWithDebInfo / MinSizeRel，默认 Debug。
- **输出目录**：可执行文件统一输出到 `${CMAKE_BINARY_DIR}/bin`。
- **禁止项**：不使用 ccache（与 PCH 不兼容）、不使用 LLD 链接器（Windows 上导致文件锁问题），显式使用默认 ld.bfd。
- **Qt 自动化**：MOC/UIC/RCC 全部由 CMake 自动处理，无需手动注册 .ui/.qrc。
- **安装规则**：`install(TARGETS openbus RUNTIME DESTINATION bin)`，驱动 DLL 通过 `install(DIRECTORY ... FILES_MATCHING PATTERN "*.dll")` 一并安装。
- **环境约定**：默认工具路径硬编码为 `C:/Qt/6.8.3/mingw_64`、`C:/Qt/Tools/mingw1310_64`、`C:/tools/cmake-3.30.3-windows-x86_64`，可通过环境变量或命令行参数覆盖。
- **Ninja 优先**：项目自带 `tools/ninja/ninja.exe`，优先使用 Ninja 生成器以获得更快的增量编译速度。
- **驱动依赖**：ZLG SDK 的 `zlgcan.dll` 必须与 exe 在同一目录才能找到 USB 驱动，因此构建后自动拷贝整个 `driver/` 目录。
- **测试数据**：`test/` 目录存放 BLF/ASC/DBC 等测试用例，不参与构建。

当前仓库未包含 CI/CD 配置文件（无 GitHub Actions/GitLab CI/Jenkinsfile），发布流程依赖本地 `scripts/build.py all` 完成配置→编译→部署→运行。