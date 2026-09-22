# -*- coding: utf-8 -*-
"""BSW — schema-driven Classic Platform module configurator (no codegen)."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFormLayout,
    QHeaderView,
    QLineEdit,
    QMenu,
    QMessageBox,
    QSpinBox,
    QSplitter,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import arxml_bsw, arxml_ecuc_schema, arxmlparse, suite_chrome, vscode_theme
from pages import _ui


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    init_btn = _ui.primary_btn(
        "Init all modules", "Create schema stubs for every BSW module", "add")
    sync_btn = _ui.ghost_btn(
        "Sync from COM", "Refresh Com/CanIf/PduR/CanNm from COM", "refresh")
    write_btn = _ui.ghost_btn(
        "Write module ARXMLs", "Save work/bsw/<Module>.arxml", "save")
    export_one_btn = _ui.ghost_btn(
        "Export module…", "Write active module ARXML only", "export")
    status = _ui.quiet_label("")
    crow.addWidget(init_btn)
    crow.addWidget(sync_btn)
    crow.addWidget(write_btn)
    crow.addWidget(export_one_btn)
    crow.addStretch(1)
    crow.addWidget(status)
    layout.addWidget(chrome)

    split = QSplitter(Qt.Orientation.Horizontal)
    split.setHandleWidth(1)

    mod_tree = QTreeWidget()
    mod_tree.setHeaderLabels(["Module", "Status"])
    _ui.style_tree(mod_tree)
    mod_tree.setMinimumWidth(220)
    mod_tree.setMaximumWidth(320)
    split.addWidget(mod_tree)

    right = QSplitter(Qt.Orientation.Horizontal)
    mid = QWidget()
    mid_l = QVBoxLayout(mid)
    mid_l.setContentsMargins(0, 0, 0, 0)
    mid_l.setSpacing(0)
    tree_chrome, tree_row = suite_chrome.make_toolbar()
    add_btn = _ui.ghost_btn("Add…", "Add container instance (schema)", "add")
    rem_btn = _ui.ghost_btn("Remove", "Remove selected container", "delete")
    dup_btn = _ui.ghost_btn("Duplicate", "Duplicate selected container", "copy")
    tree_row.addWidget(add_btn)
    tree_row.addWidget(rem_btn)
    tree_row.addWidget(dup_btn)
    tree_row.addStretch(1)
    mid_l.addWidget(tree_chrome)

    cfg_tree = QTreeWidget()
    cfg_tree.setHeaderLabels(["Name", "Kind", "Value / Definition"])
    _ui.style_tree(cfg_tree)
    cfg_tree.header().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
    mid_l.addWidget(cfg_tree, 1)
    right.addWidget(mid)

    form_host = QWidget()
    form_host.setMaximumWidth(360)
    fl = QVBoxLayout(form_host)
    fl.setContentsMargins(8, 8, 8, 8)
    form = QFormLayout()
    vscode_theme.tune_form(form)
    name_ed = QLineEdit()
    name_ed.setFixedHeight(28)
    val_ed = QLineEdit()
    val_ed.setFixedHeight(28)
    val_spin = QSpinBox()
    val_spin.setFixedHeight(28)
    val_spin.setRange(-2147483647, 2147483647)
    val_combo = QComboBox()
    val_combo.setFixedHeight(28)
    val_combo.setEditable(True)
    val_check = QCheckBox("Enabled / true")
    def_ed = QLineEdit()
    def_ed.setFixedHeight(28)
    def_ed.setReadOnly(True)
    kind_ed = QLineEdit()
    kind_ed.setFixedHeight(28)
    kind_ed.setReadOnly(True)
    tip = _ui.tip_panel()
    tip.setText(
        "Select a module, then Add containers per AUTOSAR EcucDefs schema. "
        "Export writes work/bsw/<Module>.arxml — no codegen.")
    apply_btn = _ui.primary_btn("Apply", "Write short name / value", "apply")
    form.addRow("Short name", name_ed)
    form.addRow("Kind", kind_ed)
    form.addRow("Value", val_ed)
    form.addRow("Number", val_spin)
    form.addRow("Enum / ref", val_combo)
    form.addRow("Boolean", val_check)
    form.addRow("Definition", def_ed)
    fl.addLayout(form)
    fl.addWidget(apply_btn)
    fl.addWidget(_ui.quiet_label("Spec tip"))
    fl.addWidget(tip, 1)
    right.addWidget(form_host)
    right.setStretchFactor(0, 3)
    right.setStretchFactor(1, 2)
    split.addWidget(right)
    split.setStretchFactor(0, 1)
    split.setStretchFactor(1, 4)
    layout.addWidget(split, 1)

    selected = {"module": "", "path": (), "kind": "", "pdef": None}

    def _show_value_kind(kind: str):
        kind = (kind or "string").lower()
        val_ed.setVisible(kind in ("string", "textual", ""))
        val_spin.setVisible(kind == "numerical")
        val_combo.setVisible(kind in ("enumeration", "reference"))
        val_check.setVisible(kind == "boolean")

    _show_value_kind("string")

    def os_path_exists(doc, name: str) -> bool:
        if not doc.manifest:
            return False
        import os
        p = doc.manifest.abs_module(name)
        return bool(p) and os.path.isfile(p)

    def _fill_modules():
        mod_tree.blockSignals(True)
        mod_tree.clear()
        groups = arxml_bsw.catalog_by_group()
        for group, rows in groups.items():
            gitem = QTreeWidgetItem([group, ""])
            gitem.setFlags(gitem.flags() & ~Qt.ItemFlag.ItemIsSelectable)
            mod_tree.addTopLevelItem(gitem)
            for name, summary in rows:
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
        mod_tree.expandAll()
        mod_tree.blockSignals(False)
        n_sch = len(arxml_ecuc_schema.list_schema_modules())
        status.setText("%d modules · %d schemas · active %s" % (
            len(arxml_bsw.module_names()), n_sch, document.active_bsw or "—"))

    def _add_containers(parent, containers, path_prefix):
        for i, cont in enumerate(containers):
            path = path_prefix + (i,)
            ctype = arxml_ecuc_schema.container_type_of(cont)
            detail = ctype if ctype != cont.name else (
                cont.definition.rsplit("/", 1)[-1] if cont.definition else "")
            item = QTreeWidgetItem([cont.name, "Container", detail])
            item.setData(0, Qt.ItemDataRole.UserRole, ("cont", path))
            item.setToolTip(0, cont.definition or ctype)
            if parent is None:
                cfg_tree.addTopLevelItem(item)
            else:
                parent.addChild(item)
            for j, p in enumerate(cont.params):
                ch = QTreeWidgetItem([p.name, p.kind or "Param", p.value])
                ch.setData(
                    0, Qt.ItemDataRole.UserRole, ("param", path + ("p", j)))
                ch.setToolTip(2, p.definition)
                item.addChild(ch)
            if cont.children:
                _add_containers(item, cont.children, path)

    def _fill_config():
        cfg_tree.blockSignals(True)
        cfg_tree.clear()
        name = document.active_bsw
        model = document.bsw.get(name)
        if not model:
            cfg_tree.blockSignals(False)
            return
        for mod in model.modules:
            item = QTreeWidgetItem([mod.name, "Module", mod.definition])
            item.setData(0, Qt.ItemDataRole.UserRole, ("mod", ()))
            cfg_tree.addTopLevelItem(item)
            _add_containers(item, mod.containers, ())
        cfg_tree.expandToDepth(2)
        cfg_tree.blockSignals(False)

    def _active_schema():
        name = document.active_bsw
        return arxml_ecuc_schema.load_schema(name) if name else None

    def _on_mod():
        item = mod_tree.currentItem()
        if not item:
            return
        name = item.data(0, Qt.ItemDataRole.UserRole)
        if not name:
            return
        if name not in document.bsw:
            ecu = document.manifest.ecu_name if document.manifest else "Ecu"
            document.bsw[name] = arxml_bsw.stub_module(name, ecu_name=ecu)
            if document.manifest:
                document.manifest.ensure_module_entry(name)
            document.dirty = True
            document._notify()
        document.set_active_bsw(name)
        selected["module"] = name
        selected["kind"] = ""
        _fill_config()
        sch = arxml_ecuc_schema.load_schema(name)
        tip.setText(
            "<b>%s</b> · AUTOSAR %s<br/>"
            "Stored as <code>work/bsw/%s.arxml</code>. "
            "Use Add to create container instances (multiplicity)."
            % (name,
               (sch or {}).get("autosarRelease", "CP"),
               name))

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

    def _pdu_names():
        return [p.name for p in document.model.ipdus]

    def _sibling_names(model, path):
        """SHORT-NAMEs under same parent for reference pickers."""
        if not model.modules:
            return []
        mod = model.modules[0]
        parent_path = path[:-1] if path and path[-2:-1] != ("p",) else path
        # for param path (... i, 'p', j) parent container path is ...
        if "p" in path:
            pi = path.index("p")
            parent_path = path[:pi]
        parent_list, _ = arxml_ecuc_schema._parent_list(mod, parent_path[:-1] if parent_path else ())  # noqa: SLF001
        if parent_path:
            # list siblings of the container that holds the param
            pl, pc = arxml_ecuc_schema._parent_list(mod, parent_path[:-1])  # noqa: SLF001
            if pl is None:
                return []
            return [c.name for c in pl]
        return [c.name for c in mod.containers]

    def _on_cfg():
        item = cfg_tree.currentItem()
        if not item:
            return
        kind, path = item.data(0, Qt.ItemDataRole.UserRole)
        selected["kind"] = kind
        selected["path"] = path
        selected["pdef"] = None
        model = document.bsw.get(document.active_bsw)
        if not model:
            return
        schema = _active_schema()
        if kind == "param":
            cont, param = _resolve(model, path)
            if not param:
                return
            name_ed.setText(param.name)
            def_ed.setText(param.definition)
            kind_ed.setText(param.kind or "string")
            pdef = None
            if schema and cont:
                cdef = arxml_ecuc_schema.find_cdef(
                    schema, arxml_ecuc_schema.container_type_of(cont))
                if cdef:
                    for pd in (cdef.get("params") or []):
                        if pd.get("shortName") == param.name:
                            pdef = pd
                            break
            selected["pdef"] = pdef
            kind = (param.kind or (pdef or {}).get("kind") or "string").lower()
            kind_ed.setText(kind)
            _show_value_kind(kind)
            if kind == "boolean":
                val_check.setChecked(param.value.lower() in ("true", "1", "yes"))
            elif kind == "numerical":
                lo = int((pdef or {}).get("min", -2147483647) or -2147483647)
                hi = int((pdef or {}).get("max", 2147483647) or 2147483647)
                val_spin.setRange(lo, hi)
                try:
                    val_spin.setValue(int(float(param.value or "0")))
                except ValueError:
                    val_spin.setValue(0)
            elif kind == "enumeration":
                val_combo.clear()
                lits = list((pdef or {}).get("literals") or [])
                val_combo.addItems(lits)
                val_combo.setEditable(False)
                idx = val_combo.findText(param.value)
                if idx >= 0:
                    val_combo.setCurrentIndex(idx)
                else:
                    val_combo.setEditText(param.value)
            elif kind == "reference":
                val_combo.clear()
                dest = ((pdef or {}).get("destination") or "").lower()
                if "pdu" in dest or param.name.endswith("PduRef"):
                    val_combo.addItems(_pdu_names())
                else:
                    val_combo.addItems(_sibling_names(model, path))
                val_combo.setEditable(True)
                val_combo.setEditText(param.value)
            else:
                val_ed.setText(param.value)
            tip_row = arxmlparse.tip_for_field(
                "ecuc.%s.%s" % (document.active_bsw.lower(), param.name))
            if not tip_row and pdef:
                tip.setText(
                    "<b>%s</b><br/>%s<br/><i>%s</i><br/>%s" % (
                        param.name,
                        pdef.get("summary") or "",
                        pdef.get("detail") or param.definition,
                        ("Range: %s" % pdef["range"]) if pdef.get("range") else ""))
            elif tip_row:
                tip.setText(
                    "<b>%s</b><br/>%s<br/><i>%s</i>" % (
                        tip_row["title"], tip_row["summary"],
                        tip_row.get("detail", "")))
            else:
                tip.setText(
                    "<b>%s</b><br/>Definition: %s" % (
                        param.name, param.definition or "—"))
        elif kind == "cont":
            cont, _p = _resolve(model, path)
            if cont:
                name_ed.setText(cont.name)
                kind_ed.setText("container")
                def_ed.setText(cont.definition)
                _show_value_kind("string")
                val_ed.setText("")
                tip.setText(
                    "<b>%s</b> (%s)<br/>Add child containers via Add… "
                    "when the schema allows multiplicity."
                    % (cont.name, arxml_ecuc_schema.container_type_of(cont)))
        elif kind == "mod":
            name_ed.setText(document.active_bsw)
            kind_ed.setText("module")
            def_ed.setText("")
            _show_value_kind("string")

    def _read_value(kind: str) -> str:
        kind = (kind or "string").lower()
        if kind == "boolean":
            return "true" if val_check.isChecked() else "false"
        if kind == "numerical":
            return str(val_spin.value())
        if kind in ("enumeration", "reference"):
            return val_combo.currentText().strip()
        return val_ed.text().strip()

    def _apply():
        name = document.active_bsw
        model = document.bsw.get(name)
        if not model:
            return
        ecuc = model.clone()
        kind = selected["kind"]
        if kind == "param":
            cont, param = _resolve(ecuc, selected["path"])
            if not param:
                return
            param.name = name_ed.text().strip() or param.name
            param.value = _read_value(kind_ed.text())
            if selected.get("pdef") and selected["pdef"].get("kind"):
                param.kind = selected["pdef"]["kind"]
            document.apply_bsw_module(name, ecuc)
            log_fn("OK", "%s.%s = %s" % (name, param.name, param.value))
            _fill_config()
        elif kind == "cont":
            cont, _p = _resolve(ecuc, selected["path"])
            if not cont:
                return
            new_name = name_ed.text().strip()
            if new_name:
                cont.name = new_name
                document.apply_bsw_module(name, ecuc)
                log_fn("OK", "Renamed container → %s" % new_name)
                _fill_config()

    def _parent_context():
        """Return (parent_path, parent_type) for Add based on selection."""
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
        if kind == "param":
            # add sibling under same parent as containing container
            if "p" in path:
                pi = list(path).index("p")
                cpath = tuple(path[:pi])
                cont, _ = _resolve(model, cpath)
                if cont and len(cpath) >= 1:
                    return cpath[:-1], arxml_ecuc_schema.container_type_of(
                        _resolve(model, cpath[:-1])[0]) if cpath[:-1] else None
                # under root container: parent is module
                if cont:
                    # children of this container
                    return cpath, arxml_ecuc_schema.container_type_of(cont)
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
        # If nothing selected, add roots
        if selected["kind"] in ("", "mod"):
            parent_path, parent_type = (), None
        types = arxml_ecuc_schema.addable_types(schema, parent_type)
        # Also allow adding under selected container's type
        if selected["kind"] == "cont" and parent_type:
            types = arxml_ecuc_schema.addable_types(schema, parent_type)
        if not types:
            # try root types
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
            act.setToolTip("multiplicity %s..%s" % (lo, up if up is not None else "*"))

            def _do(_checked=False, tn=tname, pp=parent_path):
                ecuc = model.clone()
                cont = arxml_ecuc_schema.add_container(
                    ecuc, name, schema, pp, tn)
                if cont is None:
                    log_fn("WARN", "Cannot add %s (multiplicity or parent)" % tn)
                    return
                document.apply_bsw_module(name, ecuc)
                log_fn("OK", "Added %s" % cont.name)
                _fill_config()

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
            # fallback unrestricted
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
        log_fn("OK", "Removed container")
        _fill_config()

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
        _fill_config()

    def _init():
        n = document.init_all_bsw()
        try:
            arxml_ecuc_schema.register_schema_tips()
        except Exception:
            pass
        log_fn("OK", "Initialized %d BSW modules from schemas" % n)
        _fill_modules()
        _fill_config()

    def _sync():
        document.sync_bsw_from_com()
        log_fn("OK", "Synced Com/CanIf/PduR/CanNm from COM")
        _fill_modules()
        _fill_config()

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

    def _export_one():
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
        document.set_active_bsw(name)
        for i in range(mod_tree.topLevelItemCount()):
            g = mod_tree.topLevelItem(i)
            for j in range(g.childCount()):
                ch = g.child(j)
                if ch.data(0, Qt.ItemDataRole.UserRole) == name:
                    mod_tree.setCurrentItem(ch)
                    return
        _fill_config()

    # Register tips once
    try:
        arxml_ecuc_schema.register_schema_tips()
    except Exception:
        pass

    mod_tree.itemSelectionChanged.connect(_on_mod)
    cfg_tree.itemSelectionChanged.connect(_on_cfg)
    apply_btn.clicked.connect(_apply)
    add_btn.clicked.connect(_add_menu)
    rem_btn.clicked.connect(_remove)
    dup_btn.clicked.connect(_duplicate)
    init_btn.clicked.connect(_init)
    sync_btn.clicked.connect(_sync)
    write_btn.clicked.connect(_write)
    export_one_btn.clicked.connect(_export_one)
    document.on_changed(lambda: (_fill_modules(), _fill_config()))
    _fill_modules()
    _fill_config()
    root.select_module = select_module
    root.apply_from_menu = _apply
    return root
