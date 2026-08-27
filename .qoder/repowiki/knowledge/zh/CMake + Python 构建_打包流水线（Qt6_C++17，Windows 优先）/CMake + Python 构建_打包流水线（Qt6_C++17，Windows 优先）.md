---
kind: build_system
name: CMake + Python 构建/打包流水线（Qt6/C++17，Windows 优先）
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - third_party/Dependencies.cmake
    - scripts/build.py
    - scripts/package.py
    - tests/CMakeLists.txt
    - drivers/CMakeLists.txt
    - drivers/zlg/CMakeLists.txt
    - drivers/peak/CMakeLists.txt
    - drivers/kvaser/CMakeLists.txt
    - drivers/slcan/CMakeLists.txt
    - drivers/candle/CMakeLists.txt
---

## 1. 系统概览

项目采用 **CMake 3.21+** 作为核心构建系统，配合自研 Python 脚本 `scripts/build.py` 封装配置、编译、运行、调试、测试、部署等常用操作；最终通过 `scripts/package.py` 完成 Release 产物打包、Qt/Python 运行时捆绑、依赖完整性校验与 zip/Inno Setup 分发。

- 语言：C++17（`CMAKE_CXX_STANDARD 17`），禁用扩展，启用 Qt AUTOMOC/AUTOUIC/AUTORCC。
- 目标平台：Windows 优先（MinGW g++ 13 + Qt6.8.3 mingw_64），使用 Ninja 或 MinGW Makefiles 生成器。
- 链接器：强制使用默认 `ld.bfd`（注释明确拒绝 LLD/gold，避免文件锁/PE 子系统错误）。
- 版本：根 `CMakeLists.txt` 声明 `project(openbus VERSION 0.1.0)`，打包时从 CMakeCache 或源码正则提取版本号。

## 2. 关键文件与职责

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层工程定义、Qt6 查找、第三方依赖 include、子目录挂载、安装规则 |
| `src/CMakeLists.txt` | 模块拆分：`openbus_data`(共享 DLL)、`openbus_market/transceive/dbc/flow/trace/graphic`(业务 DLL)、`openbus_ui`(静态库)、`openbus`(可执行)；每个目标独立 PCH 头清单 |
| `third_party/Dependencies.cmake` | 内嵌第三方库：spdlog(nlohmann_json/pugixml 为单头)、qcustomplot(静态库)、vector_blf(可选子目录) |
| `drivers/*/CMakeLists.txt` | 5 个外置驱动插件（zlg/peak/kvaser/slcan/candle），输出到 `build/bin/drivers/<id>/` 形成 `.odp` 布局 |
| `tests/CMakeLists.txt` | Qt Test 聚合：L1 集成测试 (`test_*`) + L2 offscreen UI 测试 (`test_ui_offscreen`)，统一 `add_test` + `tests` 聚合目标 |
| `scripts/build.py` | 统一入口：configure/build/run/debug/clean/rebuild/deploy/all/test/status/open，自动检测 Ninja/MinGW/Qt，支持 `--build-dir build-dev` Dev 档 |
| `scripts/package.py` | 完整发布流水线：Release 构建 → windeployqt → staging 白名单组装 → Python/PyQt6 精简捆绑 → objdump 依赖校验 → zip/Inno Setup |

## 3. 架构与约定

### 3.1 模块化分层（DLL 拆分）
`src/CMakeLists.txt` 将应用拆为多个共享库，通过 C 工厂函数暴露模块接口（如 `openbus_createTraceModule()`），主程序仅链接导入库并动态注册：
- `openbus_data`：公共底座（core/models/utils/thememanager），所有模块共享进程级单例（DbcManager/PluginManager 等）。
- `openbus_market / transceive / dbc / flow / trace / graphic`：业务 DLL，各自维护独立 PCH 头清单以加速编译。
- `openbus_ui`：壳 UI 静态库，仅编译期依赖 qcustomplot/psapi。
- `openbus`：可执行，链接全部业务 DLL，Win32 GUI 程序（`WIN32_EXECUTABLE TRUE`）。

### 3.2 构建流程
```
python scripts/build.py configure --build-type Dev|Debug|Release|RelWithDebInfo|MinSizeRel [--build-dir build-dev]
python scripts/build.py build -jN [--target <target>] [--build-dir ...]
python scripts/build.py run / debug / test / deploy / all
```
- 首次 `build` 自动触发 `configure`；构建前自动 `taskkill openbus.exe` 释放文件锁。
- 优先使用 `tools/ninja/ninja.exe`，回退 MinGW Makefiles；Ninja 未找到时降级并警告。
- Dev 档使用 `-O1 -g1` 独立 `build-dev/` 目录，与全量 Debug 并存。
- `cmake_needs_reconfigure()` 仅在 CMakeLists.txt 比生成器文件新时才 reconfigure，避免全量重编。

### 3.3 测试体系
- L1：`tests/test_*.cpp` 各一个独立 exe，链接 `openbus_data` + `Qt6::Test`，使用 `QTEST_GUILESS_MAIN`，通过 `ctest` 执行。
- L2：`test_ui_offscreen.cpp` 复刻 main 初始化链，设置 `QT_QPA_PLATFORM=offscreen` 无头运行，链接全部业务 DLL。
- 聚合目标 `tests` 由 `build.py test` 调用，仅构建测试目标不重链主程序。

### 3.4 打包与分发（`scripts/package.py`）
流水线步骤（幂等、可重复）：
1. 在 `build-rel/` 独立目录配置并构建 Release。
2. 调用 `windeployqt --release --no-translations --compiler-runtime` 部署 Qt 运行时，手动补拷 `Qt6PrintSupport.dll`（qcustomplot 传递依赖）与 `translations/qtbase_zh_CN.qm`。
3. 按白名单拷贝至 `dist/stage/openbus/`：`openbus.exe`、`libopenbus_*.dll`、`Qt6*.dll`、MinGW 运行时、`platforms/imageformats/iconengines/styles/tls/networkinformation` 等目录。
4. 捆绑 Python 运行时：精简拷贝解释器核心 + Lib/DLLs/site-packages，创建 `pythonXY._pth` 隔离 sys.path，裁剪 PyQt6 子集（QtCore/Gui/Widgets/Svg）。
5. 附加 `THIRD_PARTY_NOTICES.md` / `README-PORTABLE.txt` / `LICENSE.txt`。
6. 用 `objdump -p` 解析每个 PE 的 import 表，对照系统 DLL 白名单逐一校验，缺失即 FAIL。
7. 生成体积报告 `dist/package-report.txt`。
8a. 压缩为 `dist/openbus-<ver>-win64-portable.zip`。
8b. 可选 Inno Setup 安装器（`iscc` 不存在则跳过，不阻塞便携版）。

### 3.5 第三方依赖管理
- 源码直接放入 `third_party/`：spdlog、nlohmann_json、pugixml、concurrentqueue、dbcppp、qcustomplot、vector_blf。
- `third_party/Dependencies.cmake` 通过 `INTERFACE IMPORTED` 或 `add_subdirectory` 暴露给上层 target。
- vector_blf 可选：存在 `CMakeLists.txt` 才集成，否则发出 WARNING 并禁用 BLF 支持。

### 3.6 驱动插件构建
`drivers/CMakeLists.txt` 为每个厂商（zlg/peak/kvaser/slcan/candle）生成独立 DLL 与 `driver.json`，输出到 `build/bin/drivers/<id>/`，打包时由 `package.py` 校验布局完整性（缺任一即失败）。

## 4. 约定与约束

- **禁止使用 ccache**：与 MinGW g++ 13 的 PCH 不兼容，会静默崩溃（`STATUS_DLL_NOT_FOUND`）。
- **禁止使用 LLD/gold 链接器**：Windows 上导致文件锁问题或 ELF-only 构建无法产出 PE。
- **PCH 必须显式声明**：每个 target 通过 `target_precompile_headers(... PRIVATE <Qt 头>)` 精确控制预编译头集合，避免跨模块污染。
- **thin archive 用于静态库**：MinGW 下重写 `CMAKE_CXX_ARCHIVE_CREATE/APPEND` 使用 `ar qcT/qT`，对象列表超命令行长度时仍有效。
- **dev/rel 双构建目录**：Dev 档 `-O1 -g1` 放 `build-dev/`，全量 Debug 放 `build/`，互不干扰。
- **驱动与 ZLG SDK 不随包**：`driver/`（含 kerneldlls/ZPS 协议栈 ~24MB）和 `drivers/`（.odp）均不预装，用户通过市场安装或自行安装 ZCANPRO。
- **staging 白名单机制**：仅允许 `BIN_FILES`/`BIN_DIRS` 中列出的文件/目录进入安装包，防止遗漏依赖。
- **Python 运行时隔离**：通过 `pythonXY._pth` 实现 embeddable 风格路径隔离，不读取注册表/环境变量，确保纯净机器可运行。
- **Qt 字体目录**：`lib/fonts/` 必须存在（空目录即可消警），正式发布可放置开源字体（微软系统字体不可再分发）。
- **测试数据定位**：通过 `SIN_SOURCE_DIR` 编译宏指向源码树 `tests/` 目录，供测试用例加载样本数据。
- **windeployqt 已知限制**：对静态库（qcustomplot）传递依赖探测不完整，需手动补拷 `Qt6PrintSupport.dll`。

## 5. 适用性说明

本仓库是典型的 CMake + Qt6 桌面应用构建系统，具备完整的开发构建、测试、打包、分发流水线，且针对 Windows/MinGW 环境做了大量针对性优化与约束。