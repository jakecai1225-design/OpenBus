"""sin.workspace — 工程上下文 API

获取当前工程目录、DBC 文件列表、应用设置等。
"""

from ._transport import send_request


class _Workspace:
    """工程上下文 API"""

    def get_project_dir(self):
        """获取当前工程目录路径

        Returns:
            str: 工程目录路径，未打开工程时返回空字符串
        """
        resp = send_request("workspace.getProjectDir")
        if resp and resp.get("result") is not None:
            return resp["result"].get("path", "")
        return ""

    def get_dbc_files(self):
        """获取已加载的 DBC 文件列表

        Returns:
            list[str]: DBC 文件路径列表
        """
        resp = send_request("workspace.getDbcFiles")
        if resp and resp.get("result"):
            return resp["result"].get("files", [])
        return []

    def get_setting(self, key, default=None):
        """读取应用设置

        Args:
            key: 设置键名 (str)
            default: 默认值（键不存在时返回）

        Returns:
            设置值，类型取决于具体设置项
        """
        resp = send_request("workspace.getSetting", {"key": key, "default": default})
        if resp and resp.get("result") is not None:
            return resp["result"].get("value", default)
        return default


workspace = _Workspace()
