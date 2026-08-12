"""sin.ui — 插件 UI API（需要 PyQt6）

创建独立窗口，在插件进程中用 PyQt6 渲染。
PyQt6 与主程序的 C++ Qt6 共享同一底层库，渲染风格一致。

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

# 全局窗口跟踪列表
_windows = []


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

        win = QMainWindow()
        win.setWindowTitle(title)
        _windows.append(win)
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
