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
