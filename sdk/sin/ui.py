"""sin.ui — 插件 UI API（需要 PyQt6）

创建独立窗口，在插件进程中用 PyQt6 渲染。
PyQt6 与主程序的 C++ Qt6 共享同一底层库，渲染风格一致。

窗口关闭时自动通知主程序停用插件（关闭窗口 = 退出插件）。

使用方式:
    import sin
    from PyQt6.QtWidgets import QLabel, QVBoxLayout, QPushButton

    def activate(context):
        win = sin.ui.create_window("我的工具")
        win.resize(400, 300)
        layout = QVBoxLayout(win.centralWidget())
        layout.addWidget(QLabel("Hello!"))
        btn = QPushButton("发送帧")
        btn.clicked.connect(lambda: sin.frames.send(0x123, b'\\x01\\x02'))
        layout.addWidget(btn)
        win.show()
"""

from PyQt6.QtWidgets import QMainWindow, QApplication, QMessageBox
from PyQt6.QtCore import pyqtSignal

from ._transport import send_notification

# 全局窗口跟踪列表
_windows = []

# 当前正在激活的插件名（由 sin_host 在 activate 前设置）
_current_plugin = None


def set_current_plugin(name):
    """设置当前正在激活的插件名（供宿主调用）"""
    global _current_plugin
    _current_plugin = name


class _PluginWindow(QMainWindow):
    """QMainWindow 子类 — 关闭时发出 closed 信号

    对插件代码完全透明，用法与 QMainWindow 一致。
    """
    closed = pyqtSignal()

    def closeEvent(self, event):
        super().closeEvent(event)
        if event.isAccepted():
            self.closed.emit()


def _on_window_closed(win):
    """窗口关闭回调：从列表移除，若已无窗口则通知宿主停用"""
    if win in _windows:
        _windows.remove(win)
    if not _windows and _current_plugin:
        send_notification("pluginWindowClosed", {"plugin": _current_plugin})


class _UI:
    """插件 UI API"""

    def create_window(self, title=""):
        """创建一个独立窗口

        Args:
            title: 窗口标题 (str)

        Returns:
            QMainWindow: 可添加任意 widget 的主窗口，调用 .show() 显示
        """
        app = QApplication.instance()
        if app is None:
            raise RuntimeError("QApplication 未初始化，UI 功能不可用")

        win = _PluginWindow()
        win.setWindowTitle(title)
        _windows.append(win)

        win.closed.connect(lambda w=win: _on_window_closed(w))
        return win

    def show_message(self, title, text):
        """显示信息对话框

        Args:
            title: 对话框标题 (str)
            text: 消息文本 (str)
        """
        QMessageBox.information(None, title, text)

    def show_warning(self, title, text):
        """显示警告对话框"""
        QMessageBox.warning(None, title, text)

    def show_error(self, title, text):
        """显示错误对话框"""
        QMessageBox.critical(None, title, text)


def close_all_windows():
    """关闭所有插件创建的窗口（宿主关闭时调用）"""
    for win in list(_windows):
        try:
            win.close()
        except Exception:
            pass
    _windows.clear()


ui = _UI()
