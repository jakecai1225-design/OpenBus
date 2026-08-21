---
kind: build_system
name: CMake + Python 构建脚本驱动的 Qt6 多 DLL 模块化构建系统
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - scripts/build.py
    - third_party/Dependencies.cmake
    - drivers/CMakeLists.txt
    - tests/CMakeLists.txt
---

## 1. 构建系统与工具链

项目采用 **CMake 3.21+** 作为核心构建系统，配合自研的 **Python 构建脚本 `scripts/build.py`** 封装配置、编译、运行、调试、部署、测试等完整工作流。编译器为 **MinGW g++ 13 (x64)**，目标语言标准为 **C++17**（强制开启，禁止扩展）。Qt 版本固定为 **Qt6**，启用 `qt_standard_project_setup()` 及 AUTOMOC/AUTOUIC/AUTORCC。

构建生成器优先使用本地自带的 **Ninja**（位于 `tools/ninja/`），未检测到时回退到 **MinGW Makefiles**。链接器固定使用默认 `ld.bfd`（注释明确禁用 LLD/gold，因 Windows 文件锁问题）；归档器通过 `ar qcT/qT` 使用 **thin archive** 将静态库重打包降为毫秒级。

构建类型支持 `Dev`（-O1 -g1，日常开发快速档）、`Debug`、`Release`、`RelWithDebInfo`、`MinSizeRel`。Dev 档建议独立目录 `build-dev/`，与全量 Debug (`build/`) 并存避免切换重编。

## 2. 关键文件与目录

- `CMakeLists.txt`：根工程定义、Qt6 查找、第三方依赖引入、子目录组织
- `src/CMakeLists.txt`：核心构建逻辑——定义 `openbus_data`、`openbus_market`、`openbus_transceive`、`openbus_dbc`、`openbus_flow` 五个共享 DLL，`openbus_ui` 静态库，以及最终 `openbus` 可执行体
- `scripts/build.py`：统一入口，封装 configure/build/run/debug/clean/rebuild/deploy/test/status/open 等子命令
- `third_party/Dependencies.cmake`：声明 spdlog（INTERFACE）、qcustomplot（STATIC）、vector_blf（可选 subdirectory）
- `drivers/CMakeLists.txt`：驱动插件聚合，每个驱动（zlg/peak/kvaser/slcan/candle）输出到 `build/bin/drivers/<id>/`
- `tests/CMakeLists.txt`：L1 集成测试（Qt Test，每个套件独立进程）+ L2 offscreen UI 测试，聚合目标 `tests`

## 3. 架构与设计约定

### 3.1 模块化 DLL 拆分（“壳 + 业务模块”）
主程序 `openbus` 仅负责启动和模块注册，核心能力拆分为多个 DLL：
- `openbus_data`（SHARED）：公共底座，包含 core/models/utils/thememanager，所有 core 单例（AppConfig/DbcManager/PluginManager/ThemeManager/ModuleRegistry）在此保持全进程唯一实例
- `openbus_market` / `openbus_transceive` / `openbus_dbc` / `openbus_flow`：各业务模块 DLL，通过 C 工厂函数（如 `openbus_createMarketModule()`）暴露最小 ABI 面
- `openbus_ui`（STATIC）：剩余界面组件，改 UI 代码只重编译此库 + 最终链接

### 3.2 预编译头（PCH）策略
每个目标分别定义 `target_precompile_headers`，按层裁剪 Qt 头范围：`openbus_data` 仅含 QObject/QString 等轻量头，UI 层才引入 QWidget/QMainWindow 等重型头，显著缩短编译时间。

### 3.3 驱动插件机制
驱动以 `.odp` 包形式分发，每个驱动子目录含 `driver.json` 清单、`*_driver_plugin.cpp/h` 实现，编译后自动拷贝 `driver.json` 至输出目录，由 `DriverRegistry` 在 `<exe>/drivers/<id>/` 扫描加载。ABI 契约要求与主程序同 Qt 版本 + 同编译器。

### 3.4 资源与依赖管理
- Qt 资源通过 `resources/resources.qrc` 编译进二进制
- 第三方库以源码形式置于 `third_party/`，通过条件 `if(EXISTS ...)` 按需启用
- ZLG SDK 的 `driver/` 目录在 POST_BUILD 阶段复制到 exe 同级目录（`zlgcan.dll` 必须与 exe 同目录）

## 4. 构建流程与约束

### 4.1 标准工作流
```bash
python scripts/build.py configure --build-type Dev --build-dir build-dev
python scripts/build.py build -j8
python scripts/build.py run
python scripts/build.py deploy          # windeployqt + 手动补 Qt6PrintSupport.dll
python scripts/build.py test            # ctest 执行 L1/L2 测试
```
环境变量 `SIN_QT_DIR` / `SIN_MINGW_DIR` / `SIN_CMAKE_DIR` 覆盖默认路径。

### 4.2 强制约束
- **禁用 ccache**：与 MinGW g++ 13 的 PCH 不兼容，会静默崩溃
- **禁用 LLD/gold**：Windows 上导致文件锁或子系统选项错误
- **构建前自动终止 openbus.exe**：通过 `taskkill` 释放文件锁，避免链接失败
- **测试套件隔离**：每个测试独立进程，避免 core 层单例状态污染
- **Offscreen UI 测试**：通过 `QT_QPA_PLATFORM=offscreen` 在无头环境运行 UI 测试
- **windeployqt 限制处理**：无法检测 qcustomplot 对 Qt6PrintSupport 的传递依赖，需手动复制；Qt 6 Windows 不再自带字体，需创建空 `lib/fonts/` 目录消警

### 4.3 安装规则
`install(TARGETS openbus RUNTIME DESTINATION bin)`，并递归安装 `driver/` 下所有 `.dll` 到 `bin/`。