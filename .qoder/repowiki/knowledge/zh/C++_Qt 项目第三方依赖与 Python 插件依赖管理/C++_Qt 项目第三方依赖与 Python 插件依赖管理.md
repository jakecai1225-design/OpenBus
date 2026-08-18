---
kind: dependency_management
name: C++/Qt 项目第三方依赖与 Python 插件依赖管理
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - third_party/Dependencies.cmake
    - scripts/build.py
    - scripts/driver_tool.py
    - plugins/blf-converter/plugin.json
    - plugins/uds-diagnostic/plugin.json
    - plugins/uds-diagnostic/main.py
    - scripts/sin_host.py
---

## 1. 系统与方法

本项目采用 **CMake + 源码级 vendoring（third_party 目录）** 的 C++ 依赖管理模式，配合 **Python 插件体系**（plugins/*）各自声明运行时依赖。构建流程由 `scripts/build.py` 统一编排，通过环境变量 `SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR` 定位 Qt6、MinGW、CMake 等外部工具。

- C/C++ 层：无包管理器（如 vcpkg/conan），所有第三方库以源码形式直接放入 `third_party/`，通过 `third_party/Dependencies.cmake` 暴露为 CMake target 或 INTERFACE include 路径。
- Python 层：插件是独立 Python 模块，每个插件目录含 `plugin.json` 描述元数据，依赖通过 `pip install PyQt6` 等提示在运行期按需安装；宿主脚本 `scripts/sin_host.py` 在启动时检测 PyQt6 并给出安装提示。
- 驱动扩展：通过自定义 `.odp`（ZIP）格式分发，由 `scripts/driver_tool.py` 打包/安装/校验，使用 `CHECKSUMS.sha256` 保证完整性。

## 2. 关键文件

- `CMakeLists.txt`：顶层 CMake 入口，设置 C++17、Qt6 AUTOMOC/UIC/RCC、`find_package(Qt6)`，并 `include(third_party/Dependencies.cmake)`。
- `src/CMakeLists.txt`：定义 `openbus_core`（静态库）、`openbus_ui`（静态库）、`openbus`（可执行目标），并通过 `target_include_directories` / `target_link_libraries` 将 third_party 头文件和库链接到对应 target。
- `third_party/Dependencies.cmake`：集中声明第三方依赖——spdlog（INTERFACE IMPORTED）、nlohmann_json（单头文件）、qcustomplot（编译为 STATIC lib）、vector_blf（add_subdirectory 引入）。
- `third_party/`：实际 vendored 源码目录，包含 spdlog、nlohmann_json、concurrentqueue、pugixml、qcustomplot、dbcppp、vector_blf 等。
- `scripts/build.py`：构建脚本，封装 configure/build/run/debug/clean/rebuild/deploy/all/status/open 命令，自动选择 Ninja 或 MinGW Makefiles 生成器，调用 windeployqt 部署 Qt 运行时。
- `scripts/driver_tool.py`：驱动包（.odp）打包/安装/卸载/校验工具，强制 SHA256 校验清单。
- `plugins/*/plugin.json`：插件元数据（name、version、author、main、icon、activationEvents、contributes.commands）。
- `plugins/*/main.py`：各插件入口，统一通过 `sin.output.append("...需要 PyQt6: pip install PyQt6")` 提示用户安装依赖。

## 3. 架构与约定

- **vendoring 策略**：第三方库源码直接提交到仓库 `third_party/<lib>/`，不依赖 git submodule 或远程 fetch。`Dependencies.cmake` 用 `if(EXISTS ...)` 条件判断是否启用某依赖（如 qcustomplot、vector_blf、pugixml），使可选依赖可插拔。
- **头文件隔离**：core 层通过 `target_include_directories(openbus_core PUBLIC ...)` 把 third_party 头路径暴露给 UI 层，但仅对真正需要的库（spdlog、nlohmann_json、concurrentqueue、pugixml）显式添加，避免污染全局命名空间。
- **链接最小化**：qcustomplot 仅在 `openbus_ui` PRIVATE 链接（因为 graphicview.h 使用前向声明），zlib 和 vector_blf 也是 PRIVATE 链接，遵循“谁用谁链”的原则。
- **构建加速**：优先使用本地 `tools/ninja/ninja.exe`，回退到 MinGW Makefiles；配置阶段自动检测并打印使用的生成器。PCH 预编译头按 core/ui 分层分别维护，减少重编译时间。
- **Qt 运行时部署**：`windeployqt` 后手动补充 `Qt6PrintSupport.dll`（因 qcustomplot 静态库未触发传递依赖），确保发布包可独立运行。
- **驱动扩展机制**：驱动 DLL 随主程序输出目录一起拷贝（POST_BUILD copy_directory），或通过 `.odp` 包动态安装到 drivers 目录，加载前校验 `driver.json` 与 `CHECKSUMS.sha256`。
- **Python 插件生命周期**：插件以 ZIP 包形式分发，宿主通过 `plugin.json` 发现命令并注册到 UI；插件运行期若缺少 PyQt6，会返回友好错误信息而非崩溃。

## 4. 约定与约束

- **C++ 标准固定**：顶层 CMakeLists 强制 `CMAKE_CXX_STANDARD 17`，禁止扩展（`CMAKE_CXX_EXTENSIONS OFF`）。
- **禁用 ccache 与 LLD**：注释明确说明 ccache 与 MinGW g++ 13 的 PCH 不兼容会静默崩溃，LLD 在 Windows 上导致文件锁问题，因此不使用。
- **Qt 版本锁定**：通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg)` 查找，构建脚本默认 `SIN_QT_DIR=C:/Qt/6.8.3/mingw_64`，实际 Qt 版本由开发者环境决定。
- **第三方库来源记录**：`Dependencies.cmake` 中 vector_blf 附带 GitHub 来源注释（Technica-Engineering/vector_blf, GPL-3.0），便于合规追踪。
- **插件依赖声明**：每个插件的 `plugin.json` 必须包含 name/version/author/main/icon/activationEvents/contributes.commands，缺失会导致插件无法被宿主正确加载。
- **驱动包完整性**：`.odp` 包必须包含 `driver.json` 和 `CHECKSUMS.sha256`，安装过程两次校验（解压后、落盘后），失败则回滚。
- **无 Python 依赖清单**：项目中不存在 `requirements.txt`、`pyproject.toml`、`setup.py`、`poetry.lock` 等 Python 依赖声明文件；PyQt6 依赖通过插件代码中的字符串提示用户手动 `pip install`。
- **构建工具路径可覆盖**：通过 `--qt-dir`、`--mingw-dir`、`--cmake-dir` 命令行参数或 `SIN_*_DIR` 环境变量覆盖默认路径，支持多环境并行开发。