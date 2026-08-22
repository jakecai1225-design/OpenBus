---
kind: build_system
name: CMake + Python 脚本驱动的 Windows Qt6 构建与打包流水线
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - drivers/CMakeLists.txt
    - tests/CMakeLists.txt
    - third_party/Dependencies.cmake
    - scripts/build.py
    - scripts/package.py
    - installer/openbus.iss
    - doc/构建基线.md
---

## 1. 系统概览

openbus 采用 **CMake 作为核心构建系统**，配合自研的 **Python 构建/打包脚本**（`scripts/build.py`、`scripts/package.py`）完成从配置、编译、测试到分发产物产出的全链路。工具链固定为 **MinGW g++ 13.1 + Qt6 (mingw_64) + CMake 3.30+**，优先使用本地 `tools/ninja/ninja.exe` 作为生成器，回退到 MinGW Makefiles。

- 顶层 `CMakeLists.txt`：声明项目版本 `0.1.0`、C++17、Qt AUTOMOC/AUTOUIC/AUTORCC、输出目录 `build/bin`、Dev 档 `-O1 -g1`、thin archive (`ar qcT/qT`)、强制使用 `ld.bfd` 链接器（禁用 LLD/gold），并包含第三方依赖与子目录。
- `src/CMakeLists.txt`：定义分层目标——公共底座 DLL `openbus_data`（core/models/utils/thememanager）、业务模块 DLL `openbus_market/transceive/dbc/flow/trace/graphic`（各含唯一 C 工厂 `openbus_createXxxModule()`）、壳 UI 静态库 `openbus_ui`、可执行文件 `openbus`；每个模块均通过 `target_precompile_headers` 精确限定 PCH 头集合以加速编译。
- `drivers/CMakeLists.txt`：将 Candle/Kvaser/PEAK/SLCAN/ZLG 驱动各自编译为独立 DLL，并通过 `deploy_driver_manifest` POST_BUILD 规则把 `driver.json` 复制到输出目录，形成 `.odp` 安装单元布局。
- `tests/CMakeLists.txt`：基于 Qt Test 的 L1 集成测试（每个套件独立进程，避免 core 层单例污染）+ L2 offscreen UI 测试（`test_ui_offscreen` 链接完整 shell UI 并设置 `QT_QPA_PLATFORM=offscreen`），聚合目标 `tests` 供 `build.py test` 调用。
- `third_party/Dependencies.cmake`：集中管理 spdlog（INTERFACE IMPORTED）、qcustomplot（静态库）、vector_blf（可选 subdirectory）等第三方依赖。

## 2. 关键文件与脚本

- `scripts/build.py`：统一入口，支持 `configure/build/run/debug/clean/rebuild/deploy/all/status/open/test` 子命令；自动检测 Ninja/MinGW/Qt 路径（环境变量 `SIN_QT_DIR`/`SIN_MINGW_DIR`/`SIN_CMAKE_DIR` 优先），构建前自动 `taskkill openbus.exe` 释放文件锁，`cmd_deploy` 调用 `windeployqt` 并手动补拷 `Qt6PrintSupport.dll`、创建 `lib/fonts` 空目录。
- `scripts/package.py`：完整发布流水线，步骤包括 Release 构建 → windeployqt → staging 白名单组装（`BIN_FILES`/`BIN_DIRS` 列表）→ 捆绑精简 Python 运行时（含 PyQt6 裁剪子集）→ 附加 LICENSE/NOTICES → objdump import 表完整性校验（缺失即 FAIL）→ 体积报告 → 产出 `dist/openbus-<ver>-win64-portable.zip` 与 Inno Setup 安装器。
- `installer/openbus.iss`：Inno Setup 脚本，默认安装到 `{autopf}\openbus`，双语向导，完成页检测 ZCANPRO/PCANUSB/canlib32 缺失并提示，卸载不碰 `%APPDATA%\openbus`。
- `doc/构建基线.md`：记录 Dev(-O1 -g1)+thin archive+ld.bfd 的性能基线与 Phase A/B1~B6 度量结果（改 1 个 ui/core 文件→可运行 ≤30s，全量构建 ≈5 min，exe 体积 52 MB）。

## 3. 架构与约定

- **模块化 DLL 拆分**：按业务域拆分为 `openbus_data`（共享单例）+ 多个业务 DLL（market/transceive/dbc/flow/trace/graphic），通过 `imodule.h` 定义的 `pages()/createPage()/query()/shellInvoke()` 虚接口与 C 工厂进行解耦装配，主程序仅链接导入库并在启动时注册模块。
- **驱动插件体系**：每个厂商驱动为独立 DLL + `driver.json` 清单，由 `DriverRegistry` 扫描 `<exe>/drivers/<id>/` 动态加载，`.odp` 包结构在开发态即与真实安装布局一致。
- **构建类型**：除标准 Debug/Release/RelWithDebInfo/MinSizeRel 外，新增 `Dev` 档（`-O1 -g1`），建议用 `--build-dir build-dev` 与全量 Debug 并存，避免切换档位触发全量重编。
- **PCH 策略**：每个 target 显式声明所需 Qt 头集合（如 `openbus_data` 仅含无 Widget 的轻量头，`openbus_ui` 含完整 QWidget/QMainWindow 等），避免跨模块 PCH 膨胀。
- **测试隔离**：L1 测试每个套件独立进程，避免 core 层 DbcManager/PluginManager 等单例状态污染；L2 通过 `QT_QPA_PLATFORM=offscreen` 在无头环境驱动真实 MainWindow。

## 4. 约定与约束

- **编译器/链接器约束**：根 CMakeLists 注释明确禁止使用 ccache（与 MinGW g++ 13 PCH 不兼容，会静默崩溃）、LLD（Windows 上导致文件锁问题）、ld.gold（ELF-only，无法链接 PE），必须使用默认 `ld.bfd`。
- **Qt 版本锁定**：所有驱动 DLL ABI 契约要求与主程序同 Qt 版本 + 同编译器（MinGW 13.1 x64, C++17）。
- **部署约束**：`zlgcan.dll` 必须与 exe 同目录（ZLG SDK 运行时要求）；`windeployqt` 无法探测 qcustomplot 对 `Qt6PrintSupport.dll` 的传递依赖，需手动补拷。
- **打包白名单**：`package.py` 严格通过 `BIN_FILES`/`BIN_DIRS` 白名单拷贝产物，`zlgcan.dll/zlgcan.lib` 因厂商授权不随包（用户需自行安装 ZCANPRO）。
- **依赖完整性门禁**：打包阶段使用 `objdump -p` 解析每个 exe/dll/pyd 的 import 表，不在 staging 内且不在系统 DLL 白名单中的依赖直接 FAIL，防止“漏拷只在用户机器上炸”。
- **版本来源**：版本号来自顶层 `project(... VERSION 0.1.0)`，`package.py` 优先读取 `build-rel/CMakeCache.txt` 中的 `openbus_VERSION`，回退正则解析 `CMakeLists.txt`。
- **构建加速**：启用 thin archive（`ar qcT/qT`）使静态库重打包降至毫秒级；Ninja 生成器下增量 reconfigure 仅在 CMakeLists.txt 变更时触发，避免每次全量重编。