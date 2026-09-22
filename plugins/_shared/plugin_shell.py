"""Plugin UI chrome: empty state, status bar, CSV export, raise helpers, shortcuts."""

from __future__ import annotations

import csv
from typing import Callable, Optional

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QKeySequence, QShortcut
from PyQt6.QtWidgets import (
    QFileDialog,
    QLabel,
    QMainWindow,
    QStatusBar,
    QVBoxLayout,
    QWidget,
)


def ensure_central(win: QMainWindow) -> QWidget:
    """Ensure the window has a central widget; return it."""
    central = win.centralWidget()
    if central is None:
        central = QWidget()
        win.setCentralWidget(central)
    return central


def attach_status_bar(win: QMainWindow, initial: str = "Ready") -> QStatusBar:
    """Attach (or reuse) a status bar and set initial text."""
    bar = win.statusBar()
    if bar is None:
        bar = QStatusBar(win)
        win.setStatusBar(bar)
    bar.showMessage(initial)
    return bar


def set_status(win: QMainWindow, text: str, timeout_ms: int = 0) -> None:
    bar = win.statusBar()
    if bar is not None:
        bar.showMessage(text, timeout_ms)


def empty_state_label(text: str, parent: Optional[QWidget] = None) -> QLabel:
    """Centered hint shown when there is nothing to display."""
    lbl = QLabel(text, parent)
    lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
    lbl.setWordWrap(True)
    lbl.setStyleSheet("color:#78909c;padding:24px;font-size:13px;")
    return lbl


def bind_raise(win: QMainWindow) -> Callable[[], None]:
    """Return a callable that shows/raises the window (for register_command)."""

    def _raise() -> None:
        win.show()
        win.raise_()
        win.activateWindow()

    return _raise


def bind_shortcut(
    win: QMainWindow,
    key: str,
    slot: Callable[[], None],
) -> QShortcut:
    """Bind a keyboard shortcut on the window."""
    sc = QShortcut(QKeySequence(key), win)
    sc.activated.connect(slot)
    return sc


def export_csv(
    parent: QWidget,
    headers: list[str],
    rows: list[list],
    default_name: str = "export.csv",
) -> Optional[str]:
    """Prompt for a CSV path and write rows. Returns path or None if cancelled."""
    path, _ = QFileDialog.getSaveFileName(
        parent, "Export CSV", default_name, "CSV (*.csv)"
    )
    if not path:
        return None
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(headers)
        for row in rows:
            w.writerow(row)
    return path


def notify_plugin_closed(plugin_id: str) -> None:
    """Tell the host to deactivate this plugin (close window = exit plugin)."""
    if not plugin_id:
        return
    try:
        from sin._transport import send_notification
        send_notification("pluginWindowClosed", {"plugin": plugin_id})
    except Exception:
        pass


def wire_close_deactivates(window: QMainWindow, plugin_id: str) -> None:
    """On accepted close, notify the host so the plugin is unloaded.

    Domain suites use a plain QMainWindow (not sin.ui.create_window), so
    without this hook the host keeps the old plugin loaded and the next
    suite can reuse cached app_shell/pages modules (wrong UI).
    """
    if not plugin_id or getattr(window, "_sin_close_wired", None) == plugin_id:
        return
    window._sin_close_wired = plugin_id
    prev = window.closeEvent

    def _close_event(event, _prev=prev, _pid=plugin_id, _win=window):
        _prev(event)
        if not event.isAccepted():
            return
        if getattr(_win, "_sin_suppress_close_notify", False):
            return
        notify_plugin_closed(_pid)

    window.closeEvent = _close_event  # type: ignore[method-assign]


def help_label(text: str) -> QLabel:
    """Small muted help / tip strip under toolbars."""
    lbl = QLabel(text)
    lbl.setWordWrap(True)
    lbl.setStyleSheet("color:#607d8b;font-size:11px;padding:2px 4px;")
    return lbl


def column_layout(parent: Optional[QWidget] = None) -> tuple[QWidget, QVBoxLayout]:
    w = QWidget(parent)
    lay = QVBoxLayout(w)
    lay.setContentsMargins(8, 8, 8, 8)
    return w, lay
