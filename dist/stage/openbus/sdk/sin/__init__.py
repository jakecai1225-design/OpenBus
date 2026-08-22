"""sin 插件 SDK — 与主程序交互的公共 API

使用方式（在插件 main.py 中）:
    import sin

    def activate(context):
        sin.output.append("插件已加载")
        context.on_frame(on_frame)

    def on_frame(frame):
        sin.output.append(f"收到帧: 0x{frame.id:X}")
"""

from .output import output
from .frames import frames
from .commands import commands
from .workspace import workspace
from .signals import signals
from .files import files
from .dbc import dbc

# UI 模块需要 PyQt6，未安装时跳过
try:
    from .ui import ui
    _has_ui = True
except ImportError:
    _has_ui = False

__version__ = "1.1.0"
__all__ = ["output", "frames", "commands", "workspace", "signals", "files", "dbc"]
if _has_ui:
    __all__.append("ui")
