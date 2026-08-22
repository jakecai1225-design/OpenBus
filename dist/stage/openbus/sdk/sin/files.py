"""sin.files — 报文日志格式转换 API（G9 工具插件）

通过宿主 C++ 引擎（CanFileIO）在 BLF/ASC/CSV/PCAP/TRC 之间转换，
转换在宿主后台线程执行，进度与结果通过回调异步回报。

使用方式:
    import sin

    def on_finished(ok, frame_count, error):
        sin.output.append(f"转换{'成功' if ok else '失败'}: {frame_count} 帧 {error}")

    sin.files.convert("a.blf", "a.asc", "asc", on_finished=on_finished)
"""

import threading

from ._transport import send_request

# jobId → {"on_progress": fn(percent), "on_finished": fn(ok, frameCount, error)}
_callbacks = {}
_lock = threading.Lock()


class _Files:
    """报文日志格式转换 API"""

    def convert(self, source, target, fmt, on_progress=None, on_finished=None):
        """启动异步格式转换

        Args:
            source: 源文件路径 (str)
            target: 目标文件路径 (str)
            fmt: 目标格式 "blf"|"asc"|"csv"|"pcap"|"trc" (str)
            on_progress: 可选进度回调 fn(percent: int)
            on_finished: 完成回调 fn(ok: bool, frameCount: int, error: str)

        Returns:
            int: jobId（启动失败返回 None，失败信息走 on_finished）
        """
        resp = send_request("files.convertStart", {
            "source": source, "target": target, "format": fmt
        }, timeout=10.0)
        result = (resp or {}).get("result") or {}
        job_id = result.get("jobId")
        if job_id is None:
            if on_finished:
                on_finished(False, 0, result.get("error", "启动转换失败"))
            return None
        with _lock:
            _callbacks[job_id] = {"on_progress": on_progress, "on_finished": on_finished}
        return job_id

    def cancel(self, job_id):
        """取消进行中的转换任务（结果仍会回调一次 on_finished）"""
        send_request("files.convertCancel", {"jobId": job_id})

    # ---- 宿主通知分发（由 sin_host 调用，插件代码无需关心）----

    def handle_notification(self, method, params):
        job_id = params.get("jobId")
        with _lock:
            cb = _callbacks.get(job_id)
        if cb is None:
            return
        if method == "files.convertProgress":
            if cb.get("on_progress"):
                try:
                    cb["on_progress"](params.get("percent", 0))
                except Exception:
                    pass
        elif method == "files.convertFinished":
            with _lock:
                _callbacks.pop(job_id, None)
            if cb.get("on_finished"):
                try:
                    cb["on_finished"](params.get("ok", False),
                                      params.get("frameCount", 0),
                                      params.get("error", ""))
                except Exception:
                    pass


files = _Files()
