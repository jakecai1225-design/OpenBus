"""blf-converter 插件 — 报文日志格式转换工具（G9，替代原 C++ BlfAsConverter）

功能：
- BLF / ASC / CSV / PCAP / TRC 之间相互转换
- 转换由宿主 C++ 引擎在后台线程执行（sin.files API），不阻塞 UI
- 支持拖拽源文件、进度显示、取消、日志

依赖: pip install PyQt6
"""

import os
import time

import sin


# 目标格式: (显示名, 格式标识, 扩展名, 可写)
FORMATS = [
    ("ASC (.asc)  — Vector 文本", "asc", ".asc", True),
    ("CSV (.csv)  — 通用文本", "csv", ".csv", True),
    ("PCAP (.pcap) — 网络捕获", "pcap", ".pcap", True),
    ("TRC (.trc)  — Vector 旧文本", "trc", ".trc", True),
    ("BLF (.blf)  — Vector 二进制（暂不支持写出）", "blf", ".blf", False),
]

_job_id = None       # 当前转换 jobId
_time_start = 0.0


def _fmt_by_key(key):
    for f in FORMATS:
        if f[1] == key:
            return f
    return FORMATS[0]


def activate(context):
    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QFormLayout,
            QLabel, QLineEdit, QPushButton, QComboBox,
            QProgressBar, QTextEdit, QFileDialog, QMessageBox
        )
    except ImportError:
        sin.output.append("格式转换插件需要 PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("格式转换 — BLF / ASC / CSV / PCAP / TRC")
    win.resize(640, 520)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    title = QLabel("报文日志格式转换")
    title.setStyleSheet("font-size: 14px; font-weight: bold; padding: 2px;")
    layout.addWidget(title)
    desc = QLabel("转换由主程序 C++ 引擎在后台线程执行，源格式按扩展名自动识别。")
    desc.setStyleSheet("color: #888; font-size: 11px;")
    desc.setWordWrap(True)
    layout.addWidget(desc)

    # ---- 表单 ----
    form = QFormLayout()
    form.setContentsMargins(0, 6, 0, 6)

    src_edit = QLineEdit()
    src_edit.setPlaceholderText("拖拽文件到窗口，或点击浏览...")
    src_browse = QPushButton("浏览...")
    src_row = QHBoxLayout()
    src_row.addWidget(src_edit, 1)
    src_row.addWidget(src_browse)
    form.addRow("源文件:", src_row)

    fmt_combo = QComboBox()
    for name, _, _, _writable in FORMATS:
        fmt_combo.addItem(name)
    fmt_combo.setCurrentIndex(0)
    form.addRow("目标格式:", fmt_combo)

    tgt_edit = QLineEdit()
    tgt_edit.setPlaceholderText("缺省在源文件同目录生成（改扩展名）")
    tgt_browse = QPushButton("浏览...")
    tgt_row = QHBoxLayout()
    tgt_row.addWidget(tgt_edit, 1)
    tgt_row.addWidget(tgt_browse)
    form.addRow("目标文件:", tgt_row)

    layout.addLayout(form)

    # ---- 按钮 + 进度 ----
    btn_row = QHBoxLayout()
    btn_row.addStretch()
    convert_btn = QPushButton("开始转换")
    convert_btn.setMinimumWidth(110)
    cancel_btn = QPushButton("取消")
    cancel_btn.setEnabled(False)
    btn_row.addWidget(convert_btn)
    btn_row.addWidget(cancel_btn)
    layout.addLayout(btn_row)

    progress = QProgressBar()
    progress.setValue(0)
    layout.addWidget(progress)

    status = QLabel("就绪")
    status.setStyleSheet("color: #888;")
    layout.addWidget(status)

    log_view = QTextEdit()
    log_view.setReadOnly(True)
    layout.addWidget(log_view, 1)

    # ---- 状态机 ----
    def set_running(running):
        convert_btn.setEnabled(not running)
        cancel_btn.setEnabled(running)
        src_edit.setEnabled(not running)
        tgt_edit.setEnabled(not running)
        fmt_combo.setEnabled(not running)
        src_browse.setEnabled(not running)
        tgt_browse.setEnabled(not running)
        if not running:
            progress.setValue(0)

    def append_log(msg):
        log_view.append(msg)

    # ---- 回调（宿主通知驱动，主线程执行）----
    def on_progress(percent):
        progress.setValue(percent)
        status.setText(f"转换中... {percent}%")

    def on_finished(ok, frame_count, error):
        global _job_id
        _job_id = None
        set_running(False)
        elapsed = time.time() - _time_start
        if ok:
            progress.setValue(100)
            status.setText(f"完成 — {frame_count} 帧，耗时 {elapsed:.1f}s")
            append_log(f"<span style='color:green'>[成功]</span> {frame_count} 帧，"
                       f"耗时 {elapsed:.1f}s → {tgt_edit.text()}")
        else:
            status.setText("失败")
            append_log(f"<span style='color:red'>[失败]</span> {error or '未知错误'}")

    def suggest_target(src_path, fmt_entry):
        root, _ext = os.path.splitext(src_path)
        return root + fmt_entry[2]

    # ---- 交互 ----
    def on_src_browse():
        path, _ = QFileDialog.getOpenFileName(win, "选择源文件",
            "", "报文日志 (*.blf *.asc *.csv *.pcap *.pcapng *.trc);;所有文件 (*.*)")
        if path:
            src_edit.setText(path)
            if not tgt_edit.text():
                tgt_edit.setText(suggest_target(path, FORMATS[fmt_combo.currentIndex()]))

    def on_tgt_browse():
        fmt_entry = FORMATS[fmt_combo.currentIndex()]
        path, _ = QFileDialog.getSaveFileName(win, "目标文件", tgt_edit.text() or "",
                                              f"*{fmt_entry[2]}")
        if path:
            tgt_edit.setText(path)

    def on_fmt_changed(index):
        if src_edit.text() and not tgt_edit.text():
            tgt_edit.setText(suggest_target(src_edit.text(), FORMATS[index]))

    def on_convert():
        global _job_id, _time_start
        src = src_edit.text().strip()
        tgt = tgt_edit.text().strip()
        fmt_entry = FORMATS[fmt_combo.currentIndex()]

        if not src or not os.path.exists(src):
            QMessageBox.warning(win, "提示", "请选择存在的源文件")
            return
        if not fmt_entry[3]:
            QMessageBox.warning(win, "提示", f"{fmt_entry[0]} 暂不支持写出，请选择其他格式")
            return
        if not tgt:
            tgt = suggest_target(src, fmt_entry)
            tgt_edit.setText(tgt)
        if os.path.abspath(src) == os.path.abspath(tgt):
            QMessageBox.warning(win, "提示", "源文件与目标文件相同")
            return

        log_view.clear()
        append_log(f"[开始] {src} → {tgt}（{fmt_entry[1].upper()}）")
        status.setText("已提交转换任务...")
        set_running(True)
        _time_start = time.time()
        _job_id = sin.files.convert(src, tgt, fmt_entry[1],
                                    on_progress=on_progress,
                                    on_finished=on_finished)
        if _job_id is None:
            set_running(False)
            status.setText("提交失败")

    def on_cancel():
        if _job_id is not None:
            sin.files.cancel(_job_id)
            status.setText("正在取消...")

    # 命令：重新打开/聚焦窗口
    def on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("blfConverter.open", on_open_cmd, "工具: 格式转换")

    src_browse.clicked.connect(on_src_browse)
    tgt_browse.clicked.connect(on_tgt_browse)
    convert_btn.clicked.connect(on_convert)
    cancel_btn.clicked.connect(on_cancel)
    fmt_combo.currentIndexChanged.connect(on_fmt_changed)

    # 拖拽
    def on_drag_enter(event):
        if event.mimeData().hasUrls():
            event.acceptProposedAction()

    def on_drop(event):
        urls = event.mimeData().urls()
        if urls:
            path = urls[0].toLocalFile()
            if path.lower().endswith((".blf", ".asc", ".csv", ".pcap", ".pcapng", ".trc")):
                src_edit.setText(path)
                if not tgt_edit.text():
                    tgt_edit.setText(suggest_target(path, FORMATS[fmt_combo.currentIndex()]))

    win.dragEnterEvent = on_drag_enter
    win.dropEvent = on_drop

    win.show()
    sin.output.append("格式转换插件已加载")


def deactivate():
    global _job_id
    if _job_id is not None:
        try:
            sin.files.cancel(_job_id)
        except Exception:
            pass
        _job_id = None
    sin.output.append("格式转换插件已停用")
