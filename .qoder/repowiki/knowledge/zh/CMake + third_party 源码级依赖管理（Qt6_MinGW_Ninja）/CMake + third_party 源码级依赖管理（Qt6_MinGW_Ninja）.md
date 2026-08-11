---
kind: dependency_management
name: CMake + third_party 源码级依赖管理（Qt6/MinGW/Ninja）
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - third_party/Dependencies.cmake
    - src/CMakeLists.txt
    - scripts/build.py
    - driver/zlgcan.dll
    - driver/kerneldlls/dll_cfg.ini
---

## 1. 使用的系统/方法

本项目采用 **CMake 3.21+** 作为构建与依赖管理系统，配合 **MinGW g++ 13** 和可选的 **Ninja** 生成器。第三方依赖全部以 **源码形式直接拷贝到 `third_party/` 目录**（vendored），通过 CMake 的 `add_library` / `INTERFACE IMPORTED` / `target_include_directories` 暴露给主工程，不使用任何包管理器（如 vcpkg、Conan、NuGet）或 Git Submodule。

- Qt6 通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg)` 由系统/环境变量提供，路径由 `scripts/build.py` 传入 `-DCMAKE_PREFIX_PATH`。
- 运行时依赖通过 `windeployqt`（在 `scripts/build.py deploy` 中调用）自动收集 Qt DLL；驱动 DLL 通过 CMake `POST_BUILD copy_directory` 复制到输出目录。

## 2. 关键文件

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层工程定义、Qt6 查找、包含 `third_party/Dependencies.cmake` |
| `third_party/Dependencies.cmake` | 集中声明第三方库目标（spdlog INTERFACE、qcustomplot STATIC、vector_blf add_subdirectory） |
| `src/CMakeLists.txt` | 将第三方头路径以 PUBLIC 方式暴露给 `sin_core`，并链接 `z`、`pugixml`、`qcustomplot`、`Qt6::Svg` |
| `scripts/build.py` | 统一入口：配置/编译/部署/运行，设置 `SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR` 等环境变量 |
| `driver/zlgcan.dll` 及 `driver/kerneldlls/*.dll` | 预编译 CAN 设备驱动，随 exe 一起分发 |
| `tools/ninja/ninja.exe` | 本地内嵌的 Ninja 二进制，优先于系统 make |

## 3. 架构与约定

### 3.1 源码级 vendoring（`third_party/`）

每个第三方库以完整源码树形式驻留在 `third_party/<name>/`：

- **spdlog**：仅使用其 include 目录，通过 `INTERFACE IMPORTED` 库暴露，不编译源码。
- **nlohmann/json**：单头文件库，直接 `#include "json.hpp"`，无需额外目标。
- **concurrentqueue**：单头文件，通过 `target_include_directories(sin_core PUBLIC ...)` 暴露。
- **pugixml**：单头文件（`pugixml.hpp`、`pugiconfig.hpp`），条件式链接 `pugixml` 目标（若存在）。
- **qcustomplot**：源码库，`add_library(qcustomplot STATIC qcustomplot.cpp)`，并链接 `Qt6::Widgets`、`Qt6::PrintSupport`。
- **vector_blf**：独立 CMake 子项目，通过 `add_subdirectory(...)` 引入，支持 BLF 文件的 zlib 压缩解析。

所有第三方库的 include 路径都通过 `if(EXISTS ...)` 条件判断后以 `PUBLIC` 方式注入 `sin_core`，使上层 UI 层间接获得这些头文件路径。

### 3.2 分层静态库隔离

`src/CMakeLists.txt` 将工程拆为三个目标：
- `sin_core`（STATIC）：核心逻辑，对外暴露 spdlog、json、concurrentqueue、pugixml 的头路径。
- `sin_ui`（STATIC）：UI 组件，仅 PRIVATE 依赖 qcustomplot 和 Qt6::Svg。
- `sin`（可执行）：链接 `sin_ui`，Windows 下设为 GUI 程序。

这种设计使修改 UI 代码时不会触发 core 重编译，提升增量构建速度。

### 3.3 构建脚本约定

`scripts/build.py` 是唯一的构建入口，约定如下：
- 工具路径通过环境变量覆盖：`SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR`。
- 默认 Qt 路径：`C:/Qt/6.8.3/mingw_64`，MinGW：`C:/Qt/Tools/mingw1310_64`，CMake：`C:/tools/cmake-3.30.3-windows-x86_64`。
- 构建系统优先检测 `tools/ninja/ninja.exe`，未找到则回退到 MinGW Makefiles。
- 明确禁用 ccache（与 MinGW g++ 13 PCH 不兼容）和 LLD（Windows 文件锁问题）。
- `deploy` 命令调用 `windeployqt`，并手动补充 `Qt6PrintSupport.dll`（qcustomplot 静态库传递依赖未被 windeployqt 识别）。

### 3.4 驱动 DLL 分发

`driver/` 目录下的 ZLG SDK 动态库（`zlgcan.dll`、`CANDevCore.dll`、`USBCANFD800U.dll` 等）通过 CMake `POST_BUILD` 命令复制到输出目录，安装规则也包含 `*.dll` 匹配。注释明确指出：`zlgcan.dll 必须与 exe 在同一目录才能找到 USB 驱动`。

## 4. 约定与约束

- **禁止使用外部包管理器**：仓库中不存在 `go.mod`、`package.json`、`vcpkg.json`、`conanfile.*` 等，所有依赖均以源码形式提交。
- **第三方库版本锁定**：通过固定 `third_party/` 下的源码快照实现版本锁定，无 lockfile；升级需手动替换目录内容。
- **Qt 版本硬编码**：默认路径指向 `C:/Qt/6.8.3/mingw_64`，跨机器构建需通过环境变量调整。
- **PCH 限制**：core 层使用精简 Qt 头列表（无 Widget），UI 层使用完整 Widget 头列表；两者均启用 `CMAKE_AUTOMOC/UIC/RCC`。
- **zlib 依赖**：BLF 解析需要 zlib，MinGW 自带 `libz.a`，通过 `target_link_libraries(sin_core PRIVATE z)` 链接。
- **构建产物位置**：可执行文件输出到 `${CMAKE_BINARY_DIR}/bin/sin.exe`，驱动 DLL 复制到同目录。
- **发布流程**：`python scripts/build.py all` 依次执行 configure → build → deploy → run，其中 deploy 阶段负责打包 Qt 运行时。

## 5. 不适用项

- 无 npm/yarn/pip/go 等语言级包管理器。
- 无私有注册表或代理配置。
- 无 CI 中的依赖缓存策略（仓库未包含 `.github/workflows` 等 CI 文件）。