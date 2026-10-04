#!/usr/bin/env python3
"""sin plugin host — JSON-RPC control over ZMQ DEALER + binary frames over ZMQ SUB.

Environment:
  SIN_ZMQ_CTRL      tcp://127.0.0.1:<port>  (ROUTER endpoint on main process)
  SIN_ZMQ_DATA      tcp://127.0.0.1:<port>  (PUB endpoint on main process)
  SIN_SDK_DIR       path to sdk/ (parent of package `sin`)
  SIN_PLUGINS_DIR   plugins root (informational)
"""

from __future__ import annotations

import importlib.util
import json
import os
import queue
import sys
import threading
import traceback

# ---- paths before importing sin ------------------------------------------------

_sdk_dir = os.environ.get("SIN_SDK_DIR", "")
if _sdk_dir and _sdk_dir not in sys.path:
    sys.path.insert(0, _sdk_dir)

_host_dir = os.path.dirname(os.path.abspath(__file__))
if _host_dir not in sys.path:
    sys.path.insert(0, _host_dir)

# Plugins root so `from _shared import …` works for all tool plugins.
_plugins_root = os.environ.get("SIN_PLUGINS_DIR", "")
if _plugins_root and _plugins_root not in sys.path:
    sys.path.insert(0, _plugins_root)

from sin._transport import (  # noqa: E402
    configure as transport_configure,
    deliver_response,
    send_notification,
)
from sin._databus import DataBus  # noqa: E402

try:
    from PyQt6.QtWidgets import QApplication as _QApplication
    _has_pyqt = True
except ImportError:
    _has_pyqt = False

# ---- ZMQ control socket --------------------------------------------------------

_ctrl_sock = None
_ctrl_lock = threading.Lock()
_message_queue: queue.Queue = queue.Queue()
_SENTINEL = object()


def _send_ctrl(msg: dict) -> None:
    data = json.dumps(msg, ensure_ascii=False).encode("utf-8")
    with _ctrl_lock:
        sock = _ctrl_sock
        if sock is None:
            return
        try:
            sock.send(data)
        except Exception:
            pass


def send_response(msg_id, result):
    _send_ctrl({"jsonrpc": "2.0", "result": result, "id": msg_id})


def send_error(msg_id, code, message):
    _send_ctrl({"jsonrpc": "2.0", "error": {"code": code, "message": message}, "id": msg_id})


def log_error(message):
    send_notification("log", {"level": 1, "message": str(message)})


def log_info(message):
    send_notification("log", {"level": 0, "message": str(message)})


# ---- plugin state --------------------------------------------------------------

_plugins = {}


class PluginContext:
    def __init__(self, plugin_name):
        self.plugin_name = plugin_name
        self._frame_handlers = []
        self._commands = {}

    def on_frame(self, handler):
        if not self._frame_handlers:
            send_notification("subscribeFrames", {"plugin": self.plugin_name})
        self._frame_handlers.append(handler)

    def register_command(self, command_id, handler, title=None):
        self._commands[command_id] = handler
        send_notification("registerCommand", {
            "id": command_id,
            "title": title or command_id,
        })

    def trigger_frame_handlers(self, frames):
        for handler in self._frame_handlers:
            for frame in frames:
                try:
                    handler(frame)
                except Exception:
                    log_error(
                        f"plugin {self.plugin_name} frame handler error:\n"
                        f"{traceback.format_exc()}"
                    )

    def execute_command(self, command_id):
        handler = self._commands.get(command_id)
        if handler:
            try:
                handler()
            except Exception:
                log_error(
                    f"plugin {self.plugin_name} command '{command_id}' error:\n"
                    f"{traceback.format_exc()}"
                )
        else:
            log_error(f"plugin {self.plugin_name} has no command '{command_id}'")


def load_plugin(plugin_name, plugin_dir, main_script):
    main_path = os.path.join(plugin_dir, main_script)
    if not os.path.exists(main_path):
        log_error(f"plugin {plugin_name} entry missing: {main_path}")
        return None
    # Park still-active suites' bare tops, then clear collisions so this
    # suite can import its own app_shell/pages without closing peers.
    _park_active_suite_modules()
    _evict_stale_suite_modules(plugin_dir)
    # Plugin dir first for local modules (uds_client, …); keep plugins root for _shared.
    if plugin_dir and plugin_dir not in sys.path:
        sys.path.insert(0, plugin_dir)
    else:
        # Move this suite ahead of any other suite dir still on sys.path.
        try:
            sys.path.remove(plugin_dir)
        except ValueError:
            pass
        if plugin_dir:
            sys.path.insert(0, plugin_dir)
    parent = os.path.dirname(os.path.abspath(plugin_dir)) if plugin_dir else ""
    if parent and parent not in sys.path:
        sys.path.insert(0, parent)
    try:
        spec = importlib.util.spec_from_file_location(
            f"_sin_plugin_{plugin_name}", main_path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
    except Exception:
        log_error(f"plugin {plugin_name} load failed:\n{traceback.format_exc()}")
        return None


# Top-level names shared by every domain suite. Must not leak across plugins.
_SUITE_LOCAL_TOPS = ("app_shell", "session", "pages", "widgets", "core")


def _safe_plugin_key(plugin_name: str) -> str:
    return "".join(c if c.isalnum() else "_" for c in (plugin_name or "plugin"))


def _park_prefix(plugin_name: str) -> str:
    return "_sin_suite_%s" % _safe_plugin_key(plugin_name)


def _is_under(path, directory):
    if not path or not directory:
        return False
    try:
        path = os.path.abspath(path)
        directory = os.path.abspath(directory)
        return os.path.commonpath([path, directory]) == directory
    except ValueError:
        return False


def _evict_modules_under(directory):
    if not directory:
        return
    for name, mod in list(sys.modules.items()):
        f = getattr(mod, "__file__", None)
        if f and _is_under(f, directory):
            sys.modules.pop(name, None)


def _park_suite_modules(plugin_name: str, plugin_dir: str) -> None:
    """Keep suite tops alive under a namespaced key after bare names move.

    Multiple domain suites can stay open: peer windows already hold class
    references; parking prevents GC and lets a later activate reclaim bare
    names (app_shell/pages/...) for the newly opened suite.
    """
    if not plugin_name or not plugin_dir:
        return
    prefix = _park_prefix(plugin_name)
    root = os.path.abspath(plugin_dir)
    for name, mod in list(sys.modules.items()):
        if name.startswith("_sin_suite_"):
            continue
        top = name.split(".", 1)[0]
        if top not in _SUITE_LOCAL_TOPS:
            continue
        f = getattr(mod, "__file__", None)
        if not f or not _is_under(f, root):
            continue
        sys.modules["%s.%s" % (prefix, name)] = mod


def _park_active_suite_modules() -> None:
    for name, entry in list(_plugins.items()):
        _park_suite_modules(name, entry.get("directory") or "")


def _evict_stale_suite_modules(keep_dir):
    """Drop bare suite tops not owned by keep_dir (parked copies stay)."""
    keep = os.path.abspath(keep_dir) if keep_dir else ""
    plugins_root = os.path.dirname(keep) if keep else ""
    for name, mod in list(sys.modules.items()):
        if name.startswith("_sin_suite_"):
            continue
        top = name.split(".", 1)[0]
        if top not in _SUITE_LOCAL_TOPS:
            continue
        f = getattr(mod, "__file__", None)
        if not f:
            sys.modules.pop(name, None)
            continue
        af = os.path.abspath(f)
        if plugins_root and not _is_under(af, plugins_root):
            continue
        if keep and _is_under(af, keep):
            continue
        sys.modules.pop(name, None)


def activate_plugin(params):
    name = params.get("plugin")
    directory = params.get("directory", "")
    main_script = params.get("main", "main.py")
    if name in _plugins:
        return True
    module = load_plugin(name, directory, main_script)
    if module is None:
        return False
    context = PluginContext(name)
    _plugins[name] = {
        "module": module,
        "context": context,
        "directory": os.path.abspath(directory) if directory else "",
    }
    try:
        from sin.ui import set_current_plugin
        set_current_plugin(name)
    except ImportError:
        pass
    if hasattr(module, "activate"):
        try:
            module.activate(context)
            log_info(f"plugin {name} activated")
        except Exception:
            log_error(f"plugin {name} activate failed:\n{traceback.format_exc()}")
            _plugins.pop(name, None)
            return False
    # Park this suite so a later peer activate can reclaim bare tops.
    _park_suite_modules(name, _plugins[name]["directory"])
    return True


def deactivate_plugin(params):
    name = params.get("plugin")
    entry = _plugins.pop(name, None)
    if not entry:
        return
    try:
        from sin.ui import set_current_plugin, close_plugin_windows
        set_current_plugin(None)
        close_plugin_windows(name)
    except ImportError:
        try:
            from sin.ui import set_current_plugin
            set_current_plugin(None)
        except ImportError:
            pass
    if hasattr(entry["module"], "deactivate"):
        try:
            entry["module"].deactivate()
            log_info(f"plugin {name} deactivated")
        except Exception:
            log_error(f"plugin {name} deactivate failed:\n{traceback.format_exc()}")
    # Free this suite's modules (bare + parked) so the next open is clean.
    directory = entry.get("directory") or ""
    _evict_modules_under(directory)
    prefix = _park_prefix(name)
    for mod_name in list(sys.modules):
        if mod_name == prefix or mod_name.startswith(prefix + "."):
            sys.modules.pop(mod_name, None)
    main_key = f"_sin_plugin_{name}"
    sys.modules.pop(main_key, None)


def dispatch_frame_dicts(frames_data):
    from sin.frames import Frame
    frames = [Frame(f) for f in frames_data]
    for entry in _plugins.values():
        context = entry["context"]
        if context._frame_handlers:
            context.trigger_frame_handlers(frames)


def execute_command(params):
    command_id = params.get("id", "")
    for entry in _plugins.values():
        context = entry["context"]
        if command_id in context._commands:
            context.execute_command(command_id)
            return
    log_error(f"command not found: '{command_id}'")


def handle_file_opened(params):
    path = params.get("path", "")
    extension = params.get("extension", "")
    for name, entry in _plugins.items():
        module = entry["module"]
        if hasattr(module, "on_file_opened"):
            try:
                module.on_file_opened(path, extension)
            except Exception:
                log_error(
                    f"plugin {name} on_file_opened error:\n{traceback.format_exc()}"
                )


def handle_set_language(params):
    locale = params.get("locale") or params.get("language") or "en"
    try:
        from _shared import i18n
        i18n.set_language(locale)
        log_info(f"setLanguage: {locale}")
    except Exception:
        log_error(f"setLanguage failed:\n{traceback.format_exc()}")
        return
    for name, entry in list(_plugins.items()):
        module = entry.get("module")
        context = entry.get("context")
        for target in (module, context):
            if target is None:
                continue
            for attr in ("retranslate", "on_language_changed", "set_language"):
                fn = getattr(target, attr, None)
                if callable(fn):
                    try:
                        if attr == "set_language":
                            fn(locale)
                        else:
                            fn()
                    except TypeError:
                        try:
                            fn(locale)
                        except Exception:
                            log_error(
                                f"plugin {name} {attr} error:\n{traceback.format_exc()}"
                            )
                    except Exception:
                        log_error(
                            f"plugin {name} {attr} error:\n{traceback.format_exc()}"
                        )
                    break
        # Open suite shells often live on the context
        shell = getattr(context, "shell", None) if context else None
        if shell is None and context is not None:
            shell = getattr(context, "_shell", None)
        if shell is not None and hasattr(shell, "retranslate"):
            try:
                shell.retranslate()
            except Exception:
                log_error(
                    f"plugin {name} shell.retranslate error:\n{traceback.format_exc()}"
                )


def handle_message(msg):
    method = msg.get("method")
    params = msg.get("params") or {}
    msg_id = msg.get("id")

    if method == "activate":
        ok = activate_plugin(params)
        if msg_id is not None:
            if ok:
                send_response(msg_id, {"success": True})
            else:
                send_error(msg_id, -32000, f"activate failed: {params.get('plugin')}")
    elif method == "deactivate":
        deactivate_plugin(params)
        if msg_id is not None:
            send_response(msg_id, {"success": True})
    elif method == "frameReceived":
        # Legacy JSON path (compat); preferred path is ZMQ binary SUB.
        dispatch_frame_dicts(params.get("frames") or ([params] if "id" in params else []))
    elif method == "executeCommand":
        execute_command(params)
    elif method == "fileOpened":
        handle_file_opened(params)
    elif method in ("setLanguage", "set_language"):
        handle_set_language(params)
        if msg_id is not None:
            send_response(msg_id, {"success": True})
    elif method == "ai.attach":
        try:
            from _shared import ai_attach
            ai_attach.attach_from_host_params(params)
            # Ensure AI Agent window is available
            if "ai-agent" not in _plugins:
                # Soft open: command may activate if registered later
                pass
            execute_command({"id": "aiAgent.open"})
            log_info("ai.attach: %d attachment(s)" % len(
                params.get("attachments") or ([params] if params.get("kind") else [])))
        except Exception:
            log_error(f"ai.attach failed:\n{traceback.format_exc()}")
    elif method and method.startswith("files.convert"):
        try:
            from sin import files as _files_api
            _files_api.handle_notification(method, params)
        except Exception:
            log_error(f"files notification error:\n{traceback.format_exc()}")
    elif method == "shutdown":
        for name in list(_plugins.keys()):
            deactivate_plugin({"plugin": name})
        if msg_id is not None:
            send_response(msg_id, {"success": True})
        return False
    else:
        log_error(f"unknown method: {method}")
    return True


def _ctrl_reader():
    import zmq
    while True:
        try:
            raw = _ctrl_sock.recv()
        except zmq.ZMQError:
            _message_queue.put(_SENTINEL)
            break
        try:
            msg = json.loads(raw.decode("utf-8"))
        except Exception:
            log_error(f"ctrl JSON parse error:\n{traceback.format_exc()}")
            continue
        if "result" in msg or "error" in msg:
            deliver_response(msg.get("id"), msg.get("result"), msg.get("error"))
        else:
            _message_queue.put(msg)


def _on_frame_batch(frames_data):
    # Deliver onto the main message queue so Qt widgets stay on the UI thread.
    _message_queue.put({"method": "_frameBatch", "params": {"frames": frames_data}})


def main():
    global _ctrl_sock

    ctrl_ep = os.environ.get("SIN_ZMQ_CTRL", "")
    data_ep = os.environ.get("SIN_ZMQ_DATA", "")
    if not ctrl_ep or not data_ep:
        sys.stderr.write("sin_host: SIN_ZMQ_CTRL / SIN_ZMQ_DATA required\n")
        sys.exit(2)

    import zmq
    ctx = zmq.Context.instance()
    _ctrl_sock = ctx.socket(zmq.DEALER)
    _ctrl_sock.setsockopt(zmq.IDENTITY, b"sin-host")
    _ctrl_sock.setsockopt(zmq.LINGER, 0)
    _ctrl_sock.connect(ctrl_ep)
    transport_configure(_send_ctrl)

    databus = DataBus(data_ep, _on_frame_batch)
    databus.start()

    reader = threading.Thread(target=_ctrl_reader, name="sin-ctrl", daemon=True)
    reader.start()

    # DEALER connect is async; retransmit hello briefly so a slow ROUTER bind
    # or first-packet loss does not leave the main process waiting forever.
    def _hello_burst():
        for i in range(8):
            send_notification("host.hello", {"pid": os.getpid(), "n": i})
            time.sleep(0.25)

    import time
    threading.Thread(target=_hello_burst, name="sin-hello", daemon=True).start()
    sys.stderr.write(f"sin_host: hello burst ctrl={ctrl_ep} data={data_ep}\n")
    sys.stderr.flush()

    if _has_pyqt:
        log_info("sin plugin host started (PyQt6 available)")
    else:
        log_info("sin plugin host started (PyQt6 missing; UI disabled)")

    try:
        if _has_pyqt:
            _main_with_qt()
        else:
            _main_simple()
    finally:
        try:
            databus.stop()
        except Exception:
            pass
        sock = _ctrl_sock
        _ctrl_sock = None
        try:
            if sock is not None:
                sock.close(0)
        except Exception:
            pass
        if _has_pyqt:
            try:
                from sin.ui import close_all_windows
                close_all_windows()
            except Exception:
                pass
        sys.stderr.write("sin plugin host exited\n")
        sys.stderr.flush()


def _dispatch_queued(msg):
    if msg.get("method") == "_frameBatch":
        dispatch_frame_dicts((msg.get("params") or {}).get("frames") or [])
        return True
    return handle_message(msg)


def _main_with_qt():
    from PyQt6.QtCore import QTimer

    app = _QApplication.instance()
    if app is None:
        app = _QApplication(sys.argv)
    app.setQuitOnLastWindowClosed(False)

    def process_messages():
        while True:
            try:
                msg = _message_queue.get_nowait()
            except queue.Empty:
                break
            if msg is _SENTINEL:
                app.quit()
                return
            try:
                if _dispatch_queued(msg) is False:
                    app.quit()
                    return
            except Exception:
                log_error(f"message handling error:\n{traceback.format_exc()}")

    timer = QTimer()
    timer.timeout.connect(process_messages)
    timer.start(10)
    app.exec()


def _main_simple():
    while True:
        try:
            msg = _message_queue.get(timeout=0.1)
        except queue.Empty:
            continue
        if msg is _SENTINEL:
            break
        try:
            if _dispatch_queued(msg) is False:
                break
        except Exception:
            log_error(f"message handling error:\n{traceback.format_exc()}")


if __name__ == "__main__":
    main()
