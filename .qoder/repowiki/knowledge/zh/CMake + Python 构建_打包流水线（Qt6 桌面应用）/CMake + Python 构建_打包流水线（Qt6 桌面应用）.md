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
    - drivers/zlg/CMakeLists.txt
    - third_party/Dependencies.cmake
    - scripts/build.py
    - scripts/package.py
---

## 1. 构建系统与工具链

- **构建系统**：CMake 3.21+，C++17，Qt6（Widgets/Svg/Network/PrintSupport），默认使用 Ninja 生成器（优先 Qt Tools/ninja，回退 MinGW Makefiles）。
- **编译器**：MinGW g++ 13.1 x64（`SIN_MINGW_DIR` 环境变量或 `C:/Qt/Tools/mingw*` 自动探测），Windows GUI 可执行（`WIN32_EXECUTABLE TRUE`）。
- **链接器**：强制使用默认 `ld.bfd`；禁用 LLD（曾产出损坏可执行）、禁用 ccache（与 MinGW g++ 13 PCH 不兼容会静默崩溃）。
- **第三方依赖管理**：`third_party/Dependencies.cmake` 以 CMake INTERFACE IMPORTED 目标暴露 spdlog、nlohmann/json，源码库 qcustomplot 直接编译为静态库，vector_blf 通过 `add_subdirectory` 集成（可选，缺失时 BLF 支持关闭）。
- **版本**：根 `CMakeLists.txt` 声明 `project(openbus VERSION 0.1.0)`，打包脚本从 CMakeCache 或源码正则提取版本号。

## 2. 核心构建文件

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层工程定义、Qt 查找、子目录引入、安装规则、MinGW 特殊处理（thin archive、Dev 档 `-O1 -g1`） |
| `src/CMakeLists.txt` | 模块拆分：`openbus_data`（公共底座 DLL）、`openbus_market`/`transceive`/`dbc`/`flow`/`trace`/`graphic`（业务 DLL，每个导出一个 C 工厂）、`openbus_ui`（静态库）、`openbus`（主可执行）；每个 target 配置独立 PCH |
| `drivers/CMakeLists.txt` | 驱动插件聚合，调用各厂商子目录并部署 `driver.json` |
| `drivers/<vendor>/CMakeLists.txt` | `qt_add_plugin` 输出到 `build/bin/drivers/<id>/`，复用 `src/core/candevice_*.cpp` 后端源码 |
| `scripts/build.py` | 统一入口：configure/build/run/debug/clean/rebuild/deploy/all/status/open/test，封装环境 PATH、Ninja/MinGW 选择、windeployqt 部署、Qt6PrintSupport 手动补拷、lib/fonts 创建 |
| `scripts/package.py` | 完整发布流水线：Release 构建 → windeployqt → staging 白名单组装 → 捆绑 Python 运行时（含 PyQt6 裁剪子集）→ 附加文件 → objdump import 表校验 → zip 便携版 / Inno Setup 安装器 |
| `third_party/Dependencies.cmake` | 第三方库 CMake 接口 |

## 3. 架构与约定

- **模块化 DLL 拆分**：按功能拆为 `openbus_data`（单例共享层）+ 多个业务 DLL（market/transceive/dbc/flow/trace/graphic），每个 DLL 仅暴露一个 C 工厂函数（如 `openbus_createTraceModule()`），由主程序 `main.cpp` 经 `ModuleRegistry` 动态加载。UI 组件被抽入 `openbus_ui` 静态库，改 UI 只重编译该库 + 最终链接。
- **外置驱动插件**：`drivers/<vendor>` 通过 `qt_add_plugin` 编译为 `.dll`，配合 `driver.json` 清单形成 `.odp` 包布局（`build/bin/drivers/<id>/`），运行时由 `DriverRegistry` 扫描加载；ZLG SDK 的 `zlgcan.dll` 等厂商 DLL 不随包分发，用户需安装 ZCANPRO。
- **构建产物布局**：`build/bin/` 下放置 `openbus.exe` + 业务 DLL + `drivers/<id>/` 驱动；`POST_BUILD` 命令拷贝 `driver/`（ZLG 运行时）和 `sdk/`、`scripts/*.py` 到 exe 同级目录。
- **PCH 策略**：每个 target 用 `target_precompile_headers` 精确限定头集合（core 层不含 Widget，UI 层包含完整 QWidget 列表），避免全量预编译膨胀。
- **并行构建优化**：MinGW 下启用 thin archive（`ar qcT/qT`），静态库重打包降至毫秒级；默认只构建 `openbus` 目标跳过测试。

## 4. 约定与约束

- **开发/调试档分离**：`--build-type Dev --build-dir build-dev` 产生 `-O1 -g1` 快速档，与全量 Debug (`build/`) 并存，互不影响。
- **工具链定位**：通过 `SIN_QT_DIR` / `SIN_MINGW_DIR` / `SIN_CMAKE_DIR` 环境变量覆盖默认路径；`package.py` 还会在 `C:/Qt` / `D:/Qt` / `E:/Qt` 下扫描最新版本。
- **发布白名单**：`package.py` 中 `BIN_FILES` / `BIN_DIRS` 严格白名单控制 staging 内容；`driver/` 与 `drivers/` 均不随包（驱动一律从市场安装）。
- **依赖完整性门禁**：打包阶段用 `objdump -p` 解析 staging 内所有 PE 的 import 表，逐一核对是否在 staging 或系统 DLL 白名单（`SYSTEM_DLLS`），任一缺失即失败。
- **Python 运行时捆绑**：打包时从本机解释器精简拷贝 Python + PyQt6 子集（QtCore/Gui/Widgets/Svg），写入 `pythonXY._pth` 实现 embeddable 式隔离（不读注册表/环境变量），并在隔离环境中自验证 `import PyQt6.QtCore` 等。
- **Qt 部署补充**：`windeployqt` 无法检测 `qcustomplot` 静态库对 `Qt6PrintSupport` 的传递依赖，需手动复制；同时创建空 `lib/fonts` 目录消除 Qt6 Windows 字体告警。
- **测试入口**：`scripts/build.py test` 触发 `cmake --build ... --target tests && ctest --output-on-failure`，但当前仓库未见 `tests` target 定义，属于预留接口。
- **CI/自动化**：仓库未检出 GitHub Actions/GitLab CI 等流水线文件；发布流程完全由 `scripts/package.py` 驱动，可在本地或任意机器上重复执行（幂等）。