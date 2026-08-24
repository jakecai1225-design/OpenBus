---
kind: build_system
name: CMake + Python 构建/打包流水线（Qt6 桌面应用）
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
    - installer/openbus.iss
    - third_party/Dependencies.cmake
---

## 1. 构建系统总览

本项目采用 **CMake 3.21+** 作为核心构建系统，配合自研的 **Python 编排脚本**（`scripts/build.py`、`scripts/package.py`）完成配置、编译、部署、测试与打包。工具链固定为 **MinGW (g++ 13.1 x64) + Qt6 (Widgets/PrintSupport/Svg/Network/Test)**，生成器优先使用本地 `tools/ninja/ninja.exe`，回退到 MinGW Makefiles。

- C++ 标准：`CMAKE_CXX_STANDARD=17`，强制开启且禁止扩展；启用 `AUTOMOC/AUTOUIC/AUTORCC`。
- 链接器：Windows/MinGW 下强制使用默认 `ld.bfd`（注释明确禁用 LLD/gold，因文件锁或 PE 子系统集成问题）。
- 静态库归档：通过重写 `CMAKE_CXX_ARCHIVE_CREATE/APPEND` 使用 `ar qcT/qT` thin archive，使重打包降至毫秒级。
- 预编译头：每个共享 DLL (`openbus_data` / `openbus_market` / `openbus_transceive` / `openbus_dbc` / `openbus_flow` / `openbus_trace` / `openbus_graphic`) 均通过 `target_precompile_headers` 声明精简 Qt 头清单，显著缩短增量编译时间。
- 架构拆分：主程序 `openbus` 仅链接 UI 静态库 `openbus_ui` 和若干业务 DLL；核心逻辑集中在 `openbus_data` 共享库，单例（DbcManager、PluginManager、ThemeManager 等）跨 DLL 共享进程内唯一实例。

## 2. 关键文件与角色

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 根工程定义、Qt6 find_package、第三方依赖引入、子目录组织 |
| `src/CMakeLists.txt` | 定义全部目标：`openbus_data`(SHARED)、5个业务DLL、`openbus_ui`(STATIC)、`openbus`(EXE)，以及 POST_BUILD 拷贝 driver/、sdk/、scripts/ |
| `drivers/CMakeLists.txt` | 为 zlg/peak/kvaser/slcan/candle 各生成一个驱动插件 target，POST_BUILD 输出 `driver.json` 到 `build/bin/drivers/<id>/` |
| `tests/CMakeLists.txt` | 用 `openbus_add_test` 宏生成 L1 集成测试 exe（链接 `openbus_data` + `Qt6::Test`），并聚合 `tests` 目标供 `build.py test` 调用 |
| `scripts/build.py` | 开发者入口：configure/build/run/debug/clean/rebuild/deploy/all/status/open/test，自动检测 Ninja/MinGW/Qt，支持 `--build-dir build-dev` Dev 档（`-O1 -g1`） |
| `scripts/package.py` | 发布流水线：Release 构建 → windeployqt → staging 组装 → 捆绑 Python 运行时 → 附加文件 → objdump 依赖校验 → zip 便携版 → Inno Setup 安装器 |
| `installer/openbus.iss` | Inno Setup 脚本，打包 `dist/stage/openbus/*`，双语向导，卸载不碰 `%APPDATA%\openbus` |
| `third_party/Dependencies.cmake` | 第三方库（spdlog、nlohmann_json、pugixml、qcustomplot、vector_blf 等）的 CMake 导入 |
| `tools/ninja/` | 内置 Ninja 可执行，避免外部工具链依赖 |

## 3. 构建流程与约定

### 开发构建
```
python scripts/build.py configure --build-type Dev --build-dir build-dev
python scripts/build.py build -j8
python scripts/build.py run
python scripts/build.py debug
python scripts/build.py deploy   # windeployqt + 手动补拷 Qt6PrintSupport.dll + 创建 lib/fonts/
```
- 构建类型支持 `Dev/Debug/Release/RelWithDebInfo/MinSizeRel`；Dev 档独立目录 `build-dev/`，与全量 Debug 并存。
- `cmd_build` 在编译前自动 `taskkill /F openbus.exe`，释放文件锁后再链接。
- `cmake_needs_reconfigure()` 仅在 `CMakeLists.txt` 比生成器产物新时才触发 reconfigure，避免 Makefile 生成器全量重编。

### 测试
```bash
python scripts/build.py test -j8
```
- 仅构建 `tests` 聚合目标，不重链主程序；ctest 以 `--output-on-failure` 运行。
- L1 测试使用 `QTEST_GUILESS_MAIN`（无 GUI 平台插件）；L2 offscreen UI 测试设置 `QT_QPA_PLATFORM=offscreen` 并复制 `qwindows`/`qoffscreen` 平台插件。
- 每个测试 exe 输出到 `build/bin/`，与 `openbus_data.dll` 同目录，自动解析。

### 打包发布
```bash
python scripts/package.py [--version X.Y.Z] [--skip-build] [--skip-python] [--skip-installer]
```
流水线步骤（幂等、可重复）：
1. 在 `build-rel/` 配置并构建 Release（复用 `build.py`）。
2. 调用 `windeployqt --release --no-translations --compiler-runtime`，手动补拷 `Qt6PrintSupport.dll`（qcustomplot 传递依赖）、回拷 `translations/qtbase_zh_CN.qm`、创建 `lib/fonts/`。
3. 按白名单（`BIN_FILES`/`BIN_DIRS`）组装 `dist/stage/openbus/`；plugins/ 与 drivers/ 不预装，由市场机制分发。
4. 从本机安装版 Python 精简拷贝运行时（剔除 tkinter/tkinter 相关 DLL、idlelib 等），写 `pythonXY._pth` 实现 embeddable 式隔离路径，裁剪 PyQt6 子集（QtCore/Gui/Widgets/Svg）。
5. 复制 `THIRD_PARTY_NOTICES.md`、`README-PORTABLE.txt`、`LICENSE.txt`。
6. 用 `objdump -p` 扫描 staging 内所有 `.exe/.dll/.pyd` 的 import 表，逐一核对是否在 staging 或系统白名单（`SYSTEM_DLLS`），缺失即 FAIL。
7. 生成体积报告 `dist/package-report.txt`。
8a. 压缩为 `dist/openbus-<ver>-win64-portable.zip`。
8b. 可选调用 `iscc` 编译 Inno Setup 安装器，输出 `dist/openbus-<ver>-win64-setup.exe`。

## 4. 约定与约束

- **工具链锁定**：MinGW g++ 13.1 x64 + Qt6.8.3 mingw_64 + CMake 3.30.3；路径可通过 `SIN_QT_DIR`/`SIN_MINGW_DIR`/`SIN_CMAKE_DIR` 环境变量覆盖。
- **禁用 ccache**：与 MinGW g++ 13 的 PCH 不兼容，会静默崩溃。
- **禁用 LLD/gold**：Windows 上导致文件锁问题或 PE 子系统未知选项错误。
- **thin archive 必须带 T**：`ar qT` 追加规则必须带 `T`，否则 CMake 分批归档时出现 "Cannot convert existing thin library to normal format"。
- **驱动 .odp 布局**：每个驱动 target 输出 `driver_<id>.dll` + `driver.json` 到 `build/bin/drivers/<id>/`，打包前必须存在，否则 `package.py` 报 missing_drv 失败。
- **ZLG SDK 不随包**：`driver/zlgcan.dll` 及 `kerneldlls/` 整体排除在 staging 白名单之外，用户需自行安装 ZCANPRO。
- **Qt 字体目录**：Windows 下 `lib/fonts/` 必须存在（空目录即可消警），否则 `QFontDatabase` 告警。
- **版本来源**：`package.py` 优先读 `CMakeCache.txt` 中的 `openbus_VERSION`，回退正则解析根 `CMakeLists.txt` 的 `VERSION` 字段。
- **安装器行为**：Inno Setup 默认安装到 `C:\Program Files\openbus`，卸载只删安装目录与快捷方式，不删除 `%APPDATA%\openbus`；完成页检测 ZLG/PEAK/Kvaser 驱动缺失并提示但不阻塞安装。
- **Qt 资源**：`resources/resources.qrc` 通过 `qt_add_executable` 的 `../resources/resources.qrc` 参数嵌入，测试也通过 AUTORCC 将资源随测试 exe 编译。