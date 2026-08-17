#!/usr/bin/env python3
"""sin 插件宿主 — 从 stdin 读取 JSON-RPC，分发到已加载插件

通信协议: newline-delimited JSON-RPC 2.0 over stdin/stdout
  主程序 → 本脚本: activate/deactivate/frameReceived/executeCommand/...
  本脚本 → 主程序: output.append/sendFrame/registerCommand/...

SDK 模块 `sin` 通过同一 stdout 管道与主程序通信，
共享 _transport 模块的锁和请求队列。
"""

import sys
import os
import json
import importlib.util
import traceback
import threading
import queue

# ============================================================
#  SDK 路径设置（必须在导入 sin._transport 之前）
# ============================================================

_sdk_dir = os.environ.get("SIN_SDK_DIR", "")
if _sdk_dir and _sdk_dir not in sys.path:
    sys.path.insert(0, _sdk_dir)

_host_dir = os.path.dirname(os.path.abspath(__file__))
if _host_dir not in sys.path:
    sys.path.insert(0, _host_dir)

# 导入 SDK 传输层（共享 stdout 锁和请求队列）
from sin._transport import _send_message, send_notification, send_request, deliver_response

# 检测 PyQt6 是否可用（决定是否启用 UI 功能和 Qt 事件循环）
try:
    from PyQt6.QtWidgets import QApplication as _QApplication
    _has_pyqt = True
except ImportError:
    _has_pyqt = False

# ============================================================
#  宿主专用 I/O 函数
# ============================================================

def send_response(msg_id, result):
    """发送响应（回复主程序的请求）"""
    _send_message({"jsonrpc": "2.0", "result": result, "id": msg_id})


def send_error(msg_id, code, message):
    """发送错误响应"""
    _send_message({"jsonrpc": "2.0", "error": {"code": code, "message": message}, "id": msg_id})


def log_error(message):
    """记录错误到主程序"""
    send_notification("log", {"level": 1, "message": str(message)})


def log_info(message):
    """记录信息到主程序"""
    send_notification("log", {"level": 0, "message": str(message)})


# ============================================================
#  全局状态
# ============================================================

# 已加载的插件: name → {"module": module, "context": PluginContext}
_plugins = {}


# ============================================================
#  插件上下文
# ============================================================

class PluginContext:
    """每个插件的上下文对象，传递给 activate()"""

    def __init__(self, plugin_name):
        self.plugin_name = plugin_name
        self._frame_handlers = []
        self._commands = {}

    def on_frame(self, handler):
        """注册帧处理回调"""
        self._frame_handlers.append(handler)

    def register_command(self, command_id, handler, title=None):
        """注册命令"""
        self._commands[command_id] = handler
        send_notification("registerCommand", {
            "id": command_id,
            "title": title or command_id
        })

    def trigger_frame_handlers(self, frames):
        """触发所有帧处理回调"""
        for handler in self._frame_handlers:
            for frame in frames:
                try:
                    handler(frame)
                except Exception:
                    log_error(f"插件 {self.plugin_name} 帧处理错误:\n{traceback.format_exc()}")

    def execute_command(self, command_id):
        """执行已注册的命令"""
        handler = self._commands.get(command_id)
        if handler:
            try:
                handler()
            except Exception:
                log_error(f"插件 {self.plugin_name} 命令 '{command_id}' 执行错误:\n{traceback.format_exc()}")
        else:
            log_error(f"插件 {self.plugin_name} 未注册命令 '{command_id}'")


# ============================================================
#  插件加载与激活
# ============================================================

def load_plugin(plugin_name, plugin_dir, main_script):
    """动态加载插件 Python 模块"""
    main_path = os.path.join(plugin_dir, main_script)

    if not os.path.exists(main_path):
        log_error(f"插件 {plugin_name} 入口文件不存在: {main_path}")
        return None

    try:
        spec = importlib.util.spec_from_file_location(
            f"_sin_plugin_{plugin_name}", main_path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
    except Exception:
        log_error(f"插件 {plugin_name} 加载失败:\n{traceback.format_exc()}")
        return None


def activate_plugin(params):
    """激活插件"""
    name = params.get("plugin")
    directory = params.get("directory", "")
    main_script = params.get("main", "main.py")

    if name in _plugins:
        return  # 已激活

    module = load_plugin(name, directory, main_script)
    if module is None:
        return

    context = PluginContext(name)
    _plugins[name] = {"module": module, "context": context}

    # 设置当前插件名，供 ui 模块在窗口关闭时通知宿主
    try:
        from sin.ui import set_current_plugin
        set_current_plugin(name)
    except ImportError:
        pass

    if hasattr(module, "activate"):
        try:
            module.activate(context)
            log_info(f"插件 {name} 已激活")
        except Exception:
            log_error(f"插件 {name} 激活失败:\n{traceback.format_exc()}")


def deactivate_plugin(params):
    """停用插件"""
    name = params.get("plugin")
    entry = _plugins.pop(name, None)
    if entry:
        # 清除当前插件名，避免 close_all_windows 时发送多余的 pluginWindowClosed
        try:
            from sin.ui import set_current_plugin
            set_current_plugin(None)
        except ImportError:
            pass
        # 关闭该插件创建的所有窗口
        try:
            from sin.ui import close_all_windows
            close_all_windows()
        except ImportError:
            pass
        if hasattr(entry["module"], "deactivate"):
            try:
                entry["module"].deactivate()
                log_info(f"插件 {name} 已停用")
            except Exception:
                log_error(f"插件 {name} 停用失败:\n{traceback.format_exc()}")


def dispatch_frames(params):
    """将帧分发给所有已激活的 onFrame 插件"""
    frames_data = params.get("frames", [])
    if not frames_data:
        # 兼容单帧格式
        if "id" in params:
            frames_data = [params]

    # 转换为 Frame 对象
    from sin.frames import Frame
    frames = [Frame(f) for f in frames_data]

    for name, entry in _plugins.items():
        context = entry["context"]
        if context._frame_handlers:
            context.trigger_frame_handlers(frames)


def execute_command(params):
    """执行命令"""
    command_id = params.get("id", "")
    for name, entry in _plugins.items():
        context = entry["context"]
        if command_id in context._commands:
            context.execute_command(command_id)
            return

    log_error(f"未找到命令 '{command_id}'")


def handle_file_opened(params):
    """处理文件打开事件"""
    path = params.get("path", "")
    extension = params.get("extension", "")
    for name, entry in _plugins.items():
        module = entry["module"]
        if hasattr(module, "on_file_opened"):
            try:
                module.on_file_opened(path, extension)
            except Exception:
                log_error(f"插件 {name} 文件打开处理错误:\n{traceback.format_exc()}")


# ============================================================
#  消息分发
# ============================================================

def handle_message(msg):
    """处理来自主程序的 JSON-RPC 消息"""
    method = msg.get("method")
    params = msg.get("params", {})
    msg_id = msg.get("id")

    if method == "activate":
        activate_plugin(params)
        if msg_id is not None:
            send_response(msg_id, {"success": True})

    elif method == "deactivate":
        deactivate_plugin(params)
        if msg_id is not None:
            send_response(msg_id, {"success": True})

    elif method == "frameReceived":
        dispatch_frames(params)

    elif method == "executeCommand":
        execute_command(params)

    elif method == "fileOpened":
        handle_file_opened(params)

    elif method and method.startswith("files.convert"):
        # G9：转换进度/完成通知 → 分发给 sin.files 注册的回调
        try:
            from sin import files as _files_api
            _files_api.handle_notification(method, params)
        except Exception:
            log_error(f"files 通知分发错误:\n{traceback.format_exc()}")

    elif method == "shutdown":
        # 优雅关闭：停用所有插件
        for name in list(_plugins.keys()):
            deactivate_plugin({"plugin": name})
        if msg_id is not None:
            send_response(msg_id, {"success": True})
        return False  # 退出主循环

    else:
        log_error(f"未知方法: {method}")

    return True


def handle_response(msg):
    """处理来自主程序的响应（回复 SDK 发出的请求）"""
    msg_id = msg.get("id")
    result = msg.get("result")
    error = msg.get("error")
    deliver_response(msg_id, result, error)


# ============================================================
#  消息队列与 stdin 读取线程
# ============================================================

_message_queue = queue.Queue()
_SENTINEL = object()  # stdin 关闭标志


def _stdin_reader():
    """后台线程：持续读取 stdin，分发响应，排队请求/通知

    将 stdin 读取放到独立线程，确保：
    - send_request() 阻塞时仍能收到响应（修复原有死锁问题）
    - PyQt6 事件循环运行时 stdin 不被阻塞
    """
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)

            if "result" in msg or "error" in msg:
                # 响应消息：直接分发给等待中的 SDK 请求
                handle_response(msg)
            else:
                # 请求/通知：排队等待主线程处理
                _message_queue.put(msg)

        except Exception:
            log_error(f"消息解析错误:\n{traceback.format_exc()}")

    # stdin 已关闭（主程序退出），发送终止信号
    _message_queue.put(_SENTINEL)


# ============================================================
#  主循环
# ============================================================

def main():
    if _has_pyqt:
        log_info("sin 插件宿主已启动 (PyQt6 可用，UI 功能已启用)")
    else:
        log_info("sin 插件宿主已启动 (PyQt6 未安装，UI 不可用，pip install PyQt6 启用)")

    # 启动 stdin 读取线程（始终使用，确保 send_request 不死锁）
    reader = threading.Thread(target=_stdin_reader, daemon=True)
    reader.start()

    if _has_pyqt:
        _main_with_qt()
    else:
        _main_simple()

    # 清理：关闭所有插件窗口
    if _has_pyqt:
        try:
            from sin.ui import close_all_windows
            close_all_windows()
        except Exception:
            pass

    log_info("sin 插件宿主已退出")


def _main_with_qt():
    """使用 PyQt6 事件循环的主循环

    QApplication 事件循环驱动所有 PyQt6 窗口。
    QTimer 每 10ms 轮询消息队列，在主线程处理消息（Qt 要求 widget 操作在主线程）。
    """
    from PyQt6.QtCore import QTimer

    app = _QApplication.instance()
    if app is None:
        app = _QApplication(sys.argv)

    # 关键：关闭最后一个窗口时不退出 QApplication
    # 插件窗口关闭后宿主进程必须保持运行，等待用户再次双击激活
    app.setQuitOnLastWindowClosed(False)

    def process_messages():
        """从队列取出消息并在主线程处理"""
        while True:
            try:
                msg = _message_queue.get_nowait()
            except queue.Empty:
                break

            if msg is _SENTINEL:
                app.quit()
                return

            try:
                result = handle_message(msg)
                if result is False:
                    app.quit()
                    return
            except Exception:
                log_error(f"消息处理错误:\n{traceback.format_exc()}")

    timer = QTimer()
    timer.timeout.connect(process_messages)
    timer.start(10)  # 10ms 轮询

    app.exec()


def _main_simple():
    """不使用 PyQt6 的简单主循环"""
    while True:
        try:
            msg = _message_queue.get(timeout=0.1)
        except queue.Empty:
            continue

        if msg is _SENTINEL:
            break

        try:
            result = handle_message(msg)
            if result is False:
                break
        except Exception:
            log_error(f"消息处理错误:\n{traceback.format_exc()}")


if __name__ == "__main__":
    main()
