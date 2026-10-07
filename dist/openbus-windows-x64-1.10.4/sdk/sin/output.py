"""sin.output — 输出面板 API

将文本输出到主程序底部面板的「插件输出」标签页。
"""

from ._transport import send_notification


class _Output:
    """输出面板 API"""

    def append(self, text):
        """追加文本到输出面板

        Args:
            text: 要显示的文本（会自动转换为字符串）
        """
        send_notification("output.append", {"text": str(text)})

    def clear(self):
        """清空输出面板"""
        send_notification("output.clear")


output = _Output()
