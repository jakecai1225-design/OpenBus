# -*- coding: utf-8 -*-
"""BSW — schema-driven Classic Platform module configurator (no codegen).

DaVinci-slice UX: module catalog (nav) · container tree (nav) · large property
sheet + always-visible spec (primary work surface). Findings live in OUTPUT.
"""

from __future__ import annotations

from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QCheckBox,
    QComboBox,
    QHeaderView,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMenu,
    QMessageBox,
    QPlainTextEdit,
    QSplitter,
    QTableWidget,
    QTableWidgetItem,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import arxml_bsw, arxml_ecuc_schema, arxmlparse, suite_chrome, vscode_theme
from pages import _ui


def build(shell, document, log_fn, *, nav_in_sidebar: bool = False) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    # Counts go to status bar / Side Bar tooltip — no top status strip.
    status = _ui.quiet_label("")
    status.setVisible(False)

    split = QSplitter(Qt.Orientation.Horizontal)
    split.setHandleWidth(1)
    split.setChildrenCollapsible(False)

    # Module catalog lives in Config Side Bar when nav_in_sidebar=True.
    left = None
    filter_ed = None
    mod_tree = None
    if not nav_in_sidebar:
        left = QWidget()
        left.setObjectName("BswModulePane")
        left.setMinimumWidth(180)
        left.setMaximumWidth(260)
        left_l = QVBoxLayout(left)
        left_l.setContentsMargins(0, 0, 0, 0)
        left_l.setSpacing(0)
        filter_ed = QLineEdit()
        filter_ed.setPlaceholderText("Filter modules…")
        filter_ed.setClearButtonEnabled(True)
        filter_ed.setFixedHeight(_ui.CTRL_H)
        filter_ed.setToolTip("Filter BSW catalog by name or group")
        left_l.addWidget(filter_ed)
        mod_tree = QTreeWidget()
        mod_tree.setHeaderLabels(["Module", "Status"])
        _ui.style_tree(mod_tree)
        mod_tree.setUniformRowHeights(True)
        mod_tree.header().setStretchLastSection(False)
        mod_tree.header().setSectionResizeMode(
            0, QHeaderView.ResizeMode.Stretch)
        mod_tree.header().setSectionResizeMode(
            1, QHeaderView.ResizeMode.ResizeToContents)
        left_l.addWidget(mod_tree, 1)
        split.addWidget(left)

    # --- Mid: container tree only (no param leaves — params in sheet) ---
    mid = QWidget()
    mid.setMinimumWidth(220)
    if nav_in_sidebar:
        mid.setMaximumWidth(380)
    else:
        mid.setMaximumWidth(420)
    mid_l = QVBoxLayout(mid)
    mid_l.setContentsMargins(0, 0, 0, 0)
    mid_l.setSpacing(0)
    tree_tools = QWidget()
    tree_tools.setObjectName("SuiteContent")
    tree_row = QHBoxLayout(tree_tools)
    tree_row.setContentsMargins(4, 2, 4, 2)
    tree_row.setSpacing(4)
    add_btn = _ui.ghost_btn("Add…", "Add container instance (schema)", "add")
    rem_btn = _ui.ghost_btn("Remove", "Remove selected container", "delete")
    dup_btn = _ui.ghost_btn("Duplicate", "Duplicate selected container", "copy")
    tree_row.addWidget(add_btn)
    tree_row.addWidget(rem_btn)
    tree_row.addWidget(dup_btn)
    tree_row.addStretch(1)
    mid_l.addWidget(tree_tools)
    cfg_tree = QTreeWidget()
    cfg_tree.setHeaderLabels(["Container", "Type"])
    _ui.style_tree(cfg_tree)
    cfg_tree.header().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
    cfg_tree.header().setSectionResizeMode(
        1, QHeaderView.ResizeMode.ResizeToContents)
    mid_l.addWidget(cfg_tree, 1)
    split.addWidget(mid)

    # --- Right PRIMARY: property sheet + always-visible description ---
    main = QWidget()
    main.setObjectName("BswPropertyPane")
    main.setMinimumWidth(480)
    main_l = QVBoxLayout(main)
    main_l.setContentsMargins(8, 4, 8, 4)
    main_l.setSpacing(4)

    head = QWidget()
    head_l = QHBoxLayout(head)
    head_l.setContentsMargins(0, 0, 0, 0)
    head_l.setSpacing(8)
    head_title = QLabel("Properties")
    head_title.setObjectName("SuiteEditorTitle")
    head_l.addWidget(head_title)
    head_l.addWidget(_ui.quiet_label("Short name"))
    name_ed = QLineEdit()
    name_ed.setFixedHeight(_ui.CTRL_H)
    name_ed.setMinimumWidth(160)
    name_ed.setPlaceholderText("Container / module short name")
    head_l.addWidget(name_ed, 1)
    type_lab = _ui.quiet_label("")
    head_l.addWidget(type_lab)
    apply_btn = _ui.primary_btn(
        "Apply name", "Rename selected container", "apply")
    head_l.addWidget(apply_btn)
    main_l.addWidget(head)

    prop_table = QTableWidget(0, 4)
    prop_table.setObjectName("SuiteMatrix")
    prop_table.setHorizontalHeaderLabels(
        ["Parameter", "Value", "Kind", "Definition"])
    prop_table.setAlternatingRowColors(False)
    prop_table.setSelectionBehavior(
        QAbstractItemView.SelectionBehavior.SelectRows)
    prop_table.setSelectionMode(
        QAbstractItemView.SelectionMode.SingleSelection)
    prop_table.setEditTriggers(
        QAbstractItemView.EditTrigger.NoEditTriggers)
    prop_table.verticalHeader().setVisible(False)
    prop_table.verticalHeader().setDefaultSectionSize(26)
    hdr = prop_table.horizontalHeader()
    hdr.setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
    hdr.setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
    hdr.setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
    hdr.setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
    main_l.addWidget(prop_table, 3)

    tip = QPlainTextEdit()
    tip.setReadOnly(True)
    tip.setObjectName("BswSpecPane")
    tip.setMinimumHeight(72)
    tip.setMaximumHeight(140)
    tip.setPlainText(
        "Select a container. Parameters appear above; row selection shows "
        "EcucDefs detail here. Findings: OUTPUT → Findings. No codegen.")
    main_l.addWidget(tip, 0)

    split.addWidget(main)
    if nav_in_sidebar:
        split.setStretchFactor(0, 0)
        split.setStretchFactor(1, 1)
        split.setSizes([280, 1000])
    else:
        split.setStretchFactor(0, 0)
        split.setStretchFactor(1, 0)
        split.setStretchFactor(2, 1)
        split.setSizes([220, 280, 900])
    layout.addWidget(split, 1)

    def _apply_split_sizes():
        total = max(split.width(), 900)
        if nav_in_sidebar:
            mid_w = min(320, max(220, total // 5))
            main_w = max(total - mid_w, 520)
            split.setSizes([mid_w, main_w])
        else:
            left_w = min(240, max(180, total // 7))
            mid_w = min(340, max(240, total // 5))
            main_w = max(total - left_w - mid_w, 520)
            split.setSizes([left_w, mid_w, main_w])

    QTimer.singleShot(0, _apply_split_sizes)
    root._apply_bsw_split = _apply_split_sizes

    find_tree = getattr(shell, "_findings_tree", None)
    find_summary = getattr(shell, "_findings_summary", None)
    fix_btn = getattr(shell, "_findings_fix_btn", None)
    recheck_btn = getattr(shell, "_findings_recheck_btn", None)

    selected = {"module": "", "path": (), "kind": "", "pdef": None}
    prop_rows: list = []  # [{param_idx|None, pdef, name, kind}]
    live_cache: list = []
    _filter_text = {"q": ""}
    _guard = {"depth": 0}
    _live_full = {"need": False}
    _prop_guard = {"depth": 0}

    def os_path_exists(doc, name: str) -> bool:
        if not doc.manifest:
            return False
        import os
        p = doc.manifest.abs_module(name)
        return bool(p) and os.path.isfile(p)

    def _set_spec(text: str):
        tip.setPlainText(text or "")

    def _fill_modules():
        n_sch = len(arxml_ecuc_schema.list_schema_modules())
        msg = "%d modules · %d schemas · active %s" % (
            len(arxml_bsw.module_names()), n_sch, document.active_bsw or "—")
        status.setText(msg)
        status.setToolTip(msg)
        try:
            from _shared import plugin_shell
            plugin_shell.set_status(shell, msg, 0)
        except Exception:
            pass
        if mod_tree is None:
            return
        mod_tree.blockSignals(True)
        mod_tree.clear()
        q = (_filter_text["q"] or "").strip().lower()
        groups = arxml_bsw.catalog_by_group()
        for group, rows in groups.items():
            gitem = QTreeWidgetItem([group, ""])
            gitem.setFlags(gitem.flags() & ~Qt.ItemFlag.ItemIsSelectable)
            visible_children = 0
            for name, summary in rows:
                if q and q not in name.lower() and q not in group.lower():
                    continue
                it = QTreeWidgetItem([name, group])
                it.setToolTip(0, summary)
                it.setData(0, Qt.ItemDataRole.UserRole, name)
                exists = name in document.bsw
                if document.has_project() and os_path_exists(document, name):
                    it.setText(1, "on disk")
                elif exists:
                    it.setText(1, "loaded")
                else:
                    it.setText(1, "stub")
                gitem.addChild(it)
                visible_children += 1
            if visible_children:
                mod_tree.addTopLevelItem(gitem)
        mod_tree.expandAll()
        mod_tree.blockSignals(False)

    def _add_containers(parent, containers, path_prefix):
        for i, cont in enumerate(containers):
            path = path_prefix + (i,)
            ctype = arxml_ecuc_schema.container_type_of(cont)
            item = QTreeWidgetItem([cont.name, ctype])
            item.setData(0, Qt.ItemDataRole.UserRole, ("cont", path))
            item.setToolTip(0, cont.definition or ctype)
            n_params = len(cont.params or [])
            item.setToolTip(
                1, "%d parameters · %d children" % (
                    n_params, len(cont.children or [])))
            if parent is None:
                cfg_tree.addTopLevelItem(item)
            else:
                parent.addChild(item)
            if cont.children:
                _add_containers(item, cont.children, path)

    def _fill_config(*, reload_props: bool = True):
        cfg_tree.blockSignals(True)
        cfg_tree.clear()
        name = document.active_bsw
        model = document.bsw.get(name)
        if not model:
            cfg_tree.blockSignals(False)
            if reload_props:
                _clear_prop_sheet()
            return
        for mod in model.modules:
            item = QTreeWidgetItem([mod.name, "Module"])
            item.setData(0, Qt.ItemDataRole.UserRole, ("mod", ()))
            cfg_tree.addTopLevelItem(item)
            _add_containers(item, mod.containers, ())
        cfg_tree.expandToDepth(2)
        cfg_tree.blockSignals(False)
        if reload_props and selected.get("kind") in ("cont", "mod"):
            _load_prop_sheet()

    def _active_schema():
        name = document.active_bsw
        return arxml_ecuc_schema.load_schema(name) if name else None

    def _select_cfg_by_name(short: str) -> bool:
        if not short:
            return False

        def walk(item):
            if item.text(0) == short:
                cfg_tree.setCurrentItem(item)
                return True
            for i in range(item.childCount()):
                if walk(item.child(i)):
                    return True
            return False

        for i in range(cfg_tree.topLevelItemCount()):
            if walk(cfg_tree.topLevelItem(i)):
                return True
        return False

    def _resolve(model: arxmlparse.EcucModel, path):
        if not model.modules:
            return None, None
        mod = model.modules[0]
        containers = mod.containers
        cont = None
        param = None
        idxs = path
        i = 0
        while i < len(idxs):
            if idxs[i] == "p":
                if cont and i + 1 < len(idxs):
                    j = idxs[i + 1]
                    if 0 <= j < len(cont.params):
                        param = cont.params[j]
                break
            idx = idxs[i]
            if not isinstance(idx, int) or idx >= len(containers):
                return cont, None
            cont = containers[idx]
            containers = cont.children
            i += 1
        return cont, param

    def _clear_prop_sheet():
        nonlocal prop_rows
        _prop_guard["depth"] += 1
        try:
            prop_table.setRowCount(0)
            prop_rows = []
            name_ed.clear()
            type_lab.setText("")
            head_title.setText("Properties")
        finally:
            _prop_guard["depth"] -= 1

    def _pdef_for(schema, cont, pname: str):
        if not schema or not cont:
            return None
        cdef = arxml_ecuc_schema.find_cdef(
            schema, arxml_ecuc_schema.container_type_of(cont))
        if not cdef:
            return None
        for pd in (cdef.get("params") or []):
            if pd.get("shortName") == pname:
                return pd
        return None

    def _merged_param_rows(cont, schema):
        """Schema params first (DaVinci order), then orphan instance params."""
        by_name = {p.name: (i, p) for i, p in enumerate(cont.params or [])}
        seen = set()
        rows = []
        cdef = None
        if schema:
            cdef = arxml_ecuc_schema.find_cdef(
                schema, arxml_ecuc_schema.container_type_of(cont))
        for pd in (cdef.get("params") if cdef else None) or []:
            pname = pd.get("shortName") or ""
            if not pname:
                continue
            seen.add(pname)
            if pname in by_name:
                idx, p = by_name[pname]
                rows.append({
                    "param_idx": idx, "pdef": pd, "name": pname,
                    "kind": (p.kind or pd.get("kind") or "string").lower(),
                    "value": p.value, "definition": p.definition or pd.get(
                        "definition") or "",
                })
            else:
                kind = (pd.get("kind") or "string").lower()
                default = pd.get("default")
                if default is None:
                    default = arxml_ecuc_schema._default_for_param(pd)  # noqa
                rows.append({
                    "param_idx": None, "pdef": pd, "name": pname,
                    "kind": kind, "value": str(default),
                    "definition": pd.get("definition") or "",
                    "missing": True,
                })
        for pname, (idx, p) in by_name.items():
            if pname in seen:
                continue
            rows.append({
                "param_idx": idx, "pdef": None, "name": pname,
                "kind": (p.kind or "string").lower(),
                "value": p.value, "definition": p.definition or "",
            })
        return rows

    def _make_value_editor(row_meta: dict):
        kind = row_meta["kind"]
        value = row_meta.get("value") or ""
        pdef = row_meta.get("pdef") or {}
        if kind == "boolean":
            w = QCheckBox("true")
            w.setChecked(str(value).lower() in ("true", "1", "yes"))
            w.toggled.connect(
                lambda _c, r=row_meta: _commit_param_value(r))
            return w
        if kind == "enumeration":
            w = QComboBox()
            w.setEditable(True)  # type-ahead search over literals
            w.setInsertPolicy(QComboBox.InsertPolicy.NoInsert)
            w.addItems(list(pdef.get("literals") or []))
            idx = w.findText(value)
            if idx >= 0:
                w.setCurrentIndex(idx)
            else:
                w.setEditText(value)
            w.currentTextChanged.connect(
                lambda _t, r=row_meta: _commit_param_value(r))
            return w
        if kind == "reference":
            w = QComboBox()
            w.setEditable(True)
            w.setInsertPolicy(QComboBox.InsertPolicy.NoInsert)
            choices = arxml_ecuc_schema.collect_ref_choices(
                document.active_bsw, pdef, document)
            w.addItems(choices)
            w.setEditText(value)
            w.setToolTip("Type to filter reference targets")
            w.currentTextChanged.connect(
                lambda _t, r=row_meta: _commit_param_value(r))
            return w
        if kind == "numerical":
            w = QLineEdit(str(value))
            w.setFixedHeight(_ui.CTRL_H)
            lo = pdef.get("min")
            hi = pdef.get("max")
            if lo is not None or hi is not None:
                w.setToolTip("Range: %s … %s" % (lo, hi))
            w.editingFinished.connect(
                lambda r=row_meta: _commit_param_value(r))
            return w
        w = QLineEdit(str(value))
        w.setFixedHeight(_ui.CTRL_H)
        w.editingFinished.connect(
            lambda r=row_meta: _commit_param_value(r))
        return w

    def _read_editor(widget, kind: str) -> str:
        kind = (kind or "string").lower()
        if isinstance(widget, QCheckBox):
            return "true" if widget.isChecked() else "false"
        if isinstance(widget, QComboBox):
            return widget.currentText().strip()
        if isinstance(widget, QLineEdit):
            return widget.text().strip()
        return ""

    def _commit_param_value(row_meta: dict):
        if _prop_guard["depth"] or _guard["depth"]:
            return
        name = document.active_bsw
        model = document.bsw.get(name)
        if not model or selected.get("kind") != "cont":
            return
        path = selected.get("path") or ()
        row = row_meta.get("table_row")
        if row is None:
            return
        editor = prop_table.cellWidget(row, 1)
        if editor is None:
            return
        new_val = _read_editor(editor, row_meta["kind"])
        ecuc = model.clone()
        cont, _ = _resolve(ecuc, path)
        if not cont:
            return
        idx = row_meta.get("param_idx")
        pdef = row_meta.get("pdef")
        if idx is None:
            # Materialize missing schema param onto the instance.
            p = arxml_ecuc_schema.make_param(
                name,
                arxml_ecuc_schema.find_cdef(
                    _active_schema() or {},
                    arxml_ecuc_schema.container_type_of(cont)) or {},
                pdef or {"shortName": row_meta["name"],
                         "kind": row_meta["kind"]})
            p.value = new_val
            cont.params.append(p)
            row_meta["param_idx"] = len(cont.params) - 1
            row_meta["missing"] = False
        else:
            if idx < 0 or idx >= len(cont.params):
                return
            cont.params[idx].value = new_val
            if pdef and pdef.get("kind"):
                cont.params[idx].kind = pdef["kind"]
        document.apply_bsw_module(name, ecuc)
        # Re-bind param_idx against the stored model (clone indices differ).
        stored = document.bsw.get(name)
        if stored is not None:
            sc, _ = _resolve(stored, path)
            if sc is not None:
                for i, p in enumerate(sc.params or []):
                    if p.name == row_meta["name"]:
                        row_meta["param_idx"] = i
                        break
        log_fn("OK", "%s.%s = %s" % (name, row_meta["name"], new_val))
        if row_meta.get("missing"):
            row_meta["missing"] = False
            item0 = prop_table.item(row, 0)
            if item0 is not None:
                item0.setForeground(QColor(vscode_theme.TEXT))
                item0.setToolTip("")

    def _load_prop_sheet():
        nonlocal prop_rows
        model = document.bsw.get(document.active_bsw)
        schema = _active_schema()
        kind = selected.get("kind")
        path = selected.get("path") or ()
        _prop_guard["depth"] += 1
        try:
            prop_table.setRowCount(0)
            prop_rows = []
            if not model:
                return
            if kind == "mod":
                name_ed.setText(document.active_bsw or "")
                type_lab.setText("module")
                head_title.setText(
                    "Module · %s" % (document.active_bsw or "—"))
                sch = schema or {}
                n_c = 0
                if model.modules:
                    n_c = len(model.modules[0].containers)
                _set_spec(
                    "%s — AUTOSAR %s\n"
                    "Definition: %s\n"
                    "Root containers: %d\n"
                    "Select a container in the tree to edit all EcucDefs "
                    "parameters in the property sheet.\n"
                    "Stored as work/bsw/%s.arxml — no codegen."
                    % (document.active_bsw or "—",
                       sch.get("autosarRelease", "CP"),
                       sch.get("definition") or "—",
                       n_c,
                       document.active_bsw or "Module"))
                return
            if kind != "cont":
                _clear_prop_sheet()
                return
            cont, _ = _resolve(model, path)
            if not cont:
                return
            ctype = arxml_ecuc_schema.container_type_of(cont)
            name_ed.setText(cont.name)
            type_lab.setText(ctype)
            head_title.setText("Properties · %s" % cont.name)
            rows = _merged_param_rows(cont, schema)
            # Values map for condition evaluation (dependency graying).
            value_map = {
                (m.get("name") or ""): str(m.get("value") or "")
                for m in rows}
            prop_rows = rows
            prop_table.setRowCount(len(rows))
            for r, meta in enumerate(rows):
                meta["table_row"] = r
                name_item = QTableWidgetItem(meta["name"])
                name_item.setFlags(
                    Qt.ItemFlag.ItemIsEnabled | Qt.ItemFlag.ItemIsSelectable)
                if meta.get("missing"):
                    name_item.setForeground(QColor(vscode_theme.WARN_FG))
                    name_item.setToolTip(
                        "Defined in schema but not yet on instance — "
                        "editing creates it")
                prop_table.setItem(r, 0, name_item)
                editor = _make_value_editor(meta)
                prop_table.setCellWidget(r, 1, editor)
                pdef = meta.get("pdef") or {}
                active = arxml_ecuc_schema.param_condition_met(pdef, value_map)
                if not active:
                    editor.setEnabled(False)
                    name_item.setForeground(QColor(vscode_theme.TEXT_MUTED))
                    tip = pdef.get("condition") or ""
                    name_item.setToolTip(
                        "Inactive until condition: %s" % tip if tip
                        else "Conditional parameter (inactive)")
                kind_item = QTableWidgetItem(meta["kind"])
                kind_item.setFlags(
                    Qt.ItemFlag.ItemIsEnabled | Qt.ItemFlag.ItemIsSelectable)
                prop_table.setItem(r, 2, kind_item)
                def_item = QTableWidgetItem(meta.get("definition") or "")
                def_item.setFlags(
                    Qt.ItemFlag.ItemIsEnabled | Qt.ItemFlag.ItemIsSelectable)
                def_item.setToolTip(meta.get("definition") or "")
                prop_table.setItem(r, 3, def_item)
            _set_spec(
                "%s (%s)\nDefinition: %s\n"
                "%d parameters (schema-complete view). "
                "Select a parameter row for detail. "
                "Use Add… for child containers when multiplicity allows."
                % (cont.name, ctype, cont.definition or "—", len(rows)))
            if rows:
                prop_table.selectRow(0)
                _on_prop_sel()
        finally:
            _prop_guard["depth"] -= 1

    def _on_prop_sel():
        rows = prop_table.selectionModel().selectedRows()
        if not rows or not prop_rows:
            return
        r = rows[0].row()
        if r < 0 or r >= len(prop_rows):
            return
        meta = prop_rows[r]
        selected["pdef"] = meta.get("pdef")
        pdef = meta.get("pdef") or {}
        tip_row = arxmlparse.tip_for_field(
            "ecuc.%s.%s" % (
                (document.active_bsw or "").lower(), meta["name"]))
        lines = [meta["name"], ""]
        if tip_row:
            lines.append(tip_row.get("title") or "")
            lines.append(tip_row.get("summary") or "")
            if tip_row.get("detail"):
                lines.append(tip_row["detail"])
        else:
            if pdef.get("summary"):
                lines.append(pdef["summary"])
            if pdef.get("detail"):
                lines.append(pdef["detail"])
            if pdef.get("range"):
                lines.append("Range: %s" % pdef["range"])
            if pdef.get("min") is not None or pdef.get("max") is not None:
                lines.append(
                    "Min / max: %s … %s" % (pdef.get("min"), pdef.get("max")))
            if pdef.get("literals"):
                lines.append(
                    "Literals: %s" % ", ".join(pdef["literals"]))
            if pdef.get("destination"):
                lines.append("Reference destination: %s" % pdef["destination"])
        lines.append("")
        lines.append("Kind: %s" % meta["kind"])
        lines.append("Definition: %s" % (meta.get("definition") or "—"))
        if meta.get("missing"):
            lines.append(
                "(Not yet stored on instance — edit value to materialize.)")
        _set_spec("\n".join(x for x in lines if x is not None))

    def _on_mod():
        if mod_tree is None:
            return
        item = mod_tree.currentItem()
        if not item:
            return
        name = item.data(0, Qt.ItemDataRole.UserRole)
        if not name:
            return
        _activate_module(name)

    def _activate_module(name: str):
        if not name:
            return
        if name not in document.bsw:
            ecu = document.manifest.ecu_name if document.manifest else "Ecu"
            document.bsw[name] = arxml_bsw.stub_module(name, ecu_name=ecu)
            if document.manifest:
                document.manifest.ensure_module_entry(name)
            document.dirty = True
        # Always refresh the container tree for the selected module. Do not
        # rely solely on document.on_changed — set_active_bsw no-ops when the
        # module is already active, and _bsw_page_visible can skip the fill.
        same = (name == document.active_bsw and name in document.bsw)
        document.set_active_bsw(name)
        selected["module"] = name
        selected["kind"] = "mod"
        selected["path"] = ()
        _fill_modules()
        _fill_config(reload_props=True)
        if not same:
            _schedule_live()

    def _on_cfg():
        item = cfg_tree.currentItem()
        if not item:
            return
        data = item.data(0, Qt.ItemDataRole.UserRole)
        if not data:
            return
        kind, path = data
        selected["kind"] = kind
        selected["path"] = path
        selected["pdef"] = None
        _load_prop_sheet()

    def _apply():
        """Rename selected container (or keep module short name display)."""
        name = document.active_bsw
        model = document.bsw.get(name)
        if not model:
            return
        kind = selected["kind"]
        if kind == "cont":
            ecuc = model.clone()
            cont, _p = _resolve(ecuc, selected["path"])
            if not cont:
                return
            new_name = name_ed.text().strip()
            if new_name and new_name != cont.name:
                cont.name = new_name
                document.apply_bsw_module(name, ecuc)
                log_fn("OK", "Renamed container → %s" % new_name)
        else:
            log_fn("INFO", "Parameter values apply on edit; "
                   "select a container to rename")

    def _parent_context():
        kind = selected["kind"]
        path = selected["path"]
        model = document.bsw.get(document.active_bsw)
        if not model or not model.modules:
            return (), None
        if kind == "cont":
            cont, _ = _resolve(model, path)
            if cont:
                return path, arxml_ecuc_schema.container_type_of(cont)
            return (), None
        return (), None

    def _add_menu():
        name = document.active_bsw
        schema = _active_schema()
        model = document.bsw.get(name)
        if not schema or not model:
            QMessageBox.information(
                root, "Add container",
                "No schema for module %s" % (name or "—"))
            return
        parent_path, parent_type = _parent_context()
        if selected["kind"] in ("", "mod"):
            parent_path, parent_type = (), None
        types = arxml_ecuc_schema.addable_types(schema, parent_type)
        if selected["kind"] == "cont" and parent_type:
            types = arxml_ecuc_schema.addable_types(schema, parent_type)
        if not types:
            types = arxml_ecuc_schema.addable_types(schema, None)
            parent_path, parent_type = (), None
        if not types:
            QMessageBox.information(
                root, "Add container",
                "No addable container types here.")
            return
        menu = QMenu(root)
        parent_list, _ = arxml_ecuc_schema._parent_list(  # noqa: SLF001
            model.modules[0], parent_path)
        for cdef in types:
            tname = cdef.get("shortName") or "?"
            enabled = True
            if parent_list is not None:
                enabled = arxml_ecuc_schema.can_add(schema, parent_list, tname)
            act = menu.addAction(tname)
            act.setEnabled(enabled)
            lo, up = arxml_ecuc_schema.multiplicity(cdef)
            act.setToolTip(
                "multiplicity %s..%s" % (lo, up if up is not None else "*"))

            def _do(_checked=False, tn=tname, pp=parent_path):
                ecuc = model.clone()
                cont = arxml_ecuc_schema.add_container(
                    ecuc, name, schema, pp, tn)
                if cont is None:
                    log_fn("WARN", "Cannot add %s (multiplicity or parent)" % tn)
                    return
                document.apply_bsw_module(name, ecuc)
                log_fn("OK", "Added %s (+ required children)" % cont.name)

            act.triggered.connect(_do)
        menu.exec(add_btn.mapToGlobal(add_btn.rect().bottomLeft()))

    def _remove():
        name = document.active_bsw
        schema = _active_schema()
        model = document.bsw.get(name)
        if not model or selected["kind"] != "cont":
            log_fn("WARN", "Select a container to remove")
            return
        path = selected["path"]
        if not path:
            return
        ecuc = model.clone()
        if schema:
            ok = arxml_ecuc_schema.remove_container(ecuc, schema, path)
        else:
            pl, _ = arxml_ecuc_schema._parent_list(  # noqa: SLF001
                ecuc.modules[0], path[:-1])
            if pl is None:
                return
            del pl[path[-1]]
            ok = True
        if not ok:
            log_fn("WARN", "Cannot remove (lower multiplicity)")
            return
        document.apply_bsw_module(name, ecuc)
        selected["kind"] = ""
        _clear_prop_sheet()
        log_fn("OK", "Removed container")

    def _duplicate():
        name = document.active_bsw
        schema = _active_schema()
        model = document.bsw.get(name)
        if not model or selected["kind"] != "cont" or not schema:
            log_fn("WARN", "Select a container to duplicate")
            return
        ecuc = model.clone()
        cont = arxml_ecuc_schema.duplicate_container(
            ecuc, name, schema, selected["path"])
        if cont is None:
            log_fn("WARN", "Cannot duplicate (upper multiplicity)")
            return
        document.apply_bsw_module(name, ecuc)
        log_fn("OK", "Duplicated → %s" % cont.name)

    def _init():
        n = document.init_all_bsw()
        try:
            arxml_ecuc_schema.register_schema_tips()
        except Exception:
            pass
        log_fn("OK", "Initialized %d BSW modules from schemas" % n)
        _live_full["need"] = True

    def _sync():
        document.sync_bsw_from_com()
        log_fn("OK", "Synced Com/CanIf/PduR/CanNm from COM")
        _live_full["need"] = True

    def _write():
        if not document.has_project():
            log_fn("WARN", "Open or create a project first")
            return
        try:
            paths = document.write_intermediates()
            n = len(paths.get("bsw") or {})
            log_fn("OK", "Wrote %d module ARXMLs under work/bsw/" % n)
            _fill_modules()
        except (OSError, ValueError) as e:
            log_fn("ERR", str(e))

    def export_active_module():
        name = document.active_bsw
        if not name or name not in document.bsw:
            log_fn("WARN", "Select a BSW module first")
            return
        from PyQt6.QtWidgets import QFileDialog
        import os
        default = "%s.arxml" % name
        if document.has_project() and document.manifest:
            default = document.manifest.abs_module(name) or default
        path, _ = QFileDialog.getSaveFileName(
            root, "Export %s ARXML" % name, default,
            "ARXML (*.arxml);;All (*)")
        if not path:
            return
        try:
            text = arxmlparse.serialize_ecuc_lite(document.bsw[name])
            os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
            log_fn("OK", "Exported %s" % path)
        except OSError as e:
            log_fn("ERR", str(e))

    def select_module(name: str):
        _activate_module(name)
        if mod_tree is None:
            sb = getattr(shell, "_config_sidebar", None)
            if sb is not None and hasattr(sb, "select_module_in_tree"):
                try:
                    sb.select_module_in_tree(name)
                except Exception:
                    pass
            return
        if filter_ed is not None:
            filter_ed.blockSignals(True)
            if _filter_text["q"] and name.lower().find(
                    _filter_text["q"].lower()) < 0:
                filter_ed.clear()
                _filter_text["q"] = ""
                _fill_modules()
            filter_ed.blockSignals(False)
        for i in range(mod_tree.topLevelItemCount()):
            g = mod_tree.topLevelItem(i)
            for j in range(g.childCount()):
                ch = g.child(j)
                if ch.data(0, Qt.ItemDataRole.UserRole) == name:
                    mod_tree.blockSignals(True)
                    mod_tree.setCurrentItem(ch)
                    mod_tree.blockSignals(False)
                    return

    def select_container(short: str) -> bool:
        return _select_cfg_by_name(short or "")

    def run_live_check(full: bool = False):
        nonlocal live_cache
        name = document.active_bsw
        model = document.bsw.get(name) if name else None
        findings = []
        if name and model:
            findings.extend(arxml_ecuc_schema.schema_validate_module(
                name, model, document.model))
            do_full = full or _live_full["need"]
            if do_full:
                _live_full["need"] = False
                for f in arxml_bsw.validate_bsw_set(
                        document.bsw, document.model):
                    loc = (f.get("location") or "").lower()
                    msg = (f.get("message") or "").lower()
                    if (f.get("module") == name
                            or name.lower() in loc
                            or name.lower() in msg
                            or (name == "CanIf"
                                and f.get("rule") == "canif_dangling")
                            or (name == "PduR"
                                and f.get("rule") == "pdur_dangling")
                            or (name == "Dcm"
                                and f.get("rule") == "diag_pdu_ref")
                            or (name == "Os"
                                and f.get("rule") == "os_task_dup")
                            or f.get("rule") == "bsw_comm_missing"):
                        f = dict(f)
                        f.setdefault("module", name)
                        findings.append(f)
        live_cache = findings
        if find_tree is None:
            return
        find_tree.clear()
        colors = {
            "error": QColor(vscode_theme.ERR),
            "warning": QColor(vscode_theme.WARN_FG),
            "warn": QColor(vscode_theme.WARN_FG),
            "info": QColor(vscode_theme.ACCENT),
        }
        n_err = n_warn = 0
        for f in findings:
            sev = f.get("severity") or f.get("level") or "info"
            if sev == "error":
                n_err += 1
            elif sev in ("warning", "warn"):
                n_warn += 1
            item = QTreeWidgetItem([
                sev, f.get("rule", ""), f.get("location", ""),
                f.get("message", ""), f.get("fix", ""),
            ])
            item.setData(0, Qt.ItemDataRole.UserRole, f)
            c = colors.get(sev, QColor(vscode_theme.TEXT_MUTED))
            for col in range(5):
                item.setForeground(col, c)
            find_tree.addTopLevelItem(item)
        if find_summary is not None:
            if not findings:
                find_summary.setText("No issues for %s" % (name or "—"))
            else:
                find_summary.setText(
                    "%d error · %d warn  (double-click to jump)"
                    % (n_err, n_warn))

    live_timer = QTimer(root)
    live_timer.setSingleShot(True)
    live_timer.setInterval(450)
    live_timer.timeout.connect(lambda: run_live_check(False))

    def _schedule_live():
        live_timer.start()

    def _bsw_page_visible() -> bool:
        """True when the BSW configurator is the active Config leaf."""
        try:
            feat = getattr(shell, "_active_feature", "") or ""
            if feat in ("bsw", "config"):
                return True
            wb = getattr(shell, "_wb", None)
            if wb is not None and callable(getattr(wb, "current_page", None)):
                return wb.current_page() == "config" and feat in (
                    "bsw", "config", "")
            return False
        except Exception:
            return True

    def _on_doc_changed():
        if _guard["depth"]:
            return
        _guard["depth"] += 1
        try:
            if not _bsw_page_visible():
                _live_full["need"] = True
                return
            _fill_modules()
            # Refresh container tree only — keep property editors focused.
            _fill_config(reload_props=False)
            _schedule_live()
        finally:
            _guard["depth"] -= 1

    def refresh_view():
        if _guard["depth"]:
            return
        _guard["depth"] += 1
        try:
            _fill_modules()
            _fill_config(reload_props=True)
            _schedule_live()
        finally:
            _guard["depth"] -= 1

    def _goto_finding(item, _col):
        f = item.data(0, Qt.ItemDataRole.UserRole) or {}
        mod = f.get("module") or document.active_bsw
        if mod:
            select_module(mod)
        loc = f.get("container") or f.get("location") or ""
        if loc and loc != mod:
            _select_cfg_by_name(loc)

    def _apply_fix():
        if find_tree is None:
            return
        item = find_tree.currentItem()
        if not item:
            log_fn("WARN", "Select a finding first")
            return
        f = item.data(0, Qt.ItemDataRole.UserRole) or {}
        name = f.get("module") or document.active_bsw
        if not name or name not in document.bsw:
            log_fn("WARN", "No active module for fix")
            return
        if f.get("rule") == "bsw_comm_missing":
            for req in ("Com", "CanIf", "PduR"):
                if req not in document.bsw:
                    document.toggle_bsw_module(req)
            document.sync_bsw_from_com()
            log_fn("OK", "Enabled communication modules + sync")
            _live_full["need"] = True
            return
        ecuc = document.bsw[name].clone()
        ok, note = arxml_ecuc_schema.apply_live_fix(
            name, ecuc, f, arxml_ecuc_schema.load_schema(name))
        if ok:
            document.apply_bsw_module(name, ecuc)
            log_fn("OK", note)
        else:
            log_fn("WARN", note)

    def _on_filter(text: str):
        _filter_text["q"] = text or ""
        _fill_modules()

    try:
        arxml_ecuc_schema.register_schema_tips()
    except Exception:
        pass

    if mod_tree is not None:
        mod_tree.itemSelectionChanged.connect(_on_mod)
    cfg_tree.itemSelectionChanged.connect(_on_cfg)
    prop_table.itemSelectionChanged.connect(_on_prop_sel)
    apply_btn.clicked.connect(_apply)
    add_btn.clicked.connect(_add_menu)
    rem_btn.clicked.connect(_remove)
    dup_btn.clicked.connect(_duplicate)
    if filter_ed is not None:
        filter_ed.textChanged.connect(_on_filter)
    if find_tree is not None:
        find_tree.itemDoubleClicked.connect(_goto_finding)
    if fix_btn is not None:
        fix_btn.clicked.connect(_apply_fix)
    if recheck_btn is not None:
        recheck_btn.clicked.connect(lambda: run_live_check(True))
    document.on_changed(_on_doc_changed)
    _fill_modules()
    _fill_config()
    _schedule_live()

    root.select_module = select_module
    root.select_container = select_container
    root.apply_from_menu = _apply
    root.run_live_check = run_live_check
    root.refresh_view = refresh_view
    root.export_active_module = export_active_module
    root.init_all_modules = _init
    root.sync_from_com = _sync
    root.write_module_arxmls = _write
    return root
