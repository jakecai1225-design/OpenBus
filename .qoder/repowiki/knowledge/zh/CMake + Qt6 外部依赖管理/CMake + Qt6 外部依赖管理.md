---
kind: dependency_management
name: CMake + Qt6 外部依赖管理
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
---

本仓库采用 CMake 作为构建系统，通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets)` 查找并链接 Qt6 框架，未使用任何包管理器（如 vcpkg、Conan、pkg-config）或 vendoring 策略。所有第三方依赖（Qt6 Widgets）由 CMake 的 `find_package` 机制在构建时从系统或用户指定路径解析，属于“系统级依赖”模式。

关键特征：
- 无版本锁定文件（无 go.mod、package.json、vcpkg.json、conanfile.txt 等），依赖版本由 CMake 配置中的 `cmake_minimum_required(VERSION 3.21)` 和 `find_package(Qt6 ...)` 隐式决定。
- 无私有注册表或代理配置，完全依赖本地已安装的 Qt6。
- 仅依赖 Qt6::Widgets 组件，未引入其他第三方库。
- 安装规则仅将可执行文件输出到 bin 目录，未处理 Qt 运行时依赖的打包。

约束与约定：
- 构建环境必须预先安装匹配的 Qt6 开发包，且 CMake 能自动找到它。
- 新增依赖需通过 `target_link_libraries` 显式声明，保持 PRIVATE 可见性。
- 头文件搜索路径通过 `target_include_directories` 集中管理。