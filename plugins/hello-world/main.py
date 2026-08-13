"""hello-world 插件 — 最简示例

演示 sin 插件系统的基本用法：
- 启动时输出欢迎消息
- 注册一个命令，点击后在输出面板显示问候
"""

import sin


def activate(context):
    """插件激活时调用"""
    sin.output.append("Hello World 插件已加载！")
    context.register_command("helloWorld.say", say_hello, "Hello World: 打招呼")


def say_hello():
    """命令处理函数"""
    sin.output.append("你好，来自 Hello World 插件！")


def deactivate():
    """插件停用时调用"""
    sin.output.append("Hello World 插件已卸载")
