"""frame-counter 插件 — 帧统计示例

演示 onFrame 事件的使用：
- 实时统计接收到的 CAN 帧数量
- 按 CAN ID 分类统计
- 通过命令输出统计报告
"""

import sin

# 全局统计状态
_total_frames = 0
_id_counts = {}


def activate(context):
    """插件激活时调用"""
    sin.output.append("帧统计插件已加载")
    context.on_frame(on_frame)
    context.register_command("frameCounter.report", report, "帧统计: 输出报告")


def on_frame(frame):
    """帧处理回调 — 每收到一帧调用"""
    global _total_frames, _id_counts
    _total_frames += 1
    _id_counts[frame.id] = _id_counts.get(frame.id, 0) + 1


def report():
    """输出统计报告"""
    if _total_frames == 0:
        sin.output.append("帧统计: 暂无数据")
        return

    sin.output.append(f"===== 帧统计报告 =====")
    sin.output.append(f"总帧数: {_total_frames}")
    sin.output.append(f"不同 ID 数: {len(_id_counts)}")
    sin.output.append("--- ID 分布 (Top 10) ---")

    # 按 count 降序排列
    sorted_ids = sorted(_id_counts.items(), key=lambda x: x[1], reverse=True)
    for can_id, count in sorted_ids[:10]:
        pct = count * 100.0 / _total_frames
        sin.output.append(f"  0x{can_id:03X}: {count} 帧 ({pct:.1f}%)")


def deactivate():
    """插件停用时调用"""
    if _total_frames > 0:
        sin.output.append(f"帧统计插件卸载 (共统计 {_total_frames} 帧)")
    else:
        sin.output.append("帧统计插件卸载")
