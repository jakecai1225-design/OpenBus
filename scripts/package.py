#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""openbus 打包流水线 — Windows 免依赖分发（doc/打包安装方案.md §5）

一条命令产出可分发产物：

    python scripts/package.py [--skip-build] [--skip-python] [--skip-installer]
                              [--version X.Y.Z] [--jobs N]

步骤（方案 §5 表格，幂等可重复）：
  1  配置并构建 Release      build-rel/ 独立目录（复用 build.py 子命令）
  2  windeployqt 部署         --release --no-translations --compiler-runtime
                             + Qt6PrintSupport 手动补拷（qcustomplot 静态库
                               传递依赖，同 build.py cmd_deploy）
                             + translations/qtbase_zh_CN.qm 回拷 + lib/fonts
  3  组装 staging            dist/stage/openbus/（白名单拷贝，见方案 §5 布局）
  4  捆绑 Python 运行时       方案 §6 的零网络等价实现：本机安装版 Python
                             精简拷贝 + pythonXY._pth 隔离 + PyQt6 裁剪子集
                             （§6.2，uds-diagnostic 唯一重依赖）
  5  附加文件                THIRD_PARTY_NOTICES.md / LICENSE.txt / README-PORTABLE.txt
                             （素材在 scripts/package_assets/）
  6  依赖完整性校验           objdump -p 提取每个 exe/dll/pyd 的 import 表，
                             逐一核对在 staging 内或系统白名单，缺一即 FAIL
  7  体积与内容报告           dist/package-report.txt
  8a 便携版 zip               dist/openbus-<ver>-win64-portable.zip
  8b Inno Setup 安装器        installer/openbus.iss（iscc 不存在则提示跳过，
                             不阻塞便携版，方案 §5 8b）

不随包（方案 §7 授权决策）：zlgcan.dll / zlgcan.lib（厂商 SDK）——
staging 白名单天然排除，用户按 README-PORTABLE 引导安装 ZCANPRO。
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

# 控制台中文输出防乱码（GBK 控制台 + UTF-8 源码）
for _s in (sys.stdout, sys.stderr):
    if hasattr(_s, "reconfigure"):
        _s.reconfigure(encoding="utf-8", errors="replace")

SCRIPTS_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPTS_DIR.parent

REL_BUILD = PROJECT_ROOT / "build-rel"        # Release 构建目录（方案 §4）
REL_BIN = REL_BUILD / "bin"
DIST = PROJECT_ROOT / "dist"
STAGE_ROOT = DIST / "stage"
STAGE = STAGE_ROOT / "openbus"                # zip 顶层目录名
ASSETS = SCRIPTS_DIR / "package_assets"       # NOTICES / README 等随包素材

# staging 从 build-rel/bin 拷贝的文件白名单（glob，小写后缀匹配）
BIN_FILES = [
    "openbus.exe",
    "libopenbus_*.dll",       # 业务 DLL（B5 后含 trace/graphic）
    "Qt6*.dll",               # Qt 运行时（windeployqt 部署集）
    "libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll",  # MinGW
    "D3Dcompiler_47.dll", "opengl32sw.dll",   # Qt RHI 渲染兜底
]
# staging 从 build-rel/bin 拷贝的目录白名单（整目录）。
# drivers/ 不预装（用户决策 2026-08-21）：驱动 .odp 一律从市场安装，
# 由 driver_tool.py install 自建 drivers/ 目录（makedirs exist_ok）
BIN_DIRS = ["platforms", "imageformats", "iconengines", "styles",
            "tls", "networkinformation"]

# 源码树拷入项：plugins/sdk 剔除缓存与测试；scripts 只带宿主与工具脚本
PY_IGNORE = shutil.ignore_patterns("__pycache__", "*.pyc", "tests",
                                   ".git", "*.egg-info")
HOST_SCRIPTS = ["sin_host.py", "driver_tool.py", "plugin_tool.py"]

# driver/（ZLG 运行时历史命名）：整体不随包——kerneldlls（含 ZPS 协议栈
# 24.8MB）是 zlgcan.dll 的配套，zlgcan.dll 本身不随包（§7 授权决策），
# ZCANPRO 安装自带全套；源码零引用，openbus 不会加载（用户实测决策）

# Python 运行时捆绑（方案 §6）：本机安装版精简拷贝
PY_EXCLUDE_DIRS = {"idlelib", "tkinter", "turtledemo", "lib2to3",
                   "test", "tests", "__pycache__", "site-packages",
                   # 运行时不用：pip 安装器 / 虚拟环境 / pydoc 数据（体积优化）
                   "ensurepip", "venv", "pydoc_data"}
PYDLL_EXCLUDE = {"_tkinter.pyd", "tcl86t.dll", "tk86t.dll", "_tkinter.lib"}

# PyQt6 裁剪子集（方案 §6.2）：uds-diagnostic 所需 QtCore/Gui/Widgets/Svg
PYQT6_MODULES = ["QtCore.pyd", "QtGui.pyd", "QtWidgets.pyd", "QtSvg.pyd"]
# Qt6/bin 除 Qt 模块外须整套拷 VC 运行库（实测 Qt6Core 引 msvcp140_1、
# QtGui.pyd 引 msvcp140_2；全量约 1.4MB，防 Qt 小版本引用漂移）
PYQT6_QT6_BIN = ["Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Svg.dll",
                 "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll",
                 "msvcp140_atomic_wait.dll", "msvcp140_codecvt_ids.dll",
                 "vcruntime140.dll", "vcruntime140_1.dll",
                 "vcruntime140_threads.dll",
                 "concrt140.dll", "d3dcompiler_47.dll"]
PYQT6_PLUGIN_DIRS = ["styles", "iconengines"]  # platforms 单独
# imageformats 只带与主 staging（windeployqt 部署集）一致的 4 格式：
# qpdf 引 Qt6Pdf、qwebp/qtiff 等用不上且增体积
PYQT6_IMAGEFORMATS = ["qgif.dll", "qico.dll", "qjpeg.dll", "qsvg.dll"]

# import 校验：系统 DLL 白名单（Windows 公共运行库；api-ms-*/ext-ms-* 前缀
# 单独放行——均由系统 ucrt 提供）
SYSTEM_DLLS = {
    "kernel32.dll", "user32.dll", "gdi32.dll", "shell32.dll", "advapi32.dll",
    "ws2_32.dll", "wsock32.dll", "ole32.dll", "oleaut32.dll", "uuid.dll",
    "comctl32.dll", "comdlg32.dll", "shlwapi.dll", "winmm.dll", "winspool.drv",
    "imm32.dll", "setupapi.dll", "version.dll", "wininet.dll", "crypt32.dll",
    "secur32.dll", "userenv.dll", "netapi32.dll", "iphlpapi.dll", "dnsapi.dll",
    "dwmapi.dll", "uxtheme.dll", "d3d11.dll", "dxgi.dll", "opengl32.dll",
    "glu32.dll", "msvcrt.dll", "ucrtbase.dll", "ntdll.dll", "bcrypt.dll",
    "ncrypt.dll", "powrprof.dll", "dbghelp.dll", "psapi.dll", "mpr.dll",
    "samcli.dll", "netutils.dll", "wlanapi.dll", "mscoree.dll", "wintrust.dll",
    "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll",  # VC 运行库
    "python3.dll",  # python.exe 可选转发入口
    # 首轮 objdump 门禁实测补充（Qt6/qwindows/_wmi/D3Dcompiler 等引用）：
    "rpcrt4.dll", "shcore.dll", "wtsapi32.dll", "uiautomationcore.dll",
    "propsys.dll", "winhttp.dll", "winusb.dll", "authz.dll",
    "d3d9.dll", "d3d12.dll", "dwrite.dll", "imagehlp.dll",
    "icuuc.dll",  # Win10 1703+ 系统 ICU（PyQt6 MSVC 版 Qt6Core 引用）
}


# ============================================================
#  输出
# ============================================================

def info(msg): print(f"[INFO] {msg}")
def ok(msg): print(f"[ OK ] {msg}")
def warn(msg): print(f"[WARN] {msg}")
def fail(msg): print(f"[FAIL] {msg}")


def header(msg):
    bar = "=" * 55
    print(f"\n{bar}\n  {msg}\n{bar}")


def run(cmd, **kw):
    """执行外部命令，失败即终止流水线"""
    info(f"$ {' '.join(str(c) for c in cmd)}")
    return subprocess.run([str(c) for c in cmd], check=True, **kw)


# ============================================================
#  工具链探测（环境变量优先 → 常见安装位置扫描）
# ============================================================

def _glob_first(pattern, probe):
    """在 glob 候选中选第一个通过 probe 的路径（按名称排序求稳定）"""
    hits = sorted(Path(p) for p in __import__("glob").glob(str(pattern)))
    hits = [h for h in hits if probe(h)]
    return hits[-1] if hits else None   # 排序后取最新版本


def detect_toolchain():
    """探测 Qt / MinGW / CMake（SIN_* 环境变量 → C:/D: 常见路径扫描）"""
    qt = os.environ.get("SIN_QT_DIR")
    mingw = os.environ.get("SIN_MINGW_DIR")
    cmake = os.environ.get("SIN_CMAKE_DIR")

    if qt is None:
        for root in ("C:/Qt", "D:/Qt", "E:/Qt"):
            hit = _glob_first(f"{root}/*/mingw_64",
                              lambda p: (p / "bin" / "windeployqt.exe").exists())
            if hit:
                qt = str(hit)
                break
    if mingw is None:
        for root in ("C:/Qt", "D:/Qt", "E:/Qt"):
            hit = _glob_first(f"{root}/Tools/mingw*",
                              lambda p: (p / "bin" / "g++.exe").exists())
            if hit:
                mingw = str(hit)
                break
    if cmake is None:
        for cand in ("C:/Program Files/CMake", "C:/tools/cmake-3.30.3-windows-x86_64"):
            if (Path(cand) / "bin" / "cmake.exe").exists():
                cmake = cand
                break

    missing = [n for n, v in (("Qt", qt), ("MinGW", mingw), ("CMake", cmake)) if not v]
    if missing:
        fail(f"未找到工具链: {', '.join(missing)}")
        fail("请设置环境变量 SIN_QT_DIR / SIN_MINGW_DIR / SIN_CMAKE_DIR 后重试")
        sys.exit(1)

    tc = {
        "qt_dir": Path(qt),
        "qt_bin": Path(qt) / "bin",
        "mingw_dir": Path(mingw),
        "mingw_bin": Path(mingw) / "bin",
        "cmake_dir": Path(cmake),
        "cmake_bin": Path(cmake) / "bin",
    }
    ok(f"Qt     {tc['qt_dir']}")
    ok(f"MinGW  {tc['mingw_dir']}")
    ok(f"CMake  {tc['cmake_dir']}")
    return tc


def build_env(tc):
    """传递给 build.py 子进程的环境（SIN_* 变量 + MinGW/CMake 前置 PATH）"""
    env = dict(os.environ)
    env["SIN_QT_DIR"] = str(tc["qt_dir"])
    env["SIN_MINGW_DIR"] = str(tc["mingw_dir"])
    env["SIN_CMAKE_DIR"] = str(tc["cmake_dir"])
    env["PATH"] = os.pathsep.join([str(tc["mingw_bin"]), str(tc["cmake_bin"]),
                                   str(tc["qt_bin"])]) + os.pathsep + env.get("PATH", "")
    return env


# ============================================================
#  步骤 1：Release 构建（复用 build.py）
# ============================================================

def read_cmake_cache(build_dir, key):
    cache = build_dir / "CMakeCache.txt"
    if not cache.exists():
        return None
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(f"{key}:STATIC=") or line.startswith(f"{key}="):
            return line.split("=", 1)[1].strip()
    return None


def step_build(tc, jobs, skip_build):
    header("步骤 1: 配置并构建 Release")
    exe = REL_BIN / "openbus.exe"
    if skip_build:
        if not exe.exists():
            fail("--skip-build 但 build-rel/bin/openbus.exe 不存在")
            sys.exit(1)
        info("跳过构建（--skip-build），复用现有 Release 产物")
        return

    # 已配置且档位一致 → 直接增量构建；否则先 configure
    cached_type = read_cmake_cache(REL_BUILD, "CMAKE_BUILD_TYPE")
    if cached_type != "Release":
        run([sys.executable, SCRIPTS_DIR / "build.py", "configure",
             "--build-type", "Release", "--build-dir", "build-rel"],
            env=build_env(tc))
    # 全量构建（不带 --target）：driver_zlg/peak/kvaser/slcan/candle 是
    # QPluginLoader 外置插件，不在 openbus 依赖链上——只构 openbus 会得到
    # 空的 drivers/<id>/（首轮实瘧：5 驱动全部“缺少 driver.json”）
    run([sys.executable, SCRIPTS_DIR / "build.py", "build",
         "--build-dir", "build-rel", "-j", str(jobs)],
        env=build_env(tc))

    if not exe.exists():
        fail(f"构建完成但未找到 {exe}")
        sys.exit(1)
    # 驱动 .odp 布局校验（方案 §2.1 A 项：5 内置驱动 + driver.json）
    missing_drv = []
    for drv in ("zlg", "peak", "kvaser", "slcan", "candle"):
        d = REL_BIN / "drivers" / drv
        if not (d / "driver.json").exists() or not (d / f"driver_{drv}.dll").exists():
            missing_drv.append(drv)
    if missing_drv:
        fail(f"驱动 .odp 布局不完整（缺 {', '.join(missing_drv)}）——"
             f"staging drivers/ 将不可用")
        sys.exit(1)
    ok(f"Release 构建完成: {exe} + 5 驱动 .odp 布局")


# ============================================================
#  步骤 2：windeployqt 部署（Qt 运行时 → build-rel/bin）
# ============================================================

def step_deploy_qt(tc):
    header("步骤 2: windeployqt 部署")
    exe = REL_BIN / "openbus.exe"
    run([tc["qt_bin"] / "windeployqt.exe", "--release", "--no-translations",
         "--compiler-runtime", exe], env=build_env(tc))

    # qcustomplot（静态库）对 Qt6PrintSupport 的传递依赖 windeployqt 探测
    # 不到——与 build.py cmd_deploy 相同的手动补拷（方案 §2.2）
    for dll in ("Qt6PrintSupport.dll",):
        src = tc["qt_bin"] / dll
        if src.exists():
            shutil.copy2(src, REL_BIN / dll)
            ok(f"手动补拷 {dll}（qcustomplot 传递依赖）")

    # 中文翻译（--no-translations 后按需回拷，方案 §5 步骤 2）
    zh = tc["qt_dir"] / "translations" / "qtbase_zh_CN.qm"
    if zh.exists():
        (REL_BIN / "translations").mkdir(exist_ok=True)
        shutil.copy2(zh, REL_BIN / "translations" / "qtbase_zh_CN.qm")
        ok("回拷 translations/qtbase_zh_CN.qm（中文界面）")
    else:
        warn(f"未找到 {zh}（界面回退英文）")

    # lib/fonts：Qt6 Windows 无自带字体，目录缺失时 QFontDatabase 告警（O-2）
    (REL_BIN / "lib" / "fonts").mkdir(parents=True, exist_ok=True)
    ok("lib/fonts 目录就绪")


# ============================================================
#  步骤 3：组装 staging（白名单，方案 §5 布局图）
# ============================================================

def copy_tree(src, dst, ignore=None):
    if not src.exists():
        warn(f"跳过缺失物料: {src}")
        return
    shutil.copytree(src, dst, dirs_exist_ok=True, ignore=ignore)


def step_stage():
    header("步骤 3: 组装 staging 目录")
    if STAGE.exists():
        shutil.rmtree(STAGE)
    STAGE.mkdir(parents=True)

    # ---- 构建产物（白名单文件 + 目录） ----
    for pattern in BIN_FILES:
        for f in sorted(REL_BIN.glob(pattern)):
            shutil.copy2(f, STAGE / f.name)
    # Qt6*.dll glob 会捞进 tests target 构建时拷到 bin 的 Qt6Test.dll
    # （全量构建副产物，staging 内无任何模块引用，纯体积浪费）
    (STAGE / "Qt6Test.dll").unlink(missing_ok=True)
    for d in BIN_DIRS:
        if (REL_BIN / d).exists():
            shutil.copytree(REL_BIN / d, STAGE / d)
    if (REL_BIN / "translations" / "qtbase_zh_CN.qm").exists():
        shutil.copytree(REL_BIN / "translations", STAGE / "translations")
        # 只保留中文（方案 §11 体积优化首项）
        for qm in (STAGE / "translations").glob("*.qm"):
            if qm.name != "qtbase_zh_CN.qm":
                qm.unlink()
    shutil.copytree(REL_BIN / "lib" / "fonts", STAGE / "lib" / "fonts",
                    dirs_exist_ok=True)
    ok("构建产物: exe + 业务/Qt/MinGW DLL + 插件目录")

    # ---- 源码树运行时物料 ----
    # plugins/ 不预装（用户决策 2026-08-21）：.opk 插件一律从市场安装，
    # 首次安装时 plugin_tool.py 自建 plugins/ 目录；纯净启动下
    # discoverPlugins 对不存在的目录优雅跳过（仅 info 日志）
    copy_tree(PROJECT_ROOT / "sdk", STAGE / "sdk", PY_IGNORE)
    for name in HOST_SCRIPTS:
        src = PROJECT_ROOT / "scripts" / name
        if src.exists():
            (STAGE / "scripts").mkdir(exist_ok=True)
            shutil.copy2(src, STAGE / "scripts" / name)
        else:
            warn(f"宿主脚本缺失: {src}")
    ok("源码树: sdk/ + scripts/{sin_host,driver_tool,plugin_tool}.py（插件/驱动从市场装）")

    # ---- driver/ 与 drivers/：均不预装 ----
    # driver/（ZLG 运行时）：zlgcan.dll 及配套归 ZCANPRO（方案 §7）。
    # drivers/（.odp 外置驱动）：从市场安装（make_market 产物在 market/ 内）
    ok("driver/ 与 drivers/: 均不随包（驱动一律从市场安装）")

    # ---- market（本地市场索引：插件/驱动的安装源，方案 §2.2） ----
    # 市场化分发（用户决策 2026-08-21）：纯净机器离线可装——market/ 随包
    # 携带 market.json + 3 .odp + 5 .opk + assets，MarketIndex 优先加载
    # exe 同级 market/market.json，resolveUrl 以 file:// 解析相对路径
    for market_src in (REL_BUILD / "market", PROJECT_ROOT / "build" / "market"):
        if (market_src / "market.json").exists():
            shutil.copytree(market_src, STAGE / "market", dirs_exist_ok=True)
            ok(f"market/: {market_src}（本地市场兜底）")
            break
    else:
        warn("market.json 不存在（build-rel 与 build 均无），市场页回退远程索引")


# ============================================================
#  步骤 4：Python 运行时捆绑（方案 §6，零网络等价实现）
# ============================================================

def step_python_runtime(skip_python):
    header("步骤 4: 捆绑 Python 运行时")
    if skip_python:
        warn("--skip-python：不捆绑 Python（插件系统将依赖用户自装解释器）")
        return

    py_root = Path(sys.executable).resolve().parent
    if not (py_root / "python.exe").exists():
        fail(f"当前解释器不可作为捆绑源: {py_root}")
        sys.exit(1)
    ver = f"{sys.version_info.major}{sys.version_info.minor}"    # 如 314
    runtime = STAGE / "runtime" / "python"
    runtime.mkdir(parents=True, exist_ok=True)

    # ---- 解释器核心（pythonXY.dll 与 vcruntime 必须同行） ----
    for name in (f"python{ver}.dll", "python3.dll", "python.exe",
                 "vcruntime140.dll", "vcruntime140_1.dll", "LICENSE.txt"):
        src = py_root / name
        if src.exists():
            shutil.copy2(src, runtime / name)
    ok(f"解释器: Python {sys.version_info.major}.{sys.version_info.minor}"
       f".{sys.version_info.micro}（本机安装版精简拷贝）")

    # ---- DLLs/：剔除 tkinter ----
    dlls_src = py_root / "DLLs"
    dlls_dst = runtime / "DLLs"
    dlls_dst.mkdir()
    for f in dlls_src.iterdir():
        if f.is_file() and f.name.lower() not in PYDLL_EXCLUDE:
            shutil.copy2(f, dlls_dst / f.name)
    ok(f"DLLs/（剔除 tkinter 后 {len(list(dlls_dst.iterdir()))} 项）")

    # ---- Lib/：标准库（剔除 GUI/测试/缓存；site-packages 单独重建） ----
    lib_src = py_root / "Lib"
    lib_dst = runtime / "Lib"
    lib_dst.mkdir()
    for entry in lib_src.iterdir():
        if entry.is_dir():
            if entry.name in PY_EXCLUDE_DIRS:
                continue
            shutil.copytree(entry, lib_dst / entry.name,
                            ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        elif entry.is_file() and entry.suffix == ".py":
            shutil.copy2(entry, lib_dst / entry.name)
    ok("Lib/（剔除 idlelib/tkinter/turtledemo/lib2to3/test）")

    # ---- ._pth 隔离（embeddable 同款机制：忽略注册表/环境变量，
    #      路径全部相对 runtime 目录，不污染用户系统） ----
    pth = runtime / f"python{ver}._pth"
    pth.write_text(
        f"python{ver}.zip\n"       # embeddable 兼容行（zip 不存在时无害）
        ".\n"
        "DLLs\n"
        "Lib\n"
        "Lib/site-packages\n"
        "\n"
        "# 取消注释以自动运行 site.main()\n"
        "import site\n",
        encoding="utf-8")
    ok(f"{pth.name}（隔离 sys.path，不读注册表/环境变量）")

    # ---- PyQt6 裁剪子集（§6.2：各带各的 Qt，不与主程序共享 DLL） ----
    sp_src = py_root / "Lib" / "site-packages"
    sp_dst = lib_dst / "site-packages"
    sp_dst.mkdir()
    pyqt_src = sp_src / "PyQt6"
    pyqt_dst = sp_dst / "PyQt6"
    if pyqt_src.exists():
        pyqt_dst.mkdir()
        # 包内：__init__ + 4 个模块 .pyd + sip 绑定
        shutil.copy2(pyqt_src / "__init__.py", pyqt_dst / "__init__.py")
        for mod in PYQT6_MODULES:
            shutil.copy2(pyqt_src / mod, pyqt_dst / mod)
        for sip in pyqt_src.glob("sip.*.pyd"):
            shutil.copy2(sip, pyqt_dst / sip.name)
        # Qt 运行时（PyQt6/Qt6/bin）
        qt6_bin = pyqt_src / "Qt6" / "bin"
        (pyqt_dst / "Qt6" / "bin").mkdir(parents=True)
        for dll in PYQT6_QT6_BIN:
            if (qt6_bin / dll).exists():
                shutil.copy2(qt6_bin / dll, pyqt_dst / "Qt6" / "bin" / dll)
        # Qt 插件：platforms（必需）+ styles/imageformats/iconengines
        plug_src = pyqt_src / "Qt6" / "plugins"
        (pyqt_dst / "Qt6" / "plugins" / "platforms").mkdir(parents=True)
        shutil.copy2(plug_src / "platforms" / "qwindows.dll",
                     pyqt_dst / "Qt6" / "plugins" / "platforms" / "qwindows.dll")
        for d in PYQT6_PLUGIN_DIRS:
            if (plug_src / d).exists():
                shutil.copytree(plug_src / d, pyqt_dst / "Qt6" / "plugins" / d)
        imf_dst = pyqt_dst / "Qt6" / "plugins" / "imageformats"
        imf_dst.mkdir(parents=True, exist_ok=True)
        for q in PYQT6_IMAGEFORMATS:
            src_q = plug_src / "imageformats" / q
            if src_q.exists():
                shutil.copy2(src_q, imf_dst / q)
        ok("PyQt6 裁剪子集: QtCore/Gui/Widgets/Svg + Qt6 运行时 + 平台插件")
        # dist-info 元数据（importlib.metadata 可用，版本校验依据）
        for di in sp_src.glob("PyQt6*.dist-info"):
            shutil.copytree(di, sp_dst / di.name,
                            ignore=shutil.ignore_patterns("RECORD", "*.py"))
        # 版本锁定留档（方案 §6.2 第 3 点）
        import importlib.metadata as _md
        lock = sp_dst / "requirements-lock.txt"
        lock.write_text(
            "\n".join(f"{n}=={_md.version(n)}"
                      for n in ("PyQt6", "PyQt6-Qt6", "PyQt6-sip")
                      if _md.version(n)) + "\n",
            encoding="utf-8")
    else:
        warn("本机未安装 PyQt6：uds-diagnostic 插件将不可用")

    # ---- 捆绑运行时自验证（._pth 隔离环境下 import 全链） ----
    check = subprocess.run(
        [str(runtime / "python.exe"), "-c",
         "import json, struct, PyQt6.QtCore, PyQt6.QtWidgets, PyQt6.QtSvg;"
         "print('runtime-ok')"],
        capture_output=True, text=True, cwd=str(STAGE))
    if check.returncode != 0 or "runtime-ok" not in check.stdout:
        fail(f"捆绑 Python 自验证失败:\n{check.stdout}\n{check.stderr}")
        sys.exit(1)
    ok("自验证: import PyQt6.QtCore/QtWidgets/QtSvg 通过")


# ============================================================
#  步骤 5：附加文件
# ============================================================

def step_extras():
    header("步骤 5: 附加文件")
    pairs = [
        (ASSETS / "THIRD_PARTY_NOTICES.md", STAGE / "THIRD_PARTY_NOTICES.md"),
        (ASSETS / "README-PORTABLE.txt", STAGE / "README-PORTABLE.txt"),
        (PROJECT_ROOT / "LICENSE", STAGE / "LICENSE.txt"),
    ]
    for src, dst in pairs:
        if not src.exists():
            fail(f"随包文件缺失: {src}")
            sys.exit(1)
        shutil.copy2(src, dst)
    ok("THIRD_PARTY_NOTICES.md + README-PORTABLE.txt + LICENSE.txt")


# ============================================================
#  步骤 6：依赖完整性校验（objdump import 表）
# ============================================================

def is_system_dll(name):
    return name in SYSTEM_DLLS or name.startswith(("api-ms-", "ext-ms-"))


def step_verify_deps(tc):
    header("步骤 6: 依赖完整性校验 (objdump)")
    objdump = tc["mingw_bin"] / "objdump.exe"
    if not objdump.exists():
        fail(f"objdump 不存在: {objdump}")
        sys.exit(1)

    # staging 内全部可用 DLL 名（小写）
    staged = {p.name.lower() for p in STAGE.rglob("*") if p.suffix.lower() == ".dll"}

    # driver/kerneldlls/ 下为 ZLG 厂商 DLL——zlgcan.dll 不随包（§7）则不会
    # 加载，其 msvcp90/mfc90/commlayer 等老运行库依赖不构成发版阻塞，不纳入校验
    skipped_kd = 0
    targets = []
    for p in STAGE.rglob("*"):
        if p.suffix.lower() not in (".exe", ".dll", ".pyd"):
            continue
        if "kerneldlls" in p.parts:
            skipped_kd += 1
            continue
        targets.append(p)
    if skipped_kd:
        print(f"  跳过 driver/kerneldlls/ 内 {skipped_kd} 个厂商 PE（ZLG 延迟链，方案 §7）")
    missing = {}
    checked = 0
    for t in targets:
        out = subprocess.run([str(objdump), "-p", str(t)],
                             capture_output=True, text=True,
                             errors="replace").stdout
        for line in out.splitlines():
            s = line.strip()
            if not s.startswith("DLL Name:"):
                continue
            checked += 1
            dep = s.split(":", 1)[1].strip().lower()
            if dep not in staged and not is_system_dll(dep):
                missing.setdefault(dep, []).append(t.relative_to(STAGE))

    if missing:
        for dep, users in sorted(missing.items()):
            fail(f"缺失 {dep}（被 {len(users)} 个模块引用，如 {users[0]}）")
        fail(f"import 校验失败：{len(missing)} 个缺失依赖，防'漏拷只在用户机器上炸'门禁未过")
        sys.exit(1)
    ok(f"{len(targets)} 个 PE 模块 / {checked} 条 import 全部可解析（0 缺失）")


# ============================================================
#  步骤 7：体积与内容报告
# ============================================================

def dir_size_mb(path):
    # rglob("*") 对文件返回空迭代器——不判 is_file 会把根目录所有
    # 单文件（exe / Qt DLL 等）算成 0.0 MB（首版实测合计少了 ~64 MB）
    if path.is_file():
        return path.stat().st_size / 1024 / 1024
    total = sum(f.stat().st_size for f in path.rglob("*") if f.is_file())
    return total / 1024 / 1024


def step_report():
    header("步骤 7: 体积与内容报告")
    lines = [f"openbus staging 报告 — {STAGE}", ""]
    total = 0.0
    for entry in sorted(STAGE.iterdir()):
        size = dir_size_mb(entry)
        total += size
        lines.append(f"  {entry.name:<24} {size:8.1f} MB")
    lines.append(f"  {'(合计)':<24} {total:8.1f} MB")
    lines.append("")
    lines.append("目录树（深度 2）:")
    lines.append(f"  openbus/")

    def walk(d, prefix, depth):
        if depth > 2:
            return
        entries = sorted(d.iterdir(), key=lambda p: (p.is_file(), p.name.lower()))
        for i, e in enumerate(entries):
            last = i == len(entries) - 1
            lines.append(f"{prefix}{'└─ ' if last else '├─ '}{e.name}"
                         + (f"  ({dir_size_mb(e):.1f} MB)" if e.is_dir() else ""))
            if e.is_dir():
                walk(e, prefix + ("   " if last else "│  "), depth + 1)

    walk(STAGE, "  ", 1)
    report = DIST / "package-report.txt"
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines[:40]))
    ok(f"完整报告: {report}")
    return total


# ============================================================
#  步骤 8a/8b：zip 出口 / 安装器出口
# ============================================================

def step_zip(version):
    header("步骤 8a: 便携版 zip")
    zip_path = DIST / f"openbus-{version}-win64-portable.zip"
    if zip_path.exists():
        zip_path.unlink()
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for f in sorted(STAGE.rglob("*")):
            if f.is_file():
                zf.write(f, f.relative_to(STAGE_ROOT))
    size_mb = zip_path.stat().st_size / 1024 / 1024
    ok(f"{zip_path.name}  {size_mb:.1f} MB" + ("（超出 60MB 预算，见方案 §11 优化表）"
                                               if size_mb > 60 else "（≤60MB 预算内）"))
    return zip_path


def step_installer(version, skip_installer):
    header("步骤 8b: Inno Setup 安装器")
    iss = PROJECT_ROOT / "installer" / "openbus.iss"
    if skip_installer:
        warn("--skip-installer：跳过安装器")
        return None
    if not iss.exists():
        warn(f"{iss} 不存在，跳过安装器")
        return None
    iscc = shutil.which("iscc") or next(
        (str(p) for p in ("C:/Program Files (x86)/Inno Setup 6/ISCC.exe",
                          "C:/Program Files/Inno Setup 6/ISCC.exe") if Path(p).exists()),
        None)
    if not iscc:
        warn("未找到 iscc（Inno Setup 6 未安装）——按方案 §5 8b 跳过安装器编译，"
             "不阻塞便携版；安装 Inno Setup 后重跑即可")
        return None
    run([iscc, f"/DAppVersion={version}", str(iss)])
    setup = DIST / f"openbus-{version}-win64-setup.exe"
    ok(f"{setup.name}  {setup.stat().st_size / 1024 / 1024:.1f} MB"
       if setup.exists() else "安装器输出未找到（检查 .iss OutputDir）")
    return setup


# ============================================================
#  主流程
# ============================================================

def read_version(override):
    if override:
        return override
    ver = read_cmake_cache(REL_BUILD, "openbus_VERSION")
    if ver:
        return ver
    text = (PROJECT_ROOT / "CMakeLists.txt").read_text(encoding="utf-8",
                                                       errors="replace")
    m = re.search(r"VERSION\s+(\d+\.\d+\.\d+)", text)
    return m.group(1) if m else "0.0.0"


def main():
    parser = argparse.ArgumentParser(
        description="openbus 打包流水线（doc/打包安装方案.md）")
    parser.add_argument("--version", help="覆盖版本号（默认读 CMakeCache）")
    parser.add_argument("--jobs", "-j", type=int, default=os.cpu_count() or 8)
    parser.add_argument("--skip-build", action="store_true",
                        help="跳过 Release 构建（复用现有 build-rel）")
    parser.add_argument("--skip-python", action="store_true",
                        help="不捆绑 Python 运行时（插件系统依赖用户自装）")
    parser.add_argument("--skip-installer", action="store_true",
                        help="跳过 Inno Setup 安装器编译")
    args = parser.parse_args()

    tc = detect_toolchain()
    version = read_version(args.version)
    info(f"打包版本: {version}")

    step_build(tc, args.jobs, args.skip_build)
    step_deploy_qt(tc)
    step_stage()
    step_python_runtime(args.skip_python)
    step_extras()
    step_verify_deps(tc)
    total_mb = step_report()
    zip_path = step_zip(version)
    setup_path = step_installer(version, args.skip_installer)

    header("打包完成")
    ok(f"版本      {version}")
    ok(f"staging   {STAGE}  ({total_mb:.1f} MB)")
    ok(f"便携版    {zip_path}")
    if setup_path:
        ok(f"安装器    {setup_path}")
    print("\n发版前须过 doc/打包安装方案.md §10 验证清单（干净 VM 冒烟等 12 项）")


if __name__ == "__main__":
    main()
