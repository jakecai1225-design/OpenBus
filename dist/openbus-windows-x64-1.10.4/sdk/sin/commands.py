"""sin.commands — 命令操作 API

执行主程序注册的命令。
"""

from ._transport import send_notification


class _Commands:
    """命令操作 API"""

    def execute(self, command_id, *args):
        """执行一个已注册的命令

        Args:
            command_id: 命令 ID (str)
            *args: 命令参数（可选）
        """
        send_notification("executeCommand", {"id": command_id})


commands = _Commands()
