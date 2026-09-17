"""sin SDK transport — JSON-RPC 2.0 over ZMQ DEALER (scheme B).

The plugin host owns the DEALER socket and installs send/recv hooks via
configure(). Unit tests may leave the default no-op sender.
"""

import json
import threading
import queue

_pending_requests = {}
_next_request_id = 1
_lock = threading.Lock()

_send_impl = None  # callable(dict) set by sin_host


def configure(send_callable):
    """Install the low-level sender used by send_notification / send_request."""
    global _send_impl
    _send_impl = send_callable


def _get_next_id():
    global _next_request_id
    with _lock:
        rid = _next_request_id
        _next_request_id += 1
        return rid


def _send_message(msg):
    if _send_impl is None:
        raise RuntimeError("sin transport not configured (host did not call configure)")
    _send_impl(msg)


def send_notification(method, params=None):
    _send_message({"jsonrpc": "2.0", "method": method, "params": params or {}})


def send_request(method, params=None, timeout=5.0):
    """Send a request and block until the matching response arrives."""
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
    """Called by the host recv loop when a JSON-RPC response arrives."""
    with _lock:
        q = _pending_requests.pop(msg_id, None)
    if q is not None:
        q.put({"result": result, "error": error})
