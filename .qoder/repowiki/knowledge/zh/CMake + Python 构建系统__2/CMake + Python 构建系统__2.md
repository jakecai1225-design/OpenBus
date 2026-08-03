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
    - resources/resources.qrc
---

本项目采用 CMake 作为核心构建系统，配合自研 Python 构建脚本 `scripts/build.py` 提供统一的开发工作流。整体架构如下：

**1. 构建工具链与依赖**
- 构建系统：CMake ≥ 3.21，使用 MinGW Makefiles 生成器
- 编译器：MinGW (g++/gcc)，默认路径通过环境变量 `SIN_MINGW_DIR` 或 `--mingw-dir` 指定
- Qt6 框架：仅链接 `Qt6::Widgets`，启用 AUTOMOC/AUTOUIC/AUTORCC 自动处理 Qt 元对象、UI 和资源文件
- C++ 标准：强制 C++17，禁用扩展
- 部署：通过 `windeployqt.exe` 打包 Qt 运行时依赖

**2. 工程结构**
- 顶层 `CMakeLists.txt`：定义项目名、版本 (0.1.0)、语言、Qt 自动化开关、输出目录 (`build/bin`)、子目录引入及安装规则
- `src/CMakeLists.txt`：按模块组织源文件（core/models/utils/ui），通过 `qt_add_executable(sin ...)` 创建可执行目标，配置头文件搜索路径，Windows 下以 GUI 程序方式运行（不弹出控制台）
- `resources/resources.qrc`：Qt 资源文件，由 RCC 自动处理

**3. Python 构建脚本 (`scripts/build.py`)**
提供统一 CLI 接口，支持以下命令：
- `configure`：CMake 配置，支持 `--build-type` (Debug/Release/RelWithDebInfo/MinSizeRel) 和 `--clean`
- `build`：增量编译，自动检测是否需要先 configure，支持 `-j` 并行和 `--target` 指定目标
- `run` / `debug`：运行程序或启动 GDB 调试，自动触发编译
- `clean` / `rebuild`：清理构建目录或完整重建
- `deploy`：调用 windeployqt 部署 Qt 依赖
- `all`：一键完成配置+编译+部署+运行全流程
- `status` / `open`：显示环境状态或打开输出目录

脚本内置环境验证，自动检查 CMake、g++、gcc、windeployqt、gdb 是否可用，并通过 `PATH` 注入工具路径。

**4. 构建约定与约束**
- 构建产物统一输出到 `build/bin/` 目录
- Windows 平台下可执行文件名为 `sin.exe`，以 GUI 模式运行
- 所有源文件在 `src/CMakeLists.txt` 中显式声明，无自动扫描机制
- 安装规则将目标安装到 `bin` 目录
- 构建类型通过 CMakeCache.txt 持久化，`status` 命令可读取当前配置