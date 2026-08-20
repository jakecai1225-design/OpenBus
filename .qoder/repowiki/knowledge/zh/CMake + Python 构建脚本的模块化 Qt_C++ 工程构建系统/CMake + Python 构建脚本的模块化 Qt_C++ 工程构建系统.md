---
kind: build_system
name: CMake + Python 构建脚本的模块化 Qt/C++ 工程构建系统
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - drivers/CMakeLists.txt
    - scripts/build.py
    - third_party/Dependencies.cmake
    - tests/CMakeLists.txt
    - resources/resources.qrc
---

## 1. 使用的系统与工具

- **构建系统**: CMake 3.21+，生成器优先使用本地 `tools/ninja/ninja.exe`（Ninja），回退到 MinGW Makefiles。
- **编译器**: MinGW g++ 13.1 x64（路径由环境变量 `SIN_MINGW_DIR` 或默认 `C:/Qt/Tools/mingw1310_64` 指定），C++ 标准固定为 C++17，关闭扩展。
- **GUI 框架**: Qt6（Widgets、PrintSupport、Svg、Network），通过 `find_package(Qt6 REQUIRED ...)` 与 `qt_standard_project_setup()` 发现。
- **打包部署**: `windeployqt` 自动收集 Qt 运行时依赖；驱动 DLL 通过 `POST_BUILD` 命令复制到输出目录。
- **测试**: Qt Test + CTest，通过 `scripts/build.py test` 调用 `ctest --test-dir <build> --output-on-failure`。
- **构建编排**: 根级 Python 脚本 `scripts/build.py` 封装 configure / build / run / debug / clean / rebuild / deploy / all / status / open / test 子命令，统一处理环境 PATH、工具检测、增量 reconfigure 判断、并行 `-j` 编译等。

## 2. 关键文件与位置

| 角色 | 文件 | 说明 |
|---|---|---|
| 顶层 CMake | `CMakeLists.txt` | 定义项目名 `openbus`、版本 `0.1.0`、C++17、Qt6 查找、子目录、安装规则、MinGW 链接器/归档优化 |
| 源码 CMake | `src/CMakeLists.txt` | 拆分出 `openbus_data`（共享库）、`openbus_market/transceive/dbc/flow`（业务 DLL）、`openbus_ui`（静态库）、`openbus`（可执行）等目标，并配置 PCH、依赖、POST_BUILD 拷贝 driver DLL |
| 驱动 CMake | `drivers/CMakeLists.txt` | 每个驱动子目录产出独立 DLL，并通过 `deploy_driver_manifest` 将 `driver.json` 复制到 `<target>.dll` 同级目录 |
| 第三方依赖 | `third_party/Dependencies.cmake` | 集中引入 dbcppp、qcustomplot、spdlog、nlohmann_json、vector_blf、pugixml 等外部库 |
| 构建脚本 | `scripts/build.py` | 统一入口：自动检测 Ninja/MinGW/Qt/CMake/GDB，管理 `--build-dir`（支持 `build` 与 `build-dev` 并存），实现增量 reconfigure 与测试运行 |
| 测试 CMake | `tests/CMakeLists.txt` | 注册各 Qt Test 用例，暴露聚合目标 `tests` |
| 资源清单 | `resources/resources.qrc` | Qt 资源文件，随主程序一起编译 |

## 3. 架构与约定

### 3.1 多模块 DLL 拆分
`src/CMakeLists.txt` 将工程拆分为清晰的层次：
- `openbus_data`（共享库）：承载 core/models/utils/thememanager 等公共底座，所有单例（AppConfig、DbcManager、PluginManager、ThemeManager、ModuleRegistry…）在此保持进程唯一实例。
- 业务 DLL：`openbus_market`、`openbus_transceive`、`openbus_dbc`、`openbus_flow`，每个 DLL 导出一个 C 工厂函数（如 `openbus_createMarketModule()`），供壳通过 `ModuleRegistry` 动态加载。
- `openbus_ui`（静态库）：剩余 UI 组件，仅改 UI 时只重编译此库 + 最终链接，不影响业务 DLL。
- `openbus`（可执行）：链接上述所有 DLL，并在 Windows 下以 GUI 方式运行（`WIN32_EXECUTABLE TRUE`）。

### 3.2 驱动插件体系
`drivers/<vendor>/` 下每个厂商是一个独立 CMake target，输出 `.dll` 与 `driver.json` 到 `build/bin/drivers/<id>/`，与 `.odp` 包内布局一致。驱动通过 QPluginLoader 加载，ABI 契约要求与主程序同 Qt 版本 + 同编译器（MinGW 13.1 x64, C++17）。

### 3.3 构建类型与开发体验
- 支持 `Dev`、`Debug`、`Release`、`RelWithDebInfo`、`MinSizeRel` 五种构建类型。
- `Dev` 档使用 `-O1 -g1`，配合独立的 `build-dev/` 目录与全量 Debug `build/` 并存，避免切换构建类型导致全量重编。
- 通过 `cmake_needs_reconfigure()` 比较 CMakeLists.txt 与生成器主文件的 mtime，仅在必要时触发 reconfigure，避免每次全量重编。
- 构建前自动 `taskkill /F /IM openbus.exe` 释放文件锁，避免链接失败。

### 3.4 链接器与归档优化
- 强制使用默认 `ld.bfd` 链接器（注释明确拒绝 LLD/gold，因 PE 子系统选项不兼容或曾产出损坏可执行）。
- 启用 thin archive（`ar qcT/qT`）减少静态库重打包时间。
- 未启用 ccache（与 MinGW g++ 13 的 PCH 不兼容，会静默崩溃）。

### 3.5 预编译头 (PCH)
每个目标通过 `target_precompile_headers(... PRIVATE ...)` 显式声明所用 Qt/STL 头，按模块粒度裁剪 PCH 体积（data 层无 Widget 头，UI 层包含完整 QWidget/QMainWindow 等）。

### 3.6 资源与依赖部署
- `windeployqt` 自动复制 Qt 运行时，额外手动补充 `Qt6PrintSupport.dll`（qcustomplot 静态库对 PrintSupport 的传递依赖未被 windeployqt 识别）。
- `driver/` 目录下的 DLL（ZLG SDK 等）通过 `POST_BUILD` 命令复制到 exe 同级目录，满足 ZLG USB 驱动加载要求。
- 安装规则 `install(TARGETS openbus RUNTIME DESTINATION bin)` 用于打包分发。

## 4. 约定与约束

- **必须** 使用 C++17，禁止语言扩展（`CMAKE_CXX_STANDARD_REQUIRED ON` + `CMAKE_CXX_EXTENSIONS OFF`）。
- **必须** 在 Windows MinGW 环境下使用默认 `ld.bfd` 链接器，不得切换到 LLD/gold。
- **必须** 将驱动 DLL 与主程序放在同一目录（ZLG SDK 要求）。
- **必须** 每个驱动插件提供 `driver.json` 清单，否则 DriverRegistry 不会加载该插件。
- **建议** 新增模块时遵循“共享底座 → 业务 DLL（带 C 工厂）→ 壳链接”的拆分模式，并保持 ABI 最小化。
- **建议** 使用 `scripts/build.py` 而非直接调用 cmake，以获得统一的工具链管理、增量 reconfigure、并行编译和测试流程。
- **约束** 构建目录默认 `build/`，可通过 `--build-dir` 切换（推荐 `build-dev` 用于 Dev 档），脚本会自动更新可执行路径。
- **约束** 测试套件通过 `tests/CMakeLists.txt` 注册后，统一用 `python scripts/build.py test` 构建聚合目标 `tests` 并执行 ctest，失败时输出完整 stdout/stderr。

## 5. 适用性结论

本仓库存在完整的 CMake + Python 构建脚本 + Qt 部署 + 测试集成体系，覆盖编译、增量构建、DLL 拆分、驱动插件打包、Qt 运行时部署与 ctest 测试全流程，因此本类别完全适用。
