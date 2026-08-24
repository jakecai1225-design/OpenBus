---
kind: dependency_management
name: C++/Python 混合依赖管理：源码级 vendoring + 构建期捆绑
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - third_party/Dependencies.cmake
    - scripts/build.py
    - scripts/package.py
    - third_party/spdlog/include/spdlog/spdlog.h
    - third_party/qcustomplot/qcustomplot.h
    - third_party/vector_blf/CMakeLists.txt
    - drivers/zlg/driver.json
    - plugins/can-dashboard/plugin.json
    - installer/openbus.iss
---

## 1. 使用的系统与方法

本项目采用 **源码级 vendoring（第三方代码直接拷贝到 `third_party/`）** 与 **构建期捆绑** 的混合策略，不使用任何包管理器（无 `package.json`、`go.mod`、`requirements.txt`、`vcpkg.json`、`conanfile` 等）。

- C++ 层：通过 CMake 将 `third_party/` 下的源码作为头文件库或静态库直接编译进产物。
- Python 层：插件为纯 Python 脚本，不声明外部依赖；打包时从本机已安装的 Python 环境裁剪出最小运行时（含 PyQt6）并随包分发。
- 部署层：使用 `windeployqt` 收集 Qt 运行时 DLL，再用自定义 `scripts/package.py` 组装 staging 目录并做 import 表完整性校验。

## 2. 关键文件与位置

| 类别 | 关键路径 | 作用 |
|---|---|---|
| CMake 入口 | `CMakeLists.txt` | 声明 Qt6 依赖 (`find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg Network)`)，引入 `third_party/Dependencies.cmake` |
| 三方依赖声明 | `third_party/Dependencies.cmake` | 以 `INTERFACE IMPORTED` / `add_library(STATIC ...)` 形式暴露 spdlog、qcustomplot、vector_blf、nlohmann/json |
| 源码 vendoring | `third_party/spdlog/`、`third_party/nlohmann_json/`、`third_party/qcustomplot/`、`third_party/pugixml/`、`third_party/concurrentqueue/`、`third_party/vector_blf/`、`third_party/dbcppp/` | 完整第三方源码树 |
| 构建脚本 | `scripts/build.py` | 封装 CMake/Ninja/MinGW/Qt 工具链，提供 configure/build/run/deploy/test/all/status/open 子命令 |
| 打包流水线 | `scripts/package.py` | Release 构建 → windeployqt → staging 组装 → Python 运行时裁剪 → objdump 依赖校验 → zip/Inno Setup 输出 |
| 驱动插件清单 | `drivers/*/driver.json` | 每个硬件驱动插件的元数据（名称、版本、作者），由市场机制加载 |
| Python 插件清单 | `plugins/<name>/plugin.json` | 每个 Python 插件的元数据（name/version/author/main/icon/activationEvents/contributes.commands） |
| 安装器 | `installer/openbus.iss` | Inno Setup 安装脚本（可选，iscc 不存在则跳过） |

## 3. 架构与约定

### 3.1 C++ 第三方库（vendored header/static lib）

- **spdlog**：仅头文件，通过 `INTERFACE_INCLUDE_DIRECTORIES` 暴露 `third_party/spdlog/include`，无需链接。
- **nlohmann/json**：单头文件库，直接 `#include "json.hpp"`。
- **qcustomplot**：源码库，`third_party/qcustomplot/qcustomplot.cpp` 被编译为静态库 `libqcustomplot.a`，并链接 Qt6::Widgets、Qt6::PrintSupport。
- **vector_blf**：带独立 `CMakeLists.txt` 的子项目，通过 `add_subdirectory()` 集成，GPL-3.0 许可。
- **dbcppp**：作为 git submodule 嵌入（`.gitmodules` 存在），用于 DBC 解析。
- **pugixml、concurrentqueue**：头文件库，按同样方式 vendored。

所有第三方库均通过 `third_party/Dependencies.cmake` 统一暴露目标名（如 `spdlog`、`qcustomplot`），业务模块通过 `target_link_libraries(... spdlog qcustomplot)` 引用，避免在业务 CMake 中硬编码 include 路径。

### 3.2 Qt 框架依赖

- 通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg Network)` 查找本地 Qt6 安装。
- 默认工具路径来自环境变量：`SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR`，默认值指向 `C:/Qt/6.8.3/mingw_64`、`C:/Qt/Tools/mingw1310_64`、`C:/tools/cmake-3.30.3-windows-x86_64`。
- 发布阶段用 `windeployqt --release --no-translations --compiler-runtime` 自动收集 Qt DLL，再手动补拷 `Qt6PrintSupport.dll`（qcustomplot 静态库传递依赖）和 `translations/qtbase_zh_CN.qm`。

### 3.3 Python 插件与运行时

- 每个插件位于 `plugins/<name>/`，包含 `main.py`、`plugin.json`、可选 `icon.svg` 及共享模块（`dbcparse.py`、`isotp_client.py` 放在 `plugins/_shared/`）。
- 插件之间通过宿主进程动态加载，不声明 pip 依赖；唯一重依赖是 PyQt6（仅 `uds-diagnostic` 等少数插件使用）。
- 打包时 `scripts/package.py` 从本机 Python 环境裁剪出最小运行时：复制 `pythonXY.dll`、标准库（剔除 tkinter/turtledemo/lib2to3/test）、PyQt6 子集（QtCore/Gui/Widgets/Svg + sip + Qt6/bin + platforms/imageformats/iconengines/styles），并通过 `pythonXY._pth` 实现隔离（忽略注册表/环境变量）。
- 打包流程末尾生成 `requirements-lock.txt`，记录 `PyQt6==<ver>`、`PyQt6-Qt6==<ver>`、`PyQt6-sip==<ver>` 的版本锁定信息。

### 3.4 硬件驱动插件（C++ .odp）

- 位于 `drivers/<vendor>/`，每个驱动有 `CMakeLists.txt`、`*_driver_plugin.{cpp,h}`、`driver.json`。
- 构建输出到 `build/bin/drivers/<id>/`，由主程序通过 QPluginLoader 动态加载。
- 打包时不预装驱动（用户从市场安装），staging 白名单明确排除 `drivers/`。

## 4. 约定与约束

| 规则 | 来源/依据 |
|---|---|
| 第三方 C++ 库必须放入 `third_party/` 并以源码形式 vendored，禁止通过系统包管理器或远程 fetch 获取 | `third_party/Dependencies.cmake` 显式列出每个依赖的 include/link 方式 |
| 新增第三方库需在 `third_party/Dependencies.cmake` 中暴露 CMake target，并在业务模块中通过该 target 引用 | 现有 spdlog/qcustomplot/vector_blf 的统一模式 |
| Qt 版本固定为 6.8.3 (mingw_64)，编译器固定为 MinGW g++ 13，CMake 固定为 3.30.3 | `scripts/build.py` 默认路径常量 |
| 构建系统优先使用 Ninja（`tools/ninja/ninja.exe`），回退到 MinGW Makefiles | `scripts/build.py` 检测逻辑 |
| 不使用 ccache（与 MinGW g++ 13 PCH 不兼容）和 LLD（Windows 文件锁问题） | `CMakeLists.txt` 注释与 `scripts/build.py` 文档字符串 |
| 发布产物必须通过 `objdump -p` 对 staging 内全部 `.exe/.dll/.pyd` 做 import 表校验，缺失即失败 | `scripts/package.py` step_verify_deps |
| 系统 DLL 白名单（kernel32/user32/gdi32/...）允许跳过校验，非系统 DLL 必须出现在 staging 中 | `scripts/package.py` 中 `SYSTEM_DLLS` 集合 |
| ZLG 厂商 SDK（`zlgcan.dll`、`kerneldlls/`）不随包分发，需用户另行安装 ZCANPRO | `scripts/package.py` 注释 §7 授权决策 |
| Python 插件不得声明 `requirements.txt`/`setup.py`，依赖通过宿主绑定的 Python 运行时提供 | 插件目录结构及打包脚本行为 |
| 插件元数据必须包含 `name`、`version`、`author`、`description`、`main`、`icon`、`activationEvents`、`contributes.commands` | 各 `plugins/*/plugin.json` 的一致格式 |
| 驱动插件元数据必须包含 `id`、`name`、`version`、`author`、`description`、`main`、`icon` | 各 `drivers/*/driver.json` 的一致格式 |
| 构建类型支持 Dev/Debug/Release/RelWithDebInfo/MinSizeRel，Dev 档使用 `-O1 -g1` 独立目录 `build-dev/` | `scripts/build.py` BUILD_TYPES 与注释 |
| 可执行文件输出到 `${CMAKE_BINARY_DIR}/bin`，测试目标聚合为 `tests` 并通过 ctest 运行 | 根 `CMakeLists.txt` 与 `scripts/build.py cmd_test` |

## 5. 总结

该项目是一个典型的 **源码级 vendoring + 手工打包流水线** 的桌面应用：C++ 依赖通过 `third_party/` 源码树 + CMake INTERFACE/STATIC 目标管理；Qt 通过 `find_package` + `windeployqt` 管理；Python 依赖通过裁剪本机解释器并绑定 PyQt6 子集解决。整个依赖生命周期（获取→构建→验证→打包）由 `scripts/build.py` 和 `scripts/package.py` 两个 Python 脚本串联，没有使用任何现代包管理器。