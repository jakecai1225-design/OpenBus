---
kind: dependency_management
name: C++/Qt 源码级 vendoring + Python 插件运行时依赖声明
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - third_party/Dependencies.cmake
    - drivers/CMakeLists.txt
    - scripts/build.py
    - sdk/sin/__init__.py
    - plugins/uds-diagnostic/plugin.json
    - plugins/blf-converter/main.py
    - plugins/bus-statistics/main.py
    - plugins/canopen-explorer/main.py
    - plugins/dbc-tool/main.py
---

## 1. 总体方案

OpenBus 根工程采用**混合依赖管理策略**：
- C++/Qt 核心与驱动插件通过 **CMake + 源码级 vendoring（third_party/）** 管理，所有第三方库以源码形式直接纳入仓库。
- Python 侧的插件与 SDK 使用**运行时按需导入**，PyQt6 等可选依赖在 `import` 失败时优雅降级，不强制安装。
- 没有全局的 `requirements.txt`、`pyproject.toml`、`package.json`、`go.mod` 或包管理器 lockfile；Python 依赖由每个插件自行声明（注释提示）并在宿主中容忍缺失。

## 2. 关键文件与位置

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 项目根构建入口，定义 Qt6 查找、C++17、输出目录，并 `include(third_party/Dependencies.cmake)` |
| `third_party/Dependencies.cmake` | 集中声明第三方依赖目标：spdlog（INTERFACE IMPORTED）、qcustomplot（静态库，链接 Qt6::Widgets/PrintSupport）、vector_blf（add_subdirectory） |
| `third_party/` | vendored 源码目录，包含 spdlog、nlohmann_json、pugixml、concurrentqueue、qcustomplot、dbcppp、vector_blf 等 |
| `drivers/CMakeLists.txt` | 驱动插件聚合，每个子目录一个 .odp 单元，输出到 `build/bin/drivers/<id>/` |
| `scripts/build.py` | 统一构建脚本，封装 configure/build/run/debug/deploy/test，默认工具路径来自环境变量 `SIN_QT_DIR` / `SIN_MINGW_DIR` / `SIN_CMAKE_DIR` |
| `plugins/*/plugin.json` | Python 插件元数据（name/version/main/icon/activationEvents/contributes），无 pip 依赖字段 |
| `sdk/sin/__init__.py` | 插件 SDK 入口，PyQt6 相关 `ui` 模块 try/except ImportError 跳过 |
| `tools/ninja/` | 本地内置 Ninja 可执行，优先于系统 Ninja 作为构建调度器 |

## 3. 架构与约定

### 3.1 C++ 依赖 — 源码级 vendoring
- 所有第三方头文件与源码直接放在 `third_party/<lib>/`，通过相对路径 include 和 `add_library(... STATIC ...)` 或 `INTERFACE IMPORTED` 暴露给上层 target。
- `Dependencies.cmake` 是唯一的依赖注册点：新增库需在此处添加 `add_library` / `target_include_directories` / `target_link_libraries`。
- 对带独立 CMake 的库（如 vector_blf），使用 `add_subdirectory()` 将其纳入本构建树；对单头文件库（nlohmann_json）仅设置 include 路径。
- 构建产物（`.dll`、`.a`、`.lib`）不出现在仓库中，仅保留源码；最终可执行文件通过 `windeployqt` 部署 Qt 运行时 DLL。

### 3.2 驱动插件 ABI 约束
- 每个驱动（zlg、peak、kvaser、slcan、candle）是一个独立的 CMake target，输出为 `driver_<id>.dll` 加 `driver.json` 清单，打包成 `.odp`。
- 强制约定：**与主程序同 Qt 版本 + 同编译器（MinGW 13.1 x64, C++17）**，否则 QPluginLoader 无法加载。
- 驱动公共头路径通过 `DRIVER_COMMON_INCLUDES` 指向 `${CMAKE_SOURCE_DIR}/src`，复用 core 后端源码。

### 3.3 Python 依赖 — 运行时可选
- 插件宿主 `scripts/sin_host.py` 启动各插件的 `main.py`，若 PyQt6 未安装则记录日志“UI 不可用”，插件仍可运行非 UI 功能。
- 每个插件 `main.py` 头部注释写明 `依赖: pip install PyQt6`，但无强制校验——仅在尝试创建 UI 组件时打印错误提示。
- SDK `sdk/sin/ui` 模块被 `try/except ImportError` 包裹，保证无 PyQt6 时 SDK 仍可用。
- 测试用例（如 `plugins/uds-diagnostic/tests/`）直接 import 被测模块，不经过宿主，因此需要用户环境已安装 PyQt6。

### 3.4 构建工具链
- 构建系统：CMake 3.21+，生成器优先 Ninja（`tools/ninja/ninja.exe`），回退 MinGW Makefiles。
- 编译器：MinGW g++ 13.1 x64，C++17，禁用扩展。
- 链接器：固定使用 `ld.bfd`（注释明确禁止 LLD/gold，因 Windows PE 兼容性问题）。
- 缓存：显式禁用 ccache（与 PCH 不兼容）。
- 构建类型：支持 Dev（-O1 -g1，独立 build-dev/）、Debug、Release、RelWithDebInfo、MinSizeRel。

## 4. 约定与约束

| 规则 | 来源/依据 |
|---|---|
| 第三方 C++ 库必须放入 `third_party/<name>/` 并通过 `Dependencies.cmake` 注册 | `third_party/Dependencies.cmake` 结构及注释 |
| 驱动插件必须与主程序使用相同 Qt 版本与编译器（MinGW 13.1 x64, C++17） | `drivers/CMakeLists.txt` 顶部注释 |
| 驱动输出布局为 `<exe>/drivers/<id>/driver.json + driver_<id>.dll [+ assets]` | `drivers/CMakeLists.txt` 中 `deploy_driver_manifest` 函数 |
| 构建前自动终止正在运行的 openbus.exe 避免文件锁 | `scripts/build.py::cmd_build` 调用 `kill_running_executable()` |
| 不使用 ccache 与 LLD 链接器 | `CMakeLists.txt` 与 `scripts/build.py` 注释 |
| Qt 运行时通过 `windeployqt` 部署，且需手动补充 `Qt6PrintSupport.dll`（qcustomplot 静态库传递依赖） | `scripts/build.py::cmd_deploy` |
| Python 插件的 PyQt6 为可选依赖，缺失时 UI 功能降级 | `sdk/sin/__init__.py` 的 try/except ImportError 与各插件 main.py 中的提示 |
| 构建工具路径可通过环境变量覆盖：`SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR` | `scripts/build.py` 默认值定义 |
| 测试套件通过 `python scripts/build.py test` 调用 ctest 执行 | `scripts/build.py::cmd_test` |

## 5. 缺失项说明

- 无 Python 包清单（requirements.txt/pyproject.toml），依赖通过注释和运行时 ImportError 处理。
- 无包管理器 lockfile（pip freeze、poetry.lock 等均不存在）。
- 无私有 PyPI 源配置。
- 无 Node.js/Go/Rust 生态的依赖声明文件。
- 二进制驱动库（`driver/kerneldlls/*.dll`、`zlgcan.dll` 等）以预编译二进制形式随仓库分发，不属于源码 vendoring。