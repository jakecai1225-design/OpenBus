#!/usr/bin/env python3
"""
openbus build script

Commands:
  configure  - CMake configure
  build      - Incremental build
  run        - Run application
  debug      - GDB debug
  clean      - Clean build
  rebuild    - Rebuild (clean + configure + build)
  deploy     - Deploy Qt runtime deps
  package    - Portable Windows zip (Release + deploy + archive)
               optional --installer → NSIS setup.exe with language dialog
  all        - Full pipeline (configure + build + deploy + run)
  status     - Show environment status
  open       - Open build dir in Explorer

Examples:
  python scripts/build.py configure --build-type Release
  python scripts/build.py build -j8
  python scripts/build.py run
  python scripts/build.py debug
  python scripts/build.py rebuild
  python scripts/build.py status
  python scripts/build.py package -j8
  python scripts/build.py package --build-type Release --version 1.0.0
  python scripts/build.py package --version 1.10.4 --installer

Dev profile (daily: -O1 -g1; separate dir alongside full Debug):
  python scripts/build.py configure --build-type Dev --build-dir build-dev
  python scripts/build.py build --build-dir build-dev -j8
  python scripts/build.py run --build-dir build-dev

Multi-machine / multi-env setup:
  ============================

  This project supports MSYS2 only (default UCRT64). Do not use native
  Windows CMake, the Qt online installer, or other MinGW distros.

  Use MSYS2/bash paths (auto-converted when invoking Windows PE tools):
    /c/msys64/ucrt64          # full prefix
    /ucrt64                   # short form inside UCRT64 shell
    /d/openbus/.../sin        # project root

  Install deps with pacman:
    bash scripts/setup_msys2.sh

  Env vars (optional, bash paths):
    SIN_MSYS2_ROOT=/c/msys64
    SIN_MSYS2_ENV=ucrt64
    SIN_QT_DIR=/ucrt64
    SIN_MINGW_DIR=/ucrt64
    SIN_CMAKE_DIR=/ucrt64

  Preferred: build inside MSYS2 UCRT64 shell:
    python scripts/build.py configure --clean
    python scripts/build.py build -j8
    python scripts/build.py deploy

Note: do not use ccache (PCH-incompatible) or LLD (file-lock issues)
        """

import argparse
import ctypes
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional

# Enable Windows console ANSI colors
def _enable_ansi_colors():
    if sys.platform != "win32":
        return
    kernel32 = ctypes.windll.kernel32
    handle = kernel32.GetStdHandle(-11)  # STD_OUTPUT_HANDLE
    mode = ctypes.c_uint32()
    if kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
        kernel32.SetConsoleMode(handle, mode.value | 0x0004)  # ENABLE_VIRTUAL_TERMINAL_PROCESSING


_enable_ansi_colors()

# Windows console UTF-8 output
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    except Exception:
        pass

# ============================================================
#  Paths + MSYS2/bash path conversion
# ============================================================

PROJECT_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = PROJECT_ROOT / "build"          # default build dir; overridable via --build-dir
EXECUTABLE = BUILD_DIR / "bin" / "openbus.exe"
TOOLS_DIR = PROJECT_ROOT / "tools"

# MSYS2 env names (for /ucrt64 short paths)
_MSYS_ENV_NAMES = ("ucrt64", "mingw64", "clang64", "mingw32", "clangarm64")


def to_msys(path) -> str:
    """Windows / Path -> MSYS2 bash path.

    Examples: C:\\msys64\\ucrt64 → /c/msys64/ucrt64
        D:/foo/bar         → /d/foo/bar
        /ucrt64            → /ucrt64 (unchanged)
    """
    if path is None:
        return ""
    s = str(path).strip().replace("\\", "/")
    if not s:
        return ""
    # already bash-style
    if s.startswith("/") and not s.startswith("//"):
        return s.rstrip("/") or "/"
    # UNC \\server\share — keep as //server/share
    if s.startswith("//"):
        return s.rstrip("/")
    # X:/... or X:...
    if len(s) >= 2 and s[1] == ":":
        drive = s[0].lower()
        rest = s[2:]
        if not rest.startswith("/"):
            rest = "/" + rest if rest else ""
        return f"/{drive}{rest}".rstrip("/") or f"/{drive}"
    return s


def from_msys(path) -> Path:
    """MSYS2 bash / Windows path -> pathlib.Path (existence checks).

    Supports:
      /c/msys64/ucrt64
      /ucrt64、/mingw64 (short prefix vs SIN_MSYS2_ROOT or default /c/msys64)
      C:/msys64/ucrt64、C:\\msys64\\ucrt64
    """
    if isinstance(path, Path):
        return path
    s = str(path).strip().replace("\\", "/")
    if not s:
        return Path()

    # short /ucrt64 -> {root}/ucrt64
    bare = s.lstrip("/")
    if s.startswith("/") and bare in _MSYS_ENV_NAMES:
        root = os.environ.get("SIN_MSYS2_ROOT") or "/c/msys64"
        return from_msys(f"{to_msys(root)}/{bare}")

    # /c/foo/bar → C:/foo/bar
    if len(s) >= 3 and s[0] == "/" and s[1].isalpha() and s[2] == "/":
        return Path(f"{s[1].upper()}:{s[2:]}")
    if len(s) == 2 and s[0] == "/" and s[1].isalpha():
        return Path(f"{s[1].upper()}:/")

    # already Windows or relative
    return Path(s)


def cmake_path(path) -> str:
    """Path for MinGW/Windows cmake.exe (must be C:/..., not /c/...)."""
    p = from_msys(path).resolve() if Path(str(path)).exists() or str(path).startswith("/") else from_msys(path)
    try:
        p = p.resolve()
    except OSError:
        pass
    return p.as_posix()


def fmt_path(path) -> str:
    """Normalize path for display as bash form."""
    return to_msys(from_msys(path) if not isinstance(path, Path) else path)


# ============================================================
#  MSYS2 toolchain detection (only supported toolchain)
# ============================================================

def _msys2_roots():
    roots = []
    env_root = os.environ.get("SIN_MSYS2_ROOT")
    if env_root:
        roots.append(from_msys(env_root))
    roots.extend([
        from_msys("/c/msys64"),
        from_msys("/d/msys64"),
        from_msys("/c/tools/msys64"),
        Path("C:/msys64"),
        Path("D:/msys64"),
    ])
    seen = set()
    out = []
    for r in roots:
        try:
            key = str(r.resolve()).lower() if r.exists() else str(r).lower()
        except OSError:
            key = str(r).lower()
        if key in seen:
            continue
        seen.add(key)
        if r.exists():
            out.append(r)
    return out


def _msys2_env_names():
    """Env priority: SIN_MSYS2_ENV > MSYSTEM > ucrt64 > mingw64 > clang64"""
    preferred = (os.environ.get("SIN_MSYS2_ENV") or "").strip().lower()
    msystem = (os.environ.get("MSYSTEM") or "").strip().lower()
    ordered = []
    for name in (preferred, msystem, "ucrt64", "mingw64", "clang64"):
        if name and name not in ordered:
            ordered.append(name)
    return ordered


def _is_msys2_prefix(path: Path) -> bool:
    """True if path is an MSYS2 prefix (.../msys64/ucrt64); reject native Qt/CMake."""
    try:
        parts = [p.lower() for p in path.resolve().parts]
    except OSError:
        parts = [p.lower() for p in Path(path).parts]
    if "msys64" not in parts and "msys2" not in parts:
        return False
    return any(env in parts for env in ("ucrt64", "mingw64", "clang64", "mingw32", "clangarm64"))


def _msys2_prefixes():
    """Return usable MSYS2 prefixes (with g++)."""
    prefixes = []
    for root in _msys2_roots():
        for env_name in _msys2_env_names():
            p = root / env_name
            if (p / "bin" / "g++.exe").exists() or (p / "bin" / "g++").exists():
                prefixes.append(p)
    return prefixes


def _has_qmake(prefix: Path) -> bool:
    bin_dir = prefix / "bin"
    return (bin_dir / "qmake6.exe").exists() or (bin_dir / "qmake6").exists() \
        or (bin_dir / "qmake.exe").exists() or (bin_dir / "qmake").exists()


def _is_qt_prefix(prefix: Path) -> bool:
    if not prefix.exists() or not _is_msys2_prefix(prefix):
        return False
    if (prefix / "lib" / "cmake" / "Qt6" / "Qt6Config.cmake").exists():
        return True
    return _has_qmake(prefix)


def _find_windeployqt(qt_bin: Path):
    for name in ("windeployqt6.exe", "windeployqt6", "windeployqt-qt6.exe", "windeployqt.exe"):
        p = qt_bin / name
        if p.exists():
            return p
    return qt_bin / "windeployqt6.exe"


def _append_unique(lst, path: Path):
    if path and path not in lst:
        lst.append(path)


def _reject_non_msys2(path: Path, what: str) -> Path:
    if not _is_msys2_prefix(path):
        raise RuntimeError(
            f"{what} must be under an MSYS2 prefix; rejected: {fmt_path(path)}\n"
            f"Install deps: bash scripts/setup_msys2.sh\n"
            f"Or set SIN_MSYS2_ROOT=/c/msys64  SIN_QT_DIR=/ucrt64"
        )
    return path


def _find_msys2_prefix_dirs():
    """Unified prefix candidates: same dir provides Qt / g++ / cmake."""
    candidates = []

    for key in ("SIN_QT_DIR", "SIN_MINGW_DIR", "SIN_CMAKE_DIR"):
        val = os.environ.get(key)
        if not val:
            continue
        p = from_msys(val)
        if _is_msys2_prefix(p) and (p / "bin").exists():
            _append_unique(candidates, p)

    for p in _msys2_prefixes():
        _append_unique(candidates, p)

    return candidates


def _find_qt_dirs():
    return [p for p in _find_msys2_prefix_dirs() if _is_qt_prefix(p)]


def _find_mingw_dirs():
    out = []
    for p in _find_msys2_prefix_dirs():
        if (p / "bin" / "g++.exe").exists() or (p / "bin" / "g++").exists():
            _append_unique(out, p)
    return out


def _find_cmake_dirs():
    out = []
    for p in _find_msys2_prefix_dirs():
        if (p / "bin" / "cmake.exe").exists() or (p / "bin" / "cmake").exists():
            _append_unique(out, p)
    return out


def set_build_dir(name):
    """Switch build dir (--build-dir build-dev, etc.); refresh executable path

    Allow Dev (build-dev/) alongside full Debug (build/) without full rebuilds.
    Accepts bash paths: --build-dir /d/openbus/.../build-dev
    """
    global BUILD_DIR, EXECUTABLE
    raw = str(name)
    if raw.startswith("/") or (len(raw) >= 2 and raw[1] == ":"):
        bd = from_msys(raw)
    else:
        bd = Path(raw)
        if not bd.is_absolute():
            bd = PROJECT_ROOT / bd
    BUILD_DIR = bd
    EXECUTABLE = BUILD_DIR / "bin" / "openbus.exe"


def get_preferred_tool(finders, name, required=True):
    """Pick the first available tool path from candidates"""
    all_candidates = []
    for finder in finders:
        try:
            candidates = finder()
            if candidates:
                all_candidates.extend(candidates)
        except Exception:
            pass

    if all_candidates:
        return all_candidates[0]
    if required:
        raise RuntimeError(
            f"MSYS2 {name} not found. In UCRT64 shell run:\n"
            f"  bash scripts/setup_msys2.sh"
        )
    return None


# Defaults all point at the same MSYS2 prefix (ucrt64)
_default_prefixes = _find_msys2_prefix_dirs()
_default_qt_dirs = _find_qt_dirs()
_default_mingw_dirs = _find_mingw_dirs()
_default_cmake_dirs = _find_cmake_dirs()

_FALLBACK_MSYS = from_msys("/c/msys64/ucrt64")
DEFAULT_QT_DIR = get_preferred_tool([lambda: _default_qt_dirs], "Qt6", required=False) \
    or get_preferred_tool([lambda: _default_prefixes], "MSYS2", required=False) \
    or _FALLBACK_MSYS
DEFAULT_MINGW_DIR = get_preferred_tool([lambda: _default_mingw_dirs], "g++", required=False) \
    or DEFAULT_QT_DIR
DEFAULT_CMAKE_DIR = get_preferred_tool([lambda: _default_cmake_dirs], "CMake", required=False) \
    or DEFAULT_QT_DIR

# Dev: daily profile (-O1 -g1; see root CMakeLists.txt); use --build-dir build-dev
BUILD_TYPES = ["Dev", "Debug", "Release", "RelWithDebInfo", "MinSizeRel"]

# ============================================================
#  Color output
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


err = fail


def header(msg):
    bar = "=" * 55
    print(f"\n{C.BOLD}{C.HEADER}{bar}{C.RESET}")
    print(f"{C.BOLD}{C.HEADER}  {msg}{C.RESET}")
    print(f"{C.BOLD}{C.HEADER}{bar}{C.RESET}")


# ============================================================
#  Environment and tools
# ============================================================


def _tool_exe(bin_dir: Path, name: str) -> Path:
    """Resolve MSYS2 tool executable (prefer .exe)."""
    for cand in (bin_dir / f"{name}.exe", bin_dir / name):
        if cand.exists():
            return cand
    return bin_dir / f"{name}.exe"


def _sanitize_path_for_msys2(msys_bins):
    """Prepend MSYS2 to PATH; strip native Windows CMake / Qt / rogue MinGW."""
    blocked_needles = (
        r"\cmake\bin",
        r"/cmake/bin",
        r"\qt\tools",
        r"/qt/tools",
        r"\qt\6.",
        r"/qt/6.",
        r"\mingw\bin",
        r"/mingw/bin",
        r"\mingw64\bin",
        r"/mingw64/bin",
        r"program files\cmake",
        r"program files (x86)\cmake",
    )
    resolved_msys = set()
    for b in msys_bins:
        try:
            resolved_msys.add(Path(b).resolve())
        except OSError:
            pass
    keep = []
    for part in os.environ.get("PATH", "").split(os.pathsep):
        if not part:
            continue
        try:
            if Path(part).resolve() in resolved_msys:
                continue
        except OSError:
            pass
        low = part.replace("/", "\\").lower()
        if any(n in low for n in blocked_needles):
            if "msys64" in low or "msys2" in low:
                keep.append(part)
            continue
        keep.append(part)
    ordered, seen = [], set()
    for p in list(msys_bins) + keep:
        key = p.lower()
        if key in seen:
            continue
        seen.add(key)
        ordered.append(p)
    os.environ["PATH"] = os.pathsep.join(ordered)


class Environment:
    """Manage MSYS2 build tool paths (only supported toolchain)."""

    def __init__(self, args):
        qt = from_msys(getattr(args, "qt_dir", None) or DEFAULT_QT_DIR)
        mingw = from_msys(getattr(args, "mingw_dir", None) or DEFAULT_MINGW_DIR)
        cmake = from_msys(getattr(args, "cmake_dir", None) or DEFAULT_CMAKE_DIR)

        self.qt_dir = _reject_non_msys2(qt, "Qt6 (--qt-dir / SIN_QT_DIR)")
        self.mingw_dir = _reject_non_msys2(mingw, "compiler (--mingw-dir / SIN_MINGW_DIR)")
        self.cmake_dir = _reject_non_msys2(cmake, "CMake (--cmake-dir / SIN_CMAKE_DIR)")

        if self.qt_dir.resolve() != self.mingw_dir.resolve() or self.qt_dir.resolve() != self.cmake_dir.resolve():
            warn(
                "Qt / compiler / CMake prefixes differ; forcing align to Qt prefix:\n"
                f"  Qt={fmt_path(self.qt_dir)}\n"
                f"  CXX={fmt_path(self.mingw_dir)}\n"
                f"  CMake={fmt_path(self.cmake_dir)}"
            )
            self.mingw_dir = self.qt_dir
            self.cmake_dir = self.qt_dir

        self.qt_bin = self.qt_dir / "bin"
        self.mingw_bin = self.mingw_dir / "bin"
        self.cmake_bin = self.cmake_dir / "bin"

        self.cmake = _tool_exe(self.cmake_bin, "cmake")
        self.cxx = _tool_exe(self.mingw_bin, "g++")
        self.cc = _tool_exe(self.mingw_bin, "gcc")
        self.gdb = _tool_exe(self.mingw_bin, "gdb")
        self.windeployqt = _find_windeployqt(self.qt_bin)
        self.is_msys2 = True
        self.msys_env = next(
            (e for e in _MSYS_ENV_NAMES
             if e in [p.lower() for p in self.mingw_dir.parts]),
            "ucrt64",
        )

        mingw32_make = _tool_exe(self.mingw_bin, "mingw32-make")
        make_exe = _tool_exe(self.mingw_bin, "make")
        self.make_program = mingw32_make if mingw32_make.exists() else (
            make_exe if make_exe.exists() else mingw32_make
        )

        ninja_candidates = [
            _tool_exe(self.mingw_bin, "ninja"),
            _tool_exe(self.qt_bin, "ninja"),
        ]
        self.ninja = next((n for n in ninja_candidates if n.exists()), None)
        self.use_ninja = self.ninja is not None

    def setup_path(self):
        msys_bins = [str(self.cmake_bin), str(self.mingw_bin), str(self.qt_bin)]
        if self.use_ninja and self.ninja:
            msys_bins.insert(0, str(self.ninja.parent))
        usr_bin = self.mingw_dir.parent / "usr" / "bin"
        if usr_bin.exists():
            msys_bins.append(str(usr_bin))
        _sanitize_path_for_msys2(msys_bins)

    def print_accel_info(self):
        info(f"Toolchain:      MSYS2 / {self.msys_env}")
        info(f"Prefix:        {fmt_path(self.mingw_dir)}")
        info(f"Short:      /{self.msys_env}")
        info(f"Qt6:           {fmt_path(self.qt_dir)}")
        info(f"CMake:         {fmt_path(self.cmake)}")
        if self.use_ninja:
            ok(f"Build system: Ninja ({fmt_path(self.ninja)})")
        else:
            warn("Ninja not found; falling back to MinGW Makefiles (slower)")
            warn("Install: pacman -S mingw-w64-ucrt-x86_64-ninja")

    def _check(self, path, name, required=True):
        exists = bool(path) and path.exists()
        shown = fmt_path(path) if path else str(path)
        if exists:
            ok(f"{name:12s} {shown}")
        elif required:
            fail(f"{name:12s} {shown} (missing)")
        else:
            warn(f"{name:12s} {shown} (missing, optional)")
        return exists or not required

    def verify(self, auto_fix=False):
        header("Tool check (MSYS2 only)")
        if not _is_msys2_prefix(self.qt_dir):
            fail(f"Not an MSYS2 prefix: {fmt_path(self.qt_dir)}")
            return False
        cmake_found = self._check(self.cmake, "CMake")
        gpp_found = self._check(self.cxx, "g++")
        gcc_found = self._check(self.cc, "gcc")
        windeployqt_found = self._check(self.windeployqt, "windeployqt")
        self._check(self.ninja or (self.mingw_bin / "ninja.exe"), "ninja", required=False)
        self._check(self.gdb, "gdb", required=False)
        if not (cmake_found and gpp_found and gcc_found and windeployqt_found):
            warn("Deps incomplete; run: bash scripts/setup_msys2.sh")
            if auto_fix:
                self._auto_fix_candidates()
            return False
        return True

    def _auto_fix_candidates(self):
        print(f"\n{C.YELLOW}Available MSYS2 prefixes:{C.RESET}")
        for i, p in enumerate(_find_msys2_prefix_dirs()[:8], 1):
            print(f"  {i}. {fmt_path(p)}  (/{p.name})")
        print("  Install deps: bash scripts/setup_msys2.sh\n")


def run_cmd(cmd, cwd=None, check=True):
    """Run a command; exit on failure.

    argv for PE tools stays Windows paths; logs print bash form.
    """
    def _fmt_arg(c):
        s = str(c)
        if s.startswith("-D") and "=" in s:
            key, _, val = s.partition("=")
            if val and (":" in val or "\\" in val or val.startswith("/")):
                try:
                    return f"{key}={fmt_path(val)}"
                except Exception:
                    return s
        if ":\\" in s or ":/" in s or s.startswith("/") or "\\" in s:
            try:
                if Path(s).exists() or s.startswith("/") or (len(s) > 1 and s[1] == ":"):
                    return fmt_path(s)
            except Exception:
                pass
        return s

    display = " ".join(_fmt_arg(c) for c in cmd) if isinstance(cmd, list) else str(cmd)
    info(f"$ {display}")
    result = subprocess.run(
        cmd,
        cwd=cwd or str(PROJECT_ROOT),
        env=os.environ,
    )
    if check and result.returncode != 0:
        fail(f"Command failed (exit code {result.returncode})")
        sys.exit(result.returncode)
    return result

# ============================================================
#  Command handlers
# ============================================================


def cmd_configure(env, args):
    """CMake configure"""
    header("CMake configure")
    if not env.verify():
        sys.exit(1)

    env.print_accel_info()

    # If --clean, wipe the old build tree first
    if getattr(args, "clean", False):
        info("Cleaning old build directory...")
        if BUILD_DIR.exists():
            shutil.rmtree(BUILD_DIR)
            ok(f"Removed: {fmt_path(BUILD_DIR)}")

    # Detect cache conflicts with other source trees / toolchains
    cache_file = BUILD_DIR / "CMakeCache.txt"
    if cache_file.exists() and not getattr(args, "clean", False):
        try:
            content = cache_file.read_text(encoding="utf-8", errors="ignore")
            current_source = cmake_path(PROJECT_ROOT)
            conflict_reason = None

            for line in content.splitlines():
                if line.startswith("CMAKE_SOURCE_DIR:STATIC="):
                    old_source = line.split("=", 1)[1].strip().strip('"')
                    if old_source.replace("\\", "/") != current_source:
                        conflict_reason = (
                            f"Source directory mismatch\n"
                            f"  Previously built from: {fmt_path(old_source)}\n"
                            f"  Current project:     {fmt_path(current_source)}"
                        )
                    break

            if conflict_reason is None:
                # On toolchain change (must stay MSYS2), wipe cache to avoid ABI mix
                import re
                m = re.search(r"CMAKE_PREFIX_PATH:STRING=(.+)", content)
                if m:
                    old_prefix = from_msys(m.group(1).split(";")[0].strip())
                    new_prefix = env.qt_dir.resolve()
                    if old_prefix.exists() and old_prefix.resolve() != new_prefix:
                        conflict_reason = (
                            f"MSYS2 prefix changed\n"
                            f"  Cached prefix:  {fmt_path(old_prefix)}\n"
                            f"  Current prefix: {fmt_path(new_prefix)}"
                        )
                    elif not _is_msys2_prefix(old_prefix):
                        conflict_reason = (
                            f"Non-MSYS2 cache detected (Windows Qt/CMake); must clean\n"
                            f"  Cached prefix:  {fmt_path(old_prefix)}"
                        )
                m2 = re.search(r"CMAKE_CXX_COMPILER:FILEPATH=(.+)", content)
                if conflict_reason is None and m2:
                    old_cxx = from_msys(m2.group(1).strip())
                    new_cxx = env.cxx.resolve()
                    if old_cxx.exists() and old_cxx.resolve() != new_cxx:
                        conflict_reason = (
                            f"C++ compiler changed\n"
                            f"  Cached compiler:  {fmt_path(old_cxx)}\n"
                            f"  Current compiler: {fmt_path(new_cxx)}"
                        )

            if conflict_reason:
                fail("Conflicting build cache detected:")
                for line in conflict_reason.splitlines():
                    fail(line)
                fail("")
                fail("Run: python scripts/build.py configure --clean")
                sys.exit(1)
        except Exception as e:
            warn(f"Failed to read cache: {e}")

    build_type = getattr(args, "build_type", "Debug")
    info(f"Build type: {build_type}")

    cmd = [
        str(env.cmake),
        "-B", cmake_path(BUILD_DIR),
        "-S", cmake_path(PROJECT_ROOT),
    ]

    # Generator: prefer Ninja, else MinGW Makefiles
    if env.use_ninja:
        cmd.extend(["-G", "Ninja"])
        info("Using Ninja generator")
    else:
        if not env.make_program.exists():
            fail(f"make program not found: {fmt_path(env.make_program)}")
            fail("On MSYS2 install ninja: pacman -S mingw-w64-ucrt-x86_64-ninja")
            sys.exit(1)
        cmd.extend(["-G", "MinGW Makefiles"])
        cmd.append(f"-DCMAKE_MAKE_PROGRAM={cmake_path(env.make_program)}")
        info(f"Using MinGW Makefiles generator (make: {env.make_program.name})")

    cmd.extend([
        f"-DCMAKE_PREFIX_PATH={cmake_path(env.qt_dir)}",
        f"-DCMAKE_CXX_COMPILER={cmake_path(env.cxx)}",
        f"-DCMAKE_C_COMPILER={cmake_path(env.cc)}",
        f"-DCMAKE_BUILD_TYPE={build_type}",
    ])

    # Extra cache defines (e.g. -DTRY_GOLD=ON)
    for d in getattr(args, "define", None) or []:
        cmd.append(f"-D{d}")

    run_cmd(cmd)
    ok("CMake configure done")


def kill_running_executable():
    """Kill running openbus.exe before link to avoid file locks"""
    if sys.platform != "win32":
        return
    try:
        result = subprocess.run(
            ["taskkill", "/F", "/IM", "openbus.exe"],
            capture_output=True, text=True
        )
        if result.returncode == 0:
            warn("openbus.exe was running; terminated")
            # Wait until process exit and file lock release
            import time
            for _ in range(20):
                time.sleep(0.25)
                try:
                    # Try exclusive open; success means lock released
                    if EXECUTABLE.exists():
                        with open(EXECUTABLE, "a"):
                            pass
                    break
                except (PermissionError, OSError):
                    continue
    except Exception:
        pass


def cmd_build(env, args):
    """Incremental build (auto-configure on first run)"""
    header("Incremental build")

    if not (BUILD_DIR / "CMakeCache.txt").exists():
        info("Build dir not configured; running configure...")
        cmd_configure(env, args)

    # Terminate running app before build to avoid file locks
    kill_running_executable()

    cmd = [str(env.cmake), "--build", cmake_path(BUILD_DIR)]
    
    # Build main target openbus.exe only; skip test targets
    cmd.extend(["--target", "openbus"])

    jobs = args.jobs or os.cpu_count() or 4
    # Both Ninja and MinGW Makefiles support -j
    cmd.extend(["--", f"-j{jobs}"])

    run_cmd(cmd)
    ok(f"Build done ({jobs}  threads)")


def cmd_run(env, args):
    """Run application (auto-build)"""
    header("Run application")
    if not EXECUTABLE.exists():
        info("Executable missing; running build...")
        cmd_build(env, args)
    else:
        # Even if present, ensure previous process exited
        kill_running_executable()

    extra = args.args.split() if args.args else []
    cmd = [str(EXECUTABLE)] + extra
    info(f"Launch: {fmt_path(EXECUTABLE)}")
    subprocess.run(cmd, cwd=str(EXECUTABLE.parent))


def cmd_debug(env, args):
    """GDB debug (auto-build)"""
    header("GDB debug")
    if not env.gdb.exists():
        fail(f"GDB not found: {fmt_path(env.gdb)}")
        sys.exit(1)

    if not EXECUTABLE.exists():
        info("Executable missing; running build...")
        cmd_build(env, args)
    else:
        kill_running_executable()

    extra = args.args.split() if args.args else []
    if extra:
        cmd = [str(env.gdb), "--args", str(EXECUTABLE)] + extra
    else:
        cmd = [str(env.gdb), str(EXECUTABLE)]

    info(f"Debug: {fmt_path(EXECUTABLE)}")
    subprocess.run(cmd, cwd=str(EXECUTABLE.parent))


def cmd_clean(env, args):
    """Clean build directory"""
    header("Clean build")
    if BUILD_DIR.exists():
        info(f"Removing: {fmt_path(BUILD_DIR)}")
        shutil.rmtree(BUILD_DIR)
        ok("Clean done")
    else:
        info("Build dir missing; nothing to clean")


def cmd_rebuild(env, args):
    """Rebuild: clean + configure + build"""
    header("Rebuild")
    cmd_clean(env, args)
    args.clean = False
    # rebuild lacks --target/--jobs; supply defaults for cmd_build
    if not hasattr(args, "target"):
        args.target = None
    if not hasattr(args, "jobs"):
        args.jobs = None
    cmd_configure(env, args)
    cmd_build(env, args)
    ok("Rebuild done")


def _pe_dll_imports(pe_path: Path, objdump: Path):
    """Return DLL names imported by a PE file (via objdump -p)."""
    if not pe_path.exists() or not objdump.exists():
        return []
    try:
        r = subprocess.run(
            [str(objdump), "-p", str(pe_path)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="ignore",
            check=False,
        )
    except OSError:
        return []
    names = []
    for line in (r.stdout or "").splitlines():
        line = line.strip()
        # "DLL Name: foo.dll"
        if line.lower().startswith("dll name:"):
            name = line.split(":", 1)[1].strip()
            if name:
                names.append(name)
    return names


def _is_system_dll(name: str) -> bool:
    n = name.lower()
    if n.startswith("api-ms-win-"):
        return True
    if n.startswith("ext-ms-"):
        return True
    system = {
        "kernel32.dll", "user32.dll", "gdi32.dll", "shell32.dll", "advapi32.dll",
        "ole32.dll", "oleaut32.dll", "uuid.dll", "comdlg32.dll", "winspool.drv",
        "ws2_32.dll", "wsock32.dll", "iphlpapi.dll", "dwmapi.dll", "uxtheme.dll",
        "imm32.dll", "version.dll", "setupapi.dll", "winmm.dll", "crypt32.dll",
        "bcrypt.dll", "ncrypt.dll", "secur32.dll", "shlwapi.dll", "comctl32.dll",
        "msvcrt.dll", "ntdll.dll", "rpcrt4.dll", "mpr.dll", "netapi32.dll",
        "userenv.dll", "dnsapi.dll", "wtsapi32.dll", "dxgi.dll", "d3d11.dll",
        "d3d12.dll", "d3d9.dll", "opengl32.dll", "glu32.dll", "gdiplus.dll",
        "psapi.dll", "dbghelp.dll", "imagehlp.dll", "winhttp.dll", "wininet.dll",
        "normaliz.dll", "powrprof.dll", "cfgmgr32.dll", "devobj.dll",
    }
    return n in system


def _deploy_msys2_runtime_deps(env, out_dir: Path):
    """Recursively copy MinGW/MSYS2 DLLs required by the exe and local DLLs.

    windeployqt does not ship ICU/PCRE/harfbuzz/etc. with correct SONAMEs
    (e.g. libpcre2-16-0.dll, libicuuc78.dll). Resolve imports with objdump.
    """
    objdump = _tool_exe(env.mingw_bin, "objdump")
    if not objdump.exists():
        warn(f"objdump not found: {fmt_path(objdump)}; skip recursive runtime deploy")
        return 0

    search_dirs = [env.mingw_bin, env.qt_bin]
    # Seeds: exe + every DLL already in the output dir
    queue = []
    for p in [EXECUTABLE] + sorted(out_dir.glob("*.dll")):
        if p.is_file():
            queue.append(p)

    copied = 0
    seen = set()
    while queue:
        pe = queue.pop()
        key = pe.resolve() if pe.exists() else pe
        if key in seen:
            continue
        seen.add(key)

        for dll_name in _pe_dll_imports(pe, objdump):
            if _is_system_dll(dll_name):
                continue
            dest = out_dir / dll_name
            if dest.exists():
                # Still walk into it for transitive deps
                queue.append(dest)
                continue

            src = None
            for d in search_dirs:
                cand = d / dll_name
                if cand.exists():
                    src = cand
                    break
            if src is None:
                # Case-insensitive fallback under mingw bin
                lower = dll_name.lower()
                for d in search_dirs:
                    if not d.exists():
                        continue
                    for cand in d.glob("*.dll"):
                        if cand.name.lower() == lower:
                            src = cand
                            break
                    if src:
                        break
            if src is None:
                continue

            shutil.copy2(str(src), str(dest))
            copied += 1
            info(f"  + {dll_name}")
            queue.append(dest)

    return copied


def cmd_deploy(env, args):
    """Deploy Qt runtime (windeployqt) + MSYS2 transitive DLLs"""
    header("Deploy Qt deps")

    env.setup_path()

    if not EXECUTABLE.exists():
        info("Executable missing; running build...")
        cmd_build(env, args)

    out_dir = EXECUTABLE.parent

    run_cmd([str(env.windeployqt), str(EXECUTABLE)])

    # windeployqt misses qcustomplot static transitive Qt6PrintSupport
    printsupport = env.qt_bin / "Qt6PrintSupport.dll"
    dest = out_dir / "Qt6PrintSupport.dll"
    if printsupport.exists() and not dest.exists():
        shutil.copy2(str(printsupport), str(dest))
        ok("Copied Qt6PrintSupport.dll (qcustomplot transitive dep)")
    elif not printsupport.exists():
        warn(f"Qt6PrintSupport.dll not found under Qt prefix: {fmt_path(printsupport)}")

    info("Resolving MSYS2/MinGW runtime DLLs (objdump)...")
    copied = _deploy_msys2_runtime_deps(env, out_dir)
    if copied:
        ok(f"Copied {copied} MinGW/MSYS2 runtime DLL(s)")
    else:
        ok("MSYS2 runtime DLLs already complete (or none needed)")

    # O-2: create lib/fonts (Qt 6 on Windows ships no fonts; missing dir
    # makes QFontDatabase warn; empty dir silences it; render falls back to
    # DirectWrite. For release you may place open fonts under lib/fonts
    # (DejaVu / Noto Sans CJK, etc.; do not redistribute MS system fonts)
    fonts_dir = out_dir / "lib" / "fonts"
    fonts_dir.mkdir(parents=True, exist_ok=True)

    # UI translations (.qm) + Python locale JSON (for portable packages)
    dst_tr = out_dir / "translations"
    dst_tr.mkdir(parents=True, exist_ok=True)
    build_qm = BUILD_DIR / "translations"
    if build_qm.is_dir():
        for qm in build_qm.glob("openbus_*.qm"):
            shutil.copy2(qm, dst_tr / qm.name)
    if not any(dst_tr.glob("openbus_*.qm")):
        for qm in (PROJECT_ROOT / "translations").glob("openbus_*.qm"):
            shutil.copy2(qm, dst_tr / qm.name)
            info(f"  + translations/{qm.name}")
    src_loc = PROJECT_ROOT / "plugins" / "_shared" / "locales"
    dst_loc = out_dir / "plugins" / "_shared" / "locales"
    if src_loc.is_dir():
        dst_loc.mkdir(parents=True, exist_ok=True)
        for jf in src_loc.glob("*.json"):
            shutil.copy2(jf, dst_loc / jf.name)

    ok("Deploy done")


def cmd_package(env, args):
    """Build a portable Windows zip (no installer): Release + deploy + archive."""
    import datetime
    import zipfile

    header("Portable package (Windows x64)")

    build_type = getattr(args, "build_type", "Release") or "Release"
    if build_type == "Debug":
        warn("Packaging Debug is possible but large; prefer --build-type Release")

    cache = BUILD_DIR / "CMakeCache.txt"
    need_configure = not cache.exists()
    if cache.exists():
        text = cache.read_text(encoding="utf-8", errors="ignore")
        if f"CMAKE_BUILD_TYPE:STRING={build_type}" not in text:
            need_configure = True

    if need_configure or getattr(args, "reconfigure", False):
        cfg = argparse.Namespace(
            build_type=build_type,
            clean=getattr(args, "clean", False),
            define=getattr(args, "define", []) or [],
            build_dir=getattr(args, "build_dir", str(BUILD_DIR.name)),
            jobs=getattr(args, "jobs", None),
        )
        cmd_configure(env, cfg)

    bargs = argparse.Namespace(
        jobs=getattr(args, "jobs", None),
        target=None,
        build_type=build_type,
        build_dir=getattr(args, "build_dir", str(BUILD_DIR.name)),
    )
    cmd_build(env, bargs)
    cmd_deploy(env, bargs)

    if not EXECUTABLE.exists():
        err(f"Missing executable: {fmt_path(EXECUTABLE)}")
        sys.exit(1)

    stamp = datetime.datetime.now().strftime("%Y%m%d")
    version = getattr(args, "version", "") or stamp
    folder_name = f"openbus-windows-x64-{version}"
    dist_root = PROJECT_ROOT / "dist"
    stage = dist_root / folder_name
    zip_path = dist_root / f"{folder_name}.zip"

    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)

    src_bin = EXECUTABLE.parent
    skip_suffixes = {".pdb", ".ilk", ".exp", ".lib", ".a", ".obj"}
    skip_names = {".ninja_deps", ".ninja_log", "CMakeFiles"}

    info(f"Staging from {fmt_path(src_bin)} -> {fmt_path(stage)}")
    for root, dirs, files in os.walk(src_bin):
        dirs[:] = [d for d in dirs if d not in skip_names and not d.endswith(".dir")]
        rel = Path(root).relative_to(src_bin)
        dest_dir = stage / rel
        dest_dir.mkdir(parents=True, exist_ok=True)
        for name in files:
            if Path(name).suffix.lower() in skip_suffixes:
                continue
            if name in skip_names:
                continue
            shutil.copy2(Path(root) / name, dest_dir / name)

    root_lic = PROJECT_ROOT / "LICENSE"
    if root_lic.is_file():
        shutil.copy2(root_lic, stage / "LICENSE.txt")

    (stage / "README.txt").write_text(
        "\n".join([
            "openbus — portable package (Windows 10/11 x64)",
            "================================================",
            "",
            "No installer required.",
            "",
            "1. Unzip this folder to any path (avoid a path that needs Admin rights).",
            "2. Double-click openbus.exe to start.",
            "3. Keep all files next to openbus.exe (DLL / platforms / kerneldlls / …).",
            "",
            "Requirements:",
            "  - Windows 10 or Windows 11, 64-bit",
            "  - No separate Qt / MSYS2 install needed",
            "",
            "Optional hardware drivers:",
            "  - USB-CAN vendors may still need their own device drivers from the vendor.",
            "",
            "Troubleshooting:",
            "  - If Windows SmartScreen warns, choose More info -> Run anyway",
            "    (unsigned portable builds are normal for internal builds).",
            "  - If a DLL is missing, re-download the full zip (do not copy only .exe).",
            "",
            f"Build: {build_type}  |  Packaged: {stamp}",
            "",
        ]),
        encoding="utf-8",
    )

    (stage / "使用说明.txt").write_text(
        "\n".join([
            "openbus 绿色免安装包（Windows 10/11 64 位）",
            "==========================================",
            "",
            "1. 解压整个文件夹到任意目录（不要只拷贝 openbus.exe）。",
            "2. 双击 openbus.exe 即可运行。",
            "3. platforms、*.dll、kerneldlls 等必须与 exe 同目录保留。",
            "",
            "说明：",
            "  - 无需安装 Qt / MSYS2。",
            "  - 若使用 USB-CAN 硬件，可能仍需安装厂家设备驱动。",
            "  - 若 SmartScreen 提示，选“更多信息”→“仍要运行”。",
            "",
            f"构建类型: {build_type}  |  打包日期: {stamp}",
            "",
        ]),
        encoding="utf-8",
    )

    if zip_path.exists():
        zip_path.unlink()

    info(f"Creating zip: {fmt_path(zip_path)}")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for root, dirs, files in os.walk(stage):
            for name in files:
                full = Path(root) / name
                arc = full.relative_to(dist_root)
                zf.write(full, arc.as_posix())

    size_mb = zip_path.stat().st_size / (1024 * 1024)
    ok(f"Portable package ready: {fmt_path(zip_path)} ({size_mb:.1f} MB)")
    ok(f"Unpacked folder:       {fmt_path(stage)}")
    info("Copy the zip (or the folder) to another PC and run openbus.exe")

    if getattr(args, "installer", False):
        compile_windows_installer(stage, version)


def find_makensis() -> Optional[Path]:
    env_p = os.environ.get("NSIS") or os.environ.get("MAKENSIS")
    candidates = []
    if env_p:
        p = Path(env_p)
        candidates.append(p if p.suffix.lower() == ".exe" else p / "makensis.exe")
    candidates.extend(
        [
            Path(r"D:\tools\nsis\nsis-3.0.4.1\makensis.exe"),
            Path(r"C:\Program Files (x86)\NSIS\makensis.exe"),
            Path(r"C:\Program Files\NSIS\makensis.exe"),
        ]
    )
    which = shutil.which("makensis")
    if which:
        candidates.append(Path(which))
    for c in candidates:
        if c and c.is_file():
            return c
    return None


def compile_windows_installer(stage: Path, version: str) -> None:
    """Build NSIS setup.exe with first-page language choice (EN/ZH/DE/…)."""
    header("Windows installer (NSIS)")
    nsi = PROJECT_ROOT / "installer" / "openbus.nsi"
    if not nsi.is_file():
        err(f"Missing installer script: {fmt_path(nsi)}")
        return
    exe = stage / "openbus.exe"
    if not exe.is_file():
        err(f"Staged tree has no openbus.exe: {fmt_path(stage)}")
        return
    makensis = find_makensis()
    if not makensis:
        warn("makensis.exe not found — skip setup.exe")
        warn("Install NSIS 3 (https://nsis.sourceforge.io/) or set NSIS=C:\\Program Files (x86)\\NSIS")
        warn("Then: python scripts/build.py package --version <ver> --installer")
        return

    dist_root = PROJECT_ROOT / "dist"
    dist_root.mkdir(parents=True, exist_ok=True)
    out_file = dist_root / f"openbus-{version}-windows-x64-setup.exe"
    src = str(stage.resolve()).replace("/", "\\")
    out = str(out_file.resolve()).replace("/", "\\")
    license_src = PROJECT_ROOT / "LICENSE"
    lic = str(license_src.resolve()).replace("/", "\\") if license_src.is_file() else ""
    cmd = [
        str(makensis),
        "/V2",
        f"/DVERSION={version}",
        f"/DSOURCE_DIR={src}",
        f"/DOUT_FILE={out}",
        str(nsi),
    ]
    if license_src.is_file():
        cmd.insert(-1, f"/DLICENSE_FILE={lic}")
    env = os.environ.copy()
    env["NSISDIR"] = str(makensis.parent)
    info(" ".join(cmd))
    r = subprocess.run(cmd, env=env)
    if r.returncode != 0:
        err("NSIS compile failed")
        sys.exit(1)
    size_mb = out_file.stat().st_size / (1024 * 1024)
    ok(f"Installer ready: {fmt_path(out_file)} ({size_mb:.1f} MB)")
    info("Setup starts with a language dialog (English / 简体中文 / Deutsch / …)")
    info("Choice is stored as ui.language in %APPDATA%\\openbus\\openbus\\settings.json")


def cmd_installer(env, args):
    """Compile setup.exe from an already-staged portable folder (no rebuild)."""
    stage = Path(args.stage).expanduser().resolve()
    version = args.version or "0.0.0"
    compile_windows_installer(stage, version)


def cmd_all(env, args):
    """Full pipeline: configure + build + deploy + run"""
    header("Full build pipeline")
    cmd_configure(env, args)
    cmd_build(env, args)
    cmd_deploy(env, args)
    cmd_run(env, args)


def cmd_status(env, args):
    """Show environment and build status"""
    header("Environment status")
    print(f"  Project root: {fmt_path(PROJECT_ROOT)}")
    print(f"  Build dir:       {fmt_path(BUILD_DIR)}  {'[exists]' if BUILD_DIR.exists() else '[missing]'}")
    print(f"  Executable:   {fmt_path(EXECUTABLE)}  {'[exists]' if EXECUTABLE.exists() else '[not built]'}")
    print()

    auto_fix = getattr(args, 'auto_fix', False)
    env.verify(auto_fix=auto_fix)
    env.print_accel_info()

    # Read build type
    cache = BUILD_DIR / "CMakeCache.txt"
    if cache.exists():
        for line in cache.read_text(encoding="utf-8", errors="ignore").splitlines():
            if line.startswith("CMAKE_BUILD_TYPE:STRING="):
                print(f"  Build type:    {line.split('=', 1)[1]}")
                break
    print()


def cmd_setup(env, args):
    """Install MSYS2 deps via pacman (scripts/setup_msys2.sh)"""
    header("Install MSYS2 deps (pacman)")
    script = PROJECT_ROOT / "scripts" / "setup_msys2.sh"
    if not script.exists():
        fail(f"Not found: {script}")
        sys.exit(1)

    bash_candidates = []
    for root in _msys2_roots():
        bash_candidates.append(root / "usr" / "bin" / "bash.exe")
    bash = next((b for b in bash_candidates if b.exists()), None)
    if bash is None:
        fail("MSYS2 bash not found (install under /c/msys64)")
        sys.exit(1)

    # Use login shell to load UCRT64 env
    env_name = (os.environ.get("SIN_MSYS2_ENV") or "ucrt64").strip().lower()
    msys_cwd = to_msys(PROJECT_ROOT)

    cmd = [
        str(bash),
        "-lc",
        f"export SIN_MSYS2_ENV={env_name}; cd '{msys_cwd}' && bash scripts/setup_msys2.sh",
    ]
    run_cmd(cmd)
    ok("MSYS2 deps installed; re-run status / configure")


def cmd_open(env, args):
    """Open build output dir in Explorer"""
    header("Open output directory")
    target = EXECUTABLE.parent if EXECUTABLE.parent.exists() else BUILD_DIR
    if target.exists():
        subprocess.run(["explorer", str(target)])
        ok(f"Opened: {fmt_path(target)}")
    else:
        fail(f"Directory missing: {fmt_path(target)}")


def cmake_needs_reconfigure():
    """Any CMakeLists.txt newer than build master => needs reconfigure.

    Unconditional reconfigure rewrites flags.make (mtime bump); Makefile gen
    then rebuilds everything by timestamp. Run once after CMake changes.
    """
    masters = [BUILD_DIR / "Makefile", BUILD_DIR / "build.ninja"]
    master = next((m for m in masters if m.exists()), None)
    if master is None:
        return True
    master_ts = master.stat().st_mtime
    for cm in PROJECT_ROOT.rglob("CMakeLists.txt"):
        # Skip build-tree copies (build/, build-dev/, ...)
        if any(p.lower().startswith("build") for p in cm.parts):
            continue
        if cm.stat().st_mtime > master_ts:
            return True
    return False


def cmd_test(env, args):
    """Run test suite (build tests aggregate + ctest; see doc)

    Build test targets only; suites are separate processes; dump qDebug on fail.
    """
    header("Run tests")

    if not (BUILD_DIR / "CMakeCache.txt").exists():
        info("Build dir not configured; running configure...")
        cmd_configure(env, args)

    jobs = str(args.jobs or os.cpu_count() or 8)

    # Makefile gen limit: new targets missing from old Makefile need
    # an explicit reconfigure (else No rule). Only when CMakeLists.txt changed
    # do incremental reconfigure (avoids full rebuild; harmless under Ninja).
    if cmake_needs_reconfigure():
        info("$ cmake incremental reconfigure (CMakeLists.txt changed) ...")
        reconf_rc = subprocess.run(
            [str(env.cmake), "-B", cmake_path(BUILD_DIR), "-S", cmake_path(PROJECT_ROOT)]
        ).returncode
        if reconf_rc != 0:
            fail(f"CMake reconfigure failed (exit code  {reconf_rc})")
            sys.exit(1)
    else:
        info("CMake config up to date; skip reconfigure")

    # Build test aggregate only (avoid relinking main app)
    info("$ build tests target ...")
    build_rc = subprocess.run(
        [str(env.cmake), "--build", cmake_path(BUILD_DIR), "--target", "tests", "-j", jobs]
    ).returncode
    if build_rc != 0:
        fail(f"Test target build failed (exit code  {build_rc})")
        sys.exit(1)

    # Run ctest (print stdout/stderr on failure)
    ctest = env.cmake.with_name("ctest.exe")
    if not ctest.exists():
        fail(f"ctest not found: {ctest}")
        sys.exit(1)
    info("$ ctest --output-on-failure ...")
    test_rc = subprocess.run(
        [str(ctest), "--test-dir", str(BUILD_DIR), "--output-on-failure"]
    ).returncode

    if test_rc == 0:
        ok("All test suites passed")
    else:
        fail(f"Failing test suite(s) (exit code  {test_rc})")
    sys.exit(test_rc)


# ============================================================
#  Argument parsing
# ============================================================


def main():
    parser = argparse.ArgumentParser(
        description="openbus build script",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Common commands:
  python scripts/build.py configure                      configure (Debug, auto Ninja)
  python scripts/build.py configure --build-type Release  configure (Release)
  python scripts/build.py configure --build-type Dev --build-dir build-dev
                                                          configure Dev profile (separate dir)
  python scripts/build.py build -j8                       incremental build (8 jobs)
  python scripts/build.py build --build-dir build-dev -j8 incremental build Dev profile
  python scripts/build.py run                             run
  python scripts/build.py debug                           GDB debug
  python scripts/build.py clean                           clean
  python scripts/build.py rebuild                         Rebuild
  python scripts/build.py deploy                          Deploy Qt deps
  python scripts/build.py all                             Full pipeline
  python scripts/build.py status                          Environment status
  python scripts/build.py setup                           pacman install MSYS2 deps
  python scripts/build.py open                            Open output directory

Multi-machine examples:
  # MSYS2 (UCRT64) only; bash-style paths
  bash scripts/setup_msys2.sh
  python scripts/build.py configure --clean
  python scripts/build.py build -j8
  python scripts/build.py deploy

  # Explicit prefix (short /ucrt64 or full /c/msys64/ucrt64)
  python scripts/build.py configure --qt-dir /ucrt64 --mingw-dir /ucrt64 --cmake-dir /ucrt64

Note: no native Windows CMake/Qt; displayed paths use /c/... form
        """,
    )

    # Global options (all subcommands; --build-dir may follow subcommand)
    parser.add_argument("--qt-dir", default=None,
                        help=f"MSYS2 Qt6 prefix (default: {fmt_path(DEFAULT_QT_DIR)})")
    parser.add_argument("--mingw-dir", default=None,
                        help=f"MSYS2 compiler prefix (default: {fmt_path(DEFAULT_MINGW_DIR)})")
    parser.add_argument("--cmake-dir", default=None,
                        help=f"MSYS2 CMake prefix (default: {fmt_path(DEFAULT_CMAKE_DIR)})")
    parser.add_argument("--build-dir", default="build",
                        help="Build dir (default: build; Dev: build-dev alongside full Debug)")

    def add_build_dir_opt(p):
        """Subcommand --build-dir: SUPPRESS default so it does not override global"""
        p.add_argument("--build-dir", default=argparse.SUPPRESS, help=argparse.SUPPRESS)

    sub = parser.add_subparsers(dest="command", help="Available commands")

    # configure
    p = sub.add_parser("configure", help="CMake configure")
    p.add_argument("--build-type", choices=BUILD_TYPES, default="Debug")
    p.add_argument("--clean", action="store_true",
                   help="Fully wipe build dir before configure (incl. CMakeCache.txt)")
    p.add_argument("-D", "--define", action="append", default=[], metavar="VAR=VALUE",
                   help="Extra CMake cache define (e.g. -DTRY_GOLD=ON); repeatable")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_configure)

    # build
    p = sub.add_parser("build", help="Incremental build")
    p.add_argument("-j", "--jobs", type=int, help="Parallel jobs (default: CPU count)")
    p.add_argument("--target", help="Build target name")
    p.add_argument("--build-type", choices=BUILD_TYPES, default="Debug", help="Build type when auto-configuring")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_build)

    # run
    p = sub.add_parser("run", help="Run application")
    p.add_argument("--args", default="", help="Arguments passed to the app")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_run)

    # debug
    p = sub.add_parser("debug", help="GDB debug")
    p.add_argument("--args", default="", help="Arguments passed to the app")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_debug)

    # clean
    p = sub.add_parser("clean", help="Clean build directory")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_clean)

    # rebuild
    p = sub.add_parser("rebuild", help="Rebuild (clean + configure + build)")
    p.add_argument("--build-type", choices=BUILD_TYPES, default="Debug")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_rebuild)

    # deploy
    p = sub.add_parser("deploy", help="Deploy Qt runtime deps")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_deploy)

    # package — portable zip for other PCs
    p = sub.add_parser(
        "package",
        help="Portable Windows x64 zip (Release build + deploy + archive)",
    )
    p.add_argument("-j", "--jobs", type=int, help="Parallel build jobs")
    p.add_argument("--build-type", choices=BUILD_TYPES, default="Release",
                   help="Build type for the package (default: Release)")
    p.add_argument("--version", default="",
                   help="Version stamp in folder/zip name (default: YYYYMMDD)")
    p.add_argument("--clean", action="store_true",
                   help="Wipe package build dir before configure")
    p.add_argument("--reconfigure", action="store_true",
                   help="Force CMake reconfigure even if cache matches")
    p.add_argument("--installer", action="store_true",
                   help="Also build NSIS setup.exe (language dialog: EN/ZH/DE/…)")
    p.add_argument("-D", "--define", action="append", default=[], metavar="VAR=VALUE",
                   help="Extra CMake cache define; repeatable")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_package, build_dir="build-release")

    p = sub.add_parser(
        "installer",
        help="Build NSIS setup.exe from an existing staged folder (no rebuild)",
    )
    p.add_argument("--stage", required=True, help="Folder that contains openbus.exe")
    p.add_argument("--version", default="1.10.4", help="Version baked into setup.exe name")
    p.set_defaults(func=cmd_installer)

    # all
    p = sub.add_parser("all", help="Full pipeline (configure + build + deploy + run)")
    p.add_argument("--build-type", choices=BUILD_TYPES, default="Debug")
    p.add_argument("--args", default="", help="Arguments passed to the app")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_all)

    # status
    p = sub.add_parser("status", help="Show environment status")
    p.add_argument("--auto-fix", action="store_true",
                   help="Show path candidates when tools are missing")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_status)

    # setup — pacman install deps
    p = sub.add_parser("setup", help="Install MSYS2 build deps via pacman")
    p.set_defaults(func=cmd_setup)

    # open
    p = sub.add_parser("open", help="Open output dir in Explorer")
    add_build_dir_opt(p)
    p.set_defaults(func=cmd_open)

    

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(0)

    # Apply --build-dir (global or after subcommand)
    set_build_dir(getattr(args, "build_dir", "build"))

    env = Environment(args)
    env.setup_path()
    args.func(env, args)


if __name__ == "__main__":
    main()

