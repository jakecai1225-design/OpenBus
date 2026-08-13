---
kind: build_system
name: CMake + Python 构建脚本的 Qt6/C++17 桌面应用构建系统
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - third_party/Dependencies.cmake
    - scripts/build.py
    - resources/resources.qrc
---

## 1. 构建系统与工具链

项目采用 **CMake 3.21+** 作为核心构建系统，配合自研的 **Python 构建脚本** (`scripts/build.py`) 封装配置、编译、部署、调试等常用工作流。目标平台为 **Windows (MinGW)**，使用 C++17 标准（`CMAKE_CXX_STANDARD 17`，关闭扩展），Qt6（Widgets/PrintSupport/Svg）作为 GUI 框架。

- **生成器选择**：优先检测 `tools/ninja/ninja.exe`，若存在则使用 Ninja 生成器；否则回退到 MinGW Makefiles，并通过 `-DCMAKE_MAKE_PROGRAM` 指定 make 路径。
- **编译器**：通过环境变量 `SIN_MINGW_DIR` 或默认路径 `C:/Qt/Tools/mingw1310_64` 定位 MinGW g++/gcc；CMake 路径由 `SIN_CMAKE_DIR` 控制。
- **Qt 路径**：通过 `SIN_QT_DIR` 或默认 `C:/Qt/6.8.3/mingw_64` 指定，并传入 `CMAKE_PREFIX_PATH`。
- **链接器约束**：显式注释说明不使用 ccache（与 MinGW g++ 13 的 PCH 不兼容）、不使用 LLD（Windows 上文件锁问题），使用默认 `ld.bfd` 以保证兼容性。

## 2. 关键构建文件

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层工程定义：版本 0.1.0、Qt6 查找、子目录引入、安装规则 |
| `src/CMakeLists.txt` | 源文件组织、库拆分、依赖链接、PCH、驱动 DLL 拷贝 |
| `third_party/Dependencies.cmake` | 第三方库集成（spdlog 接口库、qcustomplot 静态库、vector_blf 子目录） |
| `scripts/build.py` | 统一入口：configure/build/run/debug/clean/rebuild/deploy/all/status/open |
| `resources/resources.qrc` | Qt 资源文件（图标、样式表） |

## 3. 架构与约定

### 3.1 分层静态库拆分
将源码拆分为三个层次，提升增量编译效率：**改 UI 代码不会触发 core 重编译**
- `sin_core`（STATIC）：core + models + utils，暴露公共头给 UI 层，链接 Qt6::Widgets、zlib、可选 pugixml
- `sin_ui`（STATIC）：ui 层，仅依赖 sin_core 和 Qt6::Svg，qcustomplot 以 PRIVATE 方式链接
- `sin`（可执行）：`main.cpp` + `resources.qrc`，仅链接 `sin_ui`

### 3.2 预编译头 (PCH)
两个库分别维护精简的 `target_precompile_headers` 列表：`sin_core` 仅包含无 Widget 的 Qt 基础头，`sin_ui` 包含完整 Widget 头，显著缩短编译时间。

### 3.3 第三方依赖管理
全部第三方库以源码形式放入 `third_party/`：
- **单头文件库**：`spdlog`、`nlohmann_json`、`concurrentqueue`、`pugixml` —— 通过 `INTERFACE_INCLUDE_DIRECTORIES` 暴露
- **源码静态库**：`qcustomplot` —— 直接 `add_library(... STATIC)` 编译进产物
- **子目录集成**：`vector_blf` —— 通过 `add_subdirectory` 集成其自带 CMakeLists
- **条件包含**：所有 third_party include 路径均用 `if(EXISTS ...)` 保护，保证仓库干净时仍可配置

### 3.4 运行时依赖处理
- **驱动 DLL**：构建后通过 `POST_BUILD` 命令将整个 `driver/` 目录复制到 exe 同级目录（ZLG SDK 的 `zlgcan.dll` 必须与 exe 同目录才能找到 USB 驱动），安装规则也复制 `*.dll`
- **Qt 部署**：`windeployqt` 无法自动识别 qcustomplot 静态库对 `Qt6PrintSupport.dll` 的传递依赖，脚本会手动补充复制该 DLL
- **输出目录**：`CMAKE_RUNTIME_OUTPUT_DIRECTORY = ${CMAKE_BINARY_DIR}/bin`，最终产物位于 `build/bin/sin.exe`

### 3.5 构建脚本工作流
`scripts/build.py` 提供统一 CLI：
- `configure`：环境检查 → 清理（可选）→ 调用 CMake 配置（自动选 Ninja/MinGW Makefiles）
- `build`：增量编译，首次运行自动触发 configure；编译前自动 `taskkill /F /IM sin.exe` 释放文件锁
- `run`：启动已编译程序
- `debug`：通过 GDB 启动（需安装 gdb）
- `clean`：删除 `build/` 目录
- `rebuild`：clean + configure + build
- `deploy`：调用 `windeployqt` 并补全缺失的 Qt DLL
- `all`：完整流水线（configure → build → deploy → run）
- `status`：显示环境与构建状态
- `open`：在资源管理器中打开构建输出目录

## 4. 约定与约束

- **构建类型**：支持 Debug / Release / RelWithDebInfo / MinSizeRel，默认 Debug
- **并行编译**：通过 `--jobs N` 或默认 CPU 核心数传递给底层构建系统
- **Qt 自动化**：启用 `AUTOMOC/AUTOUIC/AUTORCC`，无需手写 moc/uic/rcc 命令
- **Windows GUI 模式**：通过 `WIN32_EXECUTABLE TRUE` 属性隐藏控制台窗口
- **资源打包**：UI 界面使用 HTML/CSS/JS 原型（`UI/` 目录）与 Qt 资源系统分离，实际运行时资源通过 `resources.qrc` 嵌入
- **版本管理**：版本号定义在顶层 `CMakeLists.txt` 的 `project()` 中（当前 0.1.0），未看到独立的版本文件或 CI 发布流程
- **无 CI/CD**：仓库中未发现 GitHub Actions、GitLab CI、Jenkins 等配置文件，构建完全依赖本地 `scripts/build.py`
- **无 Docker**：无 Dockerfile 或容器化配置
- **测试数据**：`test/` 目录存放 `.blf/.asc/.dbc` 等测试数据文件，非单元测试代码