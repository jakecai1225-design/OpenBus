#!/usr/bin/env python3
"""driver_tool.py — 驱动包打包/安装/卸载/校验工具（.odp）

.odp 格式 = ZIP；zip 根目录含 driver.json + driver_<id>.dll（+ 可选
icon.svg / assets/ / vendor/ 厂商运行时）。pack 自动生成 CHECKSUMS.sha256，
install 强制校验（与主程序 DriverRegistry 加载前校验同一份清单）。
输出统一为单行 JSON（供 AddDeviceTab QProcess 解析），也可命令行独立使用。

用法:
    python driver_tool.py pack <驱动目录> [-o 输出.odp]
    python driver_tool.py install <包.odp> <drivers目录>
    python driver_tool.py uninstall <驱动id> <drivers目录>
    python driver_tool.py validate <驱动目录或.odp>
"""

import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
import zipfile


NAME_RE = re.compile(r"^[a-z][a-z0-9-]*$")
CHECKSUMS_NAME = "CHECKSUMS.sha256"


def _emit(ok, **kwargs):
    """输出单行 JSON 结果（C++ 侧按此解析）"""
    kwargs["ok"] = bool(ok)
    print(json.dumps(kwargs, ensure_ascii=False))
    return 0 if ok else 1


def _load_manifest(dir_path):
    manifest_path = os.path.join(dir_path, "driver.json")
    if not os.path.isfile(manifest_path):
        return None, "缺少 driver.json"
    try:
        with open(manifest_path, encoding="utf-8-sig") as f:
            manifest = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        return None, f"driver.json 解析失败: {e}"
    return manifest, None


def _validate_manifest(manifest):
    """返回 (id, error)；校验清单基本字段与插件 DLL 存在性（后者由调用方查文件）"""
    if not isinstance(manifest, dict):
        return None, "driver.json 不是对象"
    driver_id = manifest.get("id", "")
    version = manifest.get("version", "")
    if not NAME_RE.match(driver_id or ""):
        return None, f"id 非法（需匹配 {NAME_RE.pattern}）: {driver_id!r}"
    if not version:
        return None, "缺少 version"
    if not isinstance(manifest.get("devices", []), list):
        return None, "devices 必须是数组"
    return driver_id, None


def _plugin_dll_name(driver_id):
    return f"driver_{driver_id}.dll"


def _sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def _write_checksums(dir_path, skip=(CHECKSUMS_NAME,)):
    """对目录内全部文件生成 CHECKSUMS.sha256（格式: '<hex>  <相对路径>'）"""
    lines = []
    for root, _dirs, files in os.walk(dir_path):
        for fn in sorted(files):
            if fn in skip:
                continue
            full = os.path.join(root, fn)
            rel = os.path.relpath(full, dir_path).replace("\\", "/")
            lines.append(f"{_sha256_file(full)}  {rel}")
    out = os.path.join(dir_path, CHECKSUMS_NAME)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return len(lines)


def _verify_checksums(dir_path):
    """校验 CHECKSUMS.sha256 中列出的全部文件；返回 (ok, error, checked)"""
    sums_path = os.path.join(dir_path, CHECKSUMS_NAME)
    if not os.path.isfile(sums_path):
        return False, "缺少 " + CHECKSUMS_NAME, 0
    checked = 0
    with open(sums_path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("  ", 1)
            if len(parts) != 2:
                return False, f"CHECKSUMS 行格式错误: {line!r}", checked
            expected, rel = parts[0].strip(), parts[1].strip()
            full = os.path.join(dir_path, rel)
            if not os.path.isfile(full):
                return False, f"校验失败: 缺少 {rel}", checked
            if _sha256_file(full) != expected.lower():
                return False, f"校验失败: {rel} (sha256 不匹配)", checked
            checked += 1
    return True, None, checked


def _iter_package_files(src_dir):
    """遍历包内文件（排除临时产物；CHECKSUMS 需入包，供 install/Registry 校验）"""
    for root, _dirs, files in os.walk(src_dir):
        for fn in files:
            full = os.path.join(root, fn)
            rel = os.path.relpath(full, src_dir)
            if fn.endswith((".pyc", ".pyo", ".user")):
                continue
            yield full, rel


def cmd_pack(args):
    if len(args) < 1:
        return _emit(False, error="用法: pack <驱动目录> [-o 输出.odp]")
    src_dir = args[0]
    out_path = None
    if "-o" in args:
        i = args.index("-o")
        if i + 1 < len(args):
            out_path = args[i + 1]

    if not os.path.isdir(src_dir):
        return _emit(False, error=f"目录不存在: {src_dir}")
    manifest, err = _load_manifest(src_dir)
    if err:
        return _emit(False, error=err)
    driver_id, err = _validate_manifest(manifest)
    if err:
        return _emit(False, error=err)
    dll_rel = _plugin_dll_name(driver_id)
    if not os.path.isfile(os.path.join(src_dir, dll_rel)):
        return _emit(False, error=f"缺少插件 {dll_rel}（驱动 DLL 需与 driver.json 同目录）")

    # 生成/刷新 CHECKSUMS（pack 总是重写，保证与当前文件一致）
    count = _write_checksums(src_dir)

    if not out_path:
        out_path = os.path.abspath(
            f"{driver_id}-driver_{manifest.get('version', '0.0.0')}_x64.odp")

    file_count = 0
    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for full, rel in _iter_package_files(src_dir):
            zf.write(full, rel)
            file_count += 1
    return _emit(True, id=driver_id, path=out_path, files=file_count, checksums=count)


def cmd_install(args):
    if len(args) < 2:
        return _emit(False, error="用法: install <包.odp> <drivers目录>")
    odp_path, drivers_dir = args[0], args[1]
    if not os.path.isfile(odp_path):
        return _emit(False, error=f"驱动包不存在: {odp_path}")

    # 清理孤儿临时目录（上次安装被占用/杀进程时残留；空目录或未完成解压）
    try:
        if os.path.isdir(drivers_dir):
            for name in os.listdir(drivers_dir):
                if name.startswith(".odp_"):
                    shutil.rmtree(os.path.join(drivers_dir, name), ignore_errors=True)
    except OSError:
        pass

    try:
        zf = zipfile.ZipFile(odp_path)
    except (OSError, zipfile.BadZipFile) as e:
        return _emit(False, error=f"无法读取包: {e}")

    with zf:
        names = zf.namelist()
        if "driver.json" not in names:
            return _emit(False, error="包内缺少 driver.json（需位于 zip 根目录）")
        for n in names:
            if n.startswith("/") or ".." in n.replace("\\", "/").split("/"):
                return _emit(False, error=f"非法条目路径: {n}")

        # 临时目录放在 drivers 目录内：同卷原子 move；前缀 "." 避免被扫描识别
        os.makedirs(drivers_dir, exist_ok=True)
        tmp_dir = tempfile.mkdtemp(prefix=".odp_", dir=drivers_dir)
        try:
            zf.extractall(tmp_dir)
            manifest, err = _load_manifest(tmp_dir)
            if err:
                return _emit(False, error=err)
            driver_id, err = _validate_manifest(manifest)
            if err:
                return _emit(False, error=err)
            dll_rel = _plugin_dll_name(driver_id)
            if not os.path.isfile(os.path.join(tmp_dir, dll_rel)):
                return _emit(False, error=f"包内缺少插件 {dll_rel}")
            if CHECKSUMS_NAME not in names:
                return _emit(False, error=f"包内缺少 {CHECKSUMS_NAME}")

            # 第一次校验（解压后）
            ok, err, checked = _verify_checksums(tmp_dir)
            if not ok:
                return _emit(False, error=err)

            target = os.path.join(drivers_dir, driver_id)
            if os.path.exists(target):
                return _emit(False, id=driver_id,
                             error=f"驱动已存在: {target}（请先卸载后重装）")
            shutil.move(tmp_dir, target)

            # 第二次校验（落盘后，防御移动过程损坏）
            ok, err, checked2 = _verify_checksums(target)
            if not ok:
                shutil.rmtree(target, ignore_errors=True)
                return _emit(False, error=f"安装后校验失败，已回滚: {err}")
        finally:
            if os.path.isdir(tmp_dir):
                shutil.rmtree(tmp_dir, ignore_errors=True)

    return _emit(True, id=driver_id, version=manifest.get("version", ""),
                 files=checked)


def cmd_uninstall(args):
    if len(args) < 2:
        return _emit(False, error="用法: uninstall <驱动id> <drivers目录>")
    driver_id, drivers_dir = args[0], args[1]
    if not NAME_RE.match(driver_id):
        return _emit(False, error=f"非法驱动 id: {driver_id!r}")
    target = os.path.join(drivers_dir, driver_id)
    if not os.path.isdir(target):
        return _emit(False, error=f"驱动目录不存在: {target}")

    # 先写卸载标记：若驱动 DLL 已被主进程加载（文件占用无法立即删除），
    # 主程序下次启动时（Registry::initialize 前）会清理带标记目录（方案 §7.4）
    try:
        with open(os.path.join(target, ".uninstall"), "w") as f:
            f.write("pending\n")
    except OSError:
        pass

    # 尽力删除（占用中的文件失败不影响返回，目录残留由启动清理）
    shutil.rmtree(target, ignore_errors=True)
    if os.path.isdir(target):
        return _emit(True, id=driver_id, pending=True,
                     note="部分文件被占用，将在下次启动时自动清理")
    return _emit(True, id=driver_id)


def cmd_validate(args):
    if len(args) < 1:
        return _emit(False, error="用法: validate <驱动目录或.odp>")
    path = args[0]
    if os.path.isdir(path):
        manifest, err = _load_manifest(path)
        if err:
            return _emit(False, error=err)
        driver_id, err = _validate_manifest(manifest)
        if err:
            return _emit(False, error=err)
        if not os.path.isfile(os.path.join(path, _plugin_dll_name(driver_id))):
            return _emit(False, error=f"缺少插件 {_plugin_dll_name(driver_id)}")
        return _emit(True, id=driver_id, version=manifest.get("version", ""))
    try:
        with zipfile.ZipFile(path) as zf:
            if "driver.json" not in zf.namelist():
                return _emit(False, error="包内缺少 driver.json")
            manifest = json.loads(zf.read("driver.json").decode("utf-8-sig"))
    except (OSError, zipfile.BadZipFile, json.JSONDecodeError, UnicodeDecodeError) as e:
        return _emit(False, error=f"读取失败: {e}")
    driver_id, err = _validate_manifest(manifest)
    if err:
        return _emit(False, error=err)
    return _emit(True, id=driver_id, version=manifest.get("version", ""))


def main():
    # 输出强制 UTF-8（C++ QProcess 按 UTF-8 解析 JSON；Windows 管道默认 GBK）
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except AttributeError:
        pass
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    cmd = sys.argv[1]
    handler = {"pack": cmd_pack, "install": cmd_install,
               "uninstall": cmd_uninstall, "validate": cmd_validate}.get(cmd)
    if not handler:
        return _emit(False, error=f"未知命令: {cmd}")
    return handler(sys.argv[2:])


if __name__ == "__main__":
    sys.exit(main())
