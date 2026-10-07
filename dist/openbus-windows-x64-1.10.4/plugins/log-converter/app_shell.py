# -*- coding: utf-8 -*-
"""AppShell — Log Converter (VS Code chrome: Activity + Side Bar + Tabs + OUTPUT)."""

from __future__ import annotations

import os
import time
from typing import Optional

from PyQt6.QtGui import QColor, QKeySequence
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QCheckBox,
    QHeaderView,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QStackedWidget,
    QTableWidgetItem,
    QWidget,
)  # QHeaderView used by log table

from _shared import codicons, plugin_shell, state_store, suite_chrome, suite_tabs
from pages import _ui
from session import SharedSession

PLUGIN_ID = "log-converter"

NAV_PAGES = [
    ("convert", "Convert"),
    ("batch", "Batch"),
    ("inspect", "Inspect"),
    ("jobs", "Jobs"),
]

FEATURE_ROUTE = {
    "convert": ("convert", 0),
    "batch": ("batch", 0),
    "inspect": ("inspect", 0),
    "jobs": ("jobs", 0),
}

FEATURE_TITLES = {
    "convert": "Convert",
    "batch": "Batch",
    "inspect": "Inspect",
    "jobs": "Jobs",
}

_WORKSPACE_DEFAULT = {
    "convert": "convert",
    "batch": "batch",
    "inspect": "inspect",
    "jobs": "jobs",
}

_PAGE_ALIASES = {
    "single": "convert",
    "queue": "batch",
    "probe": "inspect",
    "history": "jobs",
}

_ACTIVITY_KEYS = frozenset(k for k, _ in NAV_PAGES)
_LEAF_KEYS = frozenset(FEATURE_TITLES.keys())


def _is_leaf_feature(feature: str) -> bool:
    return feature in _LEAF_KEYS and feature in FEATURE_ROUTE


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("Log Converter")
        self.setMinimumSize(1100, 720)
        self.resize(1200, 800)

        self._context = context
        self.session = SharedSession(parent=self)
        self._log_buffer: list = []
        self._pages = {}
        self._workspace_stacks: dict = {}
        self._workspace_features: dict = {}
        self._workspace_stack_index: dict[str, int] = {}
        self._sidebars: dict = {}
        self._active_feature = "convert"
        self._open_tabs: list[str] = []
        self._tab_bar = None
        self._status_chrome_mounted = False

        try:
            codicons.clear_pixmap_cache()
        except Exception:
            pass

        _icon_svg = os.path.join(os.path.dirname(__file__), "icon.svg")
        if os.path.isfile(_icon_svg):
            try:
                self.setWindowIcon(codicons.window_icon_from_svg(_icon_svg))
            except Exception:
                pass

        plugin_shell.attach_status_bar(self, "Ready")
        plugin_shell.wire_close_deactivates(self, PLUGIN_ID)

        self._wb = suite_chrome.build_workbench(
            self, NAV_PAGES, title="Log Converter", panel_title="OUTPUT",
            panel_visible=True, sidebar_visible=True,
            side_bar_enabled=True, side_bar_visible=True, lock_activity=True,
            side_bar_width=200)
        _ui.apply_converter_chrome(self)
        self.stack = self._wb.stack

        self._init_status_controls()
        self._build_menubar()
        self._build_log_panel()
        self.session.set_log_fn(self._log_row)

        from pages import batch, convert, inspect, jobs, workspace_sidebar

        self._pages["convert"] = convert.build(
            self, self.session, self._log_row)
        self._pages["batch"] = batch.build(
            self, self.session, self._log_row)
        self._pages["inspect"] = inspect.build(
            self, self.session, self._log_row)
        self._pages["jobs"] = jobs.build(
            self, self.session, self._log_row)

        self._sidebars["convert"] = workspace_sidebar.build_convert_sidebar(self)
        self._sidebars["batch"] = workspace_sidebar.build_batch_sidebar(self)
        self._sidebars["inspect"] = workspace_sidebar.build_inspect_sidebar(self)
        self._sidebars["jobs"] = workspace_sidebar.build_jobs_sidebar(self)

        self._add_workspace("convert", [("convert", self._pages["convert"])])
        self._add_workspace("batch", [("batch", self._pages["batch"])])
        self._add_workspace("inspect", [("inspect", self._pages["inspect"])])
        self._add_workspace("jobs", [("jobs", self._pages["jobs"])])

        saved = state_store.load_state(PLUGIN_ID, default={}) or {}
        self._restore_state(saved)

        goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
        page = start_page or goto.get("start_page") or saved.get("nav_page")
        if goto:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        page = _PAGE_ALIASES.get(page or "", page or "convert")
        if page in _ACTIVITY_KEYS and page not in _LEAF_KEYS:
            page = _WORKSPACE_DEFAULT.get(page, "convert")
        if not _is_leaf_feature(page):
            page = "convert"
        self._switch_activity(FEATURE_ROUTE[page][0])
        self.goto_page(page)

        self._wb.set_sidebar_visible(True)
        self._wb.set_panel_visible(True)
        suite_chrome.bind_nav_shortcuts(
            self, NAV_PAGES, self._on_activity_clicked)
        plugin_shell.bind_shortcut(
            self, "Ctrl+J",
            lambda: self._wb.set_panel_visible(not self._wb.is_panel_visible()))
        plugin_shell.bind_shortcut(
            self, "Ctrl+B",
            lambda: self._wb.set_sidebar_visible(
                not self._wb.is_sidebar_visible()))
        plugin_shell.bind_shortcut(
            self, "Ctrl+O", lambda: self.run_action("log.convert.browse"))

        self._log_row(
            "SYS", "-", b"",
            "Log Converter ready — BLF / ASC / CSV / PCAP / TRC")
        self._ensure_status_chrome()

    def _init_status_controls(self):
        self.fmt_label = QLabel("Engine: BLF ASC CSV PCAP TRC")
        self.fmt_label.setObjectName("SuiteDocPath")
        self.fmt_label.setMinimumWidth(160)
        self.fmt_label.setMaximumWidth(360)

    def _ensure_status_chrome(self):
        bar = self.statusBar()
        if bar is None:
            return
        if self._status_chrome_mounted:
            if self.fmt_label.parent() is not bar:
                bar.addPermanentWidget(self.fmt_label)
            return
        self._status_chrome_mounted = True
        self.fmt_label.setParent(bar)
        bar.addPermanentWidget(self.fmt_label)

    def _mount_tab_bar(self):
        suite_tabs.mount_editor_tabs(
            self,
            open_tabs=self._open_tabs,
            active_feature=self._active_feature,
            titles=FEATURE_TITLES,
            on_activate=self._activate_feature,
            on_close=self._close_feature_tab,
            on_reorder=lambda order: setattr(self, "_open_tabs", order),
            ensure_status=self._ensure_status_chrome,
        )

    def _open_feature_tab(self, feature: str, *, activate: bool = True):
        if not _is_leaf_feature(feature):
            return
        if feature not in self._open_tabs:
            self._open_tabs.append(feature)
        if activate:
            self._active_feature = feature
        self._mount_tab_bar()
        self._activate_feature(feature)

    def _close_feature_tab(self, feature: str):
        if feature in self._open_tabs:
            self._open_tabs.remove(feature)
        if not self._open_tabs:
            default = _WORKSPACE_DEFAULT.get(
                FEATURE_ROUTE.get(feature, ("convert",))[0], "convert")
            self._open_feature_tab(default, activate=True)
            return
        nxt = (
            self._active_feature
            if self._active_feature in self._open_tabs
            else self._open_tabs[-1])
        self._mount_tab_bar()
        self._activate_feature(nxt)

    def _build_menubar(self):
        bar = suite_chrome.begin_suite_menubar(self)

        def _act(menu, label, slot, shortcut=None):
            a = menu.addAction(label)
            a.triggered.connect(slot)
            if shortcut:
                a.setShortcut(QKeySequence(shortcut))
            return a

        m_file = bar.addMenu("&File")
        _act(m_file, "&Open log…",
             lambda: self.run_action("log.convert.browse"), "Ctrl+O")
        _act(m_file, "&Probe log…",
             lambda: self.run_action("log.inspect.probe"), "Ctrl+Shift+O")
        m_file.addSeparator()
        _act(m_file, "E&xit", self.close)

        m_view = bar.addMenu("&View")
        for key, title in NAV_PAGES:
            _act(m_view, title,
                 lambda _c=False, k=key: self._on_activity_clicked(k))
        m_view.addSeparator()
        _act(m_view, "Toggle &Side Bar",
             lambda: self._wb.set_sidebar_visible(
                 not self._wb.is_sidebar_visible()), "Ctrl+B")
        _act(m_view, "Toggle &OUTPUT",
             lambda: self._wb.set_panel_visible(
                 not self._wb.is_panel_visible()), "Ctrl+J")
        _act(m_view, "&Maximize Editor",
             lambda: self._wb.set_maximized(
                 not self._wb.is_maximized()), "Ctrl+Shift+E")

        m_help = bar.addMenu("&Help")
        _act(m_help, "&About Log Converter", self._menu_about)

        suite_chrome.attach_layout_toggles_to_menubar(self, self._wb)

    def _menu_about(self):
        QMessageBox.information(
            self, "About Log Converter",
            "Log Converter — CAN / CAN FD bus-log format suite.\n\n"
            "Engine formats: BLF · ASC · CSV · PCAP/PCAPNG · TRC\n"
            "Workspaces: Convert · Batch · Inspect · Jobs\n\n"
            "Conversions run on the OpenBus host CanFileIO engine "
            "(async progress + cancel).")

    def run_action(self, action: str, **kwargs):
        if action == "log.convert.browse":
            self.goto_page("convert")
            page = self._pages.get("convert")
            # trigger browse via synthesizing — call page helper if present
            from PyQt6.QtWidgets import QFileDialog
            from formats import OPEN_FILTER, detect_format
            path, _ = QFileDialog.getOpenFileName(
                self, "Open CAN log", self.session.start_dir(), OPEN_FILTER)
            if path and page is not None and hasattr(page, "set_source"):
                page.set_source(path)
            plugin_shell.set_status(self, "Open log", 2000)
            return
        if action == "log.inspect.probe":
            self.goto_page("inspect")
            page = self._pages.get("inspect")
            from PyQt6.QtWidgets import QFileDialog
            from formats import OPEN_FILTER
            path, _ = QFileDialog.getOpenFileName(
                self, "Probe CAN log", self.session.start_dir(), OPEN_FILTER)
            if path and page is not None and hasattr(page, "probe_path"):
                page.probe_path(path)
            return
        if action == "log.goto":
            self.goto_page(str(kwargs.get("page") or "convert"))
            return
        plugin_shell.set_status(self, "Unknown action: %s" % action, 3000)

    def rerun_convert(self, source: str, fmt: str = ""):
        self.goto_page("convert")
        page = self._pages.get("convert")
        if page is not None and hasattr(page, "set_source") and source:
            page.set_source(source)
        if fmt:
            self.session.last_fmt = fmt
        plugin_shell.set_status(self, "Re-run ready — press Convert", 4000)

    def notify_jobs_changed(self):
        page = self._pages.get("jobs")
        if page is not None and hasattr(page, "refresh_from_session"):
            try:
                page.refresh_from_session()
            except Exception:
                pass

    def _add_workspace(
            self, workspace: str,
            features: list[tuple[str, QWidget]]) -> None:
        self._workspace_stack_index[workspace] = self.stack.count()
        self.stack.addWidget(self._build_feature_workspace(workspace, features))

    def _build_feature_workspace(
            self, workspace: str,
            features: list[tuple[str, QWidget]]) -> QStackedWidget:
        stack = QStackedWidget()
        stack.setObjectName("SuiteEditorStack")
        fmap = {}
        for i, (key, page) in enumerate(features):
            stack.addWidget(page)
            fmap[key] = i
        self._workspace_stacks[workspace] = stack
        self._workspace_features[workspace] = [k for k, _ in features]
        stack._feature_index = fmap  # type: ignore[attr-defined]

        def apply(key: str):
            stack.setCurrentIndex(fmap.get(key, 0))

        stack._apply_feature = apply  # type: ignore[attr-defined]
        return stack

    def _switch_activity(self, key: str):
        if key not in dict(NAV_PAGES):
            return
        wi = self._workspace_stack_index.get(key)
        if wi is not None:
            self.stack.setCurrentIndex(wi)
        sb = self._sidebars.get(key)
        if sb is not None and self._wb.set_side_bar_widget:
            self._wb.set_side_bar_widget(sb)
        try:
            self._wb.highlight_activity(key)
        except Exception:
            pass
        if self._open_tabs:
            self._mount_tab_bar()

    def _on_activity_clicked(self, key: str):
        self._switch_activity(key)
        default = _WORKSPACE_DEFAULT.get(key, "convert")
        self._open_feature_tab(default, activate=True)
        self._persist()

    def _activate_feature(self, feature: str):
        if not _is_leaf_feature(feature):
            return
        workspace, _ = FEATURE_ROUTE[feature]
        self._active_feature = feature
        wi = self._workspace_stack_index.get(workspace)
        if wi is not None and self.stack.currentIndex() != wi:
            self.stack.setCurrentIndex(wi)
        outer = self._workspace_stacks.get(workspace)
        if outer is not None:
            apply = getattr(outer, "_apply_feature", None)
            if callable(apply):
                apply(feature)
        if feature not in self._open_tabs:
            self._open_tabs.append(feature)
        self._mount_tab_bar()
        sb = self._sidebars.get(workspace)
        if sb is not None and hasattr(sb, "select_section"):
            try:
                sb.select_section(feature)
            except Exception:
                pass
        try:
            self._wb.highlight_activity(workspace)
        except Exception:
            pass
        page = self._pages.get(feature)
        if page is not None and hasattr(page, "refresh_from_session"):
            try:
                page.refresh_from_session()
            except Exception:
                pass

    def goto_page(self, key: str):
        key = _PAGE_ALIASES.get(key, key)
        if key in _ACTIVITY_KEYS and key not in _LEAF_KEYS:
            key = _WORKSPACE_DEFAULT.get(key, "convert")
        if not _is_leaf_feature(key):
            key = "convert"
        self._open_feature_tab(key, activate=True)
        self._persist()

    def _build_log_panel(self):
        self.log_pause = QCheckBox("Pause")
        self.log_pause.setToolTip("Hold new rows until unchecked")
        self._wb.panel_tools.addWidget(self.log_pause)
        self._wb.panel_tools.addStretch(1)
        export_btn = QPushButton("Export")
        export_btn.setObjectName("GhostButton")
        export_btn.setFixedHeight(22)
        clear_btn = QPushButton("Clear")
        clear_btn.setObjectName("GhostButton")
        clear_btn.setFixedHeight(22)
        codicons.set_button(export_btn, "export", size=12)
        codicons.set_button(clear_btn, "clear", size=12)
        self._wb.panel_tools.addWidget(export_btn)
        self._wb.panel_tools.addWidget(clear_btn)
        self.log_table = suite_chrome.make_output_table()
        self.log_table.setHorizontalHeaderLabels(
            ["Time", "Dir", "CAN ID", "Data", "Note"])
        self.log_table.setEditTriggers(
            QAbstractItemView.EditTrigger.NoEditTriggers)
        self.log_table.verticalHeader().setVisible(False)
        self.log_table.setSelectionBehavior(
            QAbstractItemView.SelectionBehavior.SelectRows)
        self.log_table.horizontalHeader().setSectionResizeMode(
            4, QHeaderView.ResizeMode.Stretch)
        self._wb.panel_body.addWidget(self.log_table, 1)
        export_btn.clicked.connect(self._export_log)
        clear_btn.clicked.connect(self.clear_log)

    def _log_row(self, direction, can_id, pdu, note, color=None):
        if self.log_pause.isChecked():
            if len(self._log_buffer) < 5000:
                self._log_buffer.append(
                    (time.time(), direction, can_id, pdu, note, color))
            return
        self._append_log_row(time.time(), direction, can_id, pdu, note, color)
        while self.log_table.rowCount() > 2000:
            self.log_table.removeRow(0)

    def _append_log_row(self, ts, direction, can_id, pdu, note, color=None):
        tstr = (time.strftime("%H:%M:%S", time.localtime(ts))
                + ".%03d" % int(ts % 1 * 1000))
        if isinstance(pdu, (bytes, bytearray)):
            hex_str = " ".join("%02X" % b for b in pdu[:48])
        else:
            hex_str = str(pdu)
        idstr = ("0x%X" % can_id) if isinstance(can_id, int) else str(can_id)
        colors = {
            "TX": QColor("#1565C0"), "RX": QColor("#2E7D32"),
            "ERR": QColor("#C62828"), "SYS": QColor("#6A1B9A"),
        }
        c = colors.get(color or direction, QColor("#333"))
        row = self.log_table.rowCount()
        self.log_table.insertRow(row)
        for col, text in enumerate(
                (tstr, direction, idstr, hex_str, note or "")):
            item = QTableWidgetItem(text)
            item.setForeground(c)
            self.log_table.setItem(row, col, item)
        self.log_table.verticalScrollBar().setValue(
            self.log_table.verticalScrollBar().maximum())

    def clear_log(self):
        self.log_table.setRowCount(0)
        self._log_buffer.clear()

    def _export_log(self):
        rows = []
        for r in range(self.log_table.rowCount()):
            rows.append([
                self.log_table.item(r, c).text()
                if self.log_table.item(r, c) else ""
                for c in range(5)
            ])
        path = plugin_shell.export_csv(
            self, ["Time", "Dir", "CAN ID", "Data", "Note"], rows,
            "log_converter_output.csv")
        if path:
            plugin_shell.set_status(self, "Exported %s" % path, 4000)

    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        state_store.save_state(PLUGIN_ID, {
            "nav_page": self._active_feature or "convert",
            "geometry_hex": geo,
            "open_tabs": list(self._open_tabs),
        })

    def _restore_state(self, saved: dict):
        if not saved:
            return
        geo = saved.get("geometry_hex")
        if geo:
            from PyQt6.QtCore import QByteArray
            self.restoreGeometry(QByteArray.fromHex(geo.encode("ascii")))
        self._open_tabs = suite_tabs.normalize_open_tabs(
            saved.get("open_tabs"),
            titles=FEATURE_TITLES,
            routes=FEATURE_ROUTE,
            activity_keys=_ACTIVITY_KEYS,
            aliases=_PAGE_ALIASES,
        )

    def shutdown(self):
        self._persist()
        self.session.shutdown()
