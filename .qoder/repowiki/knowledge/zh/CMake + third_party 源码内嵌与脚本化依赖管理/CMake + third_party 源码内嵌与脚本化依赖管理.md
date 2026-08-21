---
kind: dependency_management
name: CMake + third_party 源码内嵌与脚本化依赖管理
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - third_party/Dependencies.cmake
    - scripts/build.py
    - scripts/package.py
    - scripts/download_libusb.py
    - installer/openbus.iss
    - plugins/uds-diagnostic/plugin.json
    - drivers/candle/driver.json
---

## 1. 使用的系统与方法

- **构建系统**：CMake（`cmake_minimum_required(VERSION 3.21)`，C++17），顶层 `CMakeLists.txt` 通过 `add_subdirectory(src|drivers|tests)` 聚合工程；Qt6 通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg Network)` 查找。
- **第三方依赖策略**：**源码内嵌（vendored）**。所有 C++ 第三方库以源码形式置于 `third_party/` 目录，由 `third_party/Dependencies.cmake` 统一暴露为 CMake target 或 include-only 头文件，主工程不依赖任何外部包管理器。
- **Python 依赖**：sin 插件侧使用 Python 标准库 + PyQt6（仅 uds-diagnostic 插件需要）。打包时通过 `scripts/package.py` 从本机已安装的 Python 环境裁剪出最小运行时（含 PyQt6 子集）并随包分发，而非通过 pip 在目标机器安装。
- **动态二进制依赖**：驱动侧的 `libusb-1.0.dll` 通过 `scripts/download_libusb.py` 从 PyPI wheel（`libusb1` / `libusb-package`）自动下载并放入 `driver/`，再由 CMake POST_BUILD 拷贝到 exe 同级。
- **打包与部署**：`scripts/build.py` 封装 CMake/Ninja/MinGW/Qt 工具链探测、配置、编译、`windeployqt` 部署；`scripts/package.py` 实现完整流水线（Release 构建 → windeployqt → staging 白名单组装 → Python 运行时捆绑 → THIRD_PARTY_NOTICES → objdump import 表校验 → zip/Inno Setup 输出）。

## 2. 关键文件

- `CMakeLists.txt`：工程入口，声明 Qt 组件、包含 `third_party/Dependencies.cmake`、添加子目录。
- `src/CMakeLists.txt`：业务 DLL（`openbus_data`）及测试目标定义。
- `third_party/Dependencies.cmake`：spdlog（INTERFACE IMPORTED）、qcustomplot（静态库）、vector_blf（`add_subdirectory`）等依赖的统一接入点。
- `third_party/`：实际 vendored 源码——`spdlog/`、`nlohmann_json/`、`pugixml/`、`qcustomplot/`、`vector_blf/`、`concurrentqueue/`、`dbcppp/`、`dbcppp_src/`。
- `scripts/build.py`：构建脚本，默认路径 `SIN_QT_DIR`/`SIN_MINGW_DIR`/`SIN_CMAKE_DIR` 环境变量覆盖，支持 Dev/Debug/Release/RelWithDebInfo/MinSizeRel 多档并存（`build/` 与 `build-dev/`）。
- `scripts/package.py`：打包流水线，维护 `BIN_FILES`/`BIN_DIRS`/`SYSTEM_DLLS`/`PYQT6_MODULES` 等白名单，执行 `objdump -p` 做依赖完整性门禁。
- `scripts/download_libusb.py`：从 PyPI wheel 提取 64 位 `libusb-1.0.dll` 并落盘至 `driver/libusb-1.0.dll`。
- `installer/openbus.iss`：Inno Setup 安装器脚本（可选，缺失则跳过）。
- `plugins/*/plugin.json`、`drivers/*/driver.json`：插件/驱动元数据，配合市场索引机制。

## 3. 架构与约定

- **vendor 隔离**：每个第三方库独立子目录，不混入源码树；新增依赖需在 `third_party/Dependencies.cmake` 中显式 `add_library`/`add_subdirectory` 并设置 include 目录，禁止直接 `#include <xxx>` 而不声明 target。
- **头文件-only 库**（如 nlohmann/json、spdlog、pugixml、concurrentqueue）以 INTERFACE 目标暴露 include 路径，零链接开销。
- **需编译的库**（qcustomplot、vector_blf）通过 `add_library(... STATIC ...)` 或 `add_subdirectory` 纳入构建树，并由 `Dependencies.cmake` 统一处理 Qt 传递依赖（如 qcustomplot 链接 `Qt6::Widgets`、`Qt6::PrintSupport`）。
- **Qt 运行时**：通过 `windeployqt --release --no-translations --compiler-runtime` 部署，再手动补拷 `Qt6PrintSupport.dll`（qcustomplot 静态库的传递依赖未被 windeployqt 识别）。
- **Python 运行时**：打包时从本机解释器精简复制核心 DLL/Lib/site-packages，写 `pythonXY._pth` 隔离 sys.path，只拷贝 PyQt6 的 QtCore/Gui/Widgets/Svg 四个模块及其 Qt6 运行时 DLL，生成 `requirements-lock.txt` 记录版本。
- **驱动 SDK**：`driver/zlgcan.dll`、`driver/zlgcan.lib` 因厂商授权不随包分发（见 `package.py` 注释 §7），用户需按 README-PORTABLE 自行安装 ZCANPRO；但 `driver/kerneldlls/`、`LICENSE.txt`、`zlgcan.h` 会随包。
- **构建工具链**：优先 Ninja（`tools/ninja/ninja.exe`），回退 MinGW Makefiles；禁用 ccache（与 MinGW g++ 13 PCH 不兼容）和 LLD（Windows 文件锁问题），强制使用 `ld.bfd`。

## 4. 约定与约束

- **禁止裸引入未声明依赖**：所有第三方库必须经 `third_party/Dependencies.cmake` 注册，否则 CMake configure 阶段即失败。
- **Qt 组件变更需同步两处**：`find_package(Qt6 REQUIRED COMPONENTS ...)` 与 `scripts/package.py` 中的 `BIN_FILES`/`BIN_DIRS`/`SYSTEM_DLLS` 白名单，否则打包产物缺少运行时 DLL。
- **新增可执行/DLL 需加入白名单**：`package.py` 的 `BIN_FILES`/`BIN_DIRS` 是 staging 拷贝的唯一依据，不在白名单内的产物不会进入便携包。
- **Python 插件依赖冻结**：PyQt6 相关版本通过打包时 `importlib.metadata` 写入 `requirements-lock.txt` 固化，避免目标机器版本漂移。
- **依赖完整性门禁**：`step_verify_deps` 对 staging 内全部 `.exe/.dll/.pyd` 调用 `objdump -p` 解析 import 表，凡不在 `staged` 集合且不在 `SYSTEM_DLLS` 白名单的 DLL 均视为缺失并终止打包。
- **版本来源单一**：版本号来自根 `CMakeLists.txt` 的 `project(... VERSION X.Y.Z)`，`package.py` 通过读取 `CMakeCache.txt` 的 `openbus_VERSION` 或正则解析 `CMakeLists.txt` 获取，禁止散落在多处。
- **Dev/Release 双档并行**：`build/`（全量 Debug）与 `build-dev/`（`-O1 -g1`）独立构建目录，互不影响增量编译。
- **网络访问限制**：`download_libusb.py` 仅在开发机主动运行；最终发布产物不包含网络拉取逻辑，所有二进制依赖必须在 staging 前就绪。