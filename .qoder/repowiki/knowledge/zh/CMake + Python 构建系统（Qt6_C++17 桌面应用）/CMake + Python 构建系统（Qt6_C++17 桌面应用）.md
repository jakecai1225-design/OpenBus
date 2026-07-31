---
kind: build_system
name: CMake + Python 构建系统（Qt6/C++17 桌面应用）
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

本项目采用 **CMake 3.21+** 作为核心构建系统，配合 **Python 构建脚本** (`scripts/build.py`) 提供统一的开发体验。目标平台为 Windows (MinGW)，使用 Qt6 框架和 C++17 标准。

### 构建架构

- **顶层 CMakeLists.txt**: 定义项目元信息、Qt6 自动处理 (MOC/UIC/RCC)、LLD 链接器优化、输出目录配置
- **src/CMakeLists.txt**: 核心构建逻辑，将代码拆分为三个静态库:
  - `sin_core`: 核心层 (core/models/utils) — 数据结构、录制/回放、DBC、过滤引擎
  - `sin_ui`: UI 层 — 所有界面组件
  - `sin.exe`: 可执行文件，链接 sin_ui → sin_core
- **third_party/Dependencies.cmake**: 第三方依赖声明 (spdlog 接口库、nlohmann/json 单头文件、qcustomplot 源码库)

### 构建脚本功能

`scripts/build.py` 提供完整的命令行接口：
- `configure`: CMake 配置，自动检测 Ninja/ccache/lld 加速工具
- `build`: 增量编译，支持 `-j` 并行参数
- `run/debug/clean/rebuild/deploy/all/status/open`: 完整开发工作流

### 性能优化策略

- **预编译头 (PCH)**: core 层使用精简 Qt 头列表，UI 层使用完整 Widget 头
- **LLD 链接器**: 多线程链接，比 ld.bfd 快 3-5x
- **ccache**: 编译缓存，命中时秒级返回
- **Ninja 生成器**: 优先使用，编译调度快 2-3x
- **Debug 模式**: 使用 `-g1` 精简调试信息，减少二进制大小

### 依赖管理

- Qt6 通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport)` 查找
- 第三方库集中在 `third_party/` 目录，部分通过下载脚本获取
- 支持环境变量覆盖工具路径 (`SIN_QT_DIR`, `SIN_MINGW_DIR`, `SIN_CMAKE_DIR`)

### 部署流程

- 使用 `windeployqt` 自动收集 Qt 运行时依赖
- 输出目录结构: `build/bin/sin.exe` + 动态库
- 支持 `install(TARGETS ...)` 规则用于安装

### 构建类型

支持 Debug、Release、RelWithDebInfo、MinSizeRel 四种构建类型，默认 Debug。

### 关键约束

- Windows GUI 程序必须设置 `WIN32_EXECUTABLE TRUE` 避免控制台窗口
- gold 链接器不支持 `--subsystem`，不可用于 Windows GUI 程序
- `--gc-sections` 在 ld.bfd 上反而增加链接时间，不使用