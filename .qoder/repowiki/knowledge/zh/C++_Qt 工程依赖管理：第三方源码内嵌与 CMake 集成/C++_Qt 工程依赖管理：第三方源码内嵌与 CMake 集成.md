---
kind: dependency_management
name: C++/Qt 工程依赖管理：第三方源码内嵌与 CMake 集成
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - third_party/Dependencies.cmake
    - scripts/build.py
    - src/CMakeLists.txt
    - driver/kerneldlls/dll_cfg.ini
    - driver/zlgcan.h
    - plugins/_shared/dbcparse.py
    - plugins/can-dashboard/plugin.json
    - third_party/dbcppp/CMakeLists.txt
---

## 1. 使用的系统与工具

- **构建系统**：CMake 3.21+（根 `CMakeLists.txt`），配合 MinGW g++ 13、Ninja（本地 `tools/ninja/`）。
- **第三方库策略**：**源码级 vendoring（内嵌）**，所有 C++ 第三方库直接放入 `third_party/` 目录，通过 `third_party/Dependencies.cmake` 以 `INTERFACE IMPORTED` / `add_library` / `add_subdirectory` 方式暴露给主工程。
- **包管理器**：C++ 层不使用任何包管理器（无 vcpkg/conan/spm），Python 插件层也未使用 `requirements.txt`/`pyproject.toml`，而是以运行时提示 `pip install PyQt6` 的方式声明可选依赖。
- **Qt 依赖**：通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg Network)` 查找系统已安装的 Qt6（默认路径由 `scripts/build.py` 中的 `DEFAULT_QT_DIR = Path(os.environ.get("SIN_QT_DIR", "C:/Qt/6.8.3/mingw_64"))` 控制），部署阶段调用 `windeployqt.exe` 收集运行时 DLL。

## 2. 关键文件与位置

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 项目根 CMake 入口，启用 Qt AUTOMOC/UIC/RCC，include `third_party/Dependencies.cmake` |
| `third_party/Dependencies.cmake` | 集中声明所有第三方依赖的 CMake target |
| `third_party/spdlog/` | spdlog 头文件库（单头 INTERFACE 目标） |
| `third_party/nlohmann_json/` | nlohmann/json 单头文件库 |
| `third_party/qcustomplot/` | QCustomPlot 源码，编译为静态库并链接 Qt6::Widgets/PrintSupport |
| `third_party/vector_blf/` | Vector BLF 读写库（GPL-3.0），按存在性条件 `add_subdirectory` |
| `third_party/dbcppp/` | DBC 解析库（完整 git 子仓库，含 `.gitmodules`） |
| `third_party/pugixml/` | XML 解析库 |
| `third_party/concurrentqueue/` | 无锁队列头文件库 |
| `src/CMakeLists.txt` | 各模块对第三方库的 `target_link_libraries` 引用 |
| `scripts/build.py` | 构建脚本，管理 Qt/MINGW/CMAKE/Ninja 路径，执行 `windeployqt` 打包运行期依赖 |
| `plugins/*/plugin.json` | Python 插件元数据，仅声明 name/version/main/icon，不声明 pip 依赖 |
| `plugins/_shared/dbcparse.py` | 自包含纯 Python DBC 解析器，避免插件再引入 dbcppp |

## 3. 架构与约定

### 3.1 C++ 第三方库分层

`third_party/Dependencies.cmake` 将不同形态的第三方库统一抽象为 CMake target：

- **头文件-only 库**（spdlog、nlohmann_json、concurrentqueue、pugixml）：通过 `add_library(... INTERFACE IMPORTED)` + `INTERFACE_INCLUDE_DIRECTORIES` 暴露，零链接开销。
- **源码静态库**（qcustomplot）：`add_library(qcustomplot STATIC ...)` 并显式 `target_link_libraries(qcustomplot PUBLIC Qt6::Widgets Qt6::PrintSupport)`，上层只需 link qcustomplot 即可继承 Qt 依赖。
- **可选项库**（vector_blf）：用 `if(EXISTS ...)` 包裹 `add_subdirectory`，缺失时输出 WARNING 并禁用 BLF 支持，保证构建鲁棒性。

### 3.2 驱动层二进制依赖

`driver/kerneldlls/` 下存放 ZLG 等厂商提供的 `.dll/.lib` 及 `ZPSCANFD_IMPL.dll`、`base.dll`、`dataset.dll`、`utils.dll` 等运行时组件；`driver/zlgcan.h`/`.lib` 提供 C API。这些是**预编译二进制**，随工程分发，不属于源码 vendoring。

### 3.3 Python 插件依赖

- 每个插件目录含 `main.py` + `plugin.json`，插件之间通过共享的 `plugins/_shared/dbcparse.py`、`isotp_client.py` 复用逻辑，避免重复引入第三方库。
- 插件对 PyQt6 的依赖采用**运行时检测 + 用户提示**模式（如 `sin.output.append("仪表盘插件需要 PyQt6: pip install PyQt6")`），而非安装期强制。
- 没有 `requirements.txt`、`pyproject.toml`、`setup.py` 或虚拟环境锁定机制——Python 依赖完全交由用户自行 `pip install`。

### 3.4 构建与部署流程

`scripts/build.py` 统一管理工具链：
- 通过环境变量 `SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR` 覆盖默认路径。
- 自动检测 `tools/ninja/ninja.exe` 作为构建后端。
- 构建后通过 `windeployqt` 收集 Qt 运行时 DLL。
- Dev/Debug/Release/RelWithDebInfo/MinSizeRel 多构建类型并存（Dev 档 `-O1 -g1`，独立 `build-dev/` 目录）。

## 4. 约定与约束

- **禁止外部包管理器**：C++ 层不使用 vcpkg/conan/spm，所有依赖必须手动放入 `third_party/` 并在 `Dependencies.cmake` 中注册，否则无法被 find_package/target 发现。
- **头文件-only 优先**：spdlog、nlohmann_json、pugixml、concurrentqueue 均以单头形式 vendored，避免额外链接步骤。
- **可选依赖降级**：vector_blf 未检出时仅警告并禁用功能，不阻断构建。
- **Qt 版本固定**：默认指向 `C:/Qt/6.8.3/mingw_64`，需通过 `SIN_QT_DIR` 切换。
- **链接器约束**：Windows + MinGW 场景强制使用默认 `ld.bfd`，禁用 LLD（曾产出损坏可执行）与 ccache（与 PCH 不兼容会静默崩溃），见根 `CMakeLists.txt` 注释。
- **thin archive**：MinGW 下启用 `ar qcT`/`qT` thin archive 加速静态库重打包，避免 Windows 命令行长度限制。
- **插件隔离**：Python 插件不共享全局 site-packages，依赖通过 `pip install` 安装到宿主 Python 环境，插件间通过 `_shared/` 目录共享纯 Python 代码。
- **vendor 更新方式**：替换 `third_party/<lib>/` 目录内容后重新 CMake configure，无需修改 `Dependencies.cmake`（头文件-only 库仅需头路径正确）。

## 5. 现状评估

该仓库对 C++ 依赖采用了成熟的**源码 vendoring + CMake 集成**方案，适合封闭交付的桌面工具；但对 Python 插件依赖缺乏锁定机制（无 `requirements.txt`/`poetry.lock`），升级 PyQt6 或其他 Python 库时可能出现跨插件不一致。若未来插件生态扩大，建议引入 `pyproject.toml` + `uv`/`pip-tools` 锁定 Python 依赖版本。