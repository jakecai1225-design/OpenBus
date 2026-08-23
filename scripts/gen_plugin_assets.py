# -*- coding: utf-8 -*-
"""gen_plugin_assets.py — 批次一 32 款插件脚手架资产生成器

生成 sin/plugins/<id>/{plugin.json, icon.svg}（main.py 由开发者编写）。
清单与 doc/插件市场批次一功能清单.md §四 保持一致。

用法（sin 工程根）：
    python scripts/gen_plugin_assets.py            # 生成/覆盖清单与图标
    python scripts/gen_plugin_assets.py --check    # 仅校验 main.py 是否齐全
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGINS_DIR = os.path.join(HERE, "..", "plugins")

# (id, 名称, 命令ID, 命令标题, 图标色, 图标字标, 描述)
PLUGINS = [
    ("obd2-scanner", "OBD-II 诊断扫描", "obd2Scanner.open", "诊断: OBD-II 扫描", "#E11D48", "OBD",
     "OBD-II 车辆诊断扫描：模式 01/02/03/04/07/09，动态 PID 数据、冻结帧、DTC 读取与清码、MIL 状态（ISO 15031-5 / SAE J1979）"),
    ("uds-scan", "UDS ECU 扫描器", "udsScan.open", "诊断: UDS 扫描", "#F97316", "UDS",
     "UDS ECU 扫描器：诊断 ID 段扫描与服务探测，生成 ECU 清单与支持服务地图（ISO 14229）"),
    ("uds-batch", "UDS 批量测试", "udsBatch.open", "诊断: UDS 批量测试", "#EA580C", "BAT",
     "UDS 批量测试：CSV 请求表批量执行，期望响应校验，通过率统计与报告导出（内置 ISO-TP）"),
    ("uds-security-audit", "UDS 安全审计", "udsSecurityAudit.open", "诊断: UDS 安全审计", "#DC2626", "SEC",
     "UDS 安全审计：SecurityAccess 种子采集与弱随机检测、会话时序审计、负响应统计与安全报告"),
    ("iso-tp-monitor", "ISO-TP 会话监视", "isoTpMonitor.open", "协议: ISO-TP 监视", "#0891B2", "TP",
     "ISO-TP 会话监视：被动解码 ISO 15765-2 多帧会话（FF/CF/FC 全状态机、重组、超时检测）"),
    ("j1939-analyzer", "J1939 协议分析", "j1939Analyzer.open", "协议: J1939 分析", "#7C3AED", "J19",
     "SAE J1939 协议分析：PGN/Priority/SA/DA 拆解、高频 SPN 解码、BAM/RTS-CTS 传输重组、DM1 故障码"),
    ("gbt27930-monitor", "GB/T 27930 充电监视", "gbt27930Monitor.open", "协议: 国标充电监视", "#16A34A", "GB",
     "GB/T 27930 国标充电监视：BMS/充电机报文全解码（CHM→CST）、充电阶段状态机与异常告警"),
    ("nmea2000-decoder", "NMEA 2000 解码", "nmea2000Decoder.open", "协议: NMEA 2000", "#0EA5E9", "N2K",
     "NMEA 2000 解码器：Fast Packet 重组、常用 PGN 解码（航向/航速/位置/水深/风速/温度）"),
    ("isobus-monitor", "ISOBUS 监视", "isobusMonitor.open", "协议: ISOBUS 监视", "#65A30D", "ISO",
     "ISOBUS (ISO 11783) 监视器：地址声明解析、在线节点表、TC-BAS/TC-GEO/VT 报文监视"),
    ("xcp-monitor", "XCP 监视", "xcpMonitor.open", "标定: XCP 监视", "#9333EA", "XCP",
     "XCP (CAN) 监视器：CONNECT/GET_STATUS/DIAG 等 CTO 命令解码、DAQ/STIM 识别、会话视图"),
    ("autosar-nm-monitor", "AUTOSAR NM 监视", "autosarNmMonitor.open", "协议: AUTOSAR NM", "#475569", "NM",
     "AUTOSAR CAN 网络管理监视：0x400-0x4FF NM 报文解析、节点状态机推断与时间线"),
    ("canopen-scanner", "CANopen 节点扫描", "canopenScanner.open", "协议: CANopen 扫描", "#DB2777", "CO",
     "CANopen 节点扫描器：1-127 节点 Identity 探测（SDO 0x1018）、心跳发现、节点清单导出"),
    ("can-frame-generator", "报文发送器", "canFrameGenerator.open", "工具: 报文发送器", "#2563EB", "TX",
     "CAN 报文发送器：多条目发送表（立即/周期/定次）、DBC 感知信号级编码、序列播放与导入导出"),
    ("can-simulator", "节点仿真器", "canSimulator.open", "仿真: 节点仿真器", "#0D9488", "SIM",
     "CAN 节点仿真器：DBC 驱动 Restbus 仿真，按报文周期发送，信号值模式（恒值/递增/正弦/随机/斜坡）"),
    ("can-gateway", "报文网关", "canGateway.open", "工具: 报文网关", "#6366F1", "GW",
     "CAN 报文网关：规则化转发（ID 精确/掩码匹配、ID 重映射、载荷补丁、限频），转发计数统计"),
    ("can-id-scanner", "ID 扫描与审计", "canIdScanner.open", "工具: ID 扫描审计", "#F59E0B", "ID",
     "CAN ID 扫描与审计：实时 ID 发现与分类、DBC 增量审计（无库 ID/未出现 ID）、CSV 导出"),
    ("can-reverse", "逆向位分析", "canReverse.open", "工具: 逆向位分析", "#84CC16", "REV",
     "CAN 逆向位分析：逐 ID 位活动矩阵与热力图、变化位统计、信号边界推荐、A/B 快照差异对比"),
    ("dbc-diff", "DBC 对比", "dbcDiff.open", "数据库: DBC 对比", "#64748B", "DIF",
     "DBC 对比工具：双库报文/信号/属性级 diff，差异树视图与报告导出"),
    ("dbc-merge", "DBC 合并", "dbcMerge.open", "数据库: DBC 合并", "#475569", "MRG",
     "DBC 合并工具：多库合并（ID 冲突策略可配）、节点/报文/信号合并、冲突报告、另存新库"),
    ("dbc-exporter", "DBC 导出", "dbcExporter.open", "数据库: DBC 导出", "#6D28D9", "EXP",
     "DBC 导出工具：信号矩阵导出 CSV/JSON/HTML（含单位、值表、收发节点、注释）"),
    ("dbc-codegen", "DBC C 代码生成", "dbcCodegen.open", "数据库: C 代码生成", "#1E40AF", "GEN",
     "DBC C 代码生成：pack/unpack 函数生成（Intel/Motorola、有符号、factor/offset、值表宏）"),
    ("dbc-lint", "DBC 静态检查", "dbcLint.open", "数据库: DBC 检查", "#B45309", "LNT",
     "DBC 静态检查：命名规范、信号重叠/越界、min-max 一致性、缺失属性检查，问题分级报告"),
    ("log-toolkit", "日志工具箱", "logToolkit.open", "工具: 日志工具箱", "#0369A1", "LOG",
     "CAN 日志工具箱：ASC/CSV 日志裁剪、ID 过滤、多文件合并、按帧数/时长拆分、脱敏掩码"),
    ("frame-compare", "双源报文比对", "frameCompare.open", "工具: 报文比对", "#155E75", "CMP",
     "CAN 双源报文比对：实时/缓冲/CSV 三种来源两两比对，缺帧/多帧/载荷差/周期差统计与导出"),
    ("trigger-logger", "触发记录", "triggerLogger.open", "工具: 触发记录", "#C2410C", "TRG",
     "CAN 触发记录器：条件触发（ID+数据掩码）、预触发环形缓冲、后触发窗口、单次/重复模式"),
    ("can-quality-report", "总线体检报告", "canQualityReport.open", "分析: 总线体检", "#059669", "QAR",
     "CAN 总线体检报告：负载曲线、Top Talker、周期稳定性、DBC 覆盖率、空闲率，一键 HTML/CSV 报告"),
    ("can-ids", "CAN 入侵检测", "canIds.open", "安全: 入侵检测", "#991B1B", "IDS",
     "CAN 入侵检测：ID 白名单（学习模式）、速率异常（μ+3σ）、载荷变化异常、新 ID 告警与事件导出"),
    ("can-fuzzer", "CAN 模糊测试", "canFuzzer.open", "安全: 模糊测试", "#BE123C", "FUZ",
     "CAN 模糊测试：ID 范围随机/结构化变异、限速发送、突变统计、一键停止"),
    ("can-stress", "CAN 压力测试", "canStress.open", "安全: 压力测试", "#A21CAF", "STR",
     "CAN 压力测试：突发洪泛（目标负载）、畸形帧注入（DLC/FD 边界）、持续/间隔配置与统计"),
    ("can-dashboard", "实时仪表盘", "canDashboard.open", "可视化: 仪表盘", "#0284C7", "DSH",
     "CAN 实时仪表盘：DBC 信号绑定的仪表盘/柱状/数字部件，阈值着色，暂停继续"),
    ("e2e-checksum", "E2E 校验工具", "e2eChecksum.open", "安全: E2E 校验", "#115E59", "E2E",
     "CAN E2E 校验工具：CRC8/8H2F/16/32/XOR/J1850 实时校验、alive counter 检查、错误清单"),
    ("can-bit-timing", "位时序计算器", "canBitTiming.open", "工具: 位时序计算", "#525252", "TQ",
     "CAN 位时序计算器：经典 CAN/CAN FD 波特率与分段建议、采样点、TDc 计算（离线工具）"),
]

ICON_TPL = (
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48">\n'
    '  <rect x="2" y="2" width="44" height="44" rx="10" fill="{color}"/>\n'
    '  <text x="24" y="30" font-family="Segoe UI, sans-serif" font-size="{fsize}" '
    'font-weight="bold" fill="#ffffff" text-anchor="middle">{label}</text>\n'
    '</svg>\n'
)


def main():
    check_only = "--check" in sys.argv
    os.makedirs(PLUGINS_DIR, exist_ok=True)
    missing = []
    for pid, title, cmd_id, cmd_title, color, label, desc in PLUGINS:
        d = os.path.join(PLUGINS_DIR, pid)
        os.makedirs(d, exist_ok=True)

        manifest = {
            "name": pid,
            "version": "1.0.0",
            "author": "sin",
            "description": desc,
            "main": "main.py",
            "icon": "icon.svg",
            "activationEvents": ["onCommand:" + cmd_id],
            "contributes": {
                "commands": [
                    {"id": cmd_id, "title": cmd_title}
                ]
            },
        }
        icon = ICON_TPL.format(color=color, label=label, fsize=13 if len(label) > 2 else 15)

        if check_only:
            if not os.path.exists(os.path.join(d, "main.py")):
                missing.append(pid)
            continue

        with open(os.path.join(d, "plugin.json"), "w", encoding="utf-8") as f:
            json.dump(manifest, f, ensure_ascii=False, indent=4)
            f.write("\n")
        with open(os.path.join(d, "icon.svg"), "w", encoding="utf-8") as f:
            f.write(icon)

    if check_only:
        if missing:
            print("MISSING main.py (%d): %s" % (len(missing), ", ".join(missing)))
            sys.exit(1)
        print("ALL %d plugins have main.py" % len(PLUGINS))
    else:
        print("generated %d plugin.json + icon.svg under %s" % (len(PLUGINS), os.path.abspath(PLUGINS_DIR)))


if __name__ == "__main__":
    main()
