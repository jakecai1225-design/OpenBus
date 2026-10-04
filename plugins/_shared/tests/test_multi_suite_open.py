# -*- coding: utf-8 -*-
"""Regression: opening a second domain suite must not deactivate the first."""

from __future__ import annotations

import os
import re


_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", ".."))
_PM = os.path.join(_ROOT, "src", "core", "plugin", "pluginmanager.cpp")
_HOST = os.path.join(_ROOT, "scripts", "sin_host.py")
_UI = os.path.join(_ROOT, "sdk", "sin", "ui.py")


def test_no_exclusive_suite_deactivate():
    src = open(_PM, encoding="utf-8").read()
    # Old bug: activatePlugin deactivated every other usesSharedSuiteModules peer.
    assert "deactivatePlugin(other)" not in src
    assert "Keep only one loaded" not in src


def test_host_parks_suite_modules():
    src = open(_HOST, encoding="utf-8").read()
    assert "_park_active_suite_modules" in src
    assert "_park_suite_modules" in src
    assert "close_plugin_windows" in src
    # deactivate_plugin body must close only that plugin's windows.
    m = re.search(
        r"def deactivate_plugin\(params\):\n(.*?\n)def ", src, re.S)
    assert m, "deactivate_plugin not found"
    body = m.group(1)
    assert "close_plugin_windows" in body
    assert "close_all_windows()" not in body


def test_ui_per_plugin_windows():
    src = open(_UI, encoding="utf-8").read()
    assert "close_plugin_windows" in src
    assert "_sin_plugin" in src


if __name__ == "__main__":
    test_no_exclusive_suite_deactivate()
    test_host_parks_suite_modules()
    test_ui_per_plugin_windows()
    print("PASS multi_suite_open")
