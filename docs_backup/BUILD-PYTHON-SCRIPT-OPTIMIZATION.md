# Build.py 脚本优化总结

## 🎯 问题诊断

之前的 build.py 脚本存在以下问题：
1. ❌ 硬编码的默认路径不正确（`C:\tools\cmake-3.30.3-windows-x86_64`）
2. ❌ 无法自动检测实际安装的 Qt/MinGW/CMake 位置
3. ❌ 缺少智能工具链发现机制
4. ❌ `cmd_all()` 函数缺少 `jobs`属性导致`AttributeError`

## ✅ 优化方案

### 1. 智能工具链自动检测

新增三个自动检测函数：
- `_find_qt_install()` - 优先环境变量 → 常见路径 → 回退指定值
- `_find_mingw_install()` - 优先环境变量 → Qt 捆绑版 → Git Bash MinGW
- `_find_cmake_install()` - 优先环境变量 → 常见路径 → glob 匹配最新版

**检测优先级：**
1. 环境变量 (`SIN_QT_DIR`, `SIN_MINGW_DIR`, `SIN_CMAKE_DIR`)
2. 本地安装目录 (`D:\Qt\*`, `C:\Qt\*`)
3. 命令行参数 (`--qt-dir`, `--mingw-dir`, `--cmake-dir`)
4. 默认回退值

### 2. Bug 修复

修复了 `cmd_all()`函数中缺少`args.jobs`和`args.target` 属性导致的崩溃：
```python
# 确保 args.jobs 和 args.target 存在（针对 all 子命令）
if not hasattr(args, 'jobs'):
    args.jobs = None
if not hasattr(args, 'target'):
    args.target = None
```

### 3. 文档增强

重新编写模块说明，包含：
- 功能特性清单（✓ 标记）
- 常用命令速查表
- 高级用法示例
- 环境变量说明
- 使用建议

## 📝 使用方法

### 完整构建流程
```powershell
# 一键完成所有步骤
python scripts/build.py all

# 带参数的完整流程
python scripts/build.py configure --build-type Release --build-dir build-rel
python scripts/build.py build -j16  # 16 线程编译
python scripts/build.py deploy      # 部署 Qt 依赖
python scripts/build.py run         # 运行程序
```

### Dev 快速开发档（推荐日常使用）
```powershell
# 配置 Dev 档 (-O1 -g1，编译快)
python scripts/build.py configure --build-type Dev --build-dir build-dev

# 增量编译
python scripts/build.py build --build-dir build-dev -j8

# 运行
python scripts/build.py run --build-dir build-dev
```

### 覆盖工具路径（当自动检测失败时）
```powershell
python scripts/build.py configure \
  --qt-dir D:\Qt\6.8.3\mingw_64 \
  --mingw-dir D:\Qt\Tools\mingw1310_64 \
  --cmake-dir C:\Program Files\CMake
```

### 环境变量（最高优先级）
```powershell
$env:SIN_QT_DIR = "D:\Qt\6.8.3\mingw_64"
$env:SIN_MINGW_DIR = "D:\Qt\Tools\mingw1310_64"
$env:SIN_CMAKE_DIR = "C:\Program Files\CMake"
python scripts/build.py all
```

## 🔍 环境状态检查

```powershell
# 查看当前检测到的工具链
python scripts/build.py status
```

预期输出：
```
=======================================================
  环境状态
=======================================================
  项目根目录：  D:\sin\sin_20260727\sin
  构建目录：    D:\sin\sin_20260727\sin\build  [已存在]
  可执行文件：  D:\sin\sin_20260727\sin\build\bin\openbus.exe  [未构建]

[ OK ] CMake        C:\Program Files\CMake\bin\cmake.exe
[ OK ] g++          D:\Qt\Tools\mingw1310_64\bin\g++.exe
[ OK ] gcc          D:\Qt\Tools\mingw1310_64\bin\gcc.exe
[ OK ] windeployqt  D:\Qt\6.8.3\mingw_64\bin\windeployqt.exe
[ OK ] gdb          D:\Qt\Tools\mingw1310_64\bin\gdb.exe
[WARN] 未检测到 Ninja，使用 MinGW Makefiles（较慢）

  构建类型：    Debug
```

## 🛠️ 核心改进点

### 路径检测逻辑
```python
def _find_qt_install():
    qt_paths = [
        Path(os.environ.get("SIN_QT_DIR", "")),  # 1. 环境变量
        Path(r"D:\Qt\6.8.3\mingw_64"),           # 2. 本地安装
        Path(r"C:\Qt\6.8.3\mingw_64"),
        Path(os.environ.get("QT_DIR", "")),
    ]
    
    for path in qt_paths:
        if path and (path / "bin" / "qmake.exe").exists():
            return path
    
    return Path(os.environ.get("SIN_QT_DIR", "C:/Qt/6.8.3/mingw_64"))
```

### 兼容性保证
- ✅ 支持 MinGW Makefiles 生成器
- ✅ 支持 Ninja 生成器（如果 tools/ninja/ninja.exe 存在）
- ✅ 支持多版本 Qt 并存
- ✅ 支持多种 CMake 安装方式

## 📚 持久化记忆要点

**重要提示：**
1. **始终使用 `python scripts/build.py` 进行构建**，不要手动调用 cmake
2. **Dev 档推荐日常开发**（`build-dev/` 目录），避免全量重编
3. **环境变量优先级最高**，可通过批处理文件固化常用配置
4. **编译前自动终止 openbus.exe**，避免文件锁冲突
5. **使用 `-j<N>` 参数并行编译**，充分利用多核 CPU

**推荐工作流：**
```powershell
# 首次配置
python scripts/build.py configure --build-type Dev --build-dir build-dev

# 日常开发
python scripts/build.py build --build-dir build-dev -j8
python scripts/build.py run --build-dir build-dev

# 发布构建
python scripts/build.py configure --build-type Release --build-dir build-rel
python scripts/build.py build --build-dir build-rel -j16
python scripts/build.py deploy
```

---

**更新日期**: 2026-08-25  
**版本**: v2.0  
**状态**: ✅ 已验证可用
