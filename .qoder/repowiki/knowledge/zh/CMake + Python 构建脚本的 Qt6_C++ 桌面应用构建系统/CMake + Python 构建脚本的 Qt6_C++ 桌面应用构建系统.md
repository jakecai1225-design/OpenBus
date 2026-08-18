---
kind: build_system
name: CMake + Python 构建脚本的 Qt6/C++ 桌面应用构建系统
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

## 1. 构建系统与工具链

项目采用 **CMake 3.21+** 作为核心构建系统，配合自研的 **Python 构建脚本 `scripts/build.py`** 封装配置、编译、部署、调试等常用流程。目标平台为 **Windows (MinGW)**，使用 C++17 标准与 **Qt6 (Widgets/PrintSupport/Svg)**。

- 编译器：MinGW g++/gcc（默认路径通过环境变量 `SIN_MINGW_DIR` 或 `C:/Qt/Tools/mingw1310_64` 指定）
- 构建器：优先使用本地 `tools/ninja/ninja.exe`（Ninja 生成器），回退到 MinGW Makefiles
- 资源打包：Qt RCC (`CMAKE_AUTORCC ON`) 将 `resources/resources.qrc` 内嵌进可执行文件
- 自动化处理：MOC/UIC/RCC 全部启用 (`CMAKE_AUTOMOC/AUTOUIC/AUTORCC`)

## 2. 关键文件与目录

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层工程定义、Qt6 查找、子目录引入、安装规则 |
| `src/CMakeLists.txt` | 源文件组织、静态库拆分、PCH、驱动 DLL 拷贝 |
| `third_party/Dependencies.cmake` | 第三方头文件/静态库声明 (spdlog, qcustomplot, vector_blf) |
| `scripts/build.py` | 统一入口：configure/build/run/debug/clean/rebuild/deploy/all/status/open |
| `driver/` | ZLG CAN 驱动 DLL 及 XML 设备描述，编译后复制到 exe 同级目录 |
| `plugins/` | Python 插件（独立于 CMake 构建，由运行时加载） |
| `third_party/` | 源码级第三方依赖 (spdlog, nlohmann_json, pugixml, concurrentqueue, qcustomplot, vector_blf) |

## 3. 架构与约定

### 3.1 分层静态库设计
`src/CMakeLists.txt` 将代码拆分为三个目标，实现 UI 变更不触发 core 重编译：
- `openbus_core`：核心逻辑（CAN 设备、DBC、文件 I/O、插件框架、模拟器、录制/回放）+ models + utils
- `openbus_ui`：所有 Qt 界面组件，仅链接 `openbus_core`
- `openbus`：可执行文件，链接 `openbus_ui`

### 3.2 预编译头 (PCH)
两个层分别定义精简 PCH 列表：core 层仅包含非 Widget 的 Qt 基础头；ui 层包含完整 Widget 头。这显著缩短增量编译时间。

### 3.3 第三方依赖管理
- 纯头文件库（spdlog、nlohmann_json、pugixml、concurrentqueue）通过 `INTERFACE IMPORTED` 或直接 include path 暴露
- 源码库（qcustomplot）以 `STATIC` 目标编译并链接 Qt6::Widgets/PrintSupport
- `vector_blf` 可选：若存在其 `CMakeLists.txt` 则通过 `add_subdirectory` 集成，并通过 `HAS_VECTOR_BLF` 宏控制条件编译

### 3.4 驱动 DLL 自动复制
构建后通过 `add_custom_command(TARGET openbus POST_BUILD)` 将整个 `driver/` 目录复制到输出目录，确保 `zlgcan.dll` 等运行时 DLL 与 exe 同目录。

### 3.5 构建脚本工作流
`scripts/build.py` 提供统一 CLI：
- `configure`：检测 Ninja/MinGW，设置 `CMAKE_PREFIX_PATH`、编译器、构建类型，生成 build/
- `build`：增量编译，首次运行自动 configure；自动终止正在运行的 `openbus.exe` 避免文件锁
- `run` / `debug`：启动程序或 GDB 调试
- `deploy`：调用 `windeployqt` 打包 Qt 运行时，并手动补充 `Qt6PrintSupport.dll`（qcustomplot 静态库的传递依赖）
- `all`：一键完成 configure → build → deploy → run
- 工具路径通过 `SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR` 环境变量覆盖

## 4. 约定与约束

- **禁止使用 ccache**：注释明确说明与 MinGW g++ 13 的 PCH 不兼容，会静默崩溃
- **禁止使用 LLD 链接器**：在 Windows 上会导致文件锁问题，强制使用默认 `ld.bfd`
- **构建类型**：仅支持 Debug / Release / RelWithDebInfo / MinSizeRel 四种
- **输出目录**：所有产物位于 `build/bin/`，驱动 DLL 复制到该目录
- **Qt 版本锁定**：顶层 `CMakeLists.txt` 固定 `VERSION 0.1.0`，Qt 通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg)` 查找
- **Python 插件独立构建**：`plugins/` 下的 Python 插件不参与 CMake 构建，由应用运行时动态加载（见 `core/plugin/pluginmanager.cpp`）
- **测试数据**：`test/` 目录存放 `.blf`、`.asc`、`.dbc` 等测试样本，不参与构建
- **资源文件**：图标、样式表通过 `resources/resources.qrc` 注册，编译时嵌入二进制