# -*- coding: utf-8 -*-
"""WebView2 host for the AI workbench.

Primary path: Microsoft Edge WebView2 via pywebview (gui=edgechromium) with
HWND reparent into a Qt native child. Fallback: Edge --app embed (legacy).
Browser open remains path A in ChatWindow.
"""

from __future__ import annotations

import os
import subprocess
import threading
import time
from pathlib import Path
from typing import Optional

from PyQt6.QtCore import QTimer, Qt
from PyQt6.QtWidgets import QVBoxLayout, QWidget

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
    SW_HIDE = 0

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


def find_webview2_runtime() -> bool:
    """Evergreen WebView2 runtime or Edge (bundled runtime)."""
    if find_msedge():
        return True
    roots = [
        os.path.expandvars(r"%ProgramFiles(x86)%\Microsoft\EdgeWebView\Application"),
        os.path.expandvars(r"%ProgramFiles%\Microsoft\EdgeWebView\Application"),
    ]
    for root in roots:
        if root and os.path.isdir(root):
            return True
    return False


def pywebview_available() -> bool:
    try:
        import webview  # noqa: F401
        return os.name == "nt"
    except ImportError:
        return False


def webview2_available() -> bool:
    """True when in-window WebView2 or Edge --app embed can work."""
    return find_webview2_runtime() or pywebview_available()


class EdgeAppEmbedWidget(QWidget):
    """Legacy: host an Edge --app window as a child HWND."""

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
        self.engine = "edge-app"

    def navigate(self, url: str) -> bool:
        self.close_guest()
        self._url = url
        edge = find_msedge()
        if not edge or os.name != "nt":
            return False
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
            buf = ctypes.create_unicode_buffer(max(length, 1) + 1)
            GetWindowTextW(hwnd, buf, len(buf))
            cls = ctypes.create_unicode_buffer(256)
            GetClassNameW(hwnd, cls, 256)
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


class PyWebViewEmbedWidget(QWidget):
    """True WebView2 via pywebview (edgechromium) reparented into Qt."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAttribute(Qt.WidgetAttribute.WA_NativeWindow, True)
        self.setMinimumSize(400, 300)
        self._url = ""
        self._window = None
        self._child_hwnd = 0
        self._thread: Optional[threading.Thread] = None
        self._poll = QTimer(self)
        self._poll.timeout.connect(self._try_attach)
        self.engine = "webview2"

    def navigate(self, url: str) -> bool:
        if not pywebview_available() or os.name != "nt":
            return False
        self.close_guest()
        self._url = url
        self.winId()

        import webview

        ready = threading.Event()

        def _run():
            self._window = webview.create_window(
                "openbus AI Agent",
                url,
                width=max(800, self.width()),
                height=max(600, self.height()),
                frameless=False,
                easy_drag=False,
            )

            def _on_shown():
                ready.set()

            try:
                self._window.events.shown += _on_shown
            except Exception:
                pass
            # gui=edgechromium => WebView2
            webview.start(gui="edgechromium", debug=False)

        self._thread = threading.Thread(
            target=_run, name="ai-agent-webview2", daemon=True)
        self._thread.start()
        self._poll.start(250)
        QTimer.singleShot(12000, self._poll.stop)
        return True

    def _try_attach(self) -> None:
        if self._child_hwnd and IsWindow(self._child_hwnd):
            self._layout_child()
            self._poll.stop()
            return
        hwnd = self._find_webview_hwnd()
        if not hwnd:
            return
        parent = int(self.winId())
        style = GetWindowLong(hwnd, GWL_STYLE)
        style = (style | WS_CHILD | WS_VISIBLE) & ~WS_POPUP
        SetWindowLong(hwnd, GWL_STYLE, style)
        SetParent(hwnd, parent)
        ShowWindow(hwnd, SW_SHOW)
        self._child_hwnd = hwnd
        self._layout_child()
        self._poll.stop()

    def _find_webview_hwnd(self) -> int:
        """Find a top-level window titled openbus AI Agent / WebView2 class."""
        result = {"hwnd": 0}

        @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        def enum_proc(hwnd, _lparam):
            if not user32.IsWindowVisible(hwnd):
                return True
            length = GetWindowTextLengthW(hwnd)
            buf = ctypes.create_unicode_buffer(max(length, 1) + 1)
            GetWindowTextW(hwnd, buf, len(buf))
            cls = ctypes.create_unicode_buffer(256)
            GetClassNameW(hwnd, cls, 256)
            title = buf.value or ""
            cname = cls.value or ""
            if "openbus AI Agent" in title or "WebView2" in cname:
                result["hwnd"] = hwnd
                return False
            if "Chrome_WidgetWin" in cname and "AI" in title:
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
        if self._window is not None:
            try:
                self._window.destroy()
            except Exception:
                pass
        self._window = None
        self._child_hwnd = 0

    def closeEvent(self, event):
        self.close_guest()
        super().closeEvent(event)


class WebView2EmbedWidget(QWidget):
    """Facade: prefer pywebview WebView2, else Edge --app."""

    def __init__(self, parent=None):
        super().__init__(parent)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        self._inner: Optional[QWidget] = None
        self.engine = "none"

    def navigate(self, url: str) -> bool:
        self.close_guest()
        if pywebview_available():
            w = PyWebViewEmbedWidget(self)
            self.layout().addWidget(w)
            if w.navigate(url):
                self._inner = w
                self.engine = "webview2"
                return True
            w.setParent(None)
            w.deleteLater()
        w2 = EdgeAppEmbedWidget(self)
        self.layout().addWidget(w2)
        ok = w2.navigate(url)
        if ok:
            self._inner = w2
            self.engine = "edge-app"
        return ok

    def close_guest(self) -> None:
        if self._inner is not None:
            try:
                self._inner.close_guest()
            except Exception:
                pass
            try:
                self._inner.setParent(None)
                self._inner.deleteLater()
            except Exception:
                pass
        self._inner = None
        self.engine = "none"

    def closeEvent(self, event):
        self.close_guest()
        super().closeEvent(event)
