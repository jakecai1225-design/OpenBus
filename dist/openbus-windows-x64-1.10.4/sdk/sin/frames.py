"""sin.frames — CAN 帧操作 API

获取 Trace 中选中的帧、获取最近帧、发送帧。
"""

from ._transport import send_notification, send_request


class Frame:
    """CAN / CAN FD 帧

    属性:
        id: CAN ID (int)
        extended: 是否扩展帧 (bool)
        fd: 是否 CAN FD 帧 (bool)
        dlc: 数据长度码 (int, 0-15)
        data: 帧数据 (bytes, 0-64 字节)
        timestamp: 相对时间戳 (float, 秒)
        channel: 通道号 (int, 1-based)
        direction: 方向 ("Rx" 或 "Tx")
    """

    def __init__(self, data):
        self.id = data.get("id", 0)
        self.extended = data.get("extended", False)
        self.fd = data.get("fd", False)
        self.dlc = data.get("dlc", 0)
        hex_str = data.get("data", "")
        self.data = bytes.fromhex(hex_str) if hex_str else b""
        self.timestamp = data.get("timestamp", 0.0)
        self.channel = data.get("channel", 1)
        self.direction = data.get("direction", "Rx")

    def __repr__(self):
        return (f"Frame(id=0x{self.id:X}, dlc={self.dlc}, "
                f"data={self.data.hex()}, ts={self.timestamp:.3f})")


class _Frames:
    """CAN 帧操作 API"""

    def get_selected(self):
        """获取 Trace 中当前选中的帧列表

        Returns:
            list[Frame]: 选中的帧列表（可能为空）
        """
        resp = send_request("frames.getSelected")
        if resp and resp.get("result"):
            frames_data = resp["result"].get("frames", [])
            return [Frame(f) for f in frames_data]
        return []

    def get_recent(self, count=100):
        """获取最近的 N 帧

        Args:
            count: 要获取的帧数 (int, 默认 100)

        Returns:
            list[Frame]: 最近的帧列表
        """
        resp = send_request("frames.getRecent", {"count": count})
        if resp and resp.get("result"):
            frames_data = resp["result"].get("frames", [])
            return [Frame(f) for f in frames_data]
        return []

    def send(self, id, data, extended=False, fd=False):
        """发送一帧 CAN 报文

        Args:
            id: CAN ID (int)
            data: 帧数据 (bytes 或 hex 字符串)
            extended: 是否扩展帧 (bool)
            fd: 是否 CAN FD 帧 (bool)
        """
        if isinstance(data, bytes):
            hex_data = data.hex()
        else:
            hex_data = str(data)

        send_notification("sendFrame", {
            "id": id,
            "data": hex_data,
            "extended": extended,
            "fd": fd
        })


frames = _Frames()
