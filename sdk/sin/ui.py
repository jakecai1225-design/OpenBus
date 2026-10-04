"""sin.ui — plugin UI API (requires PyQt6).

Create independent windows rendered in the plugin host process with PyQt6.
Windows are tagged with the owning plugin so multi-open suites do not steal
each other's close/deactivate notifications.
"""

from PyQt6.QtWidgets import QMainWindow, QApplication, QMessageBox
from PyQt6.QtCore import pyqtSignal

from ._transport import send_notification

# Tracked windows created via create_window()
_windows = []

# Plugin currently being activated (set by sin_host before activate())
_current_plugin = None


def set_current_plugin(name):
    """Set the plugin id being activated (host only)."""
    global _current_plugin
    _current_plugin = name


class _PluginWindow(QMainWindow):
    """QMainWindow that emits closed when the user accepts the close event."""

    closed = pyqtSignal()

    def closeEvent(self, event):
        super().closeEvent(event)
        if event.isAccepted():
            self.closed.emit()


def _windows_for_plugin(plugin_name):
    return [
        w for w in _windows
        if getattr(w, "_sin_plugin", None) == plugin_name
    ]


def _on_window_closed(win):
    """Remove window; deactivate owning plugin only when it has no windows left."""
    if getattr(win, "_sin_suppress_close_notify", False):
        if win in _windows:
            _windows.remove(win)
        return
    plugin = getattr(win, "_sin_plugin", None)
    if win in _windows:
        _windows.remove(win)
    if not plugin:
        return
    if _windows_for_plugin(plugin):
        return
    send_notification("pluginWindowClosed", {"plugin": plugin})


class _UI:
    """Plugin UI API."""

    def create_window(self, title=""):
        """Create an independent window owned by the current plugin."""
        app = QApplication.instance()
        if app is None:
            raise RuntimeError("QApplication is not initialized")

        win = _PluginWindow()
        win.setWindowTitle(title)
        win._sin_plugin = _current_plugin  # type: ignore[attr-defined]
        _windows.append(win)
        win.closed.connect(lambda w=win: _on_window_closed(w))
        return win

    def show_message(self, title, text):
        QMessageBox.information(None, title, text)

    def show_warning(self, title, text):
        QMessageBox.warning(None, title, text)

    def show_error(self, title, text):
        QMessageBox.critical(None, title, text)


def close_plugin_windows(plugin_name):
    """Close windows owned by one plugin (other plugins stay open)."""
    if not plugin_name:
        return
    for win in list(_windows_for_plugin(plugin_name)):
        try:
            # Suppress host notify — deactivate path already owns lifecycle.
            win._sin_suppress_close_notify = True  # type: ignore[attr-defined]
            win.close()
        except Exception:
            pass
        if win in _windows:
            _windows.remove(win)


def close_all_windows():
    """Close every plugin window (host shutdown)."""
    for win in list(_windows):
        try:
            win.close()
        except Exception:
            pass
    _windows.clear()


ui = _UI()
