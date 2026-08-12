"""ui-demo 插件 — 演示 PyQt6 独立窗口 UI 能力

展示：
- 创建带布局的独立窗口
- 按钮点击发送 CAN 帧
- 实时显示收到的帧
- 命令注册与交互

依赖: pip install PyQt6
"""

import sin


def activate(context):
    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout,
            QLabel, QPushButton, QLineEdit, QTextEdit
        )
    except ImportError:
        sin.output.append("UI 演示插件需要 PyQt6: pip install PyQt6")
        return

    sin.output.append("UI 演示插件已加载")

    # 创建独立窗口
    win = sin.ui.create_window("UI 演示 — 帧发送与监控")
    win.resize(520, 420)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    # --- 发送区域 ---
    layout.addWidget(QLabel("发送 CAN 帧:"))

    id_layout = QHBoxLayout()
    id_layout.addWidget(QLabel("CAN ID:"))
    id_input = QLineEdit("0x123")
    id_layout.addWidget(id_input)
    layout.addLayout(id_layout)

    data_layout = QHBoxLayout()
    data_layout.addWidget(QLabel("数据 (hex):"))
    data_input = QLineEdit("01 02 03 04")
    data_layout.addWidget(data_input)
    layout.addLayout(data_layout)

    send_btn = QPushButton("发送帧")
    layout.addWidget(send_btn)

    # --- 输出区域 ---
    layout.addWidget(QLabel("帧监控:"))
    output_text = QTextEdit()
    output_text.setReadOnly(True)
    layout.addWidget(output_text)

    # --- 统计 ---
    counter_label = QLabel("收到帧数: 0")
    layout.addWidget(counter_label)

    frame_count = [0]

    def on_send():
        try:
            can_id = int(id_input.text(), 0)
            hex_str = data_input.text().replace(" ", "")
            data = bytes.fromhex(hex_str)
            sin.frames.send(can_id, data)
            output_text.append(f"<b>[发送]</b> ID=0x{can_id:X} data={data.hex()}")
        except ValueError:
            output_text.append("<span style='color:red'>错误: 无效的 ID 或 hex 数据</span>")
        except Exception as e:
            output_text.append(f"<span style='color:red'>错误: {e}</span>")

    send_btn.clicked.connect(on_send)

    # 清空命令
    def on_clear():
        output_text.clear()
        frame_count[0] = 0
        counter_label.setText("收到帧数: 0")

    context.register_command("uidemo.clear", on_clear, "UI 演示: 清空输出")

    # 订阅帧
    def on_frame(frame):
        frame_count[0] += 1
        counter_label.setText(f"收到帧数: {frame_count[0]}")
        output_text.append(
            f"<span style='color:blue'>[接收]</span> "
            f"ID=0x{frame.id:X} dlc={frame.dlc} "
            f"data={frame.data.hex()} ts={frame.timestamp:.4f}"
        )

    context.on_frame(on_frame)

    # 显示窗口
    win.show()
    sin.output.append("UI 演示窗口已创建")


def deactivate():
    sin.output.append("UI 演示插件已停用")
