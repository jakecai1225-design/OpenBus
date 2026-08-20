---
kind: build_system
name: CMake + Python 构建脚本驱动的 Qt6 多 DLL 模块化构建系统
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - drivers/CMakeLists.txt
    - third_party/Dependencies.cmake
    - tests/CMakeLists.txt
    - scripts/build.py
    - manual_compile.txt
---

## 1. 使用的系统与工具

- **构建系统**: CMake 3.21+，作为唯一顶层构建配置入口；生成器优先使用项目内嵌的 `tools/ninja/ninja.exe`（Ninja），回退到 MinGW Makefiles。
- **编译器与工具链**: Windows 平台固定为 MinGW GCC (g++/gcc 13.1 x64)，Qt 6.8.3 (mingw_64)；通过环境变量 `SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR` 覆盖默认路径。
- **打包/部署**: `windeployqt` 自动收集 Qt 运行时依赖，并手动补充 `Qt6PrintSupport.dll`（qcustomplot 静态库对 PrintSupport 的传递依赖）。
- **测试**: Qt Test (`QTEST_GUILESS_MAIN`)，通过 `ctest` 执行；每个测试套件是独立可执行进程，隔离 core 层单例状态。
- **Python 构建编排**: `scripts/build.py` 提供 `configure / build / run / debug / clean / rebuild / deploy / all / status / open / test` 子命令，封装 CMake 调用、环境 PATH 注入、运行中进程终止、并行编译等。

## 2. 关键文件

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 根工程：C++17、Qt AUTOMOC/UIC/RCC、输出目录 `build/bin`、版本 `0.1.0`、包含第三方依赖、添加 `src/drivers/tests` 子目录 |
| `src/CMakeLists.txt` | 定义全部目标：`openbus_data`(SHARED)、`openbus_market`/`openbus_transceive`/`openbus_dbc`/`openbus_flow`/`openbus_ui`(STATIC)、最终 `openbus` 可执行，以及各模块 PCH 头清单 |
| `drivers/CMakeLists.txt` | 驱动插件聚合：每个厂商子目录产出 `.odp` 安装单元，POST_BUILD 拷贝 `driver.json` |
| `third_party/Dependencies.cmake` | 声明 `spdlog`(INTERFACE)、`qcustomplot`(STATIC)、`vector_blf`(可选 subdirectory) 等三方依赖 |
| `tests/CMakeLists.txt` | 定义 `openbus_add_test()` 宏，链接 `openbus_data` + `Qt6::Test`，注册 `tests` 聚合目标 |
| `scripts/build.py` | 统一构建入口：检测 Ninja/MinGW、设置 PATH、调用 CMake、执行 windeployqt、启动 GDB、运行 ctest |
| `doc/构建基线.md` | 文档化构建环境与约束（见下文“约定与约束”） |

## 3. 架构与约定

### 3.1 模块化 DLL 拆分
主程序被拆分为多个共享库，通过 C 工厂函数暴露 ABI 契约（如 `openbus_createMarketModule()`、`openbus_createTransceiveModule()`、`openbus_createDbcModule()`、`openbus_createFlowModule()`），由 `main.cpp` 经 `ModuleRegistry` 动态加载。分层如下：
- `openbus_data`：公共底座 SHARED DLL，包含 core/models/utils/thememanager，所有 core 单例（AppConfig/DbcManager/PluginManager/ThemeManager/ModuleRegistry）保持全进程唯一实例。
- `openbus_market`、`openbus_transceive`、`openbus_dbc`、`openbus_flow`：业务功能 DLL，各自仅导出一个 C 工厂。
- `openbus_ui`：静态库，承载剩余 UI 组件，减少主程序重链开销。
- `openbus`：GUI 可执行（`WIN32_EXECUTABLE TRUE`，不弹出控制台窗口），链接上述所有 DLL。

### 3.2 驱动插件体系
`drivers/<vendor>/` 下每个驱动是一个独立的 CMake target，输出到 `build/bin/drivers/<id>/`，结构为 `driver_<id>.dll + driver.json [+ icon.svg/assets/vendor]`，由 `scripts/driver_tool.py` 打包成 `.odp` 安装单元。驱动 ABI 要求与主程序同 Qt 版本 + 同编译器（MinGW 13.1 x64, C++17）。

### 3.3 预编译头 (PCH)
每个目标通过 `target_precompile_headers(... PRIVATE ...)` 显式声明所用 Qt 头集合，按模块裁剪（core 层不含 Widget 头，UI 层包含完整 Widget 头），显著缩短编译时间。

### 3.4 构建类型与目录
支持 `Dev`/`Debug`/`Release`/`RelWithDebInfo`/`MinSizeRel` 五种构建类型；`Dev` 档使用 `-O1 -g1`，建议放在独立 `build-dev/` 目录与全量 Debug 并存，避免切换时全量重编。

### 3.5 第三方依赖管理
- 源码级嵌入：`third_party/` 下直接存放 spdlog、nlohmann_json、qcustomplot、pugixml、concurrentqueue、dbcppp、vector_blf 等。
- 通过 `add_library(... INTERFACE IMPORTED)` 或 `add_subdirectory` 引入，`if(EXISTS ...)` 条件判断保证部分依赖缺失时仍可配置。
- qcustomplot 以静态库形式编译并链接。

## 4. 约定与约束

- **必须使用 MinGW g++ 13.1 x64**：根 CMakeLists 注释明确禁用 ccache（与 MinGW g++ 13 的 PCH 不兼容，会静默崩溃）、禁用 LLD 链接器（Windows 上导致文件锁问题），只允许默认 `ld.bfd`。
- **构建前自动终止运行中的 `openbus.exe`**：`build.py` 在 build/run/debug 前调用 `taskkill /F /IM openbus.exe`，等待文件锁释放，避免链接失败。
- **Ninja 优先**：`scripts/build.py` 自动检测 `tools/ninja/ninja.exe`，若存在则使用 Ninja 生成器（比 MinGW Makefiles 快 2–3x）。
- **Qt 路径通过环境变量配置**：`SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR`，默认指向本地固定路径（`C:/Qt/6.8.3/mingw_64` 等）。
- **驱动 DLL 部署规则**：`src/CMakeLists.txt` 的 POST_BUILD 将 `driver/` 整个目录复制到 exe 同级目录，因为 ZLG SDK 的 `zlgcan.dll` 必须在同一目录才能找到 USB 驱动。
- **测试隔离**：每个测试套件是独立进程，工作目录设为 `${CMAKE_BINARY_DIR}/bin`，以便自动解析 `openbus_data` DLL。
- **thin archive**：MinGW 下启用 `ar qcT` 仅记录对象路径不复制内容，使静态库重打包降为毫秒级。
- **资源文件**：通过 Qt RCC (`resources.qrc`) 打包 SVG 图标与样式，无需外部资源路径。
- **安装规则**：`install(TARGETS openbus RUNTIME DESTINATION bin)`，驱动 DLL 通过 `install(DIRECTORY ... FILES_MATCHING PATTERN "*.dll")` 一并安装。

## 5. 典型构建流程

```bash
# 配置（首次自动检测 Ninja/MinGW/Qt）
python scripts/build.py configure --build-type Release

# 增量编译（自动跳过已编译目标）
python scripts/build.py build -j8

# 部署 Qt 运行时依赖
python scripts/build.py deploy

# 运行
python scripts/build.py run

# 运行 L1 集成测试
python scripts/build.py test
```

该构建系统围绕 CMake 组织，由 Python 脚本统一编排，采用多 DLL 模块化设计，并通过 PCH、thin archive、Ninja 等手段优化 Windows/MinGW 下的编译性能与稳定性。