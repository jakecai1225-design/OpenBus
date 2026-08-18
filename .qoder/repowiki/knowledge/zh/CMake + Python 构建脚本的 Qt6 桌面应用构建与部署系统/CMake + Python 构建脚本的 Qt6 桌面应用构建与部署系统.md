---
kind: build_system
name: CMake + Python 构建脚本的 Qt6 桌面应用构建与部署系统
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - drivers/CMakeLists.txt
    - third_party/Dependencies.cmake
    - scripts/build.py
    - src/core/plugin/pluginhost.h
    - src/core/plugin/pluginmanager.h
    - drivers/zlg/CMakeLists.txt
    - drivers/peak/CMakeLists.txt
    - drivers/kvaser/CMakeLists.txt
    - plugins/blf-converter/plugin.json
    - plugins/uds-diagnostic/plugin.json
---

## 1. 构建系统与工具链

项目采用 **CMake (≥3.21) + Qt6** 作为核心构建系统，配合自研的 **Python 构建脚本 `scripts/build.py`** 封装配置、编译、运行、调试、部署全流程。目标平台为 Windows（MinGW 13.1 x64），通过环境变量 `SIN_QT_DIR` / `SIN_MINGW_DIR` / `SIN_CMAKE_DIR` 或默认路径定位工具链。

- C++ 标准固定为 **C++17**，启用 Qt 自动化处理（AUTOMOC/AUTOUIC/AUTORCC）。
- 构建器优先使用本地 `tools/ninja/ninja.exe`（Ninja 生成器），未检测到则回退到 MinGW Makefiles。
- 明确禁用 ccache（与 MinGW g++ 13 的 PCH 不兼容）和 LLD 链接器（Windows 上文件锁问题），使用默认 ld.bfd。
- 输出目录统一为 `build/bin/`，可执行文件名为 `openbus.exe`。

## 2. 关键构建文件

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 根工程：定义项目版本 0.1.0、查找 Qt6 (Widgets/PrintSupport/Svg/Network)、包含 third_party、添加 src 与 drivers 子目录 |
| `src/CMakeLists.txt` | 分层构建：`openbus_core`（静态库）、`openbus_ui`（静态库）、`openbus`（GUI 可执行），含 PCH 预编译头优化 |
| `drivers/CMakeLists.txt` | 驱动插件聚合，按 id 组织 zlg/peak/kvaser 三个原生驱动子目录 |
| `third_party/Dependencies.cmake` | 第三方依赖声明：spdlog（INTERFACE）、qcustomplot（静态库）、vector_blf（可选子目录） |
| `scripts/build.py` | 统一入口：configure/build/run/debug/clean/rebuild/deploy/all/status/open 等子命令 |
| `plugins/*/plugin.json` | Python 插件元数据（非 CMake 构建，由运行时加载） |

## 3. 架构与约定

### 3.1 分层静态库设计
`src/CMakeLists.txt` 将代码拆为三层静态库以隔离重编译范围：
- `openbus_core`：核心逻辑（canframe/recorder/player/simulator/dbc/file_import/plugin 等），仅依赖轻量 Qt 头（QObject/QString/QVariant 等），通过 PCH 加速编译。
- `openbus_ui`：界面组件，依赖 openbus_core 和完整 Qt Widgets，引入 qcustomplot/Qt6::Svg/Qt6::Network。
- `openbus`：GUI 可执行，链接 openbus_ui，设置 `WIN32_EXECUTABLE TRUE` 隐藏控制台窗口。

### 3.2 驱动插件体系（原生 .odp）
- 每个驱动位于 `drivers/<id>/`，包含 `driver.json`、`*_driver_plugin.cpp/.h` 及独立 `CMakeLists.txt`。
- 构建产物输出到 `build/bin/drivers/<id>/`，与 `.odp` 包内结构一致；打包由 `scripts/driver_tool.py` 完成。
- ABI 契约强制要求与主程序同 Qt 版本 + 同编译器（MinGW 13.1 x64, C++17）。
- 编译后通过 `POST_BUILD` 自定义命令将 `driver/` 下的 ZLG SDK DLL 复制到 exe 同级目录。

### 3.3 Python 插件体系
- 插件位于 `plugins/<name>/`，每个插件包含 `main.py`、`plugin.json`、图标和资源。
- 插件由运行时通过 `core/plugin/pluginhost` 动态加载，不参与 CMake 构建流程。
- 测试用例位于 `plugins/uds-diagnostic/tests/`，使用 pytest（推测）。

### 3.4 第三方依赖管理
- 源码级 vendoring：`third_party/` 下直接存放 spdlog、nlohmann_json、pugixml、concurrentqueue、qcustomplot、vector_blf 源码。
- `Dependencies.cmake` 通过 `add_library INTERFACE IMPORTED` 或 `add_subdirectory` 暴露给上层 target。
- 可选依赖通过 `if(EXISTS ...)` 条件判断，保证无 vendor 时仍可部分构建。

## 4. 构建脚本约定（`scripts/build.py`）

### 4.1 环境发现
- 工具路径优先级：命令行参数 > 环境变量 (`SIN_QT_DIR`/`SIN_MINGW_DIR`/`SIN_CMAKE_DIR`) > 默认路径。
- 自动检测 `tools/ninja/ninja.exe`，存在则用 Ninja 生成器，否则回退 MinGW Makefiles。
- 构建前自动终止正在运行的 `openbus.exe`（`taskkill /F /IM openbus.exe`），避免文件锁导致链接失败。

### 4.2 命令集
| 命令 | 行为 |
|---|---|
| `configure [--build-type Debug|Release|RelWithDebInfo|MinSizeRel]` | CMake 配置，支持 --clean 清理旧 build |
| `build [-j N] [--target X]` | 增量编译，首次自动 configure |
| `run [--args ...]` | 运行已构建的可执行文件 |
| `debug [--args ...]` | 通过 GDB 启动 |
| `deploy` | 调用 `windeployqt` 并手动补充 `Qt6PrintSupport.dll`（qcustomplot 静态库传递依赖） |
| `rebuild` | clean → configure → build |
| `all` | configure → build → deploy → run 完整流水线 |
| `status` | 显示环境与构建状态 |
| `open` | 打开资源管理器中的构建输出目录 |

### 4.3 部署约束
- `windeployqt` 无法检测静态库对 Qt 模块的传递依赖，需手动复制 `Qt6PrintSupport.dll`。
- 驱动 DLL 在 `POST_BUILD` 阶段从 `driver/` 目录复制到 exe 同级目录。
- 安装规则通过 `install(TARGETS openbus RUNTIME DESTINATION bin)` 支持 CMake install。

## 5. 约束与规则

- **C++ 标准**：强制 C++17，关闭扩展（`CMAKE_CXX_STANDARD 17`, `CMAKE_CXX_EXTENSIONS OFF`）。
- **Qt 版本锁定**：通过 `find_package(Qt6 REQUIRED COMPONENTS ...)` 显式指定 Widgets/PrintSupport/Svg/Network。
- **PCH 限制**：禁止使用 ccache，因其与 MinGW g++ 13 的预编译头不兼容会导致静默崩溃。
- **链接器限制**：禁止使用 LLD，Windows 上会产生文件锁问题。
- **驱动 ABI 契约**：原生驱动必须与主程序使用相同 Qt 版本和编译器（MinGW 13.1 x64, C++17），否则运行时加载失败。
- **构建目录隔离**：所有构建产物输出到 `build/` 目录，源树保持干净。
- **并行构建**：通过 `-j` 参数控制，Ninja 和 MinGW Makefiles 均支持。
- **Python 插件无需构建**：Python 插件由运行时解释执行，不包含在 CMake 构建图中。

## 6. CI/发布

仓库中未发现 GitHub Actions / GitLab CI / Dockerfile 等持续集成配置。发布流程依赖本地 `scripts/build.py all` 完成构建与部署，再通过 `install()` 规则或手动打包分发。