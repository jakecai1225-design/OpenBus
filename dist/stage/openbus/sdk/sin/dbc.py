"""sin.dbc — DBC 数据库会话 API（G9 工具插件）

在宿主侧独立打开/编辑/保存 DBC 文件（与当前工程已加载的 DBC 互不影响）。
会话通过 dbId 标识；插件停用（deactivate）前应调用 close 释放会话。

使用方式:
    import sin

    info = sin.dbc.open("example.dbc")     # {"dbId":1, "messageCount":..., ...}
    msgs = sin.dbc.messages(info["dbId"])
    sigs = sin.dbc.signals(info["dbId"], msgs[0]["id"])
    sin.dbc.update_signal(info["dbId"], msgs[0]["id"], "EngineRPM", factor=0.5)
    sin.dbc.save(info["dbId"])
    sin.dbc.close(info["dbId"])
"""

from ._transport import send_request


class _Dbc:
    """DBC 数据库会话 API"""

    def open(self, path, timeout=60.0):
        """打开 DBC 文件（宿主侧解析，大文件可能耗时，默认 60s 超时）

        Returns:
            dict: {"dbId", "messageCount", "nodeCount", "version", "fileName"}
        Raises:
            RuntimeError: 打开或解析失败
        """
        resp = send_request("dbc.open", {"path": path}, timeout=timeout)
        result = (resp or {}).get("result") or {}
        if "dbId" not in result:
            raise RuntimeError(result.get("error", "打开 DBC 失败"))
        return result

    def messages(self, db_id, timeout=30.0):
        """报文列表

        Returns:
            list[dict]: [{"id","name","dlc","sender","comment","cycleTime","signalCount"}]
            失败返回 None
        """
        resp = send_request("dbc.messages", {"dbId": db_id}, timeout=timeout)
        result = (resp or {}).get("result") or {}
        return result.get("messages")

    def signals(self, db_id, message_id, timeout=30.0):
        """指定报文的信号列表

        Args:
            message_id: 报文 CAN ID (int)

        Returns:
            list[dict]: [{"name","startBit","bitLength","littleEndian","isSigned",
                          "factor","offset","min","max","unit","receiver","comment",
                          "muxType","muxValue","valueTable":[[value,desc],...]}]
            失败返回 None
        """
        resp = send_request("dbc.signals", {
            "dbId": db_id, "messageId": message_id
        }, timeout=timeout)
        result = (resp or {}).get("result") or {}
        return result.get("signals")

    def update_signal(self, db_id, message_id, name, timeout=10.0, **fields):
        """修改信号属性

        可选字段: startBit, bitLength, littleEndian, isSigned,
                  factor, offset, min, max, unit, comment
        Returns:
            (bool, str): (是否成功, 错误信息)
        """
        resp = send_request("dbc.updateSignal", {
            "dbId": db_id, "messageId": message_id,
            "name": name, "fields": fields
        }, timeout=timeout)
        result = (resp or {}).get("result") or {}
        return result.get("ok", False), result.get("error", "")

    def update_message(self, db_id, message_id, timeout=10.0, **fields):
        """修改报文属性（可选字段: name, dlc, comment, cycleTime）"""
        resp = send_request("dbc.updateMessage", {
            "dbId": db_id, "messageId": message_id, "fields": fields
        }, timeout=timeout)
        result = (resp or {}).get("result") or {}
        return result.get("ok", False), result.get("error", "")

    def save(self, db_id, path=None, timeout=30.0):
        """保存到文件（path 缺省写回原路径）

        Returns:
            (bool, str): (是否成功, 错误信息)
        """
        params = {"dbId": db_id}
        if path:
            params["path"] = path
        resp = send_request("dbc.save", params, timeout=timeout)
        result = (resp or {}).get("result") or {}
        return result.get("ok", False), result.get("error", "")

    def close(self, db_id, timeout=10.0):
        """关闭会话并释放宿主侧资源

        Returns:
            bool: 是否成功
        """
        resp = send_request("dbc.close", {"dbId": db_id}, timeout=timeout)
        result = (resp or {}).get("result") or {}
        return result.get("ok", False)


dbc = _Dbc()
