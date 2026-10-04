# -*- coding: utf-8 -*-
"""Registry: a2l-studio / xcp-studio on product surface."""

from __future__ import annotations

import os
import re


_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def test_suite_ids_and_allowlist():
    tool = open(os.path.join(_ROOT, "scripts", "plugin_tool.py"), encoding="utf-8").read()
    hdr = open(
        os.path.join(_ROOT, "src", "core", "plugin", "domainplugins.h"),
        encoding="utf-8").read()
    market = open(os.path.join(_ROOT, "scripts", "make_market.py"), encoding="utf-8").read()
    for sid in ("a2l-studio", "xcp-studio"):
        assert '"%s"' % sid in tool
        assert 'QStringLiteral("%s")' % sid in hdr
        assert '"id": "%s"' % sid in market


def test_plugin_json():
    for sid in ("a2l-studio", "xcp-studio"):
        path = os.path.join(_ROOT, "plugins", sid, "plugin.json")
        raw = open(path, encoding="utf-8").read()
        assert '"name": "%s"' % sid in raw
        assert "app_shell.py" not in raw
        assert os.path.isfile(os.path.join(_ROOT, "plugins", sid, "app_shell.py"))
        assert os.path.isfile(os.path.join(_ROOT, "plugins", sid, "main.py"))


def test_no_cjk_in_plugin_py():
    cjk = re.compile(r"[\u4e00-\u9fff]")
    for sid in ("a2l-studio", "xcp-studio"):
        root = os.path.join(_ROOT, "plugins", sid)
        for dirpath, _dirs, files in os.walk(root):
            for name in files:
                if not name.endswith(".py"):
                    continue
                path = os.path.join(dirpath, name)
                src = open(path, encoding="utf-8").read()
                m = cjk.search(src)
                assert not m, "%s contains CJK at %s" % (path, m.group(0))


if __name__ == "__main__":
    test_suite_ids_and_allowlist()
    test_plugin_json()
    test_no_cjk_in_plugin_py()
    print("PASS a2l_xcp_registry")
