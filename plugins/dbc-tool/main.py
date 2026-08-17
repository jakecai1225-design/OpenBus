"""dbc-tool 插件 — DBC 查看编辑工具（G9，替代原 C++ DbcToolView）

功能：
- 独立打开任意 .dbc 文件（宿主侧会话，不影响当前工程已加载的 DBC）
- 左侧树形展示：报文 → 信号 层级（支持名称/ID 搜索过滤）
- 右侧信号属性表格：可编辑 startBit / bitLength / factor / offset / min / max / unit / comment
- 保存 / 另存为（宿主 C++ 引擎写回 DBC）

依赖: pip install PyQt6
"""

import os

import sin


# 会话状态
_db_id = None            # 当前宿主 DBC 会话
_file_path = ""          # 当前文件路径（保存默认路径）
_messages = []           # 报文列表缓存 [{"id","name","dlc","sender","comment","cycleTime","signalCount"}]
_current_msg_id = None   # 当前选中报文
_signals_cache = {}      # msgId → 信号列表
_dirty = False           # 有未保存修改

MUX_NAMES = {0: "", 1: "M", 2: "m"}   # 预留：多路复用标记


def activate(context):
    global _db_id, _file_path, _messages, _current_msg_id, _signals_cache, _dirty

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QLineEdit, QTreeWidget, QTreeWidgetItem, QTableWidget,
            QTableWidgetItem, QFileDialog, QMessageBox, QSplitter,
            QHeaderView, QAbstractItemView
        )
        from PyQt6.QtCore import Qt
    except ImportError:
        sin.output.append("DBC 工具插件需要 PyQt6: pip install PyQt6")
        return

    _db_id = None
    _file_path = ""
    _messages = []
    _current_msg_id = None
    _signals_cache = {}
    _dirty = False

    win = sin.ui.create_window("DBC 工具 — 查看 / 编辑")
    win.resize(960, 640)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    # ---- 工具栏 ----
    bar = QHBoxLayout()
    open_btn = QPushButton("打开 DBC")
    save_btn = QPushButton("保存")
    save_as_btn = QPushButton("另存为...")
    save_btn.setEnabled(False)
    save_as_btn.setEnabled(False)
    search_edit = QLineEdit()
    search_edit.setPlaceholderText("搜索报文名 / 信号名 / ID...")
    search_edit.setMaximumWidth(260)
    path_label = QLabel("未加载文件")
    path_label.setStyleSheet("color: #888;")
    bar.addWidget(open_btn)
    bar.addWidget(save_btn)
    bar.addWidget(save_as_btn)
    bar.addWidget(search_edit)
    bar.addWidget(path_label, 1)
    layout.addLayout(bar)

    # ---- 主体：左树 + 右表 ----
    splitter = QSplitter(Qt.Orientation.Horizontal)

    tree = QTreeWidget()
    tree.setHeaderLabels(["报文 / 信号", "ID", "值"])
    tree.header().setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
    splitter.addWidget(tree)

    right = QWidget()
    right_layout = QVBoxLayout(right)
    right_layout.setContentsMargins(0, 0, 0, 0)

    msg_title = QLabel("信号属性")
    msg_title.setStyleSheet("font-weight: bold;")
    right_layout.addWidget(msg_title)

    prop_table = QTableWidget()
    prop_table.setColumnCount(2)
    prop_table.setHorizontalHeaderLabels(["属性", "值"])
    prop_table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
    prop_table.setSelectionMode(QAbstractItemView.SelectionMode.NoSelection)
    right_layout.addWidget(prop_table, 1)

    summary = QLabel("")
    summary.setStyleSheet("color: #888;")
    right_layout.addWidget(summary)

    splitter.addWidget(right)
    splitter.setSizes([420, 540])
    layout.addWidget(splitter, 1)

    # ---- 更新函数 ----
    def refresh_tree(filter_text=""):
        tree.clear()
        f = filter_text.strip().lower()
        shown = 0
        for msg in _messages:
            id_hex = f"0x{msg['id']:X}"
            sig_names = [s["name"] for s in _signals_cache.get(msg["id"], [])]
            msg_hit = (not f or f in msg["name"].lower()
                       or f in id_hex.lower()
                       or f in str(msg["id"]))
            sig_hits = [n for n in sig_names if f in n.lower()] if f else []
            if msg_hit or sig_hits:
                shown += 1
                top = QTreeWidgetItem([f"{msg['name']}  ({msg.get('signalCount', len(sig_names))} 信号)",
                                       id_hex, f"{msg.get('dlc', 8)}B"])
                top.setData(0, Qt.ItemDataRole.UserRole, (msg["id"], None))
                display_sigs = sig_names if not (msg_hit and f) else sig_hits
                if not display_sigs:
                    display_sigs = sig_names
                for n in display_sigs:
                    child = QTreeWidgetItem([n, "", ""])
                    child.setData(0, Qt.ItemDataRole.UserRole, (msg["id"], n))
                    top.addChild(child)
                tree.addTopLevelItem(top)
        summary.setText(f"显示 {shown}/{len(_messages)} 个报文")

    def show_message_overview(msg):
        """报文级属性表（点击报文项时展示）"""
        prop_table.setRowCount(0)
        if msg is None:
            msg_title.setText("属性")
            return
        msg_title.setText(f"报文 — {msg['name']} (0x{msg['id']:X}, {msg['dlc']}B, "
                          f"发送: {msg.get('sender','') or '-'})")
        rows = [
            ("报文名", msg['name']),
            ("CAN ID", f"0x{msg['id']:X} ({msg['id']})"),
            ("DLC", str(msg.get('dlc', 8))),
            ("发送节点", msg.get('sender', '') or '-'),
            ("周期 (ms)", str(msg.get('cycleTime', 0)) or '-'),
            ("信号数", str(msg.get('signalCount', 0))),
            ("注释", msg.get('comment', '') or ''),
        ]
        prop_table.setRowCount(len(rows))
        for r, (label, val) in enumerate(rows):
            k_item = QTableWidgetItem(label)
            k_item.setFlags(Qt.ItemFlag.ItemIsEnabled)
            prop_table.setItem(r, 0, k_item)
            v_item = QTableWidgetItem(str(val))
            v_item.setFlags(Qt.ItemFlag.ItemIsEnabled)
            prop_table.setItem(r, 1, v_item)
    
    def show_signal_props(msg, sig):
        """信号属性表（点击信号子项时展示，可编辑字段走 dbc.updateSignal）"""
        prop_table.setRowCount(0)
        if sig is None:
            msg_title.setText("属性")
            return
        msg_title.setText(f"信号 — {sig['name']} @ {msg['name']} (0x{msg['id']:X})")
        rows = [
            # (属性名, 取值, 可编辑)
            ("信号名", sig['name'], False),
            ("起始位", sig.get('startBit', 0), True),
            ("位长度", sig.get('bitLength', 1), True),
            ("字节序", "Intel (小端)" if sig.get('littleEndian', True) else "Motorola (大端)", False),
            ("有符号", "是" if sig.get('isSigned') else "否", False),
            ("系数 factor", sig.get('factor', 1.0), True),
            ("偏移 offset", sig.get('offset', 0.0), True),
            ("最小值", sig.get('min', 0.0), True),
            ("最大值", sig.get('max', 0.0), True),
            ("单位", sig.get('unit', ''), True),
            ("接收节点", sig.get('receiver', '') or '-', False),
            ("注释", sig.get('comment', '') or '', True),
        ]
        prop_table.setRowCount(len(rows))
        for r, (label, val, editable) in enumerate(rows):
            k_item = QTableWidgetItem(label)
            k_item.setFlags(Qt.ItemFlag.ItemIsEnabled)
            prop_table.setItem(r, 0, k_item)
            if isinstance(val, float) and val == int(val):
                val = int(val)
            v_item = QTableWidgetItem(str(val))
            if not editable:
                v_item.setFlags(Qt.ItemFlag.ItemIsEnabled)
            prop_table.setItem(r, 1, v_item)

    def close_session():
        global _db_id, _messages, _signals_cache, _current_msg_id, _dirty
        if _db_id is not None:
            try:
                sin.dbc.close(_db_id)
            except Exception:
                pass
        _db_id = None
        _messages = []
        _signals_cache = {}
        _current_msg_id = None
        _dirty = False

    def on_open():
        global _db_id, _file_path, _messages, _signals_cache, _dirty
        path, _ = QFileDialog.getOpenFileName(win, "打开 DBC 文件", "", "DBC 文件 (*.dbc);;所有文件 (*.*)")
        if not path:
            return
        close_session()
        try:
            info = sin.dbc.open(path)
        except RuntimeError as e:
            QMessageBox.warning(win, "打开失败", str(e))
            return
        _db_id = info["dbId"]
        _file_path = path
        _messages = sin.dbc.messages(_db_id) or []
        # 拉取全部报文的信号（供树展示与过滤）
        for msg in _messages:
            sigs = sin.dbc.signals(_db_id, msg["id"])
            _signals_cache[msg["id"]] = sigs or []
        path_label.setText(f"已加载: {path} — {info.get('messageCount', len(_messages))} 报文, "
                           f"{info.get('nodeCount', 0)} 节点")
        save_btn.setEnabled(True)
        save_as_btn.setEnabled(True)
        win.setWindowTitle(f"DBC 工具 — {os.path.basename(path)}")
        search_edit.clear()
        refresh_tree()
        show_message_overview(None)

    def on_tree_clicked(item, _col):
        global _current_msg_id
        data = item.data(0, Qt.ItemDataRole.UserRole)
        if data is None:
            return
        msg_id, sig_name = data
        for m in _messages:
            if m["id"] == msg_id:
                _current_msg_id = msg_id
                if sig_name is None:
                    show_message_overview(m)
                else:
                    sig = next((s for s in _signals_cache.get(msg_id, [])
                                if s["name"] == sig_name), None)
                    show_signal_props(m, sig)
                return

    def on_prop_changed(item):
        global _dirty
        if _db_id is None or _current_msg_id is None:
            return
        row = item.row()
        label = prop_table.item(row, 0).text()
        text = item.text().strip()
        if not text:
            return
        sigs = _signals_cache.get(_current_msg_id) or []
        if not sigs:
            return
        sig = sigs[0]
        fields = {}
        try:
            if label == "起始位":
                fields["startBit"] = int(text)
            elif label == "位长度":
                fields["bitLength"] = int(text)
            elif label == "系数 factor":
                fields["factor"] = float(text)
            elif label == "偏移 offset":
                fields["offset"] = float(text)
            elif label == "最小值":
                fields["min"] = float(text)
            elif label == "最大值":
                fields["max"] = float(text)
            elif label == "单位":
                fields["unit"] = text
            elif label == "注释":
                fields["comment"] = text
            else:
                return
        except ValueError:
            QMessageBox.warning(win, "输入错误", f"“{label}”需要数值")
            msg = next((m for m in _messages if m["id"] == _current_msg_id), None)
            if msg is not None:
                show_signal_props(msg, sig)
            return

        ok, err = sin.dbc.update_signal(_db_id, _current_msg_id, sig["name"], **fields)
        if ok:
            sig.update(fields)
            _dirty = True
            sin.output.append(f"DBC 工具: 已修改 {sig['name']}.{label} = {text}")
        else:
            QMessageBox.warning(win, "修改失败", err or "未知错误")

    def confirm_discard():
        global _dirty
        if _dirty and _db_id is not None:
            ret = QMessageBox.question(win, "未保存的修改",
                                       "当前 DBC 有未保存的修改，继续将丢弃。是否继续？")
            if ret != QMessageBox.StandardButton.Yes:
                return False
        return True

    def on_save():
        global _dirty
        if _db_id is None:
            return
        if not _file_path:
            on_save_as()
            return
        ok, err = sin.dbc.save(_db_id, _file_path)
        if ok:
            _dirty = False
            sin.output.append(f"DBC 工具: 已保存 {_file_path}")
            QMessageBox.information(win, "保存成功", f"已保存到:\n{_file_path}")
        else:
            QMessageBox.warning(win, "保存失败", err or "未知错误")

    def on_save_as():
        global _file_path, _dirty
        if _db_id is None:
            return
        path, _ = QFileDialog.getSaveFileName(win, "另存为 DBC", _file_path or "", "DBC 文件 (*.dbc)")
        if not path:
            return
        ok, err = sin.dbc.save(_db_id, path)
        if ok:
            _file_path = path
            _dirty = False
            path_label.setText(f"已加载: {path}")
            sin.output.append(f"DBC 工具: 已另存 {path}")
        else:
            QMessageBox.warning(win, "另存失败", err or "未知错误")

    def on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("dbcTool.open", on_open_cmd, "工具: DBC 工具")

    open_btn.clicked.connect(lambda: on_open() if confirm_discard() else None)
    save_btn.clicked.connect(on_save)
    save_as_btn.clicked.connect(on_save_as)
    search_edit.textChanged.connect(refresh_tree)
    tree.itemClicked.connect(on_tree_clicked)
    prop_table.itemChanged.connect(on_prop_changed)

    # 拖拽打开 DBC
    def on_drag_enter(event):
        if event.mimeData().hasUrls():
            event.acceptProposedAction()

    def on_drop(event):
        urls = event.mimeData().urls()
        if urls and urls[0].toLocalFile().lower().endswith(".dbc"):
            path = urls[0].toLocalFile()
            if confirm_discard():
                # 复用 on_open 逻辑：手动注入路径
                nonlocal_msg = path
                global _db_id, _file_path, _messages, _signals_cache
                close_session()
                try:
                    info = sin.dbc.open(nonlocal_msg)
                except RuntimeError as e:
                    QMessageBox.warning(win, "打开失败", str(e))
                    return
                _db_id = info["dbId"]
                _file_path = nonlocal_msg
                _messages = sin.dbc.messages(_db_id) or []
                for msg in _messages:
                    _signals_cache[msg["id"]] = sin.dbc.signals(_db_id, msg["id"]) or []
                path_label.setText(f"已加载: {nonlocal_msg}")
                search_edit.clear()
                refresh_tree()
                show_message_overview(None)

    win.dragEnterEvent = on_drag_enter
    win.dropEvent = on_drop

    refresh_tree()
    win.show()
    sin.output.append("DBC 工具插件已加载")


def deactivate():
    if _db_id is not None:
        try:
            sin.dbc.close(_db_id)
        except Exception:
            pass
    sin.output.append("DBC 工具插件已停用")
