# -*- coding: utf-8 -*-
"""can-bit-timing 插件 — CAN 位时序计算器（Kvaser bit timing / CAN FD 工具风格）
功能：
- 经典 CAN / CAN FD 位时序计算：时钟频率 + 目标波特率 + 期望采样点
  → BRP / TSEG1 / TSEG2 / SJW 推荐方案（按波特率误差与采样点误差排序）
- CAN FD 双相位（仲裁段 + 数据段）计算 + TDC（收发器延迟补偿）估算
- 帧时长与总线利用率估算（含位填充近似）
- 纯离线工具：不订阅、不发送任何帧
依赖: pip install PyQt6
"""

from PyQt6.QtCore import QTimer

import sin


def solve_bit_timing(clock_mhz, baud_kbps, sample_pct, n_min=8, n_max=40):
    """搜索满足误差的 (BRP, N, TSEG1, TSEG2, SJW) 方案，按误差排序返回"""
    results = []
    target = baud_kbps * 1000.0
    for n in range(n_min, n_max + 1):
        brp_f = clock_mhz * 1e6 / (n * target)
        brp = round(brp_f)
        if brp < 1 or brp > 1024:
            continue
        actual = clock_mhz * 1e6 / (n * brp)
        err = abs(actual - target) / target * 100.0
        if err > 2.0:
            continue
        tseg1 = round(sample_pct / 100.0 * n) - 1
        tseg2 = n - 1 - tseg1
        if tseg1 < 1 or tseg2 < 1:
            continue
        sjw = min(tseg2, 4)
        sp = 100.0 * (1 + tseg1) / n
        results.append({
            "brp": brp, "n": n, "tseg1": tseg1, "tseg2": tseg2, "sjw": sjw,
            "baud": actual / 1000.0, "err": err, "sp": sp,
            "sp_err": abs(sp - sample_pct)})
    results.sort(key=lambda r: (round(r["err"], 3), r["sp_err"], r["n"]))
    # 多样性：不同 N 的方案各留一个（避免同一 N 多 BRP 近似解刷屏）
    seen_n = set()
    dedup = []
    for r in results:
        if r["n"] in seen_n:
            continue
        seen_n.add(r["n"])
        dedup.append(r)
    return dedup[:6]


def frame_time_ms(baud_kbps, dlc, extended, fd, data_baud_kbps=None):
    """帧时长估算（含位填充近似：填充位 ≈ 可填充位/5）"""
    if fd:
        arb = baud_kbps
        dat = data_baud_kbps or baud_kbps
        bits_arb = 1 + (29 if extended else 11) + 2      # SOF+ID+BRS/ESI 前段
        bits_data = 20 + 8 * dlc + (21 if dlc > 16 else (17 if dlc > 8 else 15))
        stuff = (bits_arb + bits_data) / 5.0
        total_arb = bits_arb
        total_data = bits_data + stuff
        ms = (total_arb / (arb * 1000.0) + total_data / (dat * 1000.0)) * 1000.0
        return ms
    bits = 1 + (29 if extended else 11) + 1 + 1 + 1 + 4 + 8 * dlc + 15 + 1 + 2 + 7 + 3
    stuff = (bits - 10) / 5.0
    return (bits + stuff) / (baud_kbps * 1000.0) * 1000.0


def activate(context):
    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QHeaderView, QGroupBox,
            QFormLayout, QDoubleSpinBox, QComboBox, QSpinBox, QTabWidget
        )
    except ImportError:
        sin.output.append("位时序计算插件需要 PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("CAN 位时序计算器")
    win.resize(980, 640)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    cfg = QGroupBox("参数")
    cfg_l = QFormLayout(cfg)
    clock_spin = QDoubleSpinBox()
    clock_spin.setRange(1, 200)
    clock_spin.setDecimals(1)
    clock_spin.setValue(40)
    clock_spin.setSuffix(" MHz")
    cfg_l.addRow("控制器时钟:", clock_spin)
    mode_combo = QComboBox()
    mode_combo.addItems(["经典 CAN", "CAN FD（仲裁 + 数据双相位）"])
    cfg_l.addRow("模式:", mode_combo)
    arb_baud = QComboBox()
    arb_baud.setEditable(True)
    arb_baud.addItems(["50", "100", "125", "250", "500", "800", "1000", "2000", "5000"])
    arb_baud.setCurrentText("500")
    cfg_l.addRow("仲裁段波特率 (kbps):", arb_baud)
    data_baud = QComboBox()
    data_baud.setEditable(True)
    data_baud.addItems(["1000", "2000", "4000", "5000", "8000"])
    data_baud.setCurrentText("2000")
    cfg_l.addRow("数据段波特率 (kbps):", data_baud)
    sp_arb = QDoubleSpinBox()
    sp_arb.setRange(50, 95)
    sp_arb.setValue(87.5)
    sp_arb.setSuffix(" %")
    cfg_l.addRow("仲裁段采样点:", sp_arb)
    sp_data = QDoubleSpinBox()
    sp_data.setRange(50, 95)
    sp_data.setValue(70.0)
    sp_data.setSuffix(" %")
    cfg_l.addRow("数据段采样点:", sp_data)
    layout.addWidget(cfg)

    calc_btn = QPushButton("计算")
    layout.addWidget(calc_btn)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    # 方案表
    arb_tree = QTreeWidget()
    arb_tree.setHeaderLabels(["BRP", "N(tq)", "TSEG1", "TSEG2", "SJW",
                              "实际波特率kbps", "误差%", "采样点%"])
    arb_tree.setRootIsDecorated(False)
    arb_tree.setAlternatingRowColors(True)
    tabs.addTab(arb_tree, "仲裁段方案")

    data_tree = QTreeWidget()
    data_tree.setHeaderLabels(["BRP", "N(tq)", "TSEG1", "TSEG2", "SJW",
                               "实际波特率kbps", "误差%", "采样点%"])
    data_tree.setRootIsDecorated(False)
    data_tree.setAlternatingRowColors(True)
    tabs.addTab(data_tree, "数据段方案（FD）")

    # TDC
    tdc_box = QGroupBox("TDC 收发器延迟补偿（CAN FD 数据段）")
    tdc_l = QFormLayout(tdc_box)
    loop_spin = QSpinBox()
    loop_spin.setRange(0, 2000)
    loop_spin.setValue(255)
    loop_spin.setSuffix(" ns（环回延迟）")
    tdc_l.addRow("收发器环回延迟:", loop_spin)
    tdc_result = QLabel("点击计算")
    tdc_result.setWordWrap(True)
    tdc_l.addRow("估算:", tdc_result)
    tabs.addTab(tdc_box, "TDC")

    # 帧时长
    frame_box = QGroupBox("帧时长与利用率估算")
    frame_l = QFormLayout(frame_box)
    dlc_combo = QComboBox()
    dlcs = ["0", "1", "2", "3", "4", "5", "6", "7", "8", "12", "16", "20",
            "24", "32", "48", "64"]
    dlc_combo.addItems(dlcs)
    dlc_combo.setCurrentIndex(8)
    frame_l.addRow("DLC:", dlc_combo)
    frame_result = QLabel("点击计算")
    frame_result.setWordWrap(True)
    frame_l.addRow("估算:", frame_result)
    tabs.addTab(frame_box, "帧时长")

    note = QLabel("纯离线工具 — 不订阅总线、不发送任何帧。"
                  "推荐采样点：经典 87.5% / FD 仲裁 75-80% / FD 数据 60-80%。")
    note.setWordWrap(True)
    layout.addWidget(note)

    def _fill(tree, rows):
        tree.clear()
        for r in rows:
            tree.addTopLevelItem(QTreeWidgetItem([
                str(r["brp"]), str(r["n"]), str(r["tseg1"]), str(r["tseg2"]),
                str(r["sjw"]), "%.3f" % r["baud"], "%.3f" % r["err"],
                "%.1f" % r["sp"]]))

    def _on_calc():
        try:
            clock = clock_spin.value()
            arb_kbps = float(arb_baud.currentText())
            data_kbps = float(data_baud.currentText())
        except ValueError:
            arb_kbps = data_kbps = None
        if not clock or not arb_kbps:
            sin.output.append("位时序计算：参数无效")
            return
        arb_rows = solve_bit_timing(clock, arb_kbps, sp_arb.value(), 8, 25)
        _fill(arb_tree, arb_rows)
        if not arb_rows:
            sin.output.append("位时序计算：仲裁段无 ≤2% 误差方案，请调整时钟/波特率")
        if mode_combo.currentIndex() == 1:
            data_rows = solve_bit_timing(clock, data_kbps, sp_data.value(), 8, 40)
            _fill(data_tree, data_rows)
            if not data_rows:
                sin.output.append("位时序计算：数据段无 ≤2% 误差方案，请调整时钟/波特率")
        else:
            data_tree.clear()
        # TDC：TDCO ≈ 环回延迟 + 1 数据位（以数据段 tq 计）
        if data_tree.topLevelItemCount():
            d0 = None
            for i in range(data_tree.topLevelItemCount()):
                d0 = {"n": int(data_tree.topLevelItem(i).text(1)),
                      "brp": int(data_tree.topLevelItem(i).text(0))}
                break
            if d0:
                tq_ns = d0["brp"] / clock * 1000.0     # 1 tq 时长 ns
                bit_ns = tq_ns * d0["n"]
                tdco_ns = loop_spin.value() + bit_ns
                tdco_tq = tdco_ns / tq_ns
                tdc_result.setText(
                    "数据段 1 tq ≈ %.1f ns · 数据位 ≈ %.0f ns<br>"
                    "建议 TDCO ≈ 环回延迟 + 1 数据位 ≈ %d ns ≈ %.1f tq"
                    % (tq_ns, bit_ns, tdco_ns, tdco_tq))
        else:
            tdc_result.setText("仅 CAN FD 模式需要 TDC")
        # 帧时长
        try:
            dlc = int(dlc_combo.currentText())
        except ValueError:
            dlc = 8
        classic_ms = frame_time_ms(arb_kbps, min(dlc, 8), False, False)
        ext_ms = frame_time_ms(arb_kbps, min(dlc, 8), True, False)
        parts = ["经典标准帧(8B) ≈ %.3f ms（≈%.0f 帧/s @100% 负载）"
                 % (classic_ms, 1000.0 / classic_ms),
                 "经典扩展帧(8B) ≈ %.3f ms（≈%.0f 帧/s @100% 负载）"
                 % (ext_ms, 1000.0 / ext_ms)]
        if mode_combo.currentIndex() == 1:
            fd_ms = frame_time_ms(arb_kbps, dlc, False, True, data_kbps)
            parts.append("FD 帧(DLC %d, 数据段 %.0f kbps) ≈ %.3f ms（≈%.0f 帧/s）"
                         % (dlc, data_kbps, fd_ms, 1000.0 / fd_ms))
        frame_result.setText("\n".join(parts))

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("canBitTiming.open", _on_open_cmd, "工具: 位时序计算")

    calc_btn.clicked.connect(_on_calc)
    _on_calc()

    win.show()
    sin.output.append("位时序计算插件已加载（纯离线，零收发）")


def deactivate():
    sin.output.append("位时序计算插件已停用")
