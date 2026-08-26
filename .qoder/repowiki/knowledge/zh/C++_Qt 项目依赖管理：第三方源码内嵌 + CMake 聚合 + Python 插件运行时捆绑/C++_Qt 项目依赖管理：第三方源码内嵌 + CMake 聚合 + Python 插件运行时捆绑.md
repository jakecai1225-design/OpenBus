---
kind: dependency_management
name: C++/Qt 项目依赖管理：第三方源码内嵌 + CMake 聚合 + Python 插件运行时捆绑
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - third_party/Dependencies.cmake
    - scripts/build.py
    - sdk/sin/__init__.py
    - plugins/_shared/dbcparse.py
    - drivers/CMakeLists.txt
    - drivers/candle/CMakeLists.txt
    - drivers/candle/driver.json
    - doc/打包安装方案.md
---

## 1. 整体方案

本项目（OpenBUS CAN/CAN FD 分析工具）采用**混合依赖管理策略**：
- **C++/Qt 核心工程**：通过 `third_party/` 目录**直接内嵌第三方源码**，由根 `CMakeLists.txt` 经 `third_party/Dependencies.cmake` 统一暴露为 CMake 目标。
- **Python 插件生态**：每个插件以独立 `.py` 包形式位于 `plugins/<name>/`，无全局 `requirements.txt`；可选 UI 依赖 PyQt6 通过宿主提供的 Python runtime 提供。
- **打包阶段**：通过 `scripts/package.py` 与文档《打包安装方案.md》将 Python 解释器、PyQt6 子集及插件一起打包成可分发产物。

## 2. 关键文件与位置

| 作用 | 路径 | 说明 |
|---|---|---|
| CMake 入口 | `CMakeLists.txt` | 声明 Qt6 组件、`find_package(Qt6)`、include `third_party/Dependencies.cmake` |
| 第三方依赖清单 | `third_party/Dependencies.cmake` | 定义 `spdlog`、`qcustomplot`、`vector_blf` 等 CMake 目标 |
| 第三方源码目录 | `third_party/` | 内嵌 spdlog、nlohmann/json、pugixml、qcustomplot、concurrentqueue、dbcppp、vector_blf |
| 驱动插件构建 | `drivers/*/CMakeLists.txt` | 每个厂商驱动作为独立 DLL 插件（candle/kvaser/peak/slcan/zlg） |
| 构建脚本 | `scripts/build.py` | 封装 CMake/Ninja/MinGW/Qt 工具链，支持 configure/build/run/deploy/test |
| Python SDK | `sdk/sin/__init__.py` | 插件公共 API，UI 模块按需 import PyQt6 |
| 插件市场资源 | `drivers/market/assets/*.svg` | 驱动图标资源 |
| 打包文档 | `doc/打包安装方案.md` | 描述 Python runtime 裁剪、requirements-lock.txt、NOTICES 生成 |

## 3. 架构与约定

### 3.1 C++ 第三方库（源码内嵌）
- 所有头文件/源码直接放入 `third_party/<lib>/`，不通过包管理器下载。
- `third_party/Dependencies.cmake` 用 `add_library(... IMPORTED)` 或 `add_subdirectory()` 暴露目标：
  - `spdlog`：仅头文件，INTERFACE 目标，仅设置 `INTERFACE_INCLUDE_DIRECTORIES`。
  - `nlohmann/json`：单头文件库，无需额外配置。
  - `qcustomplot`：编译为静态库 `qcustomplot`，链接 `Qt6::Widgets Qt6::PrintSupport`。
  - `vector_blf`：带自身 `CMakeLists.txt`，通过 `add_subdirectory` 引入。
- 主工程通过 `target_link_libraries(... spdlog qcustomplot vector_blf ...)` 使用这些目标。
- 版本锁定方式：**提交源码快照到仓库**（如 `third_party/spdlog/` 含完整 git 历史），升级需手动替换目录内容。

### 3.2 Qt 框架依赖
- 通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg Network)` 查找系统安装的 Qt6。
- 默认 Qt 路径由 `scripts/build.py` 中 `DEFAULT_QT_DIR = Path(os.environ.get("SIN_QT_DIR", "C:/Qt/6.8.3/mingw_64"))` 指定，可通过环境变量覆盖。
- 部署时调用 `windeployqt.exe` 自动收集运行时 DLL，并手动补充 `Qt6PrintSupport.dll`（因 qcustomplot 静态库的传递依赖未被 windeployqt 识别）。

### 3.3 驱动插件（DLL 热插拔）
- 每个厂商驱动位于 `drivers/<vendor>/`，包含 `CMakeLists.txt`、`driver.json`、`*_driver_plugin.cpp/.h`。
- 驱动输出为独立 DLL，由主程序在运行时动态加载（见 `src/core/driver/` 中的 `driverregistry`、`marketindex`）。
- 驱动图标资源集中放在 `drivers/market/assets/`。

### 3.4 Python 插件依赖
- 插件位于 `plugins/<name>/`，每个插件有独立的 `main.py`、`plugin.json`、可选 `dbcparse.py`。
- 共享 DBC 解析逻辑抽取到 `plugins/_shared/dbcparse.py`，供多个插件复用。
- **无全局 `requirements.txt`**：插件按需导入 PyQt6，未安装时通过 try/except 跳过 UI 功能（见 `sdk/sin/__init__.py` 中对 `ui` 模块的 ImportError 处理）。
- 插件运行时的 Python 环境由宿主进程注入，或通过打包阶段的 bundled Python runtime 提供。

### 3.5 构建与部署约定
- `scripts/build.py` 是统一入口，支持 `configure/build/run/debug/clean/rebuild/deploy/all/status/open/test` 子命令。
- 优先使用本地 `tools/ninja/ninja.exe` 作为构建加速器，回退到 MinGW Makefiles。
- 构建类型支持 `Dev`（-O1 -g1，快速迭代）、`Debug`、`Release`、`RelWithDebInfo`、`MinSizeRel`。
- Dev 档建议独立 `build-dev/` 目录，与全量 Debug 并存避免重编。
- 链接器强制使用 `ld.bfd`（MinGW 默认），禁用 ccache（与 PCH 不兼容）和 LLD（Windows 文件锁问题）。

## 4. 约束与规则

- **第三方 C++ 库必须以内嵌源码形式存在于 `third_party/`**，不得通过 vcpkg/conan/pkg-config 等外部包管理器获取（仓库中未发现此类配置文件）。
- **Qt 版本固定为 6.x**（当前示例 6.8.3），通过 `CMAKE_PREFIX_PATH` 指向具体安装路径。
- **Python 插件的 PyQt6 为可选依赖**：SDK 层捕获 ImportError，使无 UI 功能的插件可在无 PyQt6 环境下运行。
- **打包产物需包含 `requirements-lock.txt`**（由打包流程生成），用于记录 Python 依赖精确版本。
- **驱动插件必须提供 `driver.json` 元数据**，由市场索引机制发现。
- **禁止使用 ccache 与 LLD 链接器**：构建脚本与根 CMakeLists.txt 注释明确说明原因（PCH 兼容性、文件锁问题）。
- **Ninja 为推荐构建系统**：若检测到 `tools/ninja/ninja.exe` 则优先使用，否则回退到 MinGW Makefiles。

## 5. 缺失项

- 无 `go.mod` / `package.json` / `Cargo.toml` / `setup.py` / `pyproject.toml` 等语言级依赖清单（C++ 侧完全依赖 CMake + 源码内嵌）。
- 无私有包仓库或镜像源配置（所有第三方源码直接提交至 Git 仓库）。
- 无自动化依赖更新脚本（升级第三方库需手动替换 `third_party/` 下对应目录）。