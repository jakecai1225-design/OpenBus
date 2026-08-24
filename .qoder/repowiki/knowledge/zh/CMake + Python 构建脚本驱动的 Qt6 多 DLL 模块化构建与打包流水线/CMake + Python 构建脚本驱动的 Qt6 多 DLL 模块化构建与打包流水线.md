---
kind: build_system
name: CMake + Python 构建脚本驱动的 Qt6 多 DLL 模块化构建与打包流水线
category: build_system
scope:
    - '**'
---

## 1. 构建系统总览

OpenBus 采用 **CMake 3.21+** 作为核心构建系统，配合自研的 **Python 构建/打包脚本**（`scripts/build.py`、`scripts/package.py`）完成配置、编译、部署、测试与发布。目标平台为 Windows（MinGW g++ 13.1 x64），使用 Qt6（Widgets/Svg/Network/Test）构建桌面应用。

项目通过 CMake 将代码拆分为多个共享库（DLL）和静态库，最终链接为 `openbus.exe` 可执行文件：
- `openbus_data`（SHARED）— 公共底座，包含 core/models/utils/thememanager，所有业务 DLL 依赖它
- `openbus_market` / `openbus_transceive` / `openbus_dbc` / `openbus_flow` / `openbus_trace` / `openbus_graphic`（均为 SHARED）— 按功能拆分业务模块，每个模块导出一个 C 工厂函数供壳动态加载
- `openbus_ui`（STATIC）— 壳 UI 静态库，仅编译期依赖 qcustomplot
- 驱动插件位于 `drivers/<vendor>/`，每个输出独立 DLL（如 `driver_zlg.dll`）+ `driver.json`，由主程序通过 QPluginLoader 外置加载

Qt 自动化处理（MOC/UIC/RCC）全部启用；C++ 标准固定为 C++17，禁用扩展。

## 2. 关键文件与目录

- `CMakeLists.txt` — 根工程定义，设置版本 0.1.0、Qt6 查找、子目录、安装规则
- `src/CMakeLists.txt` — 核心构建逻辑：源文件分组、各 DLL target、PCH 预编译头、POST_BUILD 拷贝 driver/sdk/scripts
- `drivers/CMakeLists.txt` — 驱动插件聚合，调用 `deploy_driver_manifest` 在每个驱动 DLL 后拷贝 `driver.json`
- `tests/CMakeLists.txt` — L1 集成测试（Qt Test，每个套件独立 exe）+ L2 offscreen UI 测试，统一通过 `add_test` 注册到 ctest
- `third_party/Dependencies.cmake` — 第三方依赖声明：spdlog（INTERFACE）、qcustomplot（STATIC）、vector_blf（可选 subdirectory）
- `scripts/build.py` — 开发用构建入口，支持 configure/build/run/debug/clean/rebuild/deploy/all/test/status/open 等子命令，自动检测 Ninja/MinGW/Qt/CMake
- `scripts/package.py` — Release 打包流水线：构建 → windeployqt → staging 组装 → 捆绑 Python 运行时 → 依赖完整性校验（objdump import 表）→ zip 便携版 + Inno Setup 安装器
- `installer/openbus.iss` — Inno Setup 安装脚本（可选，iscc 不存在时跳过）

## 3. 架构与约定

### 3.1 构建类型与并行构建
- 默认构建类型 `Debug`，额外支持 `Dev`（-O1 -g1，独立 `build-dev/` 目录，与全量 Debug 并存避免切档重编）、`Release`、`RelWithDebInfo`、`MinSizeRel`
- 优先使用 `tools/ninja/ninja.exe` 作为构建调度器（比 MinGW Makefiles 快 2-3x），未找到时回退到 `MinGW Makefiles` 生成器
- 并行任务数默认取 CPU 核心数，可通过 `-j` 指定

### 3.2 工具链与环境
- 工具路径通过环境变量覆盖：`SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR`，默认指向 `C:/Qt/...` 常见位置
- PATH 注入顺序：`cmake/bin` → `mingw/bin` → `qt/bin`（Ninja 存在时前置其目录），确保 cc1plus.exe 能找到 libgcc_s_seh-1.dll 等运行时
- 明确禁用 ccache（与 MinGW g++ 13 的 PCH 不兼容会静默崩溃）和 LLD 链接器（Windows 上导致文件锁问题），强制使用默认 `ld.bfd`
- 使用 thin archive（`ar qcT/qT`）减少静态库重打包时间

### 3.3 模块拆分与 ABI 契约
- 业务模块通过 C 工厂函数暴露（如 `openbus_createMarketModule()`、`openbus_createTransceiveModule()` 等），壳在 `main.cpp` 中通过 ModuleRegistry 动态注册
- 所有 core 层单例（AppConfig/DbcManager/PluginManager/ThemeManager/ModuleRegistry）集中在 `openbus_data` DLL，保证全进程唯一实例
- 每个业务 DLL 仅依赖 `openbus_data` + 对应 Qt 组件，形成清晰的单向依赖图

### 3.4 测试体系
- L1 测试：每个测试套件是独立可执行进程（隔离 core 层单例状态），链接 `openbus_data` + `Qt6::Test`，通过 `ctest` 运行
- L2 测试：`test_ui_offscreen` 复刻 main.cpp 初始化链，驱动真实 MainWindow，通过 `QT_QPA_PLATFORM=offscreen` 无头运行
- 聚合目标 `tests` 仅构建测试相关目标，避免触发主程序大 exe 的重链接

### 3.5 部署与打包
- 开发态：`windeployqt --release --no-translations --compiler-runtime` 部署 Qt 运行时，手动补拷 `Qt6PrintSupport.dll`（qcustomplot 静态库传递依赖）和 `translations/qtbase_zh_CN.qm`
- 发布态：`scripts/package.py` 完整流水线产出 `dist/openbus-<ver>-win64-portable.zip` 和可选的 Inno Setup 安装器
- staging 白名单机制：仅拷贝 `BIN_FILES`（openbus.exe、libopenbus_*.dll、Qt6*.dll、MinGW 运行时等）和 `BIN_DIRS`（platforms/imageformats/iconengines/styles/tls/networkinformation）
- Python 运行时捆绑：从本机安装版 Python 精简拷贝解释器核心、Lib/DLLs/site-packages（剔除 tkinter/idlelib 等），通过 `pythonXY._pth` 实现隔离路径，并裁剪 PyQt6 子集（QtCore/Gui/Widgets/Svg）
- 依赖完整性校验：通过 `objdump -p` 提取每个 exe/dll/pyd 的 import 表，逐一核对是否在 staging 内或系统 DLL 白名单，缺失即失败

### 3.6 资源与驱动随包策略
- `driver/`（ZLG SDK 运行时）不随包，用户需安装 ZCANPRO；`drivers/`（.odp 外置驱动）一律从市场安装，staging 不预装
- `sdk/` 与宿主脚本（`sin_host.py`、`plugin_tool.py`、`driver_tool.py`）通过 POST_BUILD 拷贝到输出目录，供插件系统运行时使用
- 驱动插件每个 vendor 子目录自带 `driver.json`，构建后通过 `deploy_driver_manifest` 部署到输出目录

## 4. 约束与约定

- **编译器锁定**：MinGW g++ 13.1 x64 + Qt6，C++17 标准，禁止非标准扩展
- **链接器锁定**：必须使用默认 `ld.bfd`，禁止 gold/LLD（PE 子系统选项不兼容或产出不稳定）
- **构建目录隔离**：`build/`（Debug）、`build-dev/`（Dev 快速档）、`build-rel/`（Release 打包用）三套互不干扰
- **Qt 自动化**：AUTOMOC/AUTOUIC/AUTORCC 全局开启，`.ui`/`.qrc`/含 `Q_OBJECT` 的头文件无需手工处理
- **PCH 策略**：每个 DLL target 单独定义 `target_precompile_headers`，core 层使用精简 Qt 头列表（不含 Widget），UI 层使用完整 Widget 头列表
- **测试隔离**：每个测试套件必须是独立进程，不能共享 core 层单例状态
- **打包门禁**：`package.py` 的 objdump import 校验是硬性门禁，任何缺失依赖都会导致打包失败
- **驱动 ABI 一致性**：驱动 DLL 必须与主程序同 Qt 版本 + 同编译器（MinGW 13.1 x64, C++17），否则 QPluginLoader 无法加载
- **版本号来源**：版本字符串来自根 `CMakeLists.txt` 的 `project(... VERSION 0.1.0)`，打包脚本通过读取 `CMakeCache.txt` 中的 `openbus_VERSION` 获取