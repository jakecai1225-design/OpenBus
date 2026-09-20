#!/usr/bin/env python3
"""plugin_tool.py — pack / install / uninstall / validate .opk packages (G9).

.opk format = ZIP whose root contains plugin.json + main.py (+ optional assets).
Stdout is a single JSON object for PluginManager (QProcess); also usable from CLI.

Usage:
    python plugin_tool.py pack <plugin_dir> [-o out.opk]
    python plugin_tool.py pack-suites [-o out_dir]
    python plugin_tool.py install <package.opk> <plugins_dir>
    python plugin_tool.py uninstall <plugin_name> <plugins_dir>
    python plugin_tool.py validate <plugin_dir_or.opk>

Market packages are domain suites only. `pack` still builds any valid plugin
directory (custom plugins). `pack-suites` is the shipping command: it packs
the suite list below and nothing else.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import sys
import tempfile
import zipfile

NAME_RE = re.compile(r"^[a-z][a-z0-9-]*$")

# Domain suites shipped in the marketplace. Thin plugins were removed in S5.
SUITE_IDS = (
    "uds-suite",
    "dbc-studio",
    "canopen-suite",
    "j1939-suite",
    "obd-suite",
    "tx-lab",
    "bus-security",
    "protocol-hub",
    "log-analysis",
    "bus-utilities",
    "autosar-suite",
    "ethercat-suite",
)


def _repo_plugins_dir():
    return os.path.abspath(os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "plugins"))


def _ensure_shared(plugins_dir):
    """Copy plugins/_shared next to installed suites (required import root).

    Domain suites do `from _shared import …`; SIN_PLUGINS_DIR must contain
    the `_shared` package. Returns True if present after this call.
    """
    dest = os.path.join(plugins_dir, "_shared")
    src = os.path.join(_repo_plugins_dir(), "_shared")
    if not os.path.isdir(src):
        return os.path.isdir(dest)
    try:
        if os.path.isdir(dest):
            shutil.rmtree(dest)
        shutil.copytree(
            src,
            dest,
            ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "*.pyo"),
        )
        return True
    except OSError:
        return os.path.isdir(dest)


def _emit(ok, **kwargs):
    kwargs["ok"] = bool(ok)
    print(json.dumps(kwargs, ensure_ascii=False))
    return 0 if ok else 1


def _load_manifest(dir_path):
    manifest_path = os.path.join(dir_path, "plugin.json")
    if not os.path.isfile(manifest_path):
        return None, "missing plugin.json"
    try:
        with open(manifest_path, encoding="utf-8-sig") as f:
            manifest = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        return None, f"failed to parse plugin.json: {e}"
    return manifest, None


def _validate_manifest(manifest):
    if not isinstance(manifest, dict):
        return None, "plugin.json is not an object"
    name = manifest.get("name", "")
    version = manifest.get("version", "")
    main = manifest.get("main", "")
    if not NAME_RE.match(name or ""):
        return None, f"invalid name (must match {NAME_RE.pattern}): {name!r}"
    if not version:
        return None, "missing version"
    if not main:
        return None, "missing main"
    return name, None


def _is_pycache(path):
    parts = path.replace("\\", "/").split("/")
    return "__pycache__" in parts or path.endswith((".pyc", ".pyo"))


def _write_opk(src_dir, out_path=None):
    """Write one .opk. Returns (path, name, version, error)."""
    if not os.path.isdir(src_dir):
        return None, None, None, "directory not found: %s" % src_dir
    manifest, err = _load_manifest(src_dir)
    if err:
        return None, None, None, err
    name, err = _validate_manifest(manifest)
    if err:
        return None, None, None, err
    main_rel = manifest.get("main", "main.py")
    if not os.path.isfile(os.path.join(src_dir, main_rel)):
        return None, None, None, "main entry missing: %s" % main_rel
    version = manifest.get("version", "0.0.0")
    if not out_path:
        out_path = os.path.join(os.path.dirname(os.path.abspath(src_dir)),
                                "%s_%s.opk" % (name, version))
    try:
        with zipfile.ZipFile(out_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
            for root, dirs, files in os.walk(src_dir):
                dirs[:] = [d for d in dirs if d != "__pycache__"]
                for fn in files:
                    full = os.path.join(root, fn)
                    rel = os.path.relpath(full, src_dir).replace("\\", "/")
                    if _is_pycache(rel):
                        continue
                    zf.write(full, rel)
    except OSError as e:
        return None, None, None, "pack failed: %s" % e
    return os.path.abspath(out_path), name, version, None


def cmd_pack(args):
    if len(args) < 1:
        return _emit(False, error="usage: pack <plugin_dir> [-o out.opk]")
    src_dir = args[0]
    out_path = None
    if "-o" in args:
        i = args.index("-o")
        if i + 1 < len(args):
            out_path = args[i + 1]
    path, name, version, err = _write_opk(src_dir, out_path)
    if err:
        return _emit(False, error=err)
    return _emit(True, path=path, name=name, version=version)


def cmd_pack_suites(args):
    """Pack every domain suite into out_dir (default: <repo>/dist/opk)."""
    out_dir = None
    if "-o" in args:
        i = args.index("-o")
        if i + 1 < len(args):
            out_dir = args[i + 1]
    plugins_dir = os.path.abspath(os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "plugins"))
    if not out_dir:
        out_dir = os.path.abspath(os.path.join(
            os.path.dirname(os.path.abspath(__file__)), "..", "dist", "opk"))
    os.makedirs(out_dir, exist_ok=True)

    packages = []
    for sid in SUITE_IDS:
        src = os.path.join(plugins_dir, sid)
        manifest, err = _load_manifest(src)
        if err:
            return _emit(False, error="%s: %s" % (sid, err))
        version = manifest.get("version", "0.0.0")
        out_path = os.path.join(out_dir, "%s_%s.opk" % (sid, version))
        path, _name, _ver, err = _write_opk(src, out_path)
        if err:
            return _emit(False, error="%s: %s" % (sid, err))
        packages.append(path)
    return _emit(True, count=len(packages), packages=packages)


def cmd_install(args):
    if len(args) < 2:
        return _emit(False, error="usage: install <package.opk> <plugins_dir>")
    opk_path, plugins_dir = args[0], args[1]
    if not os.path.isfile(opk_path):
        return _emit(False, error=f"package not found: {opk_path}")
    if not zipfile.is_zipfile(opk_path):
        return _emit(False, error="not a valid .opk (zip) file")

    os.makedirs(plugins_dir, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="opk_install_")
    try:
        with zipfile.ZipFile(opk_path) as zf:
            zf.extractall(tmp)
        # Allow either flat root or single top-level folder
        manifest, err = _load_manifest(tmp)
        extract_root = tmp
        if err:
            entries = [e for e in os.listdir(tmp)
                       if os.path.isdir(os.path.join(tmp, e))]
            if len(entries) == 1:
                extract_root = os.path.join(tmp, entries[0])
                manifest, err = _load_manifest(extract_root)
        if err:
            return _emit(False, error=err)
        name, err = _validate_manifest(manifest)
        if err:
            return _emit(False, error=err)

        main_rel = manifest.get("main", "main.py")
        if not os.path.isfile(os.path.join(extract_root, main_rel)):
            return _emit(False, error=f"main entry missing in package: {main_rel}")

        dest = os.path.join(plugins_dir, name)
        if os.path.exists(dest):
            shutil.rmtree(dest)
        shutil.copytree(extract_root, dest)
        shared_ok = _ensure_shared(plugins_dir)
        return _emit(True, name=name, path=os.path.abspath(dest),
                     version=manifest.get("version", ""),
                     shared=shared_ok)
    except (OSError, zipfile.BadZipFile) as e:
        return _emit(False, error=f"install failed: {e}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def cmd_uninstall(args):
    if len(args) < 2:
        return _emit(False, error="usage: uninstall <plugin_name> <plugins_dir>")
    name, plugins_dir = args[0], args[1]
    if not NAME_RE.match(name or ""):
        return _emit(False, error=f"invalid plugin name: {name!r}")
    dest = os.path.join(plugins_dir, name)
    if not os.path.isdir(dest):
        return _emit(False, error=f"plugin not installed: {name}")
    try:
        shutil.rmtree(dest)
    except OSError as e:
        return _emit(False, error=f"uninstall failed: {e}")
    return _emit(True, name=name)


def cmd_validate(args):
    if len(args) < 1:
        return _emit(False, error="usage: validate <plugin_dir_or.opk>")
    path = args[0]
    tmp = None
    try:
        if os.path.isfile(path) and zipfile.is_zipfile(path):
            tmp = tempfile.mkdtemp(prefix="opk_validate_")
            with zipfile.ZipFile(path) as zf:
                zf.extractall(tmp)
            dir_path = tmp
            entries = [e for e in os.listdir(tmp)
                       if os.path.isdir(os.path.join(tmp, e))]
            if not os.path.isfile(os.path.join(tmp, "plugin.json")) and len(entries) == 1:
                dir_path = os.path.join(tmp, entries[0])
        elif os.path.isdir(path):
            dir_path = path
        else:
            return _emit(False, error=f"path not found: {path}")

        manifest, err = _load_manifest(dir_path)
        if err:
            return _emit(False, error=err)
        name, err = _validate_manifest(manifest)
        if err:
            return _emit(False, error=err)
        main_rel = manifest.get("main", "main.py")
        if not os.path.isfile(os.path.join(dir_path, main_rel)):
            return _emit(False, error=f"main entry missing: {main_rel}")
        return _emit(True, name=name, version=manifest.get("version", ""))
    except (OSError, zipfile.BadZipFile) as e:
        return _emit(False, error=f"validate failed: {e}")
    finally:
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)


def main():
    if len(sys.argv) < 2:
        return _emit(False, error="usage: plugin_tool.py <pack|pack-suites|install|uninstall|validate> ...")
    cmd = sys.argv[1]
    args = sys.argv[2:]
    if cmd == "pack":
        return cmd_pack(args)
    if cmd == "pack-suites":
        return cmd_pack_suites(args)
    if cmd == "install":
        return cmd_install(args)
    if cmd == "uninstall":
        return cmd_uninstall(args)
    if cmd == "validate":
        return cmd_validate(args)
    return _emit(False, error=f"unknown command: {cmd}")


if __name__ == "__main__":
    sys.exit(main())
