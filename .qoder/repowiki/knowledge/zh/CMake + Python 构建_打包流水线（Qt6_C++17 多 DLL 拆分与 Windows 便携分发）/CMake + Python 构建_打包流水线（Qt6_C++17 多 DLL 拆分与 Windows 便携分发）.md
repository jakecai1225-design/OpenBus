---
kind: build_system
name: CMake + Python 构建/打包流水线（Qt6/C++17 多 DLL 拆分与 Windows 便携分发）
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - drivers/CMakeLists.txt
    - drivers/zlg/CMakeLists.txt
    - third_party/Dependencies.cmake
    - scripts/build.py
    - scripts/package.py
    - scripts/make_market.py
    - scripts/plugin_tool.py
    - scripts/driver_tool.py
---

## 1. 构建系统与工具链

- **构建系统**：CMake 3.21+，顶层 `CMakeLists.txt` 定义项目 `openbus`（VERSION 0.1.0），通过 `add_subdirectory(src)`、`drivers` 组织子工程。
- **编译器/语言**：C++17（`CMAKE_CXX_STANDARD 17`，关闭扩展），Windows MinGW 默认使用 `ld.bfd` 链接器（注释明确禁止 LLD/gold，因 PE 文件锁/子系统选项问题）；禁用 ccache（与 MinGW g++ 13 PCH 不兼容会静默崩溃）。
- **Qt6 集成**：`find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg Network)` + `qt_standard_project_setup()`，启用 `AUTOMOC/AUTOUIC/AUTORCC`。MinGW 下额外清空 `CMAKE_MOC_PREDEFS_CMD` 以修复 AutoMoc 失败。
- **生成器**：优先 Ninja（从 Qt Tools/ninja 或 `tools/ninja.exe` 探测），回退到 MinGW Makefiles；`scripts/build.py` 统一封装配置/编译/运行/调试/清理/部署流程。
- **并行构建**：Ninja/Make 均支持 `-j`；Dev 档（`--build-type Dev`，`-O1 -g1`）独立目录 `build-dev/`，与全量 Debug `build/` 并存避免切档重编。
- **静态库 thin archive**：MinGW 下将 `ar qcT/qT` 设为 thin archive，仅记录对象路径，使静态库重打包降为毫秒级（仅限本构建树内部链接，不出库）。

## 2. 目标拆分架构（DLL 模块化）

`src/CMakeLists.txt` 将应用拆分为多个共享库 + 一个可执行，模块间通过 C 工厂函数（如 `openbus_createTraceModule()`）暴露最小 ABI 面，由主程序 `main.cpp` 动态加载：

| 目标 | 类型 | 职责 |
|---|---|---|
| `openbus_data` | SHARED | 公共底座（core/models/utils/thememanager），所有单例保持进程唯一 |
| `openbus_market` | SHARED | 插件市场页 |
| `openbus_transceive` | SHARED | 发送/回放/离线分析/录制四页面 |
| `openbus_dbc` | SHARED | DBC 详情页 + 信号清单工具 |
| `openbus_flow` | SHARED | 测量流程配置 + 设备连接页 |
| `openbus_trace` | SHARED | Trace 视图（过滤栏/报文列表/Explorer 底栏） |
| `openbus_graphic` | SHARED | GraphicView（qcustomplot 曲线）+ DataWindow |
| `openbus_ui` | STATIC | 壳 UI 剩余组件（改 UI 只重编此库 + 最终链接） |
| `openbus` | EXECUTABLE | 主程序，链接上述全部模块 |

每个业务 DLL 都定义了独立的 `target_precompile_headers` 头清单，按实际使用的 Qt 头裁剪 PCH 体积。

## 3. 驱动插件构建

`drivers/<id>/CMakeLists.txt` 使用 `qt_add_plugin()` 输出 `.dll` 到 `build/bin/drivers/<id>/`，并通过 `deploy_driver_manifest` POST_BUILD 命令复制 `driver.json`，形成与 `.odp` 包一致的布局。5 个内置驱动（zlg/peak/kvaser/slcan/candle）在 Release 打包时由 `package.py` 校验完整性。

## 4. 第三方依赖管理

`third_party/Dependencies.cmake` 集中声明：
- `spdlog`：INTERFACE IMPORTED，仅提供 include 路径。
- `nlohmann/json`：单头文件，无需链接。
- `qcustomplot`：源码静态库（`qcustomplot.cpp` 直接编译），链接 `Qt6::Widgets` + `Qt6::PrintSupport`。
- `vector_blf`：可选子目录集成，缺失时发出 WARNING 并禁用 BLF 支持。
- `pugixml`：条件链接（`if(TARGET pugixml)`），供 ARXML 导入使用。

## 5. 打包与分发流水线

`scripts/package.py` 实现完整发布流水线（参考 `doc/打包安装方案.md` §5），步骤幂等可重复：

1. **Release 构建**：调用 `build.py configure --build-type Release --build-dir build-rel`，再全量构建（含驱动插件）。
2. **windeployqt 部署**：`--release --no-translations --compiler-runtime`，手动补拷 `Qt6PrintSupport.dll`（qcustomplot 静态库传递依赖）、回拷 `qtbase_zh_CN.qm`、创建空 `lib/fonts/` 目录消解字体告警。
3. **staging 组装**：白名单拷贝 `openbus.exe`、`libopenbus_*.dll`、Qt/MinGW DLL 及 `platforms/imageformats/iconengines/styles/tls/networkinformation` 目录；`plugins/` 不预装（一律从市场安装）；`driver/` 与 `drivers/` 均不随包（ZLG SDK 授权限制）。
4. **Python 运行时捆绑**：精简本机安装版 Python，写入 `pythonXY._pth` 隔离 sys.path，裁剪 PyQt6（仅 QtCore/Gui/Widgets/Svg + 对应 Qt6 运行时 + platforms/plugins），自验证 `import PyQt6.QtCore` 通过。
5. **附加文件**：`THIRD_PARTY_NOTICES.md` / `README-PORTABLE.txt` / `LICENSE.txt`。
6. **依赖完整性校验**：`objdump -p` 扫描 staging 内所有 exe/dll/pyd 的 import 表，逐一核对是否在 staging 或系统白名单（`SYSTEM_DLLS` 集合），缺失即 FAIL。
7. **报告**：生成 `dist/package-report.txt`，统计各目录体积。
8. **出口**：`dist/openbus-<ver>-win64-portable.zip`（ZIP_DEFLATED level 9）；可选 Inno Setup 安装器（`installer/openbus.iss`，iscc 不存在则跳过不阻塞）。

版本来源：读取 `CMakeCache.txt` 中 `openbus_VERSION`，回退解析根 `CMakeLists.txt` 的 `VERSION` 字段。

## 6. 关键约定与约束

- **构建产物目录**：`build/bin/` 下输出 `openbus.exe` + 业务 DLL；驱动输出到 `build/bin/drivers/<id>/`；开发态 `sdk/` 与宿主脚本 (`sin_host.py`, `plugin_tool.py`, `driver_tool.py`) 通过 POST_BUILD 复制到输出目录。
- **Qt 运行时部署**：`scripts/build.py deploy` 和 `package.py` 均调用 `windeployqt`，且都需手动补充 `Qt6PrintSupport.dll`。
- **PCH 策略**：每个 target 显式声明 `target_precompile_headers` 私有头清单，核心层用精简 Qt 头（无 Widget），UI 层用完整 Widget 头。
- **MinGW 特殊处理**：禁用 ccache、禁用 LLD/gold、thin archive、`CMAKE_MOC_PREDEFS_CMD=""`、`WIN32_EXECUTABLE TRUE`。
- **驱动/插件不预装**：`driver/`（ZLG SDK）与 `drivers/`（.odp）均不随包；`plugins/` 一律从市场安装，纯净启动下 `discoverPlugins` 对缺失目录优雅跳过。
- **测试入口**：`scripts/build.py test` 触发 `cmake --build ... --target tests` + `ctest --test-dir <build> --output-on-failure`，但当前仓库未见 `tests/` 目录，该能力预留。
- **环境变量**：`SIN_QT_DIR` / `SIN_MINGW_DIR` / `SIN_CMAKE_DIR` 覆盖默认工具路径（`C:/Qt/...`、`C:/Program Files/CMake`）。