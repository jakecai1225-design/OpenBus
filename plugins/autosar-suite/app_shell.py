# -*- coding: utf-8 -*-
"""AppShell — AUTOSAR Studio workbench (UDS-style chrome)."""

from __future__ import annotations

import os
import time
from typing import Optional

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QAction, QFontMetrics, QKeySequence
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QApplication,
    QFileDialog,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QMainWindow,
    QMenuBar,
    QMessageBox,
    QPushButton,
    QSizePolicy,
    QStackedWidget,
    QToolButton,
    QTreeWidget,
    QWidget,
)

from _shared import (
    ai_attach, arxml_bsw, arxml_ecuc_schema, arxml_project, arxmlparse,
    codicons, plugin_shell, state_store, suite_chrome, vscode_theme,
)

from document import ArxmlDocument

PLUGIN_ID = "autosar-suite"
MAX_RECENT = 12

# Activity bar — major workspaces; sub-features live in the Side Bar.
NAV_PAGES = [
    ("project", "Project"),
    ("config", "Config"),
    ("com", "COM"),
    ("bus", "Bus"),
    ("validate", "Validate"),
    ("setup", "Setup"),
]

# Feature / legacy keys → (workspace, stack index or None).
FEATURE_ROUTE: dict[str, tuple[str, Optional[int]]] = {
    "project": ("project", 0),
    "library": ("project", 1),
    "config": ("config", None),
    "bsw": ("config", 0),
    "editor": ("config", 1),
    "spec": ("config", 2),
    "swc": ("config", 3),
    "com": ("com", 1),
    "com_layout": ("com", 0),
    "com_live": ("com", 1),
    "com_pack": ("com", 2),
    "bus": ("bus", None),
    "system": ("bus", 0),
    "system_tree": ("bus", 0),
    "system_validate": ("bus", 0),
    "system_export": ("bus", 0),
    "nm": ("bus", 1),
    "e2e": ("bus", 2),
    "secoc": ("bus", 3),
    "validate": ("validate", 0),
    "timing": ("validate", 1),
    "analysis": ("validate", 1),
    "compare": ("validate", 2),
    "merge": ("validate", 3),
    "export": ("validate", 4),
    "setup": ("setup", 0),
}

# Chrome title for the active feature (Side Bar owns navigation).
FEATURE_TITLES: dict[str, str] = {
    "project": "Workspace",
    "library": "Library",
    "bsw": "BSW",
    "editor": "Editor",
    "spec": "Spec",
    "swc": "SWC",
    "com": "Live",
    "com_layout": "Layout",
    "com_live": "Live",
    "com_pack": "Pack",
    "system": "System",
    "system_tree": "System · Tree",
    "system_validate": "System · Validate",
    "system_export": "System · Export",
    "nm": "NM",
    "e2e": "E2E",
    "secoc": "SecOC",
    "validate": "Findings",
    "timing": "Analysis",
    "analysis": "Analysis",
    "compare": "Compare",
    "merge": "Merge",
    "export": "Export",
    "setup": "Setup",
}


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("AUTOSAR Studio")
        self.setMinimumSize(1100, 720)
        # Sensible default before showMaximized(); restoreGeometry may shrink.
        self.resize(1400, 900)

        app = QApplication.instance()
        if app is not None:
            app.setApplicationName("AUTOSAR Studio")
            app.setApplicationDisplayName("AUTOSAR Studio")

        self._context = context
        self.document = ArxmlDocument()
        from session import SharedSession
        self.session = SharedSession(parent=self)
        self._log_buffer: list = []
        self._pages = {}
        self._lint_before_save = True
        self._chrome_host: Optional[QWidget] = None
        self._recent: list = []
        self._recent_projects: list = []
        self._project_tabs = None
        self._config_tabs = None
        self._com_tabs = None
        self._bus_tabs = None
        self._validate_tabs = None
        self._workspace_stacks: dict = {}
        self._workspace_features: dict = {}
        self._sidebars: dict = {}
        self._active_feature = "bsw"
        self._lint_action = None
        self._recent_proj_menu = None
        self._persist_timer = None
        # Hidden hold for SuiteEditorTabs / doc chrome between remounts.
        # Must NOT parent onto QMainWindow itself — floating children paint at
        # (0,0) and cover the native menubar / activity bar.
        self._chrome_park = QWidget(self)
        self._chrome_park.hide()
        self._chrome_park.setAttribute(
            Qt.WidgetAttribute.WA_DontShowOnScreen, True)

        self._wb = suite_chrome.build_workbench(
            self, NAV_PAGES, title="AUTOSAR Studio", panel_title="OUTPUT",
            panel_visible=True, sidebar_visible=True,
            side_bar_enabled=True, side_bar_visible=True, lock_activity=True)
        self.stack = self._wb.stack

        self._init_document_controls()
        self._build_menubar()
        self._mount_menubar_trailing()
        self._build_output_panel()
        self.session.set_log_fn(self._session_log)
        plugin_shell.wire_close_deactivates(self, PLUGIN_ID)

        from pages import (
            analysis, bsw, com, compare, config_sidebar, e2e, editor, export,
            library, merge, nm, project, secoc, setup, spec, swc, system,
            validate, workspace_sidebar,
        )

        def _doc(builder):
            return builder(self, self.document, self.log)

        def _live(builder):
            return builder(self, self.session, self._session_log)

        # Feature pages (API targets) — not activity-bar entries.
        self._pages["project"] = _doc(project.build)
        self._pages["library"] = _doc(library.build)
        self._pages["bsw"] = bsw.build(
            self, self.document, self.log, nav_in_sidebar=True)
        self._pages["editor"] = _doc(editor.build)
        self._pages["spec"] = _doc(spec.build)
        self._pages["swc"] = _doc(swc.build)
        self._pages["com"] = _live(com.build)
        self._pages["system"] = _live(system.build)
        self._pages["nm"] = _live(nm.build)
        self._pages["e2e"] = _live(e2e.build)
        self._pages["secoc"] = _live(secoc.build)
        self._pages["validate"] = _doc(validate.build)
        self._pages["timing"] = _doc(analysis.build)
        self._pages["compare"] = _doc(compare.build)
        self._pages["merge"] = _doc(merge.build)
        self._pages["export"] = _doc(export.build)
        self._pages["setup"] = _live(setup.build)

        # Side Bars (VS Code Explorer) — one per activity workspace.
        self._sidebars["config"] = config_sidebar.build_config_sidebar(
            self, self.document)
        self._sidebars["project"] = workspace_sidebar.build_section_sidebar(
            self, "Project", workspace_sidebar.PROJECT_SECTIONS)
        self._sidebars["com"] = workspace_sidebar.build_section_sidebar(
            self, "COM", workspace_sidebar.COM_SECTIONS)
        self._sidebars["bus"] = workspace_sidebar.build_section_sidebar(
            self, "Bus", workspace_sidebar.BUS_SECTIONS,
            nested=workspace_sidebar.BUS_NESTED)
        self._sidebars["validate"] = workspace_sidebar.build_section_sidebar(
            self, "Validate", workspace_sidebar.VALIDATE_SECTIONS)
        self._sidebars["setup"] = workspace_sidebar.build_section_sidebar(
            self, "Setup", workspace_sidebar.SETUP_SECTIONS)
        self._config_sidebar = self._sidebars["config"]

        # Stack order must match NAV_PAGES.
        self.stack.addWidget(self._build_feature_workspace("project", [
            ("project", self._pages["project"]),
            ("library", self._pages["library"]),
        ]))
        self.stack.addWidget(self._build_feature_workspace("config", [
            ("bsw", self._pages["bsw"]),
            ("editor", self._pages["editor"]),
            ("spec", self._pages["spec"]),
            ("swc", self._pages["swc"]),
        ]))
        self.stack.addWidget(self._pages["com"])
        self._workspace_stacks["com"] = self._pages["com"]
        self._workspace_features["com"] = [
            "com_layout", "com_live", "com_pack"]
        self.stack.addWidget(self._build_feature_workspace("bus", [
            ("system", self._pages["system"]),
            ("nm", self._pages["nm"]),
            ("e2e", self._pages["e2e"]),
            ("secoc", self._pages["secoc"]),
        ]))
        self.stack.addWidget(self._build_feature_workspace("validate", [
            ("validate", self._pages["validate"]),
            ("timing", self._pages["timing"]),
            ("compare", self._pages["compare"]),
            ("merge", self._pages["merge"]),
            ("export", self._pages["export"]),
        ]))
        self.stack.addWidget(self._build_feature_workspace("setup", [
            ("setup", self._pages["setup"]),
        ]))

        self.document.on_changed(self._on_document_changed)
        saved = state_store.load_state(PLUGIN_ID, default={}) or {}
        self._restore_state(saved)
        if saved.get("session"):
            try:
                self.session.apply_state(saved.get("session") or {})
            except Exception:
                pass
        goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
        page = start_page or goto.get("start_page") or saved.get("nav_page")
        if goto:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        # Default: Config → BSW
        self.goto_page(page or "bsw")

        suite_chrome.bind_nav_shortcuts(self, NAV_PAGES, self.goto_page)
        plugin_shell.bind_shortcut(
            self, "Ctrl+J",
            lambda: self._wb.set_panel_visible(not self._wb.is_panel_visible()))
        plugin_shell.bind_shortcut(
            self, "Ctrl+B",
            lambda: self._wb.set_sidebar_visible(
                not self._wb.is_sidebar_visible()))
        plugin_shell.bind_shortcut(self, "Ctrl+O", self.open_arxml)
        plugin_shell.bind_shortcut(self, "Ctrl+S", self.save_arxml)
        plugin_shell.bind_shortcut(self, "Ctrl+Shift+S", self.save_arxml_as)
        plugin_shell.bind_shortcut(self, "Ctrl+N", self.new_arxml)
        plugin_shell.bind_shortcut(self, "Ctrl+Shift+O", self.open_project)
        plugin_shell.bind_shortcut(self, "Ctrl+Shift+N", self.new_project)
        plugin_shell.bind_shortcut(self, "Ctrl+Z", self.undo_edit)
        plugin_shell.bind_shortcut(self, "Ctrl+Y", self.redo_edit)
        plugin_shell.bind_shortcut(self, "Ctrl+Return", self._menu_apply_editor)
        plugin_shell.bind_shortcut(self, "F7", self._menu_run_validate)
        plugin_shell.bind_shortcut(
            self, "Ctrl+Shift+D", self._menu_import_dbc)

        self.log(
            "SYS",
            "AUTOSAR Studio ready — BSW config + live COM/NM/E2E/SecOC, no codegen")
        self._on_document_changed()

    # ------------------------------------------------------------------
    def _build_feature_workspace(
            self, workspace: str,
            features: list[tuple[str, QWidget]]) -> QStackedWidget:
        """Stack of feature pages — Side Bar switches index (no top tabs)."""
        stack = QStackedWidget()
        stack.setObjectName("SuiteEditorStack")
        keys = []
        for key, page in features:
            stack.addWidget(page)
            keys.append(key)
        self._workspace_stacks[workspace] = stack
        self._workspace_features[workspace] = keys
        if workspace == "config":
            self._config_stack = stack
            self._config_feature_keys = keys
            self._config_tabs = None
        return stack

    def _select_workspace_tab(self, workspace: str, index: int):
        stack = self._workspace_stacks.get(workspace)
        if stack is None or index < 0 or index >= stack.count():
            return
        if stack.currentIndex() != index:
            stack.setCurrentIndex(index)
        self._apply_nested_feature(self._active_feature)
        sb = self._sidebars.get(workspace)
        if sb is not None and hasattr(sb, "select_section"):
            try:
                feat = self._active_feature
                if workspace == "config":
                    # Config sidebar highlights BSW/Editor/… not modules.
                    if feat not in ("bsw", "editor", "spec", "swc"):
                        feat = "bsw"
                    sb.select_section(feat, expand_bsw=True)
                else:
                    sb.select_section(feat)
            except Exception:
                pass

    def _apply_nested_feature(self, key: str):
        """COM / System nested Side Bar leaves."""
        if key in ("com_layout", "com_live", "com_pack", "com"):
            stack = self._workspace_stacks.get("com")
            idx = {"com_layout": 0, "com_live": 1, "com": 1,
                   "com_pack": 2}.get(key, 1)
            if stack is not None and 0 <= idx < stack.count():
                stack.setCurrentIndex(idx)
            return
        if key in ("system_tree", "system_validate", "system_export",
                   "system"):
            sys_page = self._pages.get("system")
            if sys_page is not None and hasattr(sys_page, "select_view"):
                view = key if key != "system" else "system_tree"
                try:
                    sys_page.select_view(view)
                except Exception:
                    pass

    def _open_recent_project(self, path: str):
        if not path or not arxml_project.is_project_dir(path):
            self.log("WARN", "Project not found: %s" % path)
            return
        if not self._confirm_discard():
            return
        try:
            self.document.open_project(path)
            self._remember_project(path)
            self.log("OK", "Opened project %s" % path)
            self.goto_page("project")
        except OSError as e:
            self.log("ERR", str(e))

    def _menu_import_dbc(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Import DBC", "", "DBC (*.dbc);;All (*)")
        if not path:
            return
        try:
            n = self.document.import_dbc(path)
            self.log("OK", "Imported DBC → %d PDUs; BSW synced" % n)
            self.goto_page("editor")
        except (OSError, ValueError) as e:
            QMessageBox.warning(self, "Import DBC", str(e))
            self.log("ERR", str(e))

    def _menu_import_arxml_com(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Import ARXML as COM", "",
            "ARXML (*.arxml *.xml);;All (*)")
        if not path:
            return
        try:
            model = arxmlparse.parse_arxml_model(path)
            self.document.apply_model(model)
            self.document.sync_bsw_from_com()
            self.log("OK", "Imported COM from %s" % os.path.basename(path))
            self.goto_page("editor")
        except OSError as e:
            self.log("ERR", str(e))

    def _menu_import_bswmd(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Import BSWMD JSON", "", "JSON (*.json);;All (*)")
        if not path:
            return
        try:
            rows = arxmlparse.load_bswmd_lite(path)
            mod, n = arxml_ecuc_schema.import_bswmd_schema(path, write=True)
            self.log(
                "OK",
                "Imported BSWMD tips (%d) + schema structure %s (+%d params)"
                % (len(rows), mod or "—", n))
            self.goto_page("spec")
        except (OSError, ValueError, KeyError) as e:
            self.log("ERR", str(e))

    def _menu_toggle_module(self):
        from PyQt6.QtWidgets import QInputDialog
        names = arxml_bsw.module_names()
        name, ok = QInputDialog.getItem(
            self, "Enable / Disable Module",
            "Module (toggle presence in project):", names, 0, False)
        if not ok or not name:
            return
        on = self.document.toggle_bsw_module(name)
        self.log("OK", "%s %s" % ("Enabled" if on else "Disabled", name))
        self.goto_page("bsw")

    def _menu_run_validate(self):
        self.goto_page("validate")
        page = self._pages.get("validate")
        if page is not None and hasattr(page, "run_lint"):
            page.run_lint()

    def _menu_recipe_pack(self):
        log = self.document.apply_fix_pack([
            "bump_dlc", "unique_can_ids", "name_empty_signals", "derive_ecuc"])
        for line in log:
            self.log("OK", line)
        self._menu_run_validate()

    def _menu_write_out(self):
        if not self.document.has_project():
            self.log("WARN", "Open a project first")
            self.goto_page("project")
            return
        try:
            paths = self.document.write_out_reports()
            self.log("OK", "Wrote %s" % paths.get("sarif", ""))
        except (OSError, ValueError) as e:
            self.log("ERR", str(e))

    def _menu_write_intermediates(self):
        if not self.document.has_project():
            self.goto_page("project")
            self.log("WARN", "Create or open a project first")
            return
        try:
            paths = self.document.write_intermediates()
            n = len(paths.get("bsw") or {})
            self.log("OK", "Wrote intermediates + %d BSW ARXMLs" % n)
            self._notify_host(paths.get("com", ""))
        except (OSError, ValueError) as e:
            self.log("ERR", str(e))

    def _menu_derive_ecuc(self):
        try:
            self.document.derive_ecuc()
            self.log("OK", "Derived ECUC from COM")
            self.goto_page("bsw")
        except (OSError, ValueError) as e:
            self.log("ERR", str(e))

    def _menu_init_bsw(self):
        n = self.document.init_all_bsw()
        self.log("OK", "Initialized %d BSW modules" % n)
        self.goto_page("bsw")

    def _menu_sync_bsw(self):
        self.document.sync_bsw_from_com()
        self.log("OK", "Synced Com/CanIf/PduR/CanNm from COM")
        self.goto_page("bsw")

    def _menu_goto_bsw(self, name: str):
        self.document.set_active_bsw(name)
        self.goto_page("bsw")
        page = self._pages.get("bsw")
        if page is not None and hasattr(page, "select_module"):
            page.select_module(name)

    def _menu_export_bsw_module(self):
        self.goto_page("bsw")
        page = self._pages.get("bsw")
        if page is not None and hasattr(page, "export_active_module"):
            page.export_active_module()
        else:
            self.log("WARN", "BSW page unavailable")

    def _menu_apply_editor(self):
        """Apply focused editor/BSW form when those pages are active."""
        for key in ("editor", "bsw", "swc"):
            page = self._pages.get(key)
            if page is not None and hasattr(page, "apply_from_menu"):
                page.apply_from_menu()
                return
        page = self._pages.get(self._active_feature)
        if page is not None and hasattr(page, "apply_from_menu"):
            page.apply_from_menu()

    def _menu_boundary(self):
        QMessageBox.information(
            self, "Boundary",
            "AUTOSAR Studio configures and validates Classic Platform ARXML "
            "intermediates only.\n\n"
            "It does not generate Com.c, Rte.c, Os, or other BSW source.\n"
            "Hand off work/com.arxml and work/bsw/*.arxml to DaVinci, "
            "tresos, or ISOLAR for generate.")

    def _menu_about(self):
        QMessageBox.information(
            self, "AUTOSAR Studio",
            "AUTOSAR Studio — AUTOSAR CP configuration & validation.\n\n"
            "Project workspace with COM extract + one ARXML per BSW module "
            "(Os, Com, ComM, PduR, BswM, CanIf, CanSM, CanTp, Dcm, Dem, …).\n\n"
            "Does not generate BSW/RTE source — hand off ARXML to DaVinci / "
            "tresos / ISOLAR.\n\n"
            "Import DBC seeds I-PDUs/signals; Sync BSW updates Com/CanIf/"
            "PduR/CanNm/EcuC.")

    def _on_lint_gate_toggled(self, checked: bool):
        self._lint_before_save = bool(checked)

    def _init_document_controls(self):
        self.path_label = QLabel("(unsaved)")
        self.path_label.setObjectName("SuiteDocPath")
        self.path_label.setMinimumWidth(120)
        self.path_label.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)

        self.dirty_label = QLabel("")
        self.dirty_label.setObjectName("SuiteDirtyDot")
        self.dirty_label.setToolTip("Unsaved changes")

        self._doc_btns = []
        specs = (
            ("", "Undo (Ctrl+Z)", "undo", self.undo_edit, False),
            ("", "Redo (Ctrl+Y)", "redo", self.redo_edit, False),
            ("", "Open ARXML (Ctrl+O)", "browse", self.open_arxml, False),
            ("Save", "Save file or project (Ctrl+S)", "save",
             self.save_arxml, True),
        )
        for text, tip, icon, slot, primary in specs:
            btn = QPushButton(text)
            btn.setCursor(Qt.CursorShape.PointingHandCursor)
            btn.setToolTip(tip)
            if text:
                btn.setFixedHeight(26)
            else:
                btn.setFixedSize(28, 26)
            if not primary:
                btn.setObjectName("GhostButton")
            codicons.set_button(btn, icon, size=12, primary=primary)
            btn.clicked.connect(slot)
            self._doc_btns.append(btn)

    def _build_menubar(self):
        """Native File/Edit/Import/Project/BSW/Validate/View/Help menubar."""
        bar = self.menuBar()
        if bar is None:
            bar = QMenuBar(self)
            self.setMenuBar(bar)
        bar.clear()
        bar.setVisible(True)

        def _act(menu, label, slot, shortcut=None):
            a = menu.addAction(label)
            a.triggered.connect(slot)
            if shortcut:
                a.setShortcut(QKeySequence(shortcut))
            return a

        m_file = bar.addMenu("&File")
        _act(m_file, "New &Project…", self.new_project, "Ctrl+Shift+N")
        _act(m_file, "&Open Project…", self.open_project, "Ctrl+Shift+O")
        self._recent_proj_menu = m_file.addMenu("Recent &Projects")
        self._fill_recent_projects_menu()
        m_file.addSeparator()
        _act(m_file, "Open &ARXML…", self.open_arxml, "Ctrl+O")
        _act(m_file, "&Save", self.save_arxml, "Ctrl+S")
        _act(m_file, "Save &As…", self.save_arxml_as, "Ctrl+Shift+S")
        _act(m_file, "&New file", self.new_arxml, "Ctrl+N")
        m_file.addSeparator()
        _act(m_file, "&Write Intermediates", self._menu_write_intermediates)
        m_file.addSeparator()
        _act(m_file, "Attach to AI Chat", self.attach_ai)
        m_file.addSeparator()
        _act(m_file, "E&xit", self.close)

        m_edit = bar.addMenu("&Edit")
        _act(m_edit, "&Undo", self.undo_edit, "Ctrl+Z")
        _act(m_edit, "&Redo", self.redo_edit, "Ctrl+Y")
        m_edit.addSeparator()
        _act(m_edit, "&Apply", self._menu_apply_editor, "Ctrl+Return")

        m_imp = bar.addMenu("&Import")
        _act(m_imp, "Import &DBC…", self._menu_import_dbc, "Ctrl+Shift+D")
        _act(m_imp, "Import ARXML as &COM…", self._menu_import_arxml_com)
        _act(m_imp, "Import &BSWMD JSON…", self._menu_import_bswmd)

        m_proj = bar.addMenu("&Project")
        _act(m_proj, "&Project Page", lambda: self.goto_page("project"))
        _act(m_proj, "&Derive ECUC from COM", self._menu_derive_ecuc)
        _act(m_proj, "Init &All BSW Modules", self._menu_init_bsw)
        _act(m_proj, "&Sync BSW from COM", self._menu_sync_bsw)
        _act(m_proj, "&Enable / Disable Module…", self._menu_toggle_module)
        m_wiz = m_proj.addMenu("&Wizards")
        _act(m_wiz, "Init &communication stack",
             self._menu_wizard_comm)
        _act(m_wiz, "Add &diagnostic path",
             self._menu_wizard_diag)
        _act(m_wiz, "&Os tasks from I-PDUs (lite)",
             self._menu_wizard_os)

        m_bsw = bar.addMenu("&BSW")
        _act(m_bsw, "&BSW Configurator", lambda: self.goto_page("bsw"))
        m_bsw.addSeparator()
        _act(m_bsw, "&Init All Modules", self._menu_init_bsw)
        _act(m_bsw, "&Sync from COM", self._menu_sync_bsw)
        _act(m_bsw, "&Write Module ARXMLs", self._menu_write_intermediates)
        _act(m_bsw, "&Export Active Module…", self._menu_export_bsw_module)
        m_bsw.addSeparator()
        for group, rows in arxml_bsw.catalog_by_group().items():
            sub = m_bsw.addMenu(group)
            for name, summary in rows:
                a = sub.addAction(name)
                a.setToolTip(summary)
                a.triggered.connect(
                    lambda _c=False, n=name: self._menu_goto_bsw(n))

        m_val = bar.addMenu("&Validate")
        _act(m_val, "&Run Validate", self._menu_run_validate, "F7")
        _act(m_val, "Apply &Recipe Pack", self._menu_recipe_pack)
        _act(m_val, "Write &out/ Reports", self._menu_write_out)
        m_val.addSeparator()
        lint = QAction("Lint on save", self)
        lint.setCheckable(True)
        lint.setChecked(self._lint_before_save)
        lint.setToolTip("Ask before saving when Validate reports errors")
        lint.toggled.connect(self._on_lint_gate_toggled)
        self._lint_action = lint
        m_val.addAction(lint)

        m_view = bar.addMenu("&View")
        for key, title in NAV_PAGES:
            _act(m_view, title, lambda _c=False, k=key: self.goto_page(k))
        m_view.addSeparator()
        _act(m_view, "Toggle &Side Bar",
             lambda: self._wb.set_sidebar_visible(
                 not self._wb.is_sidebar_visible()), "Ctrl+B")
        _act(m_view, "Toggle &OUTPUT",
             lambda: self._wb.set_panel_visible(
                 not self._wb.is_panel_visible()), "Ctrl+J")

        m_help = bar.addMenu("&Help")
        _act(m_help, "&Spec Encyclopedia", lambda: self.goto_page("spec"))
        _act(m_help, "&Handoff report…", self._menu_handoff_report)
        _act(m_help, "&Boundary (no codegen)", self._menu_boundary)
        _act(m_help, "&About AUTOSAR Studio", self._menu_about)

    def _menu_wizard_comm(self):
        log = self.document.wizard_init_comm_stack()
        for line in log:
            self.log("OK", line)
        self.goto_page("bsw")

    def _menu_wizard_diag(self):
        log = self.document.wizard_add_diag_path()
        for line in log:
            self.log("OK", line)
        self.goto_page("bsw")

    def _menu_wizard_os(self):
        log = self.document.wizard_os_tasks_lite()
        for line in log:
            self.log("OK", line)
        self.goto_page("bsw")

    def _menu_handoff_report(self):
        self.goto_page("export")
        try:
            root = ""
            if self.document.has_project() and self.document.manifest:
                root = self.document.manifest.root
            path = os.path.join(root or ".", "out", "handoff_davinci.html")
            if root:
                os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as f:
                f.write(self.document.handoff_html_report())
            self.log("OK", "Handoff report %s" % path)
            try:
                os.startfile(path)
            except OSError as exc:
                self.log("WARN", "Open report: %s" % exc)
        except OSError as e:
            self.log("ERR", str(e))

    def set_workspace_badges(self, counts: dict):
        """Update Side Bar section labels with error counts (e.g. Findings (3))."""
        self._sidebar_badges = dict(counts or {})
        for ws, sidebar in (getattr(self, "_workspace_sidebars", {}) or {}).items():
            update = getattr(sidebar, "set_badges", None)
            if callable(update):
                update(self._sidebar_badges)
            # Also refresh validate section text if present
            tree = getattr(sidebar, "tree", None)
            if tree is None:
                continue
            for i in range(tree.topLevelItemCount()):
                it = tree.topLevelItem(i)
                data = it.data(0, Qt.ItemDataRole.UserRole) or ()
                if len(data) >= 2 and data[1] == "validate":
                    n = int(self._sidebar_badges.get("validate") or 0)
                    base = "Findings"
                    it.setText(0, "%s (%d)" % (base, n) if n else base)

    def _mount_menubar_trailing(self):
        """Dirty + doc actions + layout toggles on the native menubar row."""
        host = QWidget()
        host.setObjectName("SuiteMenubarTrailing")
        row = QHBoxLayout(host)
        row.setContentsMargins(4, 0, 6, 0)
        row.setSpacing(2)
        row.addWidget(self.dirty_label)
        for btn in self._doc_btns:
            row.addWidget(btn)
        for btn in (
            self._wb.btn_sidebar,
            self._wb.btn_panel,
            self._wb.btn_maximize,
        ):
            btn.setParent(None)
            row.addWidget(btn)
        self._menubar_trailing = host
        bar = self.menuBar()
        if bar is not None:
            bar.setCornerWidget(host, Qt.Corner.TopRightCorner)

    def _fill_recent_projects_menu(self):
        menu = getattr(self, "_recent_proj_menu", None)
        if menu is None:
            return
        menu.clear()
        paths = self.recent_projects()
        if not paths:
            a = menu.addAction("(none)")
            a.setEnabled(False)
            return
        for path in paths:
            a = menu.addAction(path)
            a.triggered.connect(
                lambda _c=False, p=path: self._open_recent_project(p))

    def _park_chrome_widgets(self):
        """Park path label before chrome remount (no durable tab bars)."""
        park = self._chrome_park
        if getattr(self, "path_label", None) is not None:
            self.path_label.setParent(park)
        # dirty_label / _doc_btns / layout toggles live on menubar trailing.

    def _mount_chrome(self, page_key: str):
        """Chrome row: feature title + path (Side Bar owns navigation)."""
        self._park_chrome_widgets()
        old = self._chrome_host
        host = QWidget()
        host.setObjectName("SuiteEditorTabHost")
        row = QHBoxLayout(host)
        row.setContentsMargins(10, 0, 6, 0)
        row.setSpacing(6)
        feat = getattr(self, "_active_feature", "") or page_key
        title_text = FEATURE_TITLES.get(
            feat, dict(NAV_PAGES).get(page_key, page_key))
        title = QLabel(title_text)
        title.setObjectName("SuiteEditorTitle")
        row.addWidget(title)
        row.addWidget(self.path_label, 1)
        self._chrome_host = host
        self._wb.set_editor_tabs(host)
        if old is not None and old is not host:
            old.deleteLater()
        mb = self.menuBar()
        if mb is not None:
            mb.setVisible(True)
            trailing = getattr(self, "_menubar_trailing", None)
            if trailing is not None:
                mb.setCornerWidget(trailing, Qt.Corner.TopRightCorner)

    def _on_workbench_page(self, key: str):
        """Chrome remount + Side Bar body for the active workspace."""
        self._mount_chrome(key)
        setter = getattr(self._wb, "set_side_bar_widget", None)
        if callable(setter):
            setter(self._sidebars.get(key))
        if key == "config":
            bsw = self._pages.get("bsw")
            if bsw is not None and hasattr(bsw, "refresh_view"):
                try:
                    bsw.refresh_view()
                except Exception:
                    pass
            if bsw is not None and hasattr(bsw, "_apply_bsw_split"):
                try:
                    from PyQt6.QtCore import QTimer
                    QTimer.singleShot(0, bsw._apply_bsw_split)
                except Exception:
                    pass
        sb = self._sidebars.get(key)
        if sb is not None and hasattr(sb, "select_section"):
            try:
                feat = getattr(self, "_active_feature", "") or key
                if key == "config":
                    if feat not in ("bsw", "editor", "spec", "swc"):
                        feat = "bsw"
                    sb.select_section(feat, expand_bsw=True)
                else:
                    sb.select_section(feat)
            except Exception:
                pass
        try:
            from _shared import activity_snapshot
            activity_snapshot.update(
                active_plugin="autosar-suite", active_page=key)
        except Exception:
            pass

    def _build_output_panel(self):
        """OUTPUT = Log terminal + BSW Live Findings (one collapsible panel)."""
        from pages import _ui

        pause, clear_btn, term = suite_chrome.mount_compact_output(self._wb)
        self.log_pause = pause
        self.log_term = term
        clear_btn.clicked.connect(self.clear_log)

        tools = self._wb.panel_tools
        mode_log = QToolButton()
        mode_log.setObjectName("SuitePanelTab")
        mode_log.setText("Log")
        mode_log.setCheckable(True)
        mode_log.setChecked(True)
        mode_log.setAutoRaise(True)
        mode_log.setCursor(Qt.CursorShape.PointingHandCursor)
        mode_log.setToolTip("Session / plugin log")
        mode_find = QToolButton()
        mode_find.setObjectName("SuitePanelTab")
        mode_find.setText("Findings")
        mode_find.setCheckable(True)
        mode_find.setAutoRaise(True)
        mode_find.setCursor(Qt.CursorShape.PointingHandCursor)
        mode_find.setToolTip("BSW live schema findings")
        find_summary = _ui.quiet_label("")
        find_summary.setVisible(False)
        fix_btn = _ui.ghost_btn("", "Apply fix for selected finding", "apply")
        fix_btn.setFixedWidth(28)
        recheck_btn = _ui.ghost_btn("", "Re-run live schema checks", "refresh")
        recheck_btn.setFixedWidth(28)
        fix_btn.setVisible(False)
        recheck_btn.setVisible(False)
        # Mode + findings actions before the stretch / pause / clear.
        tools.insertWidget(0, mode_log)
        tools.insertWidget(1, mode_find)
        tools.insertWidget(2, find_summary)
        tools.insertWidget(3, fix_btn)
        tools.insertWidget(4, recheck_btn)

        find_tree = QTreeWidget()
        find_tree.setHeaderLabels(
            ["Sev", "Rule", "Location", "Message", "Fix"])
        _ui.style_tree(find_tree)
        find_tree.setRootIsDecorated(False)
        find_tree.setSelectionBehavior(
            QAbstractItemView.SelectionBehavior.SelectRows)
        find_tree.setToolTip("Double-click to jump to container / module")
        find_tree.header().setSectionResizeMode(
            3, QHeaderView.ResizeMode.Stretch)

        stack = QStackedWidget()
        body = self._wb.panel_body
        body.removeWidget(term)
        stack.addWidget(term)
        stack.addWidget(find_tree)
        body.addWidget(stack, 1)

        self._output_stack = stack
        self._out_mode_log = mode_log
        self._out_mode_find = mode_find
        self._findings_tree = find_tree
        self._findings_summary = find_summary
        self._findings_fix_btn = fix_btn
        self._findings_recheck_btn = recheck_btn

        def _set_mode(idx: int):
            stack.setCurrentIndex(idx)
            mode_log.setChecked(idx == 0)
            mode_find.setChecked(idx == 1)
            pause.setVisible(idx == 0)
            clear_btn.setVisible(idx == 0)
            find_summary.setVisible(idx == 1)
            fix_btn.setVisible(idx == 1)
            recheck_btn.setVisible(idx == 1)

        mode_log.clicked.connect(lambda: _set_mode(0))
        mode_find.clicked.connect(lambda: _set_mode(1))
        self._set_output_mode = _set_mode

    def show_findings_panel(self):
        """Reveal OUTPUT and switch to Findings (used after live check)."""
        if self._wb is not None:
            self._wb.set_panel_visible(True)
        setter = getattr(self, "_set_output_mode", None)
        if callable(setter):
            setter(1)

    def log(self, source: str, message: str, color: str | None = None):
        plugin_shell.set_status(
            self, "%s  %s" % (source, (message or "")[:72]), 4000)
        if self.log_pause.isChecked():
            return
        ts = time.time()
        tstr = (time.strftime("%H:%M:%S", time.localtime(ts))
                + ".%03d" % int(ts % 1 * 1000))
        colors = {
            "ERR": vscode_theme.ERR, "WARN": vscode_theme.WARN_FG,
            "OK": vscode_theme.OK, "SYS": vscode_theme.ACCENT,
        }
        hex_c = colors.get(color or source, vscode_theme.TEXT_MUTED)
        line = "%s  %s  %s" % (tstr, source, message or "")
        from PyQt6.QtGui import QColor, QTextCharFormat, QTextCursor
        fmt = QTextCharFormat()
        fmt.setForeground(QColor(hex_c))
        cursor = self.log_term.textCursor()
        cursor.movePosition(QTextCursor.MoveOperation.End)
        if self.log_term.blockCount() > 1 or self.log_term.toPlainText():
            cursor.insertText("\n")
        cursor.insertText(line, fmt)
        self.log_term.setTextCursor(cursor)
        self.log_term.ensureCursorVisible()

    def clear_log(self):
        self.log_term.clear()

    def _session_log(self, direction, can_id, pdu, note, color=None):
        """Adapter for live COM session → compact OUTPUT."""
        try:
            if isinstance(can_id, int):
                cid = "0x%X" % can_id
            else:
                cid = str(can_id)
        except Exception:
            cid = str(can_id)
        msg = "%s  %s  %s" % (cid, note or "", "")
        src = str(direction or "SYS")
        self.log(src, msg.strip(), color if isinstance(color, str) else None)

    def _elide_path(self, path: str) -> str:
        text = path or "(unsaved)"
        fm = QFontMetrics(self.path_label.font())
        width = max(180, self.path_label.width() or 320)
        if fm.horizontalAdvance(text) <= width:
            return text
        base = os.path.basename(text) if text != "(unsaved)" else text
        short = "…/" + base if base and base != text else text
        return fm.elidedText(short, Qt.TextElideMode.ElideLeft, width)

    def _on_document_changed(self):
        label = self.document.strip_label()
        self.path_label.setText(self._elide_path(label))
        self.path_label.setToolTip(label)
        self.dirty_label.setText("●" if self.document.dirty else "")
        # Fixed OS caption — path/dirty stay on the chrome row only.
        self.setWindowTitle("AUTOSAR Studio")
        self._sync_session_from_document()
        self._schedule_persist()

    def _sync_session_from_document(self):
        """Keep Live COM PDUs aligned with the Config document model."""
        try:
            from core.ipdu import Ipdu as CoreIpdu, Signal as CoreSig
            src = list(self.document.model.ipdus or [])
            out = []
            for p in src:
                sigs = []
                for s in (p.signals or []):
                    sigs.append(CoreSig(
                        name=s.name,
                        start_bit=int(s.start_bit),
                        length=int(s.length),
                        endian=getattr(s, "endian", "intel") or "intel",
                        factor=float(getattr(s, "factor", 1.0) or 1.0),
                        offset=float(getattr(s, "offset", 0.0) or 0.0),
                        unit=str(getattr(s, "unit", "") or ""),
                    ))
                out.append(CoreIpdu(
                    name=p.name,
                    can_id=int(p.can_id),
                    dlc=int(p.dlc),
                    signals=sigs,
                ))
            # Avoid notify loops — replace without SYS log spam when identical.
            cur_names = [x.name for x in (self.session.ipdus or [])]
            new_names = [x.name for x in out]
            if cur_names == new_names and len(cur_names) == len(out):
                same = True
                for a, b in zip(self.session.ipdus or [], out):
                    if a.can_id != b.can_id or a.dlc != b.dlc:
                        same = False
                        break
                    if len(a.signals) != len(b.signals):
                        same = False
                        break
                if same:
                    return
            self.session.ipdus = out
            if self.session.active >= len(out):
                self.session.active = 0
            self.session.notify()
        except Exception as exc:
            self.log("WARN", "Session sync: %s" % exc)

    def _schedule_persist(self):
        """Debounce disk writes — every document notify used to sync-write JSON."""
        from PyQt6.QtCore import QTimer
        timer = getattr(self, "_persist_timer", None)
        if timer is None:
            timer = QTimer(self)
            timer.setSingleShot(True)
            timer.setInterval(500)
            timer.timeout.connect(self._persist)
            self._persist_timer = timer
        timer.start()

    def _remember_path(self, path: str):
        if not path:
            return
        path = os.path.normpath(path)
        self._recent = [p for p in self._recent if os.path.normpath(p) != path]
        self._recent.insert(0, path)
        self._recent = self._recent[:MAX_RECENT]
        self._persist()

    def recent_files(self) -> list:
        return list(self._recent)

    def recent_projects(self) -> list:
        return list(self._recent_projects)

    def _remember_project(self, path: str):
        if not path:
            return
        path = os.path.normpath(path)
        self._recent_projects = [
            p for p in self._recent_projects if os.path.normpath(p) != path]
        self._recent_projects.insert(0, path)
        self._recent_projects = self._recent_projects[:MAX_RECENT]
        self._persist()
        self._fill_recent_projects_menu()

    def new_project(self):
        self.goto_page("project")
        self.log("SYS", "Use Project page → New project…")

    def open_project(self):
        if not self._confirm_discard():
            return
        path = QFileDialog.getExistingDirectory(self, "Open project folder")
        if not path:
            return
        if not arxml_project.is_project_dir(path):
            QMessageBox.warning(
                self, "AUTOSAR Studio", "No project.json in:\n%s" % path)
            return
        try:
            self.document.open_project(path)
        except OSError as e:
            QMessageBox.warning(self, "AUTOSAR Studio", "Failed:\n%s" % e)
            return
        diag = getattr(self.document, "_open_diag", "") or ""
        if diag:
            self.document._open_diag = ""
            self.log("WARN", diag)
            QMessageBox.warning(
                self, "AUTOSAR Studio",
                "Project opened with parse issues:\n\n%s" % diag)
        self._remember_project(path)
        self.log("OK", "Opened project %s" % path)
        self.goto_page("project")

    def new_arxml(self):
        if not self._confirm_discard():
            return
        box = QMessageBox(self)
        box.setWindowTitle("New document")
        box.setText("Empty COM extract, project, or Library starters?")
        empty_btn = box.addButton("Empty", QMessageBox.ButtonRole.AcceptRole)
        proj_btn = box.addButton("Project…", QMessageBox.ButtonRole.ActionRole)
        lib_btn = box.addButton("Starters…", QMessageBox.ButtonRole.ActionRole)
        box.addButton(QMessageBox.StandardButton.Cancel)
        box.setDefaultButton(lib_btn)
        box.exec()
        clicked = box.clickedButton()
        if clicked is lib_btn:
            self.goto_page("library")
            return
        if clicked is proj_btn:
            self.goto_page("project")
            return
        if clicked is not empty_btn:
            return
        self.document.new()
        self.log("SYS", "New empty ARXML")

    def open_arxml(self):
        if not self._confirm_discard():
            return
        path, _ = QFileDialog.getOpenFileName(
            self, "Open ARXML", self.document.path or "",
            "ARXML (*.arxml *.xml);;All (*)")
        if path:
            self._load_path(path)

    def _load_path(self, path: str) -> bool:
        try:
            self.document.load(path)
        except OSError as e:
            QMessageBox.warning(self, "AUTOSAR Studio", "Failed:\n%s" % e)
            return False
        self._remember_path(path)
        n = len(self.document.model.ipdus)
        self.log("OK", "Opened %s (%d PDUs)" % (os.path.basename(path), n))
        return True

    def save_arxml(self):
        if self.document.has_project():
            if not self._warn_lint_before_save():
                return False
            try:
                self.document.save_project()
            except (OSError, ValueError) as e:
                QMessageBox.warning(
                    self, "AUTOSAR Studio", "Save project failed:\n%s" % e)
                return False
            self._remember_project(self.document.project_root)
            com = self.document.handoff_com_path()
            self.log("OK", "Saved project → %s" % com)
            self._notify_host(com)
            return True
        if not self.document.path:
            return self.save_arxml_as()
        if not self._warn_lint_before_save():
            return False
        try:
            self.document.save()
        except (OSError, ValueError) as e:
            QMessageBox.warning(self, "AUTOSAR Studio", "Save failed:\n%s" % e)
            return False
        self._remember_path(self.document.path)
        self.log("OK", "Saved %s" % self.document.path)
        self._notify_host(self.document.path)
        return True

    def save_arxml_as(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "Save ARXML As",
            self.document.path or self.document.display_name(),
            "ARXML (*.arxml);;All (*)")
        if not path:
            return False
        if not path.lower().endswith((".arxml", ".xml")):
            path += ".arxml"
        if not self._warn_lint_before_save():
            return False
        try:
            self.document.save(path)
        except (OSError, ValueError) as e:
            QMessageBox.warning(self, "AUTOSAR Studio", "Save failed:\n%s" % e)
            return False
        self._remember_path(path)
        self.log("OK", "Saved as %s" % path)
        self._notify_host(path)
        return True

    def undo_edit(self):
        if self.document.undo():
            self.log("SYS", "Undo")

    def redo_edit(self):
        if self.document.redo():
            self.log("SYS", "Redo")

    def attach_ai(self):
        path = self.document.handoff_com_path() or self.document.path
        if not path:
            QMessageBox.information(
                self, "AUTOSAR Studio", "Save first, then Attach.")
            return
        try:
            summary = {}
            if hasattr(self.document, "summary"):
                summary = self.document.summary() or {}
            att = ai_attach.Attachment(
                kind="arxml",
                title=os.path.basename(path),
                uri="file:///" + path.replace("\\", "/"),
                preview="%s · %d PDUs" % (
                    os.path.basename(path),
                    summary.get("pdu_count", len(self.document.model.ipdus))),
                payload={"path": path, "summary": summary},
                strategy="lazy",
                provenance={"source": PLUGIN_ID},
            )
            ai_attach.attach(att)
            self.log("OK", "Attached to AI Chat")
        except Exception as e:
            self.log("ERR", "AI attach failed: %s" % e)

    def _notify_host(self, path: str):
        """Ask host to notice this ARXML handoff path."""
        if not path:
            return
        try:
            import sin
            sin.output.append("AUTOSAR Studio wrote %s" % path)
        except Exception:
            pass
        try:
            from sin._transport import send_notification
            send_notification("arxml.reload", {"path": path})
        except Exception:
            pass

    def _warn_lint_before_save(self) -> bool:
        if not self._lint_before_save:
            return True
        try:
            findings = self.document.validate_all()
            n_err = sum(1 for f in findings if f.get("level") == "error"
                        or f.get("severity") == "error")
            if n_err <= 0:
                return True
            box = QMessageBox(self)
            box.setWindowTitle("Validate before save")
            box.setText(
                "Document has %d error(s).\nReview in Validate, or save anyway."
                % n_err)
            review = box.addButton("Review", QMessageBox.ButtonRole.ActionRole)
            save_btn = box.addButton(
                "Save anyway", QMessageBox.ButtonRole.AcceptRole)
            box.addButton(QMessageBox.StandardButton.Cancel)
            box.setDefaultButton(review)
            box.exec()
            clicked = box.clickedButton()
            if clicked is review:
                self.goto_page("validate")
                page = self._pages.get("validate")
                if page is not None and hasattr(page, "run_lint"):
                    page.run_lint()
                return False
            return clicked is save_btn
        except Exception:
            return True

    def _confirm_discard(self) -> bool:
        if not self.document.dirty:
            return True
        ans = QMessageBox.question(
            self, "Unsaved changes",
            "Document has unsaved changes. Discard?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        return ans == QMessageBox.StandardButton.Yes

    def goto_editor_target(self, pdu: str = "", signal: str = ""):
        if getattr(self.document, "editor_mode", "com") != "com":
            self.document.set_editor_mode("com")
        self.goto_page("editor")
        api = self._pages.get("editor")
        if api is not None and hasattr(api, "select_target"):
            api.select_target(pdu, signal)

    def goto_page(self, key: str):
        """Resolve feature keys to workspace + stack index; chrome via Side Bar."""
        if not key:
            key = "bsw"
        route = FEATURE_ROUTE.get(key)
        if route is None:
            if key in dict(NAV_PAGES):
                workspace, tab = key, None
            else:
                workspace, tab = "config", 0
                key = "bsw"
        else:
            workspace, tab = route
        # Prefer the requested feature key for chrome / Side Bar highlight.
        self._active_feature = key
        # Normalize activity / parent keys to a leaf Side Bar feature.
        leaf_alias = {
            "com": "com_live",
            "bus": "system_tree",
            "config": "bsw",
            "system": "system_tree",
            "setup": "setup",
            "project": "project",
            "validate": "validate",
        }
        if key in leaf_alias:
            self._active_feature = leaf_alias[key]
        if key in dict(NAV_PAGES) and tab is None:
            keys = self._workspace_features.get(workspace) or []
            feat = self._active_feature
            if feat in keys:
                tab = keys.index(feat)
            elif workspace == "com":
                tab = 1
            else:
                tab = 0
        if tab is not None:
            self._select_workspace_tab(workspace, tab)
        else:
            self._apply_nested_feature(self._active_feature)
        cur = self._wb.current_page() if self._wb else None
        if cur != workspace:
            self._wb.goto_page(workspace)
        else:
            # Same workspace — refresh chrome title + Side Bar highlight.
            self._mount_chrome(workspace)
            sb = self._sidebars.get(workspace)
            if sb is not None and hasattr(sb, "select_section"):
                try:
                    feat = self._active_feature
                    if workspace == "config":
                        if feat not in ("bsw", "editor", "spec", "swc"):
                            feat = "bsw"
                        sb.select_section(feat, expand_bsw=True)
                    else:
                        sb.select_section(feat)
                except Exception:
                    pass
            self._apply_nested_feature(self._active_feature)
        self._schedule_persist()

    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        page = self._wb.current_page() if self._wb else "config"
        # Persist feature key when possible so restore lands on the same tab.
        nav = self._active_feature or page or "bsw"
        sess = {}
        try:
            sess = self.session.to_state()
        except Exception:
            pass
        state_store.save_state(PLUGIN_ID, {
            "last_path": self.document.path,
            "last_project": self.document.project_root,
            "recent": list(self._recent),
            "recent_projects": list(self._recent_projects),
            "nav_page": nav,
            "geometry_hex": geo,
            "lint_before_save": bool(self._lint_before_save),
            "sidebar": self._wb.is_sidebar_visible() if self._wb else True,
            "panel": self._wb.is_panel_visible() if self._wb else True,
            "chrome_ux": 1,
            "editor_mode": self.document.editor_mode,
            "session": sess,
        })

    def _restore_state(self, saved: dict):
        if not saved:
            return
        try:
            self._recent = [
                p for p in (saved.get("recent") or []) if p][:MAX_RECENT]
            self._recent_projects = [
                p for p in (saved.get("recent_projects") or []) if p][:MAX_RECENT]
            # Geometry restore skipped — suite always opens maximized (showMaximized).
            # Persisting a tiny last-session size was crushing the BSW split layout.
            last_proj = saved.get("last_project") or ""
            last = saved.get("last_path") or ""
            from_suite = bool(saved.get("from_autosar"))
            if from_suite and last and os.path.isfile(last):
                try:
                    self.document.load(last)
                except OSError:
                    pass
            elif last_proj and arxml_project.is_project_dir(last_proj):
                try:
                    self.document.open_project(last_proj)
                except OSError:
                    pass
            elif last and os.path.isfile(last):
                try:
                    self.document.load(last)
                except OSError:
                    pass
            mode = saved.get("editor_mode")
            if mode in ("com", "ecuc"):
                self.document.editor_mode = mode
            lint_gate = saved.get("lint_before_save")
            if lint_gate is not None:
                self._lint_before_save = bool(lint_gate)
                if getattr(self, "_lint_action", None) is not None:
                    self._lint_action.setChecked(self._lint_before_save)
            if "sidebar" in saved:
                self._wb.set_sidebar_visible(bool(saved["sidebar"]))
            # OUTPUT visible by default. Old persist always wrote panel=False;
            # only honor saved panel after chrome_ux redesign.
            if int(saved.get("chrome_ux") or 0) >= 1 and "panel" in saved:
                self._wb.set_panel_visible(bool(saved["panel"]))
            else:
                self._wb.set_panel_visible(True)
        except (TypeError, ValueError):
            pass

    def closeEvent(self, event):
        if self.document.dirty:
            ans = QMessageBox.question(
                self, "Unsaved changes",
                "Document has unsaved changes. Close anyway?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
            if ans != QMessageBox.StandardButton.Yes:
                event.ignore()
                return
        self._persist()
        super().closeEvent(event)

    def shutdown(self):
        self._persist()
