---
kind: build_system
name: CMake + Python 构建系统
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - scripts/build.py
    - third_party/Dependencies.cmake
---

## 构建系统与工具链

本项目采用 **CMake 3.21+** 作为核心构建系统，配合 **Python 构建脚本** 统一管理配置、编译、部署与调试流程。编译器为 **MinGW g++ 13**（Windows），Qt6 通过 `find_package` 自动发现。

### 构建架构
- **顶层 CMakeLists.txt**：定义项目元信息（版本 0.1.0）、C++17 标准、Qt 自动化处理（AUTOMOC/AUTOUIC/AUTORCC）、输出目录与 Qt6 依赖查找
- **src/CMakeLists.txt**：核心构建逻辑，将代码拆分为三个目标：
  - `sin_core`（静态库）：core/models/utils 层，使用精简 PCH（仅 QObject/QString 等基础头）加速编译
  - `sin_ui`（静态库）：ui 层，链接 sin_core 与 qcustomplot，使用完整 Qt Widget PCH
  - `sin`（可执行文件）：入口 main.cpp，链接 sin_ui
- **third_party/Dependencies.cmake**：集中管理第三方依赖（spdlog、nlohmann_json、qcustomplot、vector_blf）

### 构建脚本（scripts/build.py）
提供统一 CLI 接口，支持以下命令：
- `configure` — CMake 配置（自动检测 Ninja/MinGW Makefiles 生成器）
- `build` — 增量编译（首次自动触发 configure）
- `run/debug/clean/rebuild/deploy/all/status/open`

关键特性：
- 优先使用本地 `tools/ninja/ninja.exe` 作为构建加速器（比 MinGW Makefiles 快 2-3x）
- 自动设置 PATH 包含 cmake/mingw/qt/bin，避免编译器找不到 DLL
- 默认路径通过环境变量 `SIN_QT_DIR` / `SIN_MINGW_DIR` / `SIN_CMAKE_DIR` 覆盖
- Windows 下以 GUI 程序方式运行（不弹出控制台窗口）

### 第三方依赖管理
- **源码内嵌**：concurrentqueue、nlohmann_json、spdlog、dbcppp、qcustomplot、vector_blf 均置于 `third_party/` 目录
- **下载脚本**：`scripts/download_*.py` 用于获取依赖源码（如 concurrentqueue、exprtk、json、qcustomplot、spdlog）
- **zlib**：MinGW 自带 libz.a，BLF 解析直接链接 `-lz`

### 构建约束与约定
- **禁用 ccache**：与 MinGW g++ 13 的 PCH 不兼容，会静默崩溃
- **禁用 LLD 链接器**：Windows 上会导致文件锁问题，使用默认 ld.bfd
- **PCH 分层**：core 层仅预编译非 Widget 头，UI 层预编译完整 Qt Widget 头列表，显著减少重编译时间
- **输出目录**：所有可执行文件输出到 `build/bin/`
- **安装规则**：`install(TARGETS sin RUNTIME DESTINATION bin)`，遵循 CMake 标准安装布局

### 打包与部署
- Windows 平台使用 `windeployqt` 自动收集 Qt 运行时依赖
- 特殊处理：qcustomplot 静态库对 Qt6PrintSupport 的传递依赖需手动复制 `Qt6PrintSupport.dll`
- 构建类型支持：Debug、Release、RelWithDebInfo、MinSizeRel