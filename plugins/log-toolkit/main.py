# -*- coding: utf-8 -*-
"""log-toolkit 插件 — CAN 日志工具箱（TSMaster 日志工具风格）
功能：
- ASC / CSV 日志读取（自动识别格式）
- 按时间裁剪（起止秒）、按 ID 过滤（白名单/黑名单）
- 多文件合并（时间轴重排）、大文件拆分（按帧数/时长）
- 脱敏（ID 掩码 / 载荷字节置零）
- 处理进度条 + 结果统计；ASC/CSV 双格式输出
- 纯离线工具
依赖: pip install PyQt6
"""

import csv
import re
import time

from PyQt6.QtCore import QTimer

import sin

# ASC 行: 0.123456 1  123 Rx   d 8 01 02 ...
_ASC_RE = re.compile(
    r'^\s*([\d.]+)\s+(\d+)\s+([0-9A-Fa-f]+)\s+(Rx|Tx)\s+d\s+(\d+)\s*(.*)$')


def read_asc(path):
    frames = []
    warnings = 0
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                m = _ASC_RE.match(line)
                if m:
                    ts = float(m.group(1))
                    ch = int(m.group(2))
                    cid = int(m.group(3), 16)
                    direction = m.group(4)
                    dlc = int(m.group(5))
                    data_hex = m.group(6).strip()
                    data = bytes.fromhex(data_hex[:dlc * 3].replace(" ", "")) if data_hex else b""
                    frames.append((ts, ch, cid, direction, dlc, data))
                elif line.strip() and not line.startswith(("date", "base", "no", "//")):
                    warnings += 1
    except OSError:
        return None, "无法读取文件"
    return frames, warnings


def read_csv_log(path):
    frames = []
    try:
        with open(path, "r", encoding="utf-8-sig", errors="replace", newline="") as f:
            reader = csv.reader(f)
            header = next(reader, None)
            for row in reader:
                if len(row) < 4:
                    continue
                try:
                    ts = float(row[0])
                    cid = int(row[1], 0) if not row[1].isdigit() else int(row[1])
                    direction = row[2] if row[2] in ("Rx", "Tx") else "Rx"
                    data_hex = row[3].replace(" ", "")
                    data = bytes.fromhex(data_hex) if data_hex else b""
                    frames.append((ts, 1, cid, direction, len(data), data))
                except (ValueError, IndexError):
                    continue
    except OSError:
        return None, "无法读取文件"
    return frames, 0


def write_asc(path, frames):
    with open(path, "w", encoding="utf-8") as f:
        f.write("date %s\n" % time.strftime("%a %b %d %H:%M:%S %Y"))
        f.write("base hex timestamps absolute\n")
        f.write("no internal events logged\n")
        for ts, ch, cid, direction, dlc, data in frames:
            hexs = " ".join("%02X" % b for b in data)
            f.write("%.6f %d  %X %s d %d %s\n" % (ts, ch, cid, direction, dlc, hexs))


def write_csv_log(path, frames):
    with open(path, "w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f)
        w.writerow(["timestamp", "id", "dir", "data"])
        for ts, ch, cid, direction, dlc, data in frames:
            w.writerow(["%.6f" % ts, "0x%X" % cid, direction, data.hex()])


def activate(context):
    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QLineEdit, QFileDialog, QMessageBox, QGroupBox, QFormLayout,
            QRadioButton, QCheckBox, QSpinBox, QProgressBar, QTabWidget,
            QTextEdit, QComboBox
        )
    except ImportError:
        sin.output.append("日志工具箱插件需要 PyQt6: pip install PyQt6")
        return

    loaded = {"frames": [], "names": []}

    win = sin.ui.create_window("CAN 日志工具箱")
    win.resize(900, 580)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    load_btn = QPushButton("加载日志…（可多选）")
    info_label = QLabel("未加载")
    info_label.setStyleSheet("color:#888;")
    top.addWidget(load_btn)
    top.addWidget(info_label, 1)
    layout.addLayout(top)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    # Tab 1: 裁剪+过滤
    trim_tab = QWidget()
    tv = QFormLayout(trim_tab)
    t_start = QLineEdit("0")
    t_end = QLineEdit("999999")
    id_filter = QLineEdit("（留空=不过滤，如 123,0x456）")
    id_mode = QComboBox()
    id_mode.addItems(["白名单（仅保留）", "黑名单（排除）"])
    out_fmt_trim = QComboBox()
    out_fmt_trim.addItems(["ASC", "CSV"])
    tv.addRow("起始时间(s):", t_start)
    tv.addRow("结束时间(s):", t_end)
    tv.addRow("ID 列表:", id_filter)
    tv.addRow("ID 模式:", id_mode)
    tv.addRow("输出格式:", out_fmt_trim)
    trim_btn = QPushButton("执行 裁剪+过滤 并另存…")
    tv.addRow(trim_btn)
    tabs.addTab(trim_tab, "裁剪 / 过滤")

    # Tab 2: 合并
    merge_tab = QWidget()
    mv = QFormLayout(merge_tab)
    out_fmt_merge = QComboBox()
    out_fmt_merge.addItems(["ASC", "CSV"])
    mv.addRow("输出格式:", out_fmt_merge)
    merge_btn = QPushButton("合并全部已加载日志（按时间排序）并另存…")
    mv.addRow(merge_btn)
    tabs.addTab(merge_tab, "合并")

    # Tab 3: 拆分
    split_tab = QWidget()
    sv = QFormLayout(split_tab)
    split_mode = QComboBox()
    split_mode.addItems(["按帧数", "按时长(秒)"])
    split_size = QSpinBox()
    split_size.setRange(100, 10000000)
    split_size.setValue(100000)
    out_fmt_split = QComboBox()
    out_fmt_split.addItems(["ASC", "CSV"])
    sv.addRow("拆分方式:", split_mode)
    sv.addRow("每片大小:", split_size)
    sv.addRow("输出格式:", out_fmt_split)
    split_btn = QPushButton("执行拆分并另存…")
    sv.addRow(split_btn)
    tabs.addTab(split_tab, "拆分")

    # Tab 4: 脱敏
    mask_tab = QWidget()
    kv = QFormLayout(mask_tab)
    mask_id_chk = QCheckBox("ID 掩码（低位字节替换 0xFF）")
    mask_payload_chk = QCheckBox("载荷字节 4-7 置零")
    out_fmt_mask = QComboBox()
    out_fmt_mask.addItems(["ASC", "CSV"])
    kv.addRow(mask_id_chk)
    kv.addRow(mask_payload_chk)
    kv.addRow("输出格式:", out_fmt_mask)
    mask_btn = QPushButton("执行脱敏并另存…")
    kv.addRow(mask_btn)
    tabs.addTab(mask_tab, "脱敏")

    status = QLabel("就绪")
    status.setStyleSheet("font-weight:bold;")
    layout.addWidget(status)

    def _on_load():
        paths, _ = QFileDialog.getOpenFileNames(win, "加载日志", "",
                                                "日志文件 (*.asc *.csv *.log *.txt);;所有文件 (*)")
        if not paths:
            return
        total = 0
        for path in paths:
            frames, err = read_asc(path)
            if frames is None and path.lower().endswith(".csv"):
                frames, err = read_csv_log(path)
            if frames is None:
                QMessageBox.warning(win, "加载失败", "%s: %s" % (path, err))
                continue
            loaded["frames"].extend(frames)
            loaded["names"].append(path.split("\\")[-1])
            total += len(frames)
        if not loaded["frames"]:
            QMessageBox.warning(win, "加载失败", "未解析到任何帧")
            return
        loaded["frames"].sort(key=lambda fr: fr[0])
        t0 = loaded["frames"][0][0]
        t1 = loaded["frames"][-1][0]
        ids = len(set(fr[2] for fr in loaded["frames"]))
        info_label.setText("已加载 %d 文件 · %d 帧 · %d ID · 时间 %.1fs-%.1fs"
                           % (len(loaded["names"]), total, ids, t0, t1))
        info_label.setStyleSheet("color:#2e7d32;")
        status.setText("加载完成")

    def _parse_ids(text):
        ids = set()
        for tok in text.replace("，", ",").split(","):
            tok = tok.strip()
            if not tok or tok.startswith("（"):
                continue
            try:
                ids.add(int(tok, 0))
            except ValueError:
                pass
        return ids

    def _save(frames, fmt):
        ext = "asc" if fmt == 0 else "csv"
        path, _ = QFileDialog.getSaveFileName(win, "另存日志", "processed.%s" % ext,
                                              "日志 (*.%s)" % ext)
        if not path:
            return
        try:
            if fmt == 0:
                write_asc(path, frames)
            else:
                write_csv_log(path, frames)
            QMessageBox.information(win, "保存成功", "已保存 %d 帧:\n%s" % (len(frames), path))
            status.setText("保存 %d 帧 → %s" % (len(frames), path))
        except OSError as e:
            QMessageBox.warning(win, "保存失败", str(e))

    def _on_trim():
        if not loaded["frames"]:
            QMessageBox.information(win, "提示", "请先加载日志")
            return
        try:
            t0 = float(t_start.text() or 0)
            t1 = float(t_end.text() or 1e9)
        except ValueError:
            QMessageBox.warning(win, "格式错误", "时间需为数字")
            return
        ids = _parse_ids(id_filter.text())
        whitelist = id_mode.currentIndex() == 0
        out = []
        for fr in loaded["frames"]:
            if not (t0 <= fr[0] <= t1):
                continue
            if ids:
                keep = (fr[2] in ids) if whitelist else (fr[2] not in ids)
                if not keep:
                    continue
            out.append(fr)
        status.setText("裁剪+过滤: %d → %d 帧" % (len(loaded["frames"]), len(out)))
        _save(out, out_fmt_trim.currentIndex())

    def _on_merge():
        if not loaded["frames"]:
            QMessageBox.information(win, "提示", "请先加载日志")
            return
        frames = sorted(loaded["frames"], key=lambda fr: fr[0])
        status.setText("合并 %d 文件 → %d 帧（已按时间排序）"
                       % (len(loaded["names"]), len(frames)))
        _save(frames, out_fmt_merge.currentIndex())

    def _on_split():
        if not loaded["frames"]:
            QMessageBox.information(win, "提示", "请先加载日志")
            return
        path, _ = QFileDialog.getSaveFileName(win, "拆分输出前缀", "split_part",
                                              "日志 (*.asc)")
        if not path:
            return
        base = path.rsplit(".", 1)[0]
        fmt = out_fmt_split.currentIndex()
        by_count = split_mode.currentIndex() == 0
        size = split_size.value()
        frames = loaded["frames"]
        parts = []
        if by_count:
            for i in range(0, len(frames), size):
                parts.append(frames[i:i + size])
        else:
            t_start = frames[0][0]
            cur = []
            for fr in frames:
                if fr[0] - t_start > size and cur:
                    parts.append(cur)
                    cur = []
                    t_start = fr[0]
                cur.append(fr)
            if cur:
                parts.append(cur)
        try:
            for i, part in enumerate(parts):
                out_path = "%s_%03d.%s" % (base, i + 1, "asc" if fmt == 0 else "csv")
                if fmt == 0:
                    write_asc(out_path, part)
                else:
                    write_csv_log(out_path, part)
            QMessageBox.information(win, "拆分完成", "共 %d 片 → %s_001..%03d"
                                    % (len(parts), base, len(parts)))
            status.setText("拆分: %d 帧 → %d 片" % (len(frames), len(parts)))
        except OSError as e:
            QMessageBox.warning(win, "拆分失败", str(e))

    def _on_mask():
        if not loaded["frames"]:
            QMessageBox.information(win, "提示", "请先加载日志")
            return
        if not (mask_id_chk.isChecked() or mask_payload_chk.isChecked()):
            QMessageBox.information(win, "提示", "请至少选择一种脱敏方式")
            return
        out = []
        for ts, ch, cid, direction, dlc, data in loaded["frames"]:
            if mask_id_chk.isChecked():
                cid = (cid & 0x700) | 0x0FF if cid <= 0x7FF else (cid & 0x1F00) | 0xFF
            if mask_payload_chk.isChecked() and len(data) > 3:
                data = data[:3] + b"\x00" * (len(data) - 3)
            out.append((ts, ch, cid, direction, dlc, data))
        status.setText("脱敏: %d 帧" % len(out))
        _save(out, out_fmt_mask.currentIndex())

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("logToolkit.open", _on_open_cmd, "日志: 日志工具箱")

    load_btn.clicked.connect(_on_load)
    trim_btn.clicked.connect(_on_trim)
    merge_btn.clicked.connect(_on_merge)
    split_btn.clicked.connect(_on_split)
    mask_btn.clicked.connect(_on_mask)

    win.show()
    sin.output.append("日志工具箱插件已加载（裁剪/过滤/合并/拆分/脱敏，离线工具）")


def deactivate():
    sin.output.append("日志工具箱插件已停用")
