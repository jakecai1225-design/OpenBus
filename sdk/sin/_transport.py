"""sin SDK 内部传输层 — JSON-RPC over stdin/stdout

所有 API 模块共用此模块与主程序通信。
- send_notification(): 发送通知（无需回复）
- send_request(): 发送请求并同步等待回复
- deliver_response(): 由宿主调用，将响应分发给等待中的请求
"""

import sys
import json
import threading
import queue

# 请求/响应同步队列: request_id → queue.Queue
_pending_requests = {}
_next_request_id = 1
_lock = threading.Lock()

# stdout 写锁
_stdout_lock = threading.Lock()


def _get_next_id():
    global _next_request_id
    with _lock:
        rid = _next_request_id
        _next_request_id += 1
        return rid


def _send_message(msg):
    """发送一条 JSON-RPC 消息到 stdout"""
    data = json.dumps(msg, ensure_ascii=False) + "\n"
    with _stdout_lock:
        sys.stdout.write(data)
        sys.stdout.flush()


def send_notification(method, params=None):
    """发送通知（无需回复）"""
    _send_message({"jsonrpc": "2.0", "method": method, "params": params or {}})


def send_request(method, params=None, timeout=5.0):
    """发送请求并同步等待回复（阻塞，带超时）

    Returns:
        dict: {"result": ..., "error": ...} 或 None（超时）
    """
    rid = _get_next_id()
    q = queue.Queue()
    with _lock:
        _pending_requests[rid] = q

    _send_message({"jsonrpc": "2.0", "method": method, "params": params or {}, "id": rid})

    try:
        return q.get(timeout=timeout)
    except queue.Empty:
        with _lock:
            _pending_requests.pop(rid, None)
        return None


def deliver_response(msg_id, result, error=None):
    """由宿主主循环调用：将响应分发给等待中的请求"""
    with _lock:
        q = _pending_requests.pop(msg_id, None)

    if q is not None:
        q.put({"result": result, "error": error})
