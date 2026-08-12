"""sin.signals — DBC 信号解码 API

使用已加载的 DBC 文件解码/编码 CAN 信号。
"""

from ._transport import send_request


class _Signals:
    """DBC 信号解码 API"""

    def decode(self, can_id, data):
        """解码 CAN 帧中的信号

        Args:
            can_id: CAN ID (int)
            data: 帧数据 (bytes 或 hex 字符串)

        Returns:
            dict: 信号名 → 值 的映射，无匹配 DBC 时返回空字典
        """
        if isinstance(data, bytes):
            hex_data = data.hex()
        else:
            hex_data = str(data)

        resp = send_request("signals.decode", {
            "id": can_id,
            "data": hex_data
        })
        if resp and resp.get("result"):
            return resp["result"].get("signals", {})
        return {}

    def encode(self, can_id, signal_values):
        """将信号值编码为 CAN 帧数据

        Args:
            can_id: CAN ID (int)
            signal_values: 信号名 → 值 的映射 (dict)

        Returns:
            bytes: 编码后的帧数据，编码失败返回 None
        """
        resp = send_request("signals.encode", {
            "id": can_id,
            "signals": signal_values
        })
        if resp and resp.get("result"):
            hex_str = resp["result"].get("data", "")
            return bytes.fromhex(hex_str) if hex_str else None
        return None


signals = _Signals()
