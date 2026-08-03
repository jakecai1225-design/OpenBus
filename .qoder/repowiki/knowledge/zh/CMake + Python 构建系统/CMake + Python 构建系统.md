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

本项目采用 CMake 作为核心构建系统，配合 Python 脚本 scripts/build.py 提供统一的构建入口，面向 Windows + MinGW + Qt6 桌面应用（sin.exe）的编译、打包与部署。

**1. 使用的系统与工具**
- 构建系统：CMake 3.21+，生成器优先 Ninja，回退 MinGW Makefiles
- 编译器：MinGW g++/gcc（默认路径 C:/Qt/Tools/mingw1310_64）
- GUI 框架：Qt6（Widgets、PrintSupport），通过 qt_standard_project_setup() 启用 MOC/UIC/RCC 自动化
- 链接器：LLD（本地 tools/lld/ld.lld.exe，未检测到则回退 ld.bfd）
- 加速工具：Ninja（并行调度）、ccache（编译缓存）、LLD（多线程链接）
- 部署：windeployqt 自动收集 Qt DLL，额外手动复制 Qt6PrintSupport.dll

**2. 关键文件与结构**
- CMakeLists.txt（根）：项目元信息、C++17 标准、Qt 查找、第三方依赖引入、子目录挂载
- src/CMakeLists.txt：源文件分组（core/models/utils/ui 四层）、静态库拆分（sin_core、sin_ui）、可执行目标 sin、PCH 预编译头配置
- third_party/Dependencies.cmake：第三方库声明（spdlog 接口库、nlohmann_json 单头文件、qcustomplot 静态库、vector_blf 子目录）
- scripts/build.py：Python 构建脚本，封装 configure/build/run/debug/clean/rebuild/deploy/all/status/open 等命令

**3. 架构与约定**
- 分层静态库设计：sin_core（核心逻辑 + 模型 + 工具）→ sin_ui（界面组件）→ sin（可执行），修改 UI 不触发 core 重编译
- PCH 优化：core 层仅预编译无 Widget 的 Qt 基础头，ui 层预编译完整 Widget 头列表，显著缩短增量编译时间
- 调试优化：Windows + GCC Debug 模式使用 -g1 精简调试信息，减小二进制体积并提升链接速度
- 依赖隔离：vector_blf、qcustomplot 等仅被个别源文件使用，通过 PRIVATE 链接避免污染公共接口
- 资源管理：通过 resources/resources.qrc 嵌入样式与资源，由 AUTORCC 自动生成

**4. 约定与约束**
- 构建类型：Debug / Release / RelWithDebInfo / MinSizeRel，默认 Debug
- 输出目录：build/bin/sin.exe
- 环境变量覆盖：SIN_QT_DIR、SIN_MINGW_DIR、SIN_CMAKE_DIR 可自定义工具链路径
- 加速工具检测：tools/ninja/、tools/ccache/、tools/lld/ 目录存在即自动启用
- 安装规则：install(TARGETS sin RUNTIME DESTINATION bin) 定义安装布局
- 版本管理：CMake project 中声明 VERSION 0.1.0，随构建产物传播