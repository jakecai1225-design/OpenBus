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

### 1. 系统与工具链
- 构建系统：CMake 3.21+，生成器优先使用 Ninja（若存在 tools/ninja/ninja.exe），否则回退到 MinGW Makefiles。
- 编译器：MinGW-w64 g++/gcc（默认路径 C:/Qt/Tools/mingw1310_64/bin），通过环境变量 SIN_MINGW_DIR 覆盖。
- Qt 依赖：Qt6（Widgets、PrintSupport 等），通过 CMAKE_PREFIX_PATH 指向 Qt 安装目录（默认 C:/Qt/6.8.3/mingw_64）。
- 可选加速工具：ccache（编译缓存）、LLD（ld.lld.exe，链接加速 3-5x），均放在项目 tools/ 目录下自动检测。
- 部署：windeployqt 打包 Qt 运行时依赖，并手动补充 Qt6PrintSupport.dll（qcustomplot 静态库的传递依赖）。

### 2. 核心文件与结构
- 顶层 CMakeLists.txt：定义项目 sin v0.1.0、C++17、启用 AUTOMOC/AUTOUIC/AUTORCC、输出目录 bin、查找 Qt6、包含 third_party/Dependencies.cmake、add_subdirectory(src)。
- src/CMakeLists.txt：按层组织源文件集合（SRC_CORE / SRC_MODELS / SRC_UI），构建两个静态库 sin_core 和 sin_ui，最终链接生成可执行目标 sin；配置 PCH 预编译头以加速编译；Windows 下设置 WIN32_EXECUTABLE 隐藏控制台。
- scripts/build.py：统一入口脚本，提供 configure/build/run/debug/clean/rebuild/deploy/all/status/open 子命令，自动探测工具链、设置 PATH、调用 CMake 并支持 -j 并行。
- third_party/Dependencies.cmake：声明第三方依赖（spdlog INTERFACE、nlohmann_json 单头、qcustomplot 静态库、vector_blf 子目录），按需 add_library/add_subdirectory。

### 3. 架构与约定
- 分层静态库：sin_core（core/models/utils，含 spdlog/json/concurrentqueue 公共头）→ sin_ui（ui 组件，依赖 sin_core 与 qcustomplot）→ sin（可执行）。修改 UI 代码仅重编译 sin_ui，不触发 core 重编译。
- PCH 策略：为 sin_core 和 sin_ui 分别定义精简/完整的 Qt 头列表作为 target_precompile_headers，减少重复编译开销。
- 依赖可见性：仅被特定源文件使用的库（如 vector_blf、qcustomplot）通过 PRIVATE 链接，避免污染上游 include 路径。
- 资源管理：resources.qrc 通过 qt_add_executable 嵌入，样式 default.qss 位于 resources/styles/。
- 构建优化：Debug 模式使用 -g1 精简调试信息；GCC on Windows 下自动尝试 -fuse-ld=lld 链接器；Ninja 作为首选生成器以获得更快调度。

### 4. 约定与约束
- 构建类型：支持 Debug / Release / RelWithDebInfo / MinSizeRel，默认 Debug。
- 工具路径优先级：命令行 --qt-dir/--mingw-dir/--cmake-dir > 环境变量 SIN_*_DIR > 脚本内置默认值。
- 加速工具检测：scripts/build.py 在 tools/ 下寻找 ninja/ccache/lld，存在则自动加入 PATH 并启用。
- 部署约束：windeployqt 无法识别 qcustomplot 对 Qt6PrintSupport 的传递依赖，需手动复制 Qt6PrintSupport.dll 至输出目录。
- 安装规则：顶层与 src/CMakeLists.txt 均定义 install(TARGETS sin RUNTIME DESTINATION bin)，遵循 CMake 标准安装布局。
- 版本与描述：project(sin VERSION 0.1.0 DESCRIPTION "报文解析、分析、回放、录制、trace、graphic 桌面软件") 集中声明。

### 5. 典型工作流
- 首次构建：python scripts/build.py configure --build-type Release → python scripts/build.py build -j8 → python scripts/build.py deploy → python scripts/build.py run
- 增量开发：直接 python scripts/build.py build -j8（自动跳过已配置的步骤）
- 清理重建：python scripts/build.py rebuild
- 调试：python scripts/build.py debug（自动拉起 GDB）

关键文件：CMakeLists.txt、src/CMakeLists.txt、scripts/build.py、third_party/Dependencies.cmake