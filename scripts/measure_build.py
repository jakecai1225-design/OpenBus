#!/usr/bin/env python3
"""构建性能度量脚本 — 对应 doc/拆分应用实施方案.md §3.4 / §5

对指定构建目录执行场景化增量构建并解析 .ninja_log，输出各步骤耗时表，
结果追加留档到 doc/构建基线.md，保证优化前后可比。

场景:
  ui     touch 一个 ui 源文件 → 增量构建   (日常改 UI 的迭代耗时)
  core   touch 一个 core 源文件 → 增量构建 (日常改 core 的迭代耗时)
  clean  ninja clean → 全量构建            (全量构建耗时)
  analyze 仅解析现有 .ninja_log 不构建     (历史日志分析/基线提取)

用法:
  python scripts/measure_build.py --build-dir build-dev --scenario all
  python scripts/measure_build.py --build-dir build     --scenario analyze
"""

import argparse
import os
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
from build import (DEFAULT_CMAKE_DIR, DEFAULT_MINGW_DIR, DEFAULT_QT_DIR,  # noqa: E402
                   TOOLS_DIR, kill_running_executable)

BASELINE_DOC = PROJECT_ROOT / "doc" / "构建基线.md"


def setup_path():
    """与 build.py 保持一致：MinGW bin 必须在 PATH 前，否则 cc1plus 静默崩溃"""
    prepend = [
        str(Path(DEFAULT_CMAKE_DIR) / "bin"),
        str(Path(DEFAULT_MINGW_DIR) / "bin"),
        str(Path(DEFAULT_QT_DIR) / "bin"),
    ]
    ninja = TOOLS_DIR / "ninja" / "ninja.exe"
    if ninja.exists():
        prepend.insert(0, str(ninja.parent))
    os.environ["PATH"] = os.pathsep.join(prepend) + os.pathsep + os.environ.get("PATH", "")

UI_TOUCH_FILE = PROJECT_ROOT / "src" / "ui" / "markettab.cpp"
CORE_TOUCH_FILE = PROJECT_ROOT / "src" / "core" / "appconfig.cpp"

# 关键步骤（始终单独列出，便于横向对比）
KEY_STEPS = [
    "openbus.exe",
    "libopenbus_ui.a",      # 壳 UI 静态库（thin archive）
    "libqcustomplot.a",
    "openbus_data.dll",     # B1：公共底座 DLL（原 openbus_core 静态库）
    "openbus_market.dll",   # B1：首个业务模块 DLL
]


def find_cmake():
    candidates = [
        Path(DEFAULT_CMAKE_DIR) / "bin" / "cmake.exe",
        Path("C:/Program Files/CMake/bin/cmake.exe"),
    ]
    for c in candidates:
        if c.exists():
            return c
    import shutil
    found = shutil.which("cmake")
    return Path(found) if found else None


def parse_ninja_log(path: Path):
    """解析 .ninja_log，返回 [（start_ms, end_ms, output）] 列表"""
    entries = []
    if not path.exists():
        return entries
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        # ninja log v5: start end mtime output command_hash（输出路径可能含空格，末列为哈希）
        if len(parts) < 5:
            continue
        try:
            start, end = int(parts[0]), int(parts[1])
        except ValueError:
            continue
        entries.append((start, end, " ".join(parts[3:-1])))
    return entries


def fmt(ms):
    if ms >= 1000:
        return f"{ms / 1000:.1f}s"
    return f"{ms}ms"


def run_build(build_dir: Path, jobs: int, target=None):
    cmake = find_cmake()
    if cmake is None:
        print("[FAIL] 未找到 cmake.exe")
        sys.exit(1)
    cmd = [str(cmake), "--build", str(build_dir)]
    if target:
        cmd += ["--target", target]
    cmd += ["--", f"-j{jobs}"]
    t0 = time.perf_counter()
    result = subprocess.run(cmd, cwd=str(PROJECT_ROOT))
    wall = time.perf_counter() - t0
    return result.returncode, wall


def snapshot_outputs(log: Path):
    """记录 output → end_time 映射。

    ninja 每次构建后重写 .ninja_log，时间基准为该次进程启动（跨会话不可比），
    因此用「输出新增 / end 变化」判定本次会话重建的步骤。
    """
    snap = {}
    for _, end, output in parse_ninja_log(log):
        snap[output] = end  # 同一输出取最后一条
    return snap


def diff_new_entries(log: Path, snap):
    """构建后与快照对比，返回本次重建的步骤列表"""
    latest = {}
    for start, end, output in parse_ninja_log(log):
        latest[output] = (start, end, output)
    return [e for o, e in latest.items() if o not in snap or e[1] != snap[o]]


def measure_incremental(build_dir: Path, jobs: int, touch_file: Path, label: str):
    """touch 指定源文件后增量构建，返回（新步骤列表, 构建墙钟秒数）"""
    kill_running_executable()
    log = build_dir / ".ninja_log"
    snap = snapshot_outputs(log)

    touch_file.touch()

    code, wall = run_build(build_dir, jobs)
    if code != 0:
        print(f"[FAIL] {label} 场景构建失败 (退出码 {code})")
        sys.exit(code)

    return diff_new_entries(log, snap), wall


def step_table(entries, limit=12):
    """按耗时降序的步骤表"""
    rows = sorted(entries, key=lambda e: e[1] - e[0], reverse=True)
    lines = []
    for start, end, output in rows[:limit]:
        lines.append(f"| {fmt(end - start):>10s} | {output} |")
    if len(rows) > limit:
        lines.append(f"| ... 共 {len(rows)} 个步骤 | |")
    return lines


def key_step_lines(entries):
    """关键步骤耗时（存在才输出）"""
    lines = []
    for key in KEY_STEPS:
        for start, end, output in entries:
            if output.endswith(key):
                lines.append(f"| {fmt(end - start):>10s} | {key} |")
                break
    return lines


def read_build_type(build_dir: Path):
    cache = build_dir / "CMakeCache.txt"
    if cache.exists():
        for line in cache.read_text(encoding="utf-8", errors="ignore").splitlines():
            if line.startswith("CMAKE_BUILD_TYPE:STRING="):
                return line.split("=", 1)[1]
    return "?"


def append_baseline(build_dir: Path, title: str, sections):
    """追加度量结果到 doc/构建基线.md"""
    build_type = read_build_type(build_dir)
    stamp = datetime.now().strftime("%Y-%m-%d %H:%M")
    out = [f"\n## {stamp} — {title} (build type: {build_type}, dir: {build_dir.name})\n"]
    for name, new, wall in sections:
        span = 0
        if new:
            span = max(e[1] for e in new) - min(e[0] for e in new)
        out.append(f"### {name} — 墙钟 {wall:.1f}s / 步骤跨度 {span / 1000:.1f}s / {len(new)} 个步骤\n")
        out.append("| 耗时 | 步骤 |")
        out.append("| --- | --- |")
        out.extend(key_step_lines(new))
        out.extend(step_table(new))
        out.append("")
    BASELINE_DOC.parent.mkdir(parents=True, exist_ok=True)
    with open(BASELINE_DOC, "a", encoding="utf-8") as f:
        f.write("\n".join(out))
    print(f"\n[OK] 结果已追加到 {BASELINE_DOC}")


def main():
    parser = argparse.ArgumentParser(description="openbus 构建性能度量")
    parser.add_argument("--build-dir", default="build", help="构建目录 (默认: build)")
    parser.add_argument("--scenario", default="all",
                        choices=["ui", "core", "clean", "all", "analyze"],
                        help="度量场景 (默认: all = ui + core + clean)")
    parser.add_argument("-j", "--jobs", type=int, default=8, help="并行任务数 (默认: 8)")
    args = parser.parse_args()

    setup_path()

    bd = Path(args.build_dir)
    build_dir = bd if bd.is_absolute() else PROJECT_ROOT / bd
    if not build_dir.exists():
        print(f"[FAIL] 构建目录不存在: {build_dir}，请先 configure")
        sys.exit(1)

    log = build_dir / ".ninja_log"

    if args.scenario == "analyze":
        entries = parse_ninja_log(log)
        print(f".ninja_log 共 {len(entries)} 条记录（含历史会话），按耗时排序 Top 25:\n")
        print("| 耗时 | 步骤 |")
        print("| --- | --- |")
        for line in step_table(entries, limit=25):
            print(line)
        return

    sections = []

    if args.scenario in ("ui", "all"):
        print(f"\n=== 场景 ui: touch {UI_TOUCH_FILE.name} ===")
        new, wall = measure_incremental(build_dir, args.jobs, UI_TOUCH_FILE, "ui")
        sections.append(("场景 ui（改 1 个 ui 文件 → 可运行）", new, wall))

    if args.scenario in ("core", "all"):
        print(f"\n=== 场景 core: touch {CORE_TOUCH_FILE.name} ===")
        new, wall = measure_incremental(build_dir, args.jobs, CORE_TOUCH_FILE, "core")
        sections.append(("场景 core（改 1 个 core 文件 → 可运行）", new, wall))

    if args.scenario in ("clean", "all"):
        print("\n=== 场景 clean: 全量构建 ===")
        kill_running_executable()
        snap = snapshot_outputs(log)
        code, _ = run_build(build_dir, args.jobs, target="clean")
        if code != 0:
            print(f"[FAIL] clean 失败 (退出码 {code})")
            sys.exit(code)
        code, wall = run_build(build_dir, args.jobs)
        if code != 0:
            print(f"[FAIL] clean 后全量构建失败 (退出码 {code})")
            sys.exit(code)
        new = diff_new_entries(log, snap)
        sections.append(("场景 clean（全量构建）", new, wall))

    append_baseline(build_dir, f"度量 {args.scenario}", sections)

    # 控制台摘要
    print("\n========== 摘要 ==========")
    for name, new, wall in sections:
        print(f"  {name}: 墙钟 {wall:.1f}s, {len(new)} 步")


if __name__ == "__main__":
    main()
