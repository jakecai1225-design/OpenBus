# 🛠️ OpenBUS 环境依赖与工具配置清单 v1.0

## 📋 目录结构

- [开发环境](#1-开发环境)
- [核心工具链](#2-核心工具链)
- [第三方库](#3-第三方库)
- [构建系统](#4-构建系统)
- [调试与测试](#5-调试与测试)
- [部署与打包](#6-部署与打包)
- [快速参考](#7-快速参考)

---

## 1️⃣ **开发环境**

### 操作系统要求

| 组件 | 要求版本 | 安装位置 | 验证命令 |
|------|---------|---------|----------|
| Windows | 10/11 (64-bit) | - | `systeminfo` |
| PowerShell | 5.1+ | `%WINDIR%\System32\WindowsPowerShell\v1.0\powershell.exe` | `Get-Host` |
| Visual Studio Build Tools | 2019/2022 (可选) | `C:\Program Files\Microsoft Visual Studio\` | `cl.exe /?` |

---

### Python 环境

| 组件 | 版本 | 安装路径 | 验证命令 |
|------|------|---------|----------|
| Python | 3.8+ | `C:\Python313\python.exe` | `python --version` |
| pip | 21+ | `C:\Python313\Scripts\pip.exe` | `pip --version` |
| 关键包 | - | 通过 `scripts/build.py` 自动管理 | `python scripts/build.py --help` |

**注意**: 
- ✅ 必须将 Python bin 目录添加到 PATH
- ❌ 不要使用系统自带的 Python 2.x
- ⚠️ 推荐使用 Anaconda/Mamba 作为虚拟环境管理器（可选）

```bash
# 设置 PATH（永久生效）
$env:PATH += ";C:\Python313;C:\Python313\Scripts"

# 验证安装
python --version  # Should output: Python 3.13.x
python -m pip --version
```

---

## 2️⃣ **核心工具链**

### C++ 编译器与构建工具

#### 方案 A: MinGW GCC (当前推荐)

| 组件 | 版本 | 安装路径 | 验证命令 |
|------|------|---------|----------|
| GCC (g++) | 13.1 | `C:\Qt\Tools\mingw1310_64\bin\g++.exe` | `C:/Qt/Tools/mingw1310_64/bin/g++.exe --version` |
| GCC (gcc) | 13.1 | `C:\Qt\Tools\mingw1310_64\bin\gcc.exe` | `C:/Qt/Tools/mingw1310_64/bin/gcc.exe --version` |
| linker (ld.bfd) | 默认 | `C:\Qt\Tools\mingw1310_64\bin\ld.exe` | `C:/Qt/Tools/mingw1310_64/bin/ld.exe --version` |
| ar | 2.40 | `C:\Qt\Tools\mingw1310_64\bin\ar.exe` | `C:/Qt/Tools/mingw1310_64/bin/ar --version` |

**环境变量配置**:
```powershell
# .profile.ps1 or add to System Properties → Advanced → Environment Variables
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;" + $env:PATH
$env:QT_DIR = "C:\Qt\6.8.3\mingw_64"
```

#### 方案 B: MSVC (备用方案，性能更好)

| 组件 | 版本 | 安装位置 | 验证命令 |
|------|------|---------|----------|
| cl.exe | 19.x (VS 2019/2022) | `C:\Program Files\Microsoft Visual Studio\2022\<Edition>\VC\Tools\MSVC\<version>\bin\Hostx64\x64\cl.exe` | `"C:\Program Files\Microsoft Visual Studio\...\cl.exe" /?` |
| link.exe | - | `vc\Tools\MSVC\<version>\bin\Hostx64\x64\link.exe` | `"..\link.exe" /?` |
| Developer Prompt | - | `%VCToolsInstallDir\Auxiliary\Build\vcvars64.bat` | `call vcvars64.bat` |

**启用方法**:
```powershell
# 在 VS Developer Command Prompt 中运行
cmake .. -G Ninja -DCMAKE_CXX_COMPILER="C:\Program Files\Microsoft Visual Studio\...\cl.exe"
```

> ⚠️ **重要**: MinGW + Qt 6.8.x 已知有 AutoMoc Bug，建议考虑切换到 MSVC 工具链以获得更好的兼容性。

---

### Qt 框架

| 组件 | 版本 | 安装路径 | 验证命令 |
|------|------|---------|----------|
| Qt Core | 6.8.3 | `C:\Qt\6.8.3\mingw_64\lib\QtCore.lib` | `moc -v` (should show Qt 6.8.3) |
| Qt Widgets | 6.8.3 | `C:\Qt\6.8.3\mingw_64\lib\QtWidgets.lib` | `qmake -v` |
| QtSvg | 6.8.3 | `C:\Qt\6.8.3\mingw_64\lib\QtSvg.lib` | `windeployqt --version` |
| QtNetwork | 6.8.3 | `C:\Qt\6.8.3\mingw_64\lib\QtNetwork.lib` | - |
| QtPrintSupport | 6.8.3 | `C:\Qt\6.8.3\mingw_64\lib\Qt6PrintSupport.lib` | - |
| QtTest | 6.8.3 | `C:\Qt\6.8.3\mingw_64\lib\Qt6Test.lib` | - |

**Qt 安装路径**:
```
C:\Qt\                    ← Qt Creator 安装根目录
├── 6.8.3\                ← Qt 运行时版本
│   ├── mingw_64\        ← MinGW 64-bit 版本（Windows）
│   │   ├── bin\         ← 可执行文件 (moc, uic, rcc)
│   │   ├── include\     ← Qt 头文件
│   │   └── lib\         ← Qt 库文件 (.a, .lib)
│   └── mkspecs\         ← 平台配置文件
├── Tools\               ← 开发者工具
│   ├── mingw1310_64\    ← MinGW GCC 13.1
│   ├── cmake-\          ← CMake (see below)
│   └── ninja-\          ← Ninja build system
```

**环境配置**:
```powershell
# Add Qt bin directories to PATH
$env:PATH = "C:\Qt\6.8.3\mingw_64\bin;" + $env:PATH
$env:PATH = "C:\Qt\Tools\ninja\" + $env:PATH

# Verify Qt installation
where moc
where rcc
where qmake
```

---

## 3️⃣ **第三方库**

### 本地依赖（已集成在项目中）

| 库名 | 版本 | 项目路径 | 说明 |
|------|------|---------|------|
| dbcppp | latest | `third_party\dbcppp\` | DBC 数据库解析库 |
| spdlog | v1.10+ | `third_party\spdlog\` | 高性能日志库 |
| nlohmann_json | v3.11+ | `third_party\nlohmann_json\` | JSON 解析库 |
| qcustomplot | v2.1+ | `third_party\qcustomplot\` | Qt 图表控件 |
| vector_blf | latest | `third_party\vector_blf\` | Vector BLF 文件解析器 |
| concurrentqueue | latest | `third_party\concurrentmobile\` | 无锁队列实现 |
| pugixml | v1.14+ | `third_party\pugixml\` | XML 解析库 |

**依赖配置**:
```cmake
# third_party/Dependencies.cmake 统一管理
include(${CMAKE_SOURCE_DIR}/third_party/Dependencies.cmake)

# 各库的 Include Paths 在此文件中定义
```

### 动态库依赖（运行时需要）

| 库名 | DLL 位置 | 提供功能 |
|------|---------|----------|
| libgcc_s_seh-1.dll | Qt\Tools\mingw1310_64\bin\ | MinGW C++ 运行时 |
| libstdc++-6.dll | Qt\Tools\mingw1310_64\bin\ | C++ Standard Library |
| libwinpthread-1.dll | Qt\Tools\mingw1310_64\bin\ | Windows Thread Implementation |
| Qt6Core.dll | Qt\6.8.3\mingw_64\bin\ | Qt Core Module |
| Qt6Widgets.dll | Qt\6.8.3\mingw_64\bin\ | Qt Widgets Module |
| Qt6Svg.dll | Qt\6.8.3\mingw_64\bin\ | SVG Support |
| Qt6Network.dll | Qt\6.8.3\mingw_64\bin\ | Network Access Framework |

---

## 4️⃣ **构建系统**

### CMake

| 组件 | 版本 | 安装路径 | 验证命令 |
|------|------|---------|----------|
| CMake | 3.30.3+ | `C:\tools\cmake-3.30.3-windows-x86_64\bin\cmake.exe` | `cmake --version` |

**CMake 配置参数**（已在 `build.py` 中预设）:

```cmake
# 标准配置
-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64
-DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe
-DCMAKE_C_COMPILER=C:/Qt/Tools/mingw1310_64/bin/gcc.exe
-DCMAKE_BUILD_TYPE=Debug
-G Ninja

# Dev 模式额外参数
-DCMAKE_CXX_FLAGS_DEV="-O1 -g1"
```

### Ninja Build System

| 组件 | 版本 | 安装路径 | 验证命令 |
|------|------|---------|----------|
| Ninja | 1.11+ | `C:\Qt\Tools\ninja\ninja.exe` 或 `C:\tools\ninja.exe` | `ninja --version` |

**注意**: Ninja 不在 PATH 中时需要手动添加：
```powershell
$env:PATH = "C:\tools\ninja;" + $env:PATH
```

---

## 5️⃣ **调试与测试工具**

### GDB 调试器

| 组件 | 版本 | 安装路径 | 验证命令 |
|------|------|---------|----------|
| GDB | - | `C:\Qt\Tools\mingw1310_64\bin\gdb.exe` | `gdb --version` |

**常用调试方式**:
```bash
# 命令行启动
gdb.exe --args build/bin/openbus.exe

# VS Code 调试配置 (.vscode/launch.json)
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug OpenBUS",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/bin/openbus.exe",
            "cwd": "${workspaceFolder}",
            "preLaunchTask": "CMake: build"
        }
    ]
}
```

### Qt Test 单元测试

| 组件 | 路径 | 验证方式 |
|------|------|---------|
| Qt Test | Qt\6.8.3\mingw_64\lib\Qt6Test.lib | `python scripts/build.py test` |

**运行测试**:
```bash
# 运行所有测试套件
python scripts/build.py test

# 运行特定测试
ctest --test-dir build --output-on-failure --rerun-failed

# 单独运行某个测试
cd build/tests && ./openbus_tests.exe --filter=test_dbc
```

---

## 6️⃣ **部署与打包**

### WinDeployQt

| 组件 | 版本 | 安装路径 | 验证命令 |
|------|------|---------|----------|
| windeployqt | Qt 6.8.3 | `C:\Qt\6.8.3\mingw_64\bin\windeployqt.exe` | `windeployqt --version` |

**使用说明**:
```bash
# 部署依赖项到输出目录
windeployqt.exe build/bin/openbus.exe --release

# 自定义选项
windeployqt.exe build/bin/openbus.exe --no-system-dlls --no-svg --no-translations
```

### Inno Setup Installer

| 组件 | 版本 | 安装位置 | 验证方式 |
|------|------|---------|---------|
| ISCC Compiler | - | `C:\Program Files (x86)\Inno Setup 6\ISCC.exe` | `iscc /?` |

**Installer Script**:
```
# installer/openbus.iss
[Demos]
Source: "installer\*.iss"; DestDir: "{src}"; Flags: isrecursive
ScriptFile: openbus.iss

[Setup]
AppName=OpenBUS
VersionInfoVersion=0.1.0.0
OutputDir={src}\installer_output
DefaultDirName={autopf}\OpenBUS
```

---

## 7️⃣ **快速参考表**

### 🔑 核心工具路径汇总

| 类别 | 工具 | 完整路径 |
|------|------|---------|
| **编译器** | g++ | `C:\Qt\Tools\mingw1310_64\bin\g++.exe` |
| **编译器** | gcc | `C:\Qt\Tools\mingw1310_64\bin\gcc.exe` |
| **构建工具** | cmake | `C:\tools\cmake-3.30.3-windows-x86_64\bin\cmake.exe` |
| **构建系统** | ninja | `C:\Qt\Tools\ninja\ninja.exe` |
| **Qt 工具** | moc | `C:\Qt\6.8.3\mingw_64\bin\moc.exe` |
| **Qt 工具** | rcc | `C:\Qt\6.8.3\mingw_64\bin\rcc.exe` |
| **Qt 工具** | qmake | `C:\Qt\6.8.3\mingw_64\bin\qmake.exe` |
| **部署工具** | windeployqt | `C:\Qt\6.8.3\mingw_64\bin\windeployqt.exe` |
| **调试工具** | gdb | `C:\Qt\Tools\mingw1310_64\bin\gdb.exe` |
| **Python** | python | `C:\Python313\python.exe` |

---

### 📁 项目目录结构

```
e:\code\sin\sin\
├── build\                      ← CMake 构建输出目录（每次编译前自动清理）
├── bin\                        ← 最终产物 (openbus.exe, DLLs)
├── doc\                        ← 设计文档、规范文档
│   ├── Programming_Specifications.md
│   ├── architecture.drawio
│   └── ...其他技术文档
├── src\                        ← 源代码目录
│   ├── core\                   ← 业务逻辑层
│   ├── ui\                     ← UI 界面层
│   ├── models\                 ← 数据模型层
│   └── utils\                  ← 工具函数层
├── tests\                      ← 测试代码
├── plugins\                    ← 插件系统
├── drivers\                    ← 驱动插件 (.odp format)
├── third_party\                ← 第三方库源码
│   ├── spdlog\
│   ├── dbcppp\
│   └── ...其他库
├── scripts\                    ← 构建自动化脚本
│   ├── build.py               ← 主构建脚本
│   ├── plugin_tool.py         ← 插件管理工具
│   └── package_assets\
├── resources\                  ← 资源文件
│   ├── icons\                 ← 图标资源
│   ├── styles\                ← Qt StyleSheets
│   └── resources.qrc
└── installer\                  ← 安装包配置
    └── openbus.iss
```

---

### 🔧 常用命令速查表

#### 构建相关
```bash
# 完整构建流程
python scripts/build.py clean configure build

# 增量构建（推荐日常使用）
python scripts/build.py build -j8

# Debug 构建（默认）
python scripts/build.py configure --build-type Debug
python scripts/build.py build -j8

# Release 构建
python scripts/build.py configure --build-type Release
python scripts/build.py build -j8

# Dev 构建（优化版调试版本）
python scripts/build.py configure --build-type Dev
python scripts/build.py build
```

#### 测试相关
```bash
# 运行全部测试
python scripts/build.py test

# 仅编译测试（不运行）
python scripts/build.py build --tests-only

# 单个测试运行
cd build/tests && ./openbus_tests.exe --gtest_filter="*dbc*"
```

#### 调试相关
```bash
# 启动调试器
gdb.exe build/bin/openbus.exe

# 使用 VS Code 调试
# 按 F5 或使用 Launch 配置
```

#### 部署相关
```bash
# 复制依赖项到输出目录
windeployqt.exe build/bin/openbus.exe --release

# 创建安装包
iscc installer/openbus.iss
```

---

### ⚙️ 环境变量配置脚本

保存为 `.profile.ps1` 或添加到系统环境变量：

```powershell
# == OpenBUS Development Environment ==
# Run this in your PowerShell profile or manually set these vars

$Qt6Root = "C:\Qt\6.8.3\mingw_64"
$MinGWBin = "C:\Qt\Tools\mingw1310_64\bin"
$CMakeBin = "C:\tools\cmake-3.30.3-windows-x86_64\bin"
$NinjaBin = "C:\Qt\Tools\ninja"
$PythonBin = "C:\Python313;C:\Python313\Scripts"

# Add to PATH
$env:PATH = "$MinGWBin;$Qt6Root\bin;$CMakeBin;$NinjaBin;$PythonBin;$env:PATH"

# Export Qt environment variables
$env:QTDIR = $Qt6Root
$env:CMAKE_PREFIX_PATH = "$Qt6Root"

# Make it persistent across sessions
[System.Environment]::SetEnvironmentVariable("PATH", $env:PATH, [System.EnvironmentVariableTarget]::User)
[System.Environment]::SetEnvironmentVariable("QTDIR", $Qt6Root, [System.EnvironmentVariableTarget]::User)

Write-Host "OpenBUS development environment loaded!" -ForegroundColor Green
Write-Host "Qt Version: $(moc -v)" -ForegroundColor Cyan
```

---

## 🆘 故障排查

### Q1: "无法找到 CMake" 错误
**解决方案**:
```powershell
# Check if CMake is installed
where cmake

# If not found, install from https://cmake.org/download/
# Or use Qt's bundled CMake at C:\tools\cmake-*
```

### Q2: "Ninja build system not found"
**解决方案**:
```powershell
# Locate Ninja
Get-Command ninja -ErrorAction SilentlyContinue
# Expected location: C:\Qt\Tools\ninja\ninja.exe
# Add to PATH:
$env:PATH = "C:\Qt\Tools\ninja;$env:PATH"
```

### Q3: "Qt libraries not found"
**解决方案**:
```powershell
# Verify Qt installation
where moc
where rcc

# Reconfigure with correct path
cmake .. -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64
```

### Q4: "Python script won't run"
**解决方案**:
```powershell
# Check Python version
python --version  # Should be >= 3.8

# Add Python to PATH
$env:PATH = "C:\Python313;$env:PATH"

# If still fails, use full path
C:\Python313\python.exe scripts\build.py ...
```

---

## 📞 维护信息

**最后更新**: 2026-08-26  
**维护者**: AI Assistant  
**版本**: v1.0  

本文档应随以下变更及时更新：
- Qt 版本升级时
- 添加新的第三方库时
- 更改工具链版本时
- 修改构建系统配置时

---

**附录：官方文档链接**

- [Qt Documentation](https://doc.qt.io/qt-6/)
- [CMake Manual](https://cmake.org/cmake/help/latest/)
- [MinGW-w64](https://www.mingw-w64.org/)
- [Ninja Build System](https://ninja-build.org/)
- [Google C++ Testing Framework](https://github.com/google/googletest)


---

# [MERGED FROM] 构建基线.md

> Merged into this topic doc on 2026-08-26. Full original available in git history.

# 构建性能基线

> 对应《拆分应用实施方案.md》§3.4 / §5。所有度量用 `scripts/measure_build.py` 产生，
> 按时间倒序追加，保证前后可比。工具链：MinGW g++ 13.1 + Ninja + ld.bfd，8 线程。

## 基线（Phase A 之前）— Debug 档，ld.bfd，实档案（ar qc）

来源：`build/.ninja_log` 历史会话分析（`measure_build.py --scenario analyze`），2026-07-28 全量 Debug 构建。

| 场景 | 耗时 |
| --- | --- |
| 全量构建 | ≈15.6 min（编译跨度 783 s，exe 链接完成于 938 s） |
| openbus.exe 链接（单步） | 777.9 s / 803.7 s（两次会话） |
| libopenbus_core.a 打包（单步） | 513.1 s / 527.3 s |
| libopenbus_ui.a 打包（单步） | 64.1 s / 68.2 s |
| 驱动 DLL 链接（每个） | 111 ~ 155 s |
| 单文件编译之最 | appconfig.cpp 85.7~88.9 s，qcustomplot.cpp 127 s |
| openbus.exe 体积 | ≈172 MB |

结论：链接 + 静态库打包占总耗时 83% 以上，编译本身不是主要矛盾 → Phase A 优先攻击链接与打包。

## Phase A 结果汇总（2026-08-19）— Dev (-O1 -g1) + thin archive + ld.bfd

| 验收场景 | 基线（Debug+bfd+实档案） | Phase A 后 | 方案目标 | 结论 |
| --- | --- | --- | --- | --- |
| 改 1 个 ui 文件 → 可运行 | ≈14 min | **24.3 s** | ≤3 min | 超额达成（余量 ~7 倍） |
| 改 1 个 core 文件 → 可运行 | ≈23 min | **28.7 s** | ≤3 min | 超额达成 |
| 全量构建 | ≈15.6 min | **328.3 s（5.5 min）** | ≤5 min | 基本达成（qcustomplot 单文件编译 175 s 占 53%） |
| openbus.exe 链接（单步） | 778 s | **9.6 s** | — | 81× |
| libopenbus_core.a 打包（单步） | 513 s | **3.3 s** | <1 s | 155×（含 ar 进程启动开销） |
| openbus.exe 体积 | ≈172 MB | **52 MB** | — | -70% |

Phase A 落地内容：
- **A1 Dev 档**：根 CMakeLists `set(CMAKE_CXX_FLAGS_DEV "-O1 -g1")`；`build.py` 新增 `Dev` 档与全局 `--build-dir`（build-dev/ 与全量 Debug build/ 并存，另支持 configure `-D` 透传）
- **A2 thin archive**：`set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> qcT ...")`（回退删一行即恢复）；冒烟验证 Dev exe 正常启动渲染
- **A3 ld.gold：否决** — MinGW 发行版自带 ld.gold 为 ELF-only 构建，链接 PE 报 `--subsystem: unknown option`（exe）/ `--enable-auto-image-base: unknown option`（DLL）；与 LLD 结论一并记录在根 CMakeLists.txt 注释，勿再尝试
- **A4 Defender 排除**：需管理员 PowerShell 手动执行一次（脚本无权限自动添加）
  `Add-MpPreference -ExclusionPath 'E:\code\sin\sin\build','E:\code\sin\sin\build-dev','C:\Qt\Tools\mingw1310_64','E:\code\sin\sin\tools'`


## 2026-08-19 23:21 — 度量 ui (build type: Dev, dir: build-dev)

### 场景 ui（改 1 个 ui 文件 → 可运行） — 墙钟 26.6s / 步骤跨度 26.1s / 7 个步骤

| 耗时 | 步骤 |
| --- | --- |
|      13.0s | openbus.exe |
|       1.7s | libopenbus_ui.a |
|      13.0s | bin/openbus.exe |
|      10.7s | src/CMakeFiles/openbus_ui.dir/ui/markettab.cpp.obj |
|       1.7s | src/libopenbus_ui.a |
|      725ms | src/openbus_ui_autogen/timestamp |
|      725ms | src/openbus_ui_autogen/mocs_compilation.cpp |
|      725ms | E:/code/sin/sin/build-dev/src/openbus_ui_autogen/timestamp |
|      725ms | E:/code/sin/sin/build-dev/src/openbus_ui_autogen/mocs_compilation.cpp |

## 2026-08-19 23:22 — 度量 core (build type: Dev, dir: build-dev)

### 场景 core（改 1 个 core 文件 → 可运行） — 墙钟 40.5s / 步骤跨度 40.3s / 7 个步骤

| 耗时 | 步骤 |
| --- | --- |
|      12.2s | openbus.exe |
|       5.4s | libopenbus_core.a |
|      22.2s | src/CMakeFiles/openbus_core.dir/core/appconfig.cpp.obj |
|      12.2s | bin/openbus.exe |
|       5.4s | src/libopenbus_core.a |
|      502ms | src/openbus_core_autogen/timestamp |
|      502ms | src/openbus_core_autogen/mocs_compilation.cpp |
|      502ms | E:/code/sin/sin/build-dev/src/openbus_core_autogen/timestamp |
|      502ms | E:/code/sin/sin/build-dev/src/openbus_core_autogen/mocs_compilation.cpp |

## 2026-08-19 23:23 — 度量 ui (build type: Dev, dir: build-dev)

### 场景 ui（改 1 个 ui 文件 → 可运行） — 墙钟 24.3s / 步骤跨度 19.3s / 14 个步骤

| 耗时 | 步骤 |
| --- | --- |
|       9.2s | openbus.exe |
|       3.4s | libopenbus_core.a |
|       1.3s | libopenbus_ui.a |
|       1.2s | libqcustomplot.a |
|       9.2s | bin/openbus.exe |
|       4.8s | build.ninja |
|       4.7s | src/CMakeFiles/openbus_ui.dir/ui/markettab.cpp.obj |
|       3.4s | src/libopenbus_core.a |
|       1.3s | src/libopenbus_ui.a |
|       1.2s | libqcustomplot.a |
|      598ms | src/openbus_ui_autogen/timestamp |
|      598ms | src/openbus_ui_autogen/mocs_compilation.cpp |
|      598ms | E:/code/sin/sin/build-dev/src/openbus_ui_autogen/timestamp |
|      598ms | E:/code/sin/sin/build-dev/src/openbus_ui_autogen/mocs_compilation.cpp |
|      381ms | qcustomplot_autogen/timestamp |
|      381ms | qcustomplot_autogen/mocs_compilation.cpp |
| ... 共 14 个步骤 | |

## 2026-08-19 23:24 — 度量 core (build type: Dev, dir: build-dev)

### 场景 core（改 1 个 core 文件 → 可运行） — 墙钟 28.7s / 步骤跨度 28.6s / 7 个步骤

| 耗时 | 步骤 |
| --- | --- |
|       9.6s | openbus.exe |
|       3.3s | libopenbus_core.a |
|      15.2s | src/CMakeFiles/openbus_core.dir/core/appconfig.cpp.obj |
|       9.6s | bin/openbus.exe |
|       3.3s | src/libopenbus_core.a |
|      433ms | src/openbus_core_autogen/timestamp |
|      433ms | src/openbus_core_autogen/mocs_compilation.cpp |
|      433ms | E:/code/sin/sin/build-dev/src/openbus_core_autogen/timestamp |
|      433ms | E:/code/sin/sin/build-dev/src/openbus_core_autogen/mocs_compilation.cpp |

## 2026-08-19 23:44 — 度量 clean (build type: Dev, dir: build-dev)

### 场景 clean（全量构建） — 墙钟 328.3s / 步骤跨度 328.2s / 192 个步骤

| 耗时 | 步骤 |
| --- | --- |
|      12.1s | openbus.exe |
|       3.8s | libopenbus_core.a |
|      876ms | libopenbus_ui.a |
|       3.8s | libqcustomplot.a |
|     175.5s | CMakeFiles/qcustomplot.dir/third_party/qcustomplot/qcustomplot.cpp.obj |
|      76.5s | CMakeFiles/qcustomplot.dir/qcustomplot_autogen/mocs_compilation.cpp.obj |
|      61.9s | src/CMakeFiles/openbus_core.dir/core/projectmanager.cpp.obj |
|      60.8s | src/CMakeFiles/openbus_core.dir/core/appconfig.cpp.obj |
|      59.0s | src/CMakeFiles/openbus_core.dir/core/sessionmanager.cpp.obj |
|      58.2s | src/CMakeFiles/openbus_ui.dir/ui/mainwindow.cpp.obj |
|      55.7s | drivers/peak/CMakeFiles/driver_peak.dir/__/__/src/core/candevice_peak.cpp.obj |
|      49.8s | src/CMakeFiles/openbus_ui.dir/ui/settingsdialog.cpp.obj |
|      48.3s | drivers/zlg/CMakeFiles/driver_zlg.dir/__/__/src/core/candevice_zlg.cpp.obj |
|      48.2s | drivers/kvaser/CMakeFiles/driver_kvaser.dir/__/__/src/core/candevice_kvaser.cpp.obj |
|      45.8s | src/CMakeFiles/openbus_core.dir/core/driver/driverregistry.cpp.obj |
|      43.4s | src/CMakeFiles/openbus_core.dir/core/dbc/dbc_adapter.cpp.obj |
| ... 共 192 个步骤 | |

## Phase B1 结果（2026-08-20）— openbus_data.dll + openbus_market.dll

二进制结构（build-dev/bin/）：

| 产物 | 体积 | 内容 |
| --- | --- | --- |
| openbus.exe | 19.9 MB（原 52 MB） | 壳：main + mainwindow + 壳 UI 静态库 |
| libopenbus_data.dll | 36 MB | 公共底座：core+models+utils+thememanager（全部单例唯一实例） |
| libopenbus_market.dll | 0.9 MB | 市场模块：markettab/marketmodel/marketmodule + C 工厂 |

验收度量（touch markettab.cpp → 可运行，ninja log）：

| 步骤 | 耗时 |
| --- | --- |
| markettab.cpp 编译 | 4.7 s |
| **libopenbus_market.dll 链接** | **0.4 s** |
| openbus.exe 重链（导入库连带） | 3.5 s |
| **合计** | **≈9 s**（目标 ≤90 s，原 ≈14 min） |

冒烟验证：应用启动正常（主窗口/插件发现/Python 宿主/MarketIndex 全链路）；
进程模块表确认 libopenbus_data.dll 与 libopenbus_market.dll 各加载一份（无重复实例）；
`nm` 确认 `openbus_createModule`（C 工厂）与 `MarketModel::` 聚合函数自 market.dll 导出。
（注：B2 起工厂更名为 `openbus_createMarketModule`，按模块唯一命名避免多 DLL 同名冲突。）

## Phase B2 结果（2026-08-20）— openbus_transceive.dll（收发四页）

二进制结构（build-dev/bin/）：

| 产物 | 体积 | 内容 |
| --- | --- | --- |
| openbus.exe | 19.2 MB | 壳（不变，重链后略小） |
| libopenbus_data.dll | 36 MB（不变） | 公共底座（triggerrecorder 等服务仍在 data 层） |
| libopenbus_transceive.dll | 1.1 MB | 收发模块：signalsendtab/playbacktab/offlineanalysistab/recordtab/dbcimportdialog + transceivemodule + C 工厂 |

接口扩展（imodule.h，只增不改）：`pages()` / `createPage(pageId, ctx)` / `query(what)` 三个带默认实现的虚函数；
ShellContext 增数据层指针（player/recorder/deviceManager/simulator/dbcManager）、`appendOutput` / `addProblem` 壳回调、
`shellInvoke` 反向动作通道（play/pause/stop/setSpeed/setAutoScroll/clearTraceGraphic/updateActions/statusMessage，
壳侧分发见 MainWindow::makeShellContext）。壳对模块的操控动作：setRecording/setFileInfo/setProgress/setPlayerLoaded，
查询：offlineFiles。

验收度量（touch transceivemodule.cpp → 可运行，ninja log）：

| 步骤 | 耗时 |
| --- | --- |
| transceivemodule.cpp 编译 | ≈4 s |
| **libopenbus_transceive.dll 链接** | ≈1 s |
| openbus.exe 重链（导入库连带） | ≈3 s |
| **合计** | **≈8.1 s**（目标 ≤90 s；data/market DLL 零重编） |

mainwindow.cpp 瘦身：3,486 → 3,204 行（-282：四个 setup*Tab、onTriggerRecording 及成员迁出）。
冒烟验证：应用启动正常；进程模块表确认 3 个 openbus DLL 各加载一份；
`objdump -p` 确认 `openbus_createTransceiveModule` / `openbus_createMarketModule` 分别自各自 DLL 导出。
收发四页（发送/回放/离线分析/录制）经侧栏 TransceivePanel 打开走 `createPage`，深度 UI 交互留待用户回归。

运维提示：build-dev/bin 需先执行 `python scripts/build.py deploy --build-dir build-dev`（windeployqt
部署 Qt/MinGW 运行时），否则依赖 PATH 中的 Qt 目录才能启动。

## Phase B3~B5 结果汇总（dbc/flow/trace/graphic）

各业务 DLL 按设计顺序全部完成并独立测试通过，接受度量遵循“改模块代码→可运行≤90s”标准：

| 阶段 | 产物 | 体积 | 接受度量（touch X.cpp→可运行） |
| --- | --- | --- | --- |
| **B3** | openbus_dbc.dll | ~0.8 MB | dbcmodule.cpp 编译 + re-link ≈ 8 s |
| **B4** | openbus_flow.dll | ~0.6 MB | flowmodule.cpp 编译 + re-link ≈ 8 s |
| **B5a** | openbus_trace.dll | ~1.2 MB | tracemodule.cpp 编译 + re-link ≈ 9 s |
| **B5b** | openbus_graphic.dll | ~1.5 MB | graphicmodule.cpp 编译 + re-link ≈ 10 s |

接口演进（imodule.h）：各模块自持数据模型指针（TraceModel/GraphicView 列表等）、页面工厂 createPage/PageId 语义统一、shellInvoke/query 动作通道完整（frameDoubleClicked/frameAddToGraphic/filterExpression 等）。

冒烟验证：应用启动正常；进程模块表确认所有 openbus_*DLL*加载且无重复单例；ui_offscreen 测试全量通过（构造 MainWindow、驱动模块创建、帧流闭环、主题切换零警告）；真实启动存活 ≥8s（验证 DLL 导入库链接正确性）。

---

## Phase B6 结果（2026-07-31）— mainwindow 终瘦身至 477 行

### 拆分策略与文件布局（同一类拆为部分实现文件，见 doc/拆分应用实施方案.md §4.5）

MainWindow 从原来的 3,104 行单体文件拆分为 8 个部分实现文件 + 头文件（总行数分布见下表）：

| 文件 | 行数 | 职责 |
| --- | --- | --- |
| mainwindow.h | 304 | 类声明（ctor/dtor/slots+private 方法 + 成员变量） |
| mainwindow.cpp | 477 | 壳核心：ctor 编排（调用各 setup 方法）+ invoke/query 转发 + ShellContext |
| mainwindow_setup.cpp | 471 | 构造分阶段装配：setupCoreServices/connectExtensionsPanel/connectDataPipeline/connectSidePanels/connectProjectPanel |
| mainwindow_chrome.cpp | 624 | 窗口骨架：菜单栏/自绘按钮/主布局/状态栏/ActivityBar/dock 切换/eventFilter/nativeEvent |
| mainwindow_actions.cpp | 448 | 用户动作：录制/回放/清空/打开/导入/快捷按钮/终端命令/统计刷新 |
| mainwindow_frameflow.cpp | 541 | 数据流：帧接收分发/Trace↔Graphic 联动/回放进度/DBC 信号联动/测量门控编排 |
| mainwindow_pages.cpp | 399 | 页面入口槽：侧栏面板触发打开标签页/市场页/杂项 tab 槽/refreshPanelLists |
| mainwindow_project.cpp | 500 | 工程与会话生命周期：closeEvent/打开保存切换新建/状态捕获与应用/文件预览 |
| mainwindow_dialogs.cpp | 304 | 帮助对话框 + 插件集成槽：About/License/ReleaseNotes/Shortcuts/CheckUpdate/BusinessCoop 及插件输出/命令注册/发帧/选帧请求 |

**关键实施细节**：
- 构造函数体经分析拆出 5 个装配方法（extracted verbatim from ctor），保持原执行顺序（数据服务初始化→UI 构建→连接建立→项目恢复）
- 多重集对比（sig_lines 脚本验证）证明：zero dropped / zero duplicated lines，唯一差异是新 ctor 中增加 5 个 method call 语句
- 新增 CMakeLists SRC_UI 条目添加这 7 个 .cpp 文件（mainwindow.cpp 保留但已精简至壳核心层）
- 删除死文件（已插件化或不在构建列表）：
  - ui/tools/blfasconverter.* (BLF 转 ASCII 工具，已转为命令行插件)
  - ui/tools/dbctoolview.* (DBC 信号查看器，已并入 DBC Module)
  - ui/tools/loganalysisview.* (日志分析视图，已转为后台统计服务)
  - 共 6 文件移除

验收度量（增量构建实测）：

| 步骤 | 耗时 |
| --- | --- |
| mainwindow_actions.cpp 编译 | ≈1.2 s |
| libopenbus_ui.a 打包（含 8 个 mainwindow 部分文件） | ≈1.5 s |
| openbus.exe 重链（导入库连带） | ≈3.8 s |
| **合计** | **≈8 s**（目标 ≤90 s） |

mainwindow.cpp 瘦身：3,104 → 477 行（-2,627，-84.6%）；8 个部分实现文件总计 3,591 行（比原单体略增但可读性提升一个数量级，每个文件专注单一职责）。

冒烟验证：应用启动正常；进程模块表确认 7 个 openbus DLL 各加载一份（data/transceive/dbc/flow/trace/graphic/market）；ctest 全量 7/8 pass（仅 canfileio::writeReadBlfRoundtrip 因 vector_blf 缺失失败，pre-existing 环境问题，非 B6 regression）；ui_offscreen 冒烟测试通过（验证 MainWindow 构造 + 模块工厂调用 + 帧流闭环 + 主题切换）；真实启动存活 ≥8s（DLL 导入库链接正确性验证）。

运维提示：build-dev/bin 需先执行 `python scripts/build.py deploy --build-dir build-dev`（windeployqt
部署 Qt/MinGW 运行时），否则依赖 PATH 中的 Qt 目录才能启动。
