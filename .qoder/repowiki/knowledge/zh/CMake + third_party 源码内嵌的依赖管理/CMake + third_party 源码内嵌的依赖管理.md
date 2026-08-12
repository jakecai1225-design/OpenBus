---
kind: dependency_management
name: CMake + third_party 源码内嵌的依赖管理
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - third_party/Dependencies.cmake
    - src/CMakeLists.txt
    - third_party/vector_blf/CMakeLists.txt
    - third_party/dbcppp/CMakeLists.txt
    - third_party/qcustomplot/qcustomplot.cpp
    - third_party/spdlog/include/spdlog/spdlog.h
    - third_party/nlohmann_json/nlohmann/json.hpp
    - third_party/concurrentqueue/concurrentqueue.h
    - third_party/pugixml/pugixml.hpp
    - driver/zlgcan.dll
    - driver/zlgcan.h
---

## 1. 使用的系统/方法

本项目采用 **CMake 构建系统**（要求 CMake ≥ 3.21，C++17）配合 **third_party 目录源码内嵌（vendoring）** 的方式管理第三方依赖。没有使用任何包管理器（如 vcpkg、Conan、NuGet），也没有 lockfile；所有第三方库以源代码形式直接提交到仓库 `third_party/` 下，通过 CMake 的 `add_subdirectory` / `INTERFACE IMPORTED` / `target_include_directories` 集成。

Qt6 通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg)` 从系统或 Qt 安装路径查找，属于“外部依赖”，其余 C++ 依赖全部 vendored。

Python 侧（`plugins/`、`sdk/sin/`、`scripts/`）未检测到 `requirements.txt`、`pyproject.toml`、`setup.py` 等依赖声明文件，插件仅通过 `plugin.json` 描述元数据，不声明 Python 包依赖。

## 2. 关键文件

- `CMakeLists.txt`：顶层工程入口，定义项目版本、Qt6 查找、包含 `third_party/Dependencies.cmake`、添加 `src/` 子目录。
- `third_party/Dependencies.cmake`：集中声明 vendored 依赖的目标（spdlog INTERFACE、qcustomplot STATIC、vector_blf 条件子目录）。
- `src/CMakeLists.txt`：将依赖头文件路径以 `PUBLIC` 暴露给 `sin_core`，并以 `PRIVATE` 链接 `z`、`vector_blf`、`pugixml` 等库；还负责拷贝 `driver/` 下的 DLL 到输出目录。
- `third_party/dbcppp/CMakeLists.txt`：dbcppp 自身是独立 CMake 工程，依赖 Boost 与 LibXml2（需由宿主环境提供）。
- `third_party/vector_blf/CMakeLists.txt`：对 vector_blf 做静态库适配，硬编码 MinGW zlib 路径作为回退。
- `third_party/concurrentqueue/`、`third_party/nlohmann_json/`、`third_party/pugixml/`、`third_party/spdlog/`、`third_party/qcustomplot/`：纯源码 vendored。
- `driver/`：预编译的 ZLG USB-CAN SDK DLL（`zlgcan.dll`、`*.dll`、`*.lib`、`*.h`）随二进制分发。

## 3. 架构与约定

- **分层目标**：`sin_core`（核心静态库）、`sin_ui`（UI 静态库）、`sin`（可执行）。依赖头文件通过 `target_include_directories(... PUBLIC ...)` 自 `sin_core` 向上传递，实现“单点声明、下游继承”的依赖传播。
- **条件启用**：对可选依赖使用 `if(EXISTS ...)` 判断源码是否存在再添加 include 路径或链接目标，使仓库在缺少某个 vendored 子模块时仍可部分构建。
- **vendor 策略差异**：
  - 单头文件库（nlohmann/json、concurrentqueue）：只暴露 include 目录。
  - 源码库（qcustomplot、spdlog、pugixml）：通过 `INTERFACE_INCLUDE_DIRECTORIES` 或 `target_include_directories` 暴露。
  - 完整 CMake 子项目（vector_blf、dbcppp）：直接 `add_subdirectory` 纳入构建。
- **外部依赖**：Qt6、Boost、LibXml2、zlib、Threads 通过 `find_package` 获取；其中 vector_blf 的 zlib 在找不到时回退到硬编码的 MinGW 路径，说明该回退是平台/工具链绑定的。
- **运行时 DLL 分发**：`driver/` 目录中的 ZLG SDK DLL 在 `POST_BUILD` 阶段复制到 exe 同级目录，并写入 `install()` 规则，保证运行期可加载。

## 4. 约定与约束

- **所有 C++ 第三方依赖必须放入 `third_party/` 并以源码形式提交**，不得通过包管理器自动下载；新增依赖需在 `third_party/Dependencies.cmake` 或对应子目录的 CMakeLists 中注册目标，并在 `src/CMakeLists.txt` 中添加 include/link。
- **Qt 版本锁定为 Qt6**，且仅启用 `Widgets`、`PrintSupport`、`Svg` 组件；C++ 标准强制 C++17 且关闭扩展。
- **PCH 预编译头**按层维护（`sin_core` 精简 Qt 头、`sin_ui` 完整 Widget 头），新增公共头应加入对应层的 `target_precompile_headers` 列表。
- **可选依赖必须用 `if(EXISTS ...)` 包裹**，避免缺失 vendored 源码导致配置失败。
- **Windows 平台限制**：注释明确不使用 ccache（与 MinGW g++ 13 PCH 不兼容）和 LLD 链接器（Windows 上文件锁问题），使用默认 `ld.bfd`。
- **驱动 DLL 不可修改**：`driver/` 下的 `.dll/.lib/.h` 作为预编译二进制随仓库分发，构建脚本仅复制，不重新编译。
- **无 Python 依赖清单**：插件 `plugin.json` 仅描述名称/版本/入口，未声明 Python 包依赖，因此 Python 依赖由运行环境自行保证。
- **dbcppp 仍保留其内部 `third_party/libxmlmm` 及 `find_package(Boost 1.72.0 REQUIRED)`**，意味着引入 dbcppp 的子模块仍需宿主环境提供 Boost 与 LibXml2，这是当前 vendoring 的不完整之处。