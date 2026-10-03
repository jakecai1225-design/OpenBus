# -*- coding: utf-8 -*-
"""AI workbench shell — WebView2 embed (B) with browser fallback (A)."""

from __future__ import annotations

import traceback

from PyQt6.QtCore import QTimer, QUrl
from PyQt6.QtGui import QDesktopServices
from PyQt6.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QPlainTextEdit,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from _shared import activity_snapshot, plugin_shell, vscode_theme

from deps import check_all, check_webview2
from webview2_host import WebView2EmbedWidget, webview2_available


class ChatWindow(QMainWindow):
    """Top chrome + WebView2 (or Edge) embedded client. Browser = path A."""

    def __init__(self):
        super().__init__()
        self.setWindowTitle("AI Agent")
        self.setMinimumSize(1100, 720)
        self.resize(1180, 800)
        self._bridge = None
        self._sidecar_url = ""
        self._url = ""
        self._embed: WebView2EmbedWidget | None = None
        self._mode = "none"  # embed | browser | error

        vscode_theme.apply(self)
        plugin_shell.attach_status_bar(self, "Starting…")

        status = check_all(require_agents=False, require_webview2=False)
        if not status.ok:
            self._show_deps_error(status.message())
            return

        try:
            from bridge.server import get_bridge
            self._bridge = get_bridge()
            bridge_url = self._bridge.start()
            self._url = bridge_url
            # Prefer Node Mastra sidecar when built; UI still talks HTTP
            try:
                from node_sidecar import get_sidecar
                sc = get_sidecar()
                side = sc.start(tool_bridge_url=bridge_url)
                if side:
                    self._sidecar_url = side
                    self._url = side
            except Exception:
                pass
        except Exception as e:
            self._show_deps_error(
                "Bridge failed to start:\n%s\n\n%s" % (e, traceback.format_exc()))
            return

        central = QWidget()
        root = QVBoxLayout(central)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        chrome = QWidget()
        chrome.setObjectName("SuiteToolbar")
        row = QHBoxLayout(chrome)
        row.setContentsMargins(12, 6, 12, 6)
        row.setSpacing(8)
        title = QLabel("AI Agent")
        title.setObjectName("SuiteToolbarTitle")
        row.addWidget(title)
        row.addStretch(1)

        self._mode_label = QLabel("")
        self._mode_label.setStyleSheet("color: palette(mid);")
        row.addWidget(self._mode_label)

        self._btn_embed = QPushButton("Open in window")
        self._btn_embed.setObjectName("GhostButton")
        self._btn_embed.setFixedHeight(28)
        self._btn_embed.setToolTip(
            "Embed WebView2 (pywebview) or Edge app window (path B)")
        self._btn_embed.clicked.connect(self._open_embed)

        self._btn_browser = QPushButton("Open in browser")
        self._btn_browser.setObjectName("GhostButton")
        self._btn_browser.setFixedHeight(28)
        self._btn_browser.setToolTip("Open the same URL in the system browser (path A)")
        self._btn_browser.clicked.connect(self._open_browser)

        self._btn_reload = QPushButton("Reload")
        self._btn_reload.setObjectName("GhostButton")
        self._btn_reload.setFixedHeight(28)
        self._btn_reload.clicked.connect(self._reload)

        row.addWidget(self._btn_embed)
        row.addWidget(self._btn_browser)
        row.addWidget(self._btn_reload)
        root.addWidget(chrome)

        self._embed = WebView2EmbedWidget(self)
        root.addWidget(self._embed, 1)
        self.setCentralWidget(central)

        can_embed = webview2_available()
        self._btn_embed.setEnabled(can_embed)
        agent_tag = "Mastra" if self._sidecar_url else "Python"
        if not can_embed:
            wv = check_webview2()
            self._mode_label.setText("No WebView2/Edge — use browser · %s" % agent_tag)
            plugin_shell.set_status(
                self, "Path A only: %s" % (wv.detail or "no WebView2"), 0)
            QTimer.singleShot(300, self._open_browser)
        else:
            self._mode_label.setText(agent_tag)
            QTimer.singleShot(300, self._open_embed)

        activity_snapshot.update(active_plugin="ai-agent", active_page="chat")
        activity_snapshot.touch_from_env()
        plugin_shell.set_status(self, "Bridge %s" % self._url, 0)

    def _show_deps_error(self, message: str) -> None:
        self._mode = "error"
        central = QWidget()
        lay = QVBoxLayout(central)
        title = QLabel("AI Agent cannot start")
        title.setObjectName("SuiteSectionTitle")
        body = QPlainTextEdit()
        body.setReadOnly(True)
        body.setPlainText(message)
        lay.addWidget(title)
        lay.addWidget(body, 1)
        self.setCentralWidget(central)
        plugin_shell.set_status(self, "Dependencies missing", 0)
        try:
            import sin
            sin.output.append("AI Agent: " + message.split("\n")[0])
        except Exception:
            pass

    def _open_browser(self) -> None:
        if not self._url:
            return
        QDesktopServices.openUrl(QUrl(self._url))
        self._mode = "browser"
        self._mode_label.setText("Browser (A)")
        plugin_shell.set_status(self, "Opened browser %s" % self._url, 4000)

    def _open_embed(self) -> None:
        if not self._url or self._embed is None:
            return
        if not webview2_available():
            self._open_browser()
            return
        ok = self._embed.navigate(self._url)
        if not ok:
            self._mode_label.setText("Embed failed — browser")
            self._open_browser()
            return
        self._mode = "embed"
        eng = getattr(self._embed, "engine", "webview2")
        self._mode_label.setText("In-window %s (B)" % eng)
        plugin_shell.set_status(self, "Embedded at %s" % self._url, 4000)

    def _reload(self) -> None:
        if self._mode == "embed":
            self._open_embed()
        elif self._mode == "browser":
            self._open_browser()
        elif webview2_available():
            self._open_embed()
        else:
            self._open_browser()

    def showEvent(self, event):
        super().showEvent(event)
        activity_snapshot.update(active_plugin="ai-agent", active_page="chat")

    def closeEvent(self, event):
        super().closeEvent(event)
        if event.isAccepted() and not getattr(self, "_sin_suppress_close_notify", False):
            try:
                from _shared import plugin_shell
                plugin_shell.notify_plugin_closed("ai-agent")
            except Exception:
                pass

    def shutdown(self):
        self._sin_suppress_close_notify = True
        if self._embed is not None:
            try:
                self._embed.close_guest()
            except Exception:
                pass
        try:
            from node_sidecar import get_sidecar
            get_sidecar().stop()
        except Exception:
            pass
        if self._bridge is not None:
            try:
                self._bridge.stop()
            except Exception:
                pass
            self._bridge = None
