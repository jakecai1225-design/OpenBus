---
kind: build_system
name: CMake + Python 构建/打包流水线（Qt6 多 DLL 架构）
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - drivers/CMakeLists.txt
    - tests/CMakeLists.txt
    - scripts/build.py
    - scripts/package.py
    - third_party/Dependencies.cmake
---

## 1. 构建系统与工具链

项目采用 **CMake 3.21+** 作为核心构建系统，配合 **Python 脚本** 封装开发工作流与发布流水线。编译器为 **MinGW g++ 13.1 x64 (C++17)**，Qt 版本固定为 **Qt6**（Widgets / PrintSupport / Svg / Network / Test）。构建生成器优先使用本地 `tools/ninja/ninja.exe`（加速 2-3x），回退到 MinGW Makefiles。

关键工具链通过环境变量注入：`SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR`，由 `scripts/build.py` 的 `Environment` 类统一探测并前置 PATH。链接器强制使用默认 `ld.bfd`（禁用 LLD/gold，避免 PE 文件锁与子系统参数问题）；静态库归档启用 thin archive（`ar qcT/qT`）以将重打包降为毫秒级。

## 2. 核心 CMake 工程结构

根 `CMakeLists.txt` 定义项目名 `openbus`、版本 `0.1.0`、C++17 标准、Qt AUTOMOC/AUTOUIC/AUTORCC，并通过 `third_party/Dependencies.cmake` 引入第三方依赖。子目录组织：
- `src/` — 主程序与业务 DLL
- `drivers/` — 外置驱动插件（QPluginLoader 加载）
- `tests/` — Qt Test 集成测试

### 多 DLL 拆分架构（B1-B5 拆分方案）
`src/CMakeLists.txt` 将应用拆分为多个共享库（DLL），每个 DLL 暴露一个 C 工厂函数供壳动态注册：
- `openbus_data`（SHARED）— 公共底座：core/models/utils/thememanager，所有单例（AppConfig/DbcManager/PluginManager/ThemeManager/ModuleRegistry…）在此保持进程唯一实例
- `openbus_market`（SHARED）— 插件市场页
- `openbus_transceive`（SHARED）— 发送/回放/离线分析/录制四页面
- `openbus_dbc`（SHARED）— DBC 详情页 + 信号清单工具
- `openbus_flow`（SHARED）— 测量流程配置 + 设备连接页
- `openbus_trace`（SHARED）— Trace 三栏视图
- `openbus_graphic`（SHARED）— qcustomplot 曲线图
- `openbus_ui`（STATIC）— 壳 UI 静态库（MainWindow 等剩余界面组件，thin archive 仅编译期依赖）
- `openbus`（qt_add_executable）— 可执行目标，链接上述全部 DLL

每个 DLL 均声明独立的 `target_precompile_headers` 头清单，按模块实际使用的 Qt 头裁剪 PCH 体积。

## 3. 驱动插件构建

`drivers/CMakeLists.txt` 为每个厂商（zlg/peak/kvaser/slcan/candle）输出独立 DLL 到 `build/bin/drivers/<id>/`，并通过 `deploy_driver_manifest` 在 POST_BUILD 阶段复制 `driver.json` 清单。这些 `.odp` 包由 `DriverRegistry` 扫描加载，ABI 契约要求与主程序同 Qt 版本 + 同编译器。

## 4. 开发构建脚本 `scripts/build.py`

提供统一 CLI 接口：`configure` / `build` / `run` / `debug` / `clean` / `rebuild` / `deploy` / `all` / `status` / `test` / `open`。

关键特性：
- **Dev 快速档**：支持 `--build-type Dev --build-dir build-dev`，使用 `-O1 -g1` 独立构建目录，与全量 Debug (`build/`) 并存避免切档重编
- **自动 kill 运行中进程**：构建前调用 `taskkill /F /IM openbus.exe` 释放文件锁
- **增量 reconfigure**：比较 `CMakeLists.txt` mtime 与生成器文件，仅在必要时重新配置
- **windeployqt 部署**：自动补拷 `Qt6PrintSupport.dll`（qcustomplot 传递依赖）、创建空 `lib/fonts` 目录消解 QFontDatabase 告警
- **ctest 集成**：`python scripts/build.py test` 仅构建 `tests` 聚合目标，不触发主程序重链

## 5. 发布打包流水线 `scripts/package.py`

一条命令产出 Windows 免依赖分发产物，步骤（幂等可重复）：
1. 配置并构建 Release（独立 `build-rel/` 目录，复用 `build.py`）
2. `windeployqt --release --no-translations --compiler-runtime` 部署 Qt 运行时
3. 组装 staging `dist/stage/openbus/`（白名单拷贝：`openbus.exe`、`libopenbus_*.dll`、`Qt6*.dll`、MinGW 运行时、Qt 平台/图像/样式插件）
4. 捆绑 Python 运行时（精简拷贝本机安装版 Python + PyQt6 裁剪子集 QtCore/Gui/Widgets/Svg + Qt6 运行时 + `pythonXY._pth` 隔离 sys.path）
5. 附加 `THIRD_PARTY_NOTICES.md` / `LICENSE.txt` / `README-PORTABLE.txt`
6. 依赖完整性校验：`objdump -p` 提取每个 exe/dll/pyd 的 import 表，逐一核对 staging 内或系统白名单（含 `api-ms-*`/`ext-ms-*` 放行），缺失即 FAIL
7. 体积报告 → `dist/package-report.txt`
8a. 便携版 zip → `dist/openbus-<ver>-win64-portable.zip`
8b. Inno Setup 安装器（可选，iscc 不存在则跳过）

**策略约束**：`driver/`（ZLG SDK）与 `drivers/`（.odp 外置驱动）均不随包——用户通过 ZCANPRO 或市场安装；`market/` 本地索引随包兜底离线安装。

## 6. 测试体系

`tests/CMakeLists.txt` 定义两类测试：
- **L1 核心逻辑测试**：每个套件独立可执行（`test_canfileio` / `test_filterengine` / `test_tracecore` / `test_dbc` / `test_sim_rec_play` / `test_market_driver` / `test_project` / `test_protocol`），链接 `openbus_data` + `Qt6::Test`，使用 `QTEST_GUILESS_MAIN` 不加载平台插件
- **L2 offscreen UI 测试**：`test_ui_offscreen` 复刻 main.cpp 初始化链驱动真实 MainWindow，链接全部业务 DLL + 平台插件，通过 `QT_QPA_PLATFORM=offscreen` 无头运行

聚合目标 `tests` 由 `build.py test` 调用 ctest 执行，失败时 `--output-on-failure` 输出完整日志。

## 7. 约定与约束

- **PCH 策略**：不使用 ccache（与 MinGW g++ 13 的 PCH 不兼容，会静默崩溃）；每个 DLL 维护精确的 Qt 头 PCH 清单
- **链接器限制**：禁用 LLD（Windows 上导致文件锁问题），强制 ld.bfd
- **DLL 边界**：业务 DLL 之间不直接互相链接，仅依赖 `openbus_data`；通过 C 工厂函数 + `ModuleRegistry` 动态发现
- **构建目录隔离**：`build/`（Debug）、`build-dev/`（Dev）、`build-rel/`（Release）互不影响
- **资源跟随**：`resources/resources.qrc` 通过 AUTORCC 嵌入；`driver/` 与 `sdk/` 通过 `POST_BUILD` 复制到输出目录
- **版本来源**：版本号来自根 `CMakeLists.txt` 的 `project(... VERSION 0.1.0)`，打包脚本通过正则解析或读取 `CMakeCache.txt` 中的 `openbus_VERSION`