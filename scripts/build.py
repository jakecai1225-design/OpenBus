#!/usr/bin/env python3
"""
openbus 项目构建脚本

支持命令:
  configure  - CMake 配置
  build      - 增量编译
  run        - 运行程序
  debug      - GDB 调试
  clean      - 清理构建
  rebuild    - 重新构建 (清理 + 配置 + 编译)
  deploy     - 部署 Qt 运行时依赖
  all        - 完整流程 (配置 + 编译 + 部署 + 运行)
  status     - 显示环境状态
  open       - 在资源管理器中打开构建目录

用法示例:
  python scripts/build.py configure --build-type Release
  python scripts/build.py build -j8
  python scripts/build.py run
  python scripts/build.py debug
  python scripts/build.py rebuild
  python scripts/build.py status

Dev 快速构建档 (日常开发，-O1 -g1，独立目录与全量 Debug 并存):
  python scripts/build.py configure --build-type Dev --build-dir build-dev
  python scripts/build.py build --build-dir build-dev -j8
  python scripts/build.py run --build-dir build-dev
"""

import argparse
import ctypes
import os
import shutil
import subprocess
import sys
from pathlib import Path

# 启用 Windows 终端 ANSI 颜色支持
def _enable_ansi_colors():
    if sys.platform != "win32":
        return
    kernel32 = ctypes.windll.kernel32
    handle = kernel32.GetStdHandle(-11)  # STD_OUTPUT_HANDLE
    mode = ctypes.c_uint32()
    if kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
        kernel32.SetConsoleMode(handle, mode.value | 0x0004)  # ENABLE_VIRTUAL_TERMINAL_PROCESSING


_enable_ansi_colors()

# Windows 终端中文输出兼容
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    except Exception:
        pass

# ============================================================
#  路径配置
# ============================================================

PROJECT_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = PROJECT_ROOT / "build"          # 默认构建目录，可被 --build-dir 覆盖
EXECUTABLE = BUILD_DIR / "bin" / "openbus.exe"
TOOLS_DIR = PROJECT_ROOT / "tools"


def set_build_dir(name):
    """切换构建目录（--build-dir build-dev 等），同步更新可执行文件路径

    允许 Dev 档 (build-dev/) 与全量 Debug (build/) 并存，避免切档全量重编。
    """
    global BUILD_DIR, EXECUTABLE
    bd = Path(name)
    BUILD_DIR = bd if bd.is_absolute() else PROJECT_ROOT / bd
    EXECUTABLE = BUILD_DIR / "bin" / "openbus.exe"

# 默认工具路径 (可通过环境变量或 --qt-dir / --mingw-dir / --cmake-dir 覆盖)
DEFAULT_QT_DIR = Path(os.environ.get("SIN_QT_DIR", "C:/Qt/6.8.3/mingw_64"))
DEFAULT_MINGW_DIR = Path(os.environ.get("SIN_MINGW_DIR", "C:/Qt/Tools/mingw1310_64"))
DEFAULT_CMAKE_DIR = Path(os.environ.get("SIN_CMAKE_DIR", "C:/tools/cmake-3.30.3-windows-x86_64"))

# Dev: 日常开发档 (-O1 -g1，见根 CMakeLists.txt)，建议配合 --build-dir build-dev
BUILD_TYPES = ["Dev", "Debug", "Release", "RelWithDebInfo", "MinSizeRel"]

# ============================================================
#  颜色输出
# ============================================================


class C:
    CYAN = "\033[96m"
    GREEN = "\033[92m"
    YELLOW = "\033[93m"
    RED = "\033[91m"
    BOLD = "\033[1m"
    HEADER = "\033[95m"
    RESET = "\033[0m"


def info(msg):
    print(f"{C.CYAN}[INFO]{C.RESET} {msg}")


def ok(msg):
    print(f"{C.GREEN}[ OK ]{C.RESET} {msg}")


def warn(msg):
    print(f"{C.YELLOW}[WARN]{C.RESET} {msg}")


def fail(msg):
    print(f"{C.RED}[FAIL]{C.RESET} {msg}")


def header(msg):
    bar = "=" * 55
    print(f"\n{C.BOLD}{C.HEADER}{bar}{C.RESET}")
    print(f"{C.BOLD}{C.HEADER}  {msg}{C.RESET}")
    print(f"{C.BOLD}{C.HEADER}{bar}{C.RESET}")


# ============================================================
#  环境与工具
# ============================================================


class Environment:
    """管理构建工具路径与环境变量"""

    def __init__(self, args):
        self.qt_dir = Path(getattr(args, "qt_dir", None) or DEFAULT_QT_DIR)
        self.mingw_dir = Path(getattr(args, "mingw_dir", None) or DEFAULT_MINGW_DIR)
        self.cmake_dir = Path(getattr(args, "cmake_dir", None) or DEFAULT_CMAKE_DIR)

        self.qt_bin = self.qt_dir / "bin"
        self.mingw_bin = self.mingw_dir / "bin"
        self.cmake_bin = self.cmake_dir / "bin"

        self.cmake = self.cmake_bin / "cmake.exe"
        self.cxx = self.mingw_bin / "g++.exe"
        self.cc = self.mingw_bin / "gcc.exe"
        self.gdb = self.mingw_bin / "gdb.exe"
        self.windeployqt = self.qt_bin / "windeployqt.exe"

        # ---- make 程序 (MinGW Makefiles 生成器需要) ----
        # 优先 mingw32-make.exe，回退 make.exe
        mingw32_make = self.mingw_bin / "mingw32-make.exe"
        make_exe = self.mingw_bin / "make.exe"
        if mingw32_make.exists():
            self.make_program = mingw32_make
        elif make_exe.exists():
            self.make_program = make_exe
        else:
            self.make_program = mingw32_make  # 默认值，verify 时会报错

        # ---- Ninja 构建系统 (项目本地 tools/ 目录，自动检测) ----
        self.ninja = TOOLS_DIR / "ninja" / "ninja.exe"
        self.use_ninja = self.ninja.exists()

        # 注意: 不使用 ccache（与 MinGW g++ 13 的 PCH 不兼容，会静默崩溃）
        # 注意: 不使用 LLD 链接器（在 Windows 上会导致文件锁问题）

    def setup_path(self):
        """将工具路径加入 PATH

        关键: MinGW bin 必须在 PATH 中，否则 cc1plus.exe 找不到
        libgcc_s_seh-1.dll / libstdc++-6.dll / libwinpthread-1.dll 等 DLL
        会导致编译器静默崩溃（STATUS_DLL_NOT_FOUND, 退出码 -1073741515）
        """
        prepend = [str(self.cmake_bin), str(self.mingw_bin), str(self.qt_bin)]
        if self.use_ninja:
            prepend.insert(0, str(self.ninja.parent))
        os.environ["PATH"] = os.pathsep.join(prepend) + os.pathsep + os.environ.get("PATH", "")

    def print_accel_info(self):
        """打印构建工具状态"""
        if self.use_ninja:
            ok("构建系统: Ninja")
        else:
            warn("未检测到 Ninja，使用 MinGW Makefiles（较慢）")

    def _check(self, path, name, required=True):
        exists = path.exists()
        if exists:
            ok(f"{name:12s} {path}")
        elif required:
            fail(f"{name:12s} {path} (未找到)")
        else:
            warn(f"{name:12s} {path} (未找到，可选)")
        return exists or not required

    def verify(self):
        """检查必需工具是否存在"""
        header("工具检查")
        all_ok = True
        all_ok &= self._check(self.cmake, "CMake")
        all_ok &= self._check(self.cxx, "g++")
        all_ok &= self._check(self.cc, "gcc")
        all_ok &= self._check(self.windeployqt, "windeployqt")
        self._check(self.gdb, "gdb", required=False)
        return all_ok


def run_cmd(cmd, cwd=None, check=True):
    """执行命令，失败时退出"""
    display = " ".join(str(c) for c in cmd) if isinstance(cmd, list) else cmd
    info(f"$ {display}")
    result = subprocess.run(cmd, cwd=cwd or str(PROJECT_ROOT))
    if check and result.returncode != 0:
        fail(f"命令失败 (退出码: {result.returncode})")
        sys.exit(result.returncode)
    return result


# ============================================================
#  命令实现
# ============================================================


def cmd_configure(env, args):
    """CMake 配置"""
    header("CMake 配置")
    if not env.verify():
        sys.exit(1)

    env.print_accel_info()

    if getattr(args, "clean", False) and BUILD_DIR.exists():
        info("清理旧构建目录...")
        shutil.rmtree(BUILD_DIR)

    build_type = getattr(args, "build_type", "Debug")
    info(f"构建类型: {build_type}")

    cmd = [
        str(env.cmake),
        "-B", str(BUILD_DIR),
        "-S", str(PROJECT_ROOT),
    ]

    # 生成器: 优先 Ninja，回退 MinGW Makefiles
    if env.use_ninja:
        cmd.extend(["-G", "Ninja"])
        info("使用 Ninja 生成器")
    else:
        cmd.extend(["-G", "MinGW Makefiles"])
        cmd.append(f"-DCMAKE_MAKE_PROGRAM={env.make_program.as_posix()}")
        info(f"使用 MinGW Makefiles 生成器 (make: {env.make_program.name})")

    cmd.extend([
        f"-DCMAKE_PREFIX_PATH={env.qt_dir.as_posix()}",
        f"-DCMAKE_CXX_COMPILER={env.cxx.as_posix()}",
        f"-DCMAKE_C_COMPILER={env.cc.as_posix()}",
        f"-DCMAKE_BUILD_TYPE={build_type}",
    ])

    # 额外缓存定义（如 -DTRY_GOLD=ON）
    for d in getattr(args, "define", None) or []:
        cmd.append(f"-D{d}")

    run_cmd(cmd)
    ok("CMake 配置完成")


def kill_running_executable():
    """编译前自动终止正在运行的 openbus.exe，避免文件锁导致链接失败"""
    if sys.platform != "win32":
        return
    try:
        result = subprocess.run(
            ["taskkill", "/F", "/IM", "openbus.exe"],
            capture_output=True, text=True
        )
        if result.returncode == 0:
            warn("检测到 openbus.exe 正在运行，已自动终止")
            # 等待进程完全退出、文件锁释放
            import time
            for _ in range(20):
                time.sleep(0.25)
                try:
                    # 尝试以独占模式打开文件，成功则说明锁已释放
                    if EXECUTABLE.exists():
                        with open(EXECUTABLE, "a"):
                            pass
                    break
                except (PermissionError, OSError):
                    continue
    except Exception:
        pass


def cmd_build(env, args):
    """增量编译 (首次运行自动配置)"""
    header("增量编译")

    if not (BUILD_DIR / "CMakeCache.txt").exists():
        info("构建目录未配置，自动执行 configure...")
        cmd_configure(env, args)

    # 编译前自动终止正在运行的程序，避免文件锁
    kill_running_executable()

    cmd = [str(env.cmake), "--build", str(BUILD_DIR)]
    if args.target:
        cmd.extend(["--target", args.target])

    jobs = args.jobs or os.cpu_count() or 4
    # Ninja 和 MinGW Makefiles 都支持 -j 参数
    cmd.extend(["--", f"-j{jobs}"])

    run_cmd(cmd)
    ok(f"编译完成 ({jobs} 线程)")


def cmd_run(env, args):
    """运行程序 (自动编译)"""
    header("运行程序")
    if not EXECUTABLE.exists():
        info("可执行文件不存在，自动执行 build...")
        cmd_build(env, args)
    else:
        # 即使已存在，也先确保旧进程已退出
        kill_running_executable()

    extra = args.args.split() if args.args else []
    cmd = [str(EXECUTABLE)] + extra
    info(f"启动: {EXECUTABLE}")
    subprocess.run(cmd, cwd=str(EXECUTABLE.parent))


def cmd_debug(env, args):
    """GDB 调试 (自动编译)"""
    header("GDB 调试")
    if not env.gdb.exists():
        fail(f"GDB 未找到: {env.gdb}")
        sys.exit(1)

    if not EXECUTABLE.exists():
        info("可执行文件不存在，自动执行 build...")
        cmd_build(env, args)
    else:
        kill_running_executable()

    extra = args.args.split() if args.args else []
    if extra:
        cmd = [str(env.gdb), "--args", str(EXECUTABLE)] + extra
    else:
        cmd = [str(env.gdb), str(EXECUTABLE)]

    info(f"调试: {EXECUTABLE}")
    subprocess.run(cmd, cwd=str(EXECUTABLE.parent))


def cmd_clean(env, args):
    """清理构建目录"""
    header("清理构建")
    if BUILD_DIR.exists():
        info(f"删除: {BUILD_DIR}")
        shutil.rmtree(BUILD_DIR)
        ok("清理完成")
    else:
        info("构建目录不存在，无需清理")


def cmd_rebuild(env, args):
    """重新构建: 清理 + 配置 + 编译"""
    header("重新构建")
    cmd_clean(env, args)
    args.clean = False
    # rebuild 子命令没有 --target / --jobs 参数，补齐默认值供 cmd_build 使用
    if not hasattr(args, "target"):
        args.target = None
    if not hasattr(args, "jobs"):
        args.jobs = None
    cmd_configure(env, args)
    cmd_build(env, args)
    ok("重新构建完成")


def cmd_deploy(env, args):
    """部署 Qt 运行时依赖 (windeployqt)"""
    header("部署 Qt 依赖")
    if not EXECUTABLE.exists():
        info("可执行文件不存在，自动执行 build...")
        cmd_build(env, args)

    run_cmd([str(env.windeployqt), str(EXECUTABLE)])

    # windeployqt 无法检测静态库 (qcustomplot) 对 Qt6PrintSupport 的传递依赖，
    # 需手动复制 Qt6PrintSupport.dll 到输出目录
    printsupport = env.qt_bin / "Qt6PrintSupport.dll"
    dest = EXECUTABLE.parent / "Qt6PrintSupport.dll"
    if printsupport.exists() and not dest.exists():
        shutil.copy2(str(printsupport), str(dest))
        ok(f"手动补充复制 Qt6PrintSupport.dll（qcustomplot 静态库传递依赖）")
    elif not printsupport.exists():
        warn(f"Qt6PrintSupport.dll 在 Qt 安装目录中未找到: {printsupport}")

    ok("部署完成")


def cmd_all(env, args):
    """完整流程: 配置 + 编译 + 部署 + 运行"""
    header("完整构建流程")
    cmd_configure(env, args)
    cmd_build(env, args)
    cmd_deploy(env, args)
    cmd_run(env, args)


def cmd_status(env, args):
    """显示环境与构建状态"""
    header("环境状态")
    print(f"  项目根目录:  {PROJECT_ROOT}")
    print(f"  构建目录:    {BUILD_DIR}  {'[已存在]' if BUILD_DIR.exists() else '[未创建]'}")
    print(f"  可执行文件:  {EXECUTABLE}  {'[已存在]' if EXECUTABLE.exists() else '[未构建]'}")
    print()

    env.verify()
    env.print_accel_info()

    # 读取构建类型
    cache = BUILD_DIR / "CMakeCache.txt"
    if cache.exists():
        for line in cache.read_text(encoding="utf-8", errors="ignore").splitlines():
            if line.startswith("CMAKE_BUILD_TYPE:STRING="):
                print(f"  构建类型:    {line.split('=', 1)[1]}")
                break
    print()


def cmd_open(env, args):
    """在资源管理器中打开构建输出目录"""
    target = EXECUTABLE.parent if EXECUTABLE.parent.exists() else BUILD_DIR
    if target.exists():
        subprocess.run(["explorer", str(target)])
        ok(f"已打开: {target}")
    else:
        fail(f"目录不存在: {target}")


# ============================================================
#  参数解析
# ============================================================


def main():
    parser = argparse.ArgumentParser(
        description="openbus 项目构建脚本",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
常用命令:
  python scripts/build.py configure                      配置 (Debug, 自动检测 Ninja)
  python scripts/build.py configure --build-type Release  配置 (Release)
  python scripts/build.py configure --build-type Dev --build-dir build-dev
                                                          配置 Dev 快速档 (独立目录)
  python scripts/build.py build -j8                       增量编译 (8 线程)
  python scripts/build.py build --build-dir build-dev -j8 增量编译 Dev 档
  python scripts/build.py run                             运行
  python scripts/build.py debug                           GDB 调试
  python scripts/build.py clean                           清理
  python scripts/build.py rebuild                         重新构建
  python scripts/build.py deploy                          部署 Qt 依赖
  python scripts/build.py all                             完整流程
  python scripts/build.py status                          环境状态
  python scripts/build.py open                            打开输出目录

构建系统 (放在 tools/ 目录自动检测):
  tools/ninja/ninja.exe    Ninja 构建系统 (编译调度快 2-3x)

注意: 不使用 ccache (与 PCH 不兼容) 和 LLD (文件锁问题)
        """,
    )

    # 全局选项 (所有子命令可用；--build-dir 也可放在子命令之后)
    parser.add_argument("--qt-dir", default=None, help=f"Qt6 路径 (默认: {DEFAULT_QT_DIR})")
    parser.add_argument("--mingw-dir", default=None, help=f"MinGW 路径 (默认: {DEFAULT_MINGW_DIR})")
    parser.add_argument("--cmake-dir", default=None, help=f"CMake 路径 (默认: {DEFAULT_CMAKE_DIR})")
    parser.add_argument("--build-dir", default="build",
                        help="构建目录 (默认: build；Dev 档建议 build-dev，可与全量 Debug 并存)")

    def add_build_dir_opt(p):
        """子命令级 --build-dir：SUPPRESS 默认值，避免覆盖全局解析结果"""
        p.add_argument("--build-dir", default=argparse.SUPPRESS, help=argparse.SUPPRESS)

    sub = parser.add_subparsers(dest="command", help="可用命令")

    # configure
    p = sub.add_parser("configure", help="CMake 配置")
    p.add_argument("--build-type", choices=BUILD_TYPES, default="Debug")
    p.add_argument("--clean", action="store_true", help="配置前清理构建目录")
    p.add_argument("-D", "--define", action="append", default=[], metavar="VAR=VALUE",
                   help="额外 CMake 缓存定义 (如 -DTRY_GOLD=ON)，可多次使用")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_configure)

    # build
    p = sub.add_parser("build", help="增量编译")
    p.add_argument("-j", "--jobs", type=int, help="并行任务数 (默认: CPU 核心数)")
    p.add_argument("--target", help="指定构建目标")
    p.add_argument("--build-type", choices=BUILD_TYPES, default="Debug", help="自动配置时的构建类型")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_build)

    # run
    p = sub.add_parser("run", help="运行程序")
    p.add_argument("--args", default="", help="传递给程序的参数")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_run)

    # debug
    p = sub.add_parser("debug", help="GDB 调试")
    p.add_argument("--args", default="", help="传递给程序的参数")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_debug)

    # clean
    p = sub.add_parser("clean", help="清理构建目录")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_clean)

    # rebuild
    p = sub.add_parser("rebuild", help="重新构建 (清理 + 配置 + 编译)")
    p.add_argument("--build-type", choices=BUILD_TYPES, default="Debug")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_rebuild)

    # deploy
    p = sub.add_parser("deploy", help="部署 Qt 运行时依赖")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_deploy)

    # all
    p = sub.add_parser("all", help="完整流程 (配置 + 编译 + 部署 + 运行)")
    p.add_argument("--build-type", choices=BUILD_TYPES, default="Debug")
    p.add_argument("--args", default="", help="传递给程序的参数")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_all)

    # status
    p = sub.add_parser("status", help="显示环境状态")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_status)

    # open
    p = sub.add_parser("open", help="在资源管理器中打开输出目录")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_open)

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(0)

    # 生效 --build-dir（全局或子命令位置均可传入）
    set_build_dir(getattr(args, "build_dir", "build"))

    env = Environment(args)
    env.setup_path()
    args.func(env, args)


if __name__ == "__main__":
    main()

