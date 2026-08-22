#!/usr/bin/env python3
"""plugin_tool.py — 插件包打包/安装/卸载/校验工具（G9 .opk）

.opk 格式 = ZIP；zip 根目录含 plugin.json + main.py（+ 可选资源）。
输出统一为单行 JSON（供 PluginManager QProcess 解析），也可命令行独立使用。

用法:
    python plugin_tool.py pack <插件目录> [-o 输出.opk]
    python plugin_tool.py install <包.opk> <plugins目录>
    python plugin_tool.py uninstall <插件名> <plugins目录>
    python plugin_tool.py validate <插件目录或.opk>
"""

import json
import os
import re
import shutil
import sys
import tempfile
import zipfile


NAME_RE = re.compile(r"^[a-z][a-z0-9-]*$")


def _emit(ok, **kwargs):
    """输出单行 JSON 结果（C++ 侧按此解析）"""
    kwargs["ok"] = bool(ok)
    print(json.dumps(kwargs, ensure_ascii=False))
    return 0 if ok else 1


def _load_manifest(dir_path):
    manifest_path = os.path.join(dir_path, "plugin.json")
    if not os.path.isfile(manifest_path):
        return None, "缺少 plugin.json"
    try:
        with open(manifest_path, encoding="utf-8-sig") as f:
            manifest = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        return None, f"plugin.json 解析失败: {e}"
    return manifest, None


def _validate_manifest(manifest):
    """返回 (name, error)"""
    if not isinstance(manifest, dict):
        return None, "plugin.json 不是对象"
    name = manifest.get("name", "")
    version = manifest.get("version", "")
    main = manifest.get("main", "")
    if not NAME_RE.match(name or ""):
        return None, f"name 非法（需匹配 {NAME_RE.pattern}）: {name!r}"
    if not version:
        return None, "缺少 version"
    if not main:
        return None, "缺少 main"
    return name, None


def _is_pycache(path):
    parts = path.replace("\\", "/").split("/")
    return "__pycache__" in parts or path.endswith((".pyc", ".pyo"))


def cmd_pack(args):
    if len(args) < 1:
        return _emit(False, error="用法: pack <插件目录> [-o 输出.opk]")
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
    name, err = _validate_manifest(manifest)
    if err:
        return _emit(False, error=err)
    main_path = os.path.join(src_dir, manifest["main"])
    if not os.path.isfile(main_path):
        return _emit(False, error=f"入口文件不存在: {manifest['main']}")

    if not out_path:
        out_path = os.path.abspath(f"{name}-{manifest.get('version', '0.0.0')}.opk")

    file_count = 0
    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for root, _dirs, files in os.walk(src_dir):
            for fn in files:
                full = os.path.join(root, fn)
                rel = os.path.relpath(full, src_dir)
                if _is_pycache(rel):
                    continue
                zf.write(full, rel)
                file_count += 1
    return _emit(True, name=name, path=out_path, files=file_count)


def cmd_install(args):
    if len(args) < 2:
        return _emit(False, error="用法: install <包.opk> <plugins目录>")
    opk_path, plugins_dir = args[0], args[1]
    if not os.path.isfile(opk_path):
        return _emit(False, error=f"插件包不存在: {opk_path}")
    try:
        zf = zipfile.ZipFile(opk_path)
    except (OSError, zipfile.BadZipFile) as e:
        return _emit(False, error=f"无法读取包: {e}")

    with zf:
        names = zf.namelist()
        # 支持 manifest 在 zip 根（标准）
        if "plugin.json" not in names:
            return _emit(False, error="包内缺少 plugin.json（需位于 zip 根目录）")
        # 防路径穿越
        for n in names:
            if n.startswith("/") or ".." in n.replace("\\", "/").split("/"):
                return _emit(False, error=f"非法条目路径: {n}")

        # 临时目录放在 plugins 目录内：受限环境下系统 %TEMP% 可能不可写，
        # 且同卷 move 为原子重命名。前缀 "." 使其不会被 discoverPlugins 误识别。
        os.makedirs(plugins_dir, exist_ok=True)
        tmp_dir = tempfile.mkdtemp(prefix=".opk_", dir=plugins_dir)
        try:
            zf.extractall(tmp_dir)
            manifest, err = _load_manifest(tmp_dir)
            if err:
                return _emit(False, error=err)
            name, err = _validate_manifest(manifest)
            if err:
                return _emit(False, error=err)
            if not os.path.isfile(os.path.join(tmp_dir, manifest["main"])):
                return _emit(False, error=f"包内入口文件不存在: {manifest['main']}")

            target = os.path.join(plugins_dir, name)
            if os.path.exists(target):
                return _emit(False, name=name,
                             error=f"插件已存在: {target}（v1 不支持覆盖安装，请先卸载）")
            os.makedirs(plugins_dir, exist_ok=True)
            shutil.move(tmp_dir, target)
        finally:
            if os.path.isdir(tmp_dir):
                shutil.rmtree(tmp_dir, ignore_errors=True)

    return _emit(True, name=name)


def cmd_uninstall(args):
    if len(args) < 2:
        return _emit(False, error="用法: uninstall <插件名> <plugins目录>")
    name, plugins_dir = args[0], args[1]
    if not NAME_RE.match(name):
        return _emit(False, error=f"非法插件名: {name!r}")
    target = os.path.join(plugins_dir, name)
    if not os.path.isdir(target):
        return _emit(False, error=f"插件目录不存在: {target}")
    shutil.rmtree(target)
    return _emit(True, name=name)


def cmd_validate(args):
    if len(args) < 1:
        return _emit(False, error="用法: validate <插件目录或.opk>")
    path = args[0]
    if os.path.isdir(path):
        manifest, err = _load_manifest(path)
        if err:
            return _emit(False, error=err)
    else:
        try:
            with zipfile.ZipFile(path) as zf:
                if "plugin.json" not in zf.namelist():
                    return _emit(False, error="包内缺少 plugin.json")
                manifest = json.loads(zf.read("plugin.json").decode("utf-8-sig"))
        except (OSError, zipfile.BadZipFile, json.JSONDecodeError, UnicodeDecodeError) as e:
            return _emit(False, error=f"读取失败: {e}")
    name, err = _validate_manifest(manifest)
    if err:
        return _emit(False, error=err)
    return _emit(True, name=name, version=manifest.get("version", ""))


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
