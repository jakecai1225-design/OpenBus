---
kind: dependency_management
name: C++/Python 混合依赖管理：源码级 vendoring + 构建期 CMake 声明 + Python 运行时捆绑
category: dependency_management
scope:
    - '**'
source_files:
    - third_party/Dependencies.cmake
    - CMakeLists.txt
    - scripts/build.py
    - drivers/zlg/driver.json
    - doc/打包安装方案.md
    - plugins/can-ids/plugin.json
    - plugins/_shared/dbcparse.py
    - plugins/_shared/isotp_client.py
    - installer/openbus.iss
---

## 1. 总体方案

OpenBus 是一个 C++ Qt6 桌面应用，同时通过 Python 插件系统扩展功能。依赖管理分为三条线并行：

- **C++ 第三方库**：采用源码级 vendoring（`third_party/`），由 `third_party/Dependencies.cmake` 统一以 CMake INTERFACE IMPORTED / STATIC 目标暴露，不引入外部包管理器。
- **Qt 与工具链**：通过 `scripts/build.py` 在本地路径查找 Qt6、MinGW、CMake、Ninja，并通过 `CMAKE_PREFIX_PATH` 注入 CMake；Windows 下用 `windeployqt` 部署运行时 DLL。
- **Python 插件运行时**：打包阶段将精简版 Python 解释器 + PyQt6 裁剪子集随安装包分发，版本锁定在 `requirements-lock.txt`，插件侧仅声明 `pip install PyQt6` 的注释提示。

## 2. C++ 第三方依赖（vendoring）

### 已 vendored 的库
| 库 | 位置 | 接入方式 |
|---|---|---|
| spdlog | `third_party/spdlog/` | INTERFACE IMPORTED 头文件库，仅设置 `INTERFACE_INCLUDE_DIRECTORIES` |
| nlohmann/json | `third_party/nlohmann_json/` | 单头文件，直接 `#include "json.hpp"` |
| qcustomplot | `third_party/qcustomplot/` | 编译为静态库 `qcustomplot`，链接 Qt6::Widgets / Qt6::PrintSupport |
| vector_blf | `third_party/vector_blf/` | 条件 `add_subdirectory()`，需存在其自身 CMakeLists.txt |
| pugixml | `third_party/pugixml/` | 目录存在（未在上述 Dependencies.cmake 显式声明，推测被其他模块引用） |
| dbcppp | `third_party/dbcppp/` 与 `third_party/dbcppp_src/` | 目录存在（DBC 解析核心） |
| concurrentqueue | `third_party/concurrentqueue/` | 头文件库 |

所有依赖集中在 `third_party/Dependencies.cmake`，根 `CMakeLists.txt` 第 73 行 `include(${CMAKE_SOURCE_DIR}/third_party/Dependencies.cmake)` 统一加载。该设计使新增第三方库只需在该文件追加目标定义，无需修改业务模块的 CMakeLists。

### 版本来源
- `third_party/dbcppp.zip` 存在于仓库，表明 dbcppp 通过 zip 压缩包形式纳入版本控制。
- 其余库（spdlog、nlohmann_json、qcustomplot、vector_blf、pugixml、concurrentqueue）以源码目录形式直接提交到 `third_party/`，属于典型的 git subtree / 手动拷贝式 vendoring。

## 3. 驱动与厂商 SDK 依赖

- `driver/kerneldlls/` 存放 ZLG 等厂商提供的二进制驱动 DLL（如 `zlgcan.dll`、`ZPSCANFD.dll`、`CANFDCOM.dll` 等）及 XML 设备描述文件。
- `drivers/<id>/driver.json` 声明每个驱动的元数据，其中 `vendorSdk.dll` 字段指明厂商 SDK 的相对路径，并标注 `bundled: false` —— 说明这些 DLL 不随安装包默认分发，需用户从 ZCANPRO 或官网获取（见 `drivers/zlg/driver.json` 中 license 注释）。
- 驱动插件本身是 C++ 动态库（`*.dll`），按 `build/bin/drivers/<id>/` 输出，由主程序在运行时按需加载。

## 4. Python 插件依赖管理

### 运行时捆绑策略
根据 `doc/打包安装方案.md`：
- 安装包内包含精简版 Python 解释器（含 `python314._pth` 隔离）+ 标准库裁剪后的 Lib。
- 插件若需要 GUI（目前只有 `uds-diagnostic`），额外捆绑 PyQt6 裁剪子集（QtCore/QtGui/QtWidgets/QtSvg + sip + Qt6/bin + platform/imageformats），体积约 43 MB，不与主程序 Qt 共享 DLL。
- 打包时生成 `runtime/python/Lib/site-packages/requirements-lock.txt`，锁定 PyQt6 / PyQt6-Qt6 / PyQt6-sip 精确版本，供 `importlib.metadata` 校验。
- 插件代码中的 `main.py` 顶部注释写 `依赖: pip install PyQt6`，这是给开发者本地调试用的提示，不是正式依赖声明机制。

### 插件间共享代码
`plugins/_shared/dbcparse.py`、`plugins/_shared/isotp_client.py` 作为多个插件共用的 DBC 解析和 ISO-TP 客户端实现，避免重复依赖。

## 5. 构建期依赖发现

`scripts/build.py` 集中管理构建工具链路径：
- Qt6：`SIN_QT_DIR` 环境变量或默认 `C:/Qt/6.8.3/mingw_64`，通过 `-DCMAKE_PREFIX_PATH` 传给 CMake。
- MinGW：`SIN_MINGW_DIR` 或 `C:/Qt/Tools/mingw1310_64`，提供 g++/gcc/gdb。
- CMake：`SIN_CMAKE_DIR` 或 `C:/tools/cmake-3.30.3-windows-x86_64`。
- Ninja：优先使用 `tools/ninja/ninja.exe`（本地自带），回退到 MinGW Makefiles。
- 部署：调用 `windeployqt <exe>` 自动收集 Qt 运行时 DLL，并手动补充 `Qt6PrintSupport.dll`（因 qcustomplot 静态库传递依赖未被 windeployqt 识别）。

## 6. 约束与约定

- **不使用外部 C++ 包管理器**：无 vcpkg、Conan、NuGet 等配置，全部依赖以源码形式驻留 `third_party/`。
- **Qt 版本固定**：硬编码 `C:/Qt/6.8.3/mingw_64`，通过环境变量覆盖。
- **编译器/链接器限制**：根 CMakeLists.txt 明确禁止使用 LLD（Windows 上导致文件锁问题）和 ccache（与 MinGW g++ 13 PCH 不兼容），强制使用 ld.bfd。
- **Python 插件不声明 requirements 文件**：依赖以注释形式记录在 `main.py` 头部，正式打包时由打包脚本拉取并锁定到 `requirements-lock.txt`。
- **厂商 SDK 不随包分发**：`driver.json` 中标注 `bundled: false`，要求用户自行安装对应厂商工具链。
- **PyQt6 与主程序 Qt 隔离**：插件进程内的 PyQt6 携带独立 Qt 副本，不与宿主 Qt 6.8.3 共享，避免版本冲突。

## 7. 关键文件

- `third_party/Dependencies.cmake` — C++ 第三方库的统一 CMake 接口
- `CMakeLists.txt`（根）— Qt 查找、第三方 include、子项目组织
- `scripts/build.py` — 工具链发现、CMake 配置、测试执行、windeployqt 部署
- `drivers/*/driver.json` — 驱动插件元数据与厂商 SDK 声明
- `doc/打包安装方案.md` — Python 运行时与 PyQt6 捆绑策略的权威文档
- `plugins/*/plugin.json` — Python 插件清单（当前不含依赖声明字段）
- `plugins/_shared/` — 插件间共享的 Python 模块
- `third_party/` — 所有 vendored C++ 源码
- `installer/openbus.iss` — Inno Setup 安装脚本（负责最终产物打包）
