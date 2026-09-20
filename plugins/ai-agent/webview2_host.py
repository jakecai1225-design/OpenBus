# -*- coding: utf-8 -*-
"""Windows WebView2-style host: Edge --app embedded into a Qt native child HWND.

Uses system Edge (no Qt WebEngine, no bundled Chromium). If Edge is missing,
callers fall back to opening the system browser (path A).
"""

from __future__ import annotations

import os
import subprocess
import time
from pathlib import Path
from typing import Optional

from PyQt6.QtCore import QTimer, Qt
from PyQt6.QtWidgets import QWidget

user32 = None
user32 = None
if os.name == "nt":
    import ctypes
    from ctypes import wintypes

    user32 = ctypes.windll.user32
    kernel32 = ctypes.windll.kernel32

    SetParent = user32.SetParent
    SetParent.argtypes = [wintypes.HWND, wintypes.HWND]
    SetParent.restype = wintypes.HWND

    MoveWindow = user32.MoveWindow
    MoveWindow.argtypes = [
        wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
        wintypes.BOOL]
    MoveWindow.restype = wintypes.BOOL

    ShowWindow = user32.ShowWindow
    ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
    ShowWindow.restype = wintypes.BOOL

    IsWindow = user32.IsWindow
    IsWindow.argtypes = [wintypes.HWND]
    IsWindow.restype = wintypes.BOOL

    EnumWindows = user32.EnumWindows
    GetWindowThreadProcessId = user32.GetWindowThreadProcessId
    GetWindowTextW = user32.GetWindowTextW
    GetWindowTextLengthW = user32.GetWindowTextLengthW
    GetClassNameW = user32.GetClassNameW

    GWL_STYLE = -16
    WS_CHILD = 0x40000000
    WS_VISIBLE = 0x10000000
    WS_POPUP = 0x80000000
    SW_SHOW = 5

    GetWindowLong = user32.GetWindowLongW
    SetWindowLong = user32.SetWindowLongW


def find_msedge() -> Optional[str]:
    candidates = [
        os.path.expandvars(r"%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe"),
        os.path.expandvars(r"%ProgramFiles%\Microsoft\Edge\Application\msedge.exe"),
        os.path.expandvars(r"%LocalAppData%\Microsoft\Edge\Application\msedge.exe"),
    ]
    for path in candidates:
        if path and os.path.isfile(path):
            return path
    return None


def webview2_available() -> bool:
    """Edge present implies WebView2/Chromium stack usable for --app embed."""
    return find_msedge() is not None


class EdgeAppEmbedWidget(QWidget):
    """QWidget that hosts an Edge --app window as a child HWND."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAttribute(Qt.WidgetAttribute.WA_NativeWindow, True)
        self.setMinimumSize(400, 300)
        self._url = ""
        self._proc: Optional[subprocess.Popen] = None
        self._child_hwnd = 0
        self._poll = QTimer(self)
        self._poll.timeout.connect(self._try_attach)
        self._profile = Path(
            os.environ.get("OPENBUS_AI_HOME")
            or Path.home() / ".openbus" / "ai-agent"
        ) / "edge-profile"
        self._profile.mkdir(parents=True, exist_ok=True)

    def navigate(self, url: str) -> bool:
        self.close_guest()
        self._url = url
        edge = find_msedge()
        if not edge or os.name != "nt":
            return False
        # Force native handle before launch
        self.winId()
        args = [
            edge,
            "--app=%s" % url,
            "--user-data-dir=%s" % str(self._profile),
            "--disable-features=TranslateUI",
            "--no-first-run",
            "--new-window",
        ]
        try:
            self._proc = subprocess.Popen(
                args,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        except OSError:
            return False
        self._poll.start(200)
        QTimer.singleShot(8000, self._poll.stop)
        return True

    def _try_attach(self) -> None:
        if self._child_hwnd and IsWindow(self._child_hwnd):
            self._layout_child()
            self._poll.stop()
            return
        if self._proc is None or self._proc.poll() is not None:
            self._poll.stop()
            return
        hwnd = self._find_edge_hwnd(self._proc.pid)
        if not hwnd:
            return
        parent = int(self.winId())
        # Reparent into Qt widget
        style = GetWindowLong(hwnd, GWL_STYLE)
        style = (style | WS_CHILD | WS_VISIBLE) & ~WS_POPUP
        SetWindowLong(hwnd, GWL_STYLE, style)
        SetParent(hwnd, parent)
        ShowWindow(hwnd, SW_SHOW)
        self._child_hwnd = hwnd
        self._layout_child()
        self._poll.stop()

    def _find_edge_hwnd(self, pid: int) -> int:
        result = {"hwnd": 0}

        @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        def enum_proc(hwnd, _lparam):
            proc_id = wintypes.DWORD()
            GetWindowThreadProcessId(hwnd, ctypes.byref(proc_id))
            if proc_id.value != pid:
                return True
            if not user32.IsWindowVisible(hwnd):
                return True
            length = GetWindowTextLengthW(hwnd)
            # Prefer top-level chrome windows with a title
            buf = ctypes.create_unicode_buffer(max(length, 1) + 1)
            GetWindowTextW(hwnd, buf, len(buf))
            cls = ctypes.create_unicode_buffer(256)
            GetClassNameW(hwnd, cls, 256)
            # Edge app windows typically Chrome_WidgetWin_1
            if "Chrome_WidgetWin" in cls.value or buf.value:
                result["hwnd"] = hwnd
                return False
            return True

        EnumWindows(enum_proc, 0)
        return int(result["hwnd"])

    def _layout_child(self) -> None:
        if not self._child_hwnd or not IsWindow(self._child_hwnd):
            return
        MoveWindow(
            self._child_hwnd, 0, 0, max(1, self.width()), max(1, self.height()), True)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._layout_child()

    def close_guest(self) -> None:
        self._poll.stop()
        if self._proc is not None and self._proc.poll() is None:
            try:
                self._proc.terminate()
            except Exception:
                pass
            try:
                self._proc.wait(timeout=2)
            except Exception:
                try:
                    self._proc.kill()
                except Exception:
                    pass
        self._proc = None
        self._child_hwnd = 0

    def closeEvent(self, event):
        self.close_guest()
        super().closeEvent(event)
