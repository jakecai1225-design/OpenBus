---
kind: build_system
name: CMake + Python 构建系统（Qt6 桌面应用）
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - scripts/build.py
    - third_party/Dependencies.cmake
    - resources/resources.qrc
---

## 1. 构建系统与工具链

项目采用 **CMake 3.21+** 作为核心构建系统，配合 **Python 构建脚本** (`scripts/build.py`) 提供统一入口。工具链基于 **MinGW (GCC)** 与 **Qt6**，Windows 平台可选使用 **Ninja**、**ccache**、**LLD** 加速编译与链接。

- C++ 标准：C++17（强制开启，禁用扩展）
- Qt 自动化：AUTOMOC/AUTOUIC/AUTORCC 全部启用
- 输出目录：`build/bin/`，可执行文件 `sin.exe`
- 安装规则：`install(TARGETS sin RUNTIME DESTINATION bin)`

## 2. 核心构建文件与结构

### 顶层 CMakeLists.txt
- 定义项目元信息（版本 0.1.0）、C++17 标准、Qt6 查找与 `qt_standard_project_setup()`
- Windows/GCC 下自动检测并启用 LLD 链接器（`tools/lld/ld.lld.exe`），Debug 模式使用 `-g1` 精简调试信息
- 包含 `third_party/Dependencies.cmake`，添加 `src` 子目录

### src/CMakeLists.txt（分层静态库设计）
将源码拆分为三个目标，实现增量编译优化：
- **`sin_core`**（静态库）：core/models/utils 三层，仅依赖 Qt::Widgets 基础头，通过 PCH 预编译常用 Qt 头加速
- ****`sin_ui`**（静态库）：ui 层组件，依赖 `sin_core` 和 qcustomplot（PRIVATE，因仅 graphicview.cpp 使用）
- **`sin`**（可执行）：链接 `sin_ui`，Windows 下设为 `WIN32_EXECUTABLE` 以隐藏控制台窗口

每个库都配置了 `target_precompile_headers`，按层裁剪 Qt 头集合（core 用轻量头，ui 用完整 Widget 头）。

### scripts/build.py（统一构建入口）
支持命令：`configure` / `build` / `run` / `debug` / `clean` / `rebuild` / `deploy` / `all` / `status` / `open`
- 自动检测 Ninja/ccache/LLD（位于 `tools/` 目录），优先使用 Ninja 生成器
- 环境变量覆盖：`SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR`
- `windeployqt` 打包 Qt 运行时依赖

## 3. 第三方依赖管理

### 头文件库（Header-only）
- **spdlog**：`third_party/spdlog/include`，通过 INTERFACE IMPORTED 库暴露
- **nlohmann/json**：单头文件 `third_party/nlohmann_json/json.hpp`，直接 include
- **concurrentqueue**：`third_party/concurrentqueue/concurrentqueue.h`，由 `sin_core` PUBLIC 暴露
- **qcustomplot**：单独编译为静态库 `qcustomplot`，供 `sin_ui` PRIVATE 链接
- **dbcppp**：DBC 解析库，位于 `third_party/dbcppp/` 或 `dbcppp_src/`

### 依赖下载脚本
`scripts/download_*.py` 分别下载 spdlog、json、concurrentqueue、qcustomplot 等依赖到 `third_party/`。

### Dependencies.cmake
集中声明 `spdlog` 的 INTERFACE_INCLUDE_DIRECTORIES，其他头文件库在 `src/CMakeLists.txt` 中按需检查路径后添加。

## 4. 资源与样式管理

- **Qt 资源文件**：`resources/resources.qrc` 注册 `styles/default.qss` 样式表
- **QSS 样式**：`resources/styles/default.qss` 通过 `:/:/styles/default.qss` 路径加载
- **UI 原型**：`UI/` 目录存放 HTML/CSS/JS 原型文件，与 Qt 代码分离

## 5. 构建约定与约束

- **分层编译**：修改 UI 代码不会触发 core 重编译（sin_ui → sin_core 单向依赖）
- **PCH 优化**：每层独立预编译头列表，core 层避免包含重型 Widget 头
- **Windows GUI 程序**：`WIN32_EXECUTABLE TRUE` 确保不弹出控制台窗口
- **链接器优化**：GCC/Windows 下优先使用 LLD（`-fuse-ld=lld`），无则回退 ld.bfd
- **构建类型**：支持 Debug / Release / RelWithDebInfo / MinSizeRel
- **部署流程**：`python scripts/build.py deploy` 调用 windeployqt 收集 Qt 依赖 DLL

## 6. 关键文件清单

| 文件 | 作用 |
|------|------|
| `CMakeLists.txt` | 顶层 CMake 配置，Qt6 查找，LLD 检测 |
| `src/CMakeLists.txt` | 分层静态库定义，PCH，链接关系 |
| `scripts/build.py` | Python 构建脚本，统一入口 |
| `third_party/Dependencies.cmake` | 第三方头文件库声明 |
| `resources/resources.qrc` | Qt 资源文件（样式表） |
| `src/main.cpp` | 程序入口 |

## 7. 未发现的 CI/容器化配置

仓库中未发现 GitHub Actions、Jenkins、Dockerfile 等 CI/CD 配置文件，构建主要依赖本地 `scripts/build.py` 脚本手动执行。