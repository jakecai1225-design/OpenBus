# -*- coding: utf-8 -*-
"""AppShell — CANopen Suite (AUTOSAR-style workbench chrome).

Activity bar | Side Bar (foldable sections) | closable editor tabs | body |
OUTPUT (hidden by default) | status bar.
"""

from __future__ import annotations

import os
import time
from typing import Optional

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QMainWindow,
    QPushButton,
    QStackedWidget,
    QTabBar,
    QTableWidget,
    QTableWidgetItem,
    QWidget,
)

from _shared import codicons, plugin_shell, state_store, suite_chrome, vscode_theme

from session import SharedSession

PLUGIN_ID = "canopen-suite"

# Activity bar — major workspaces only; sub-features live in the Side Bar.
NAV_PAGES = [
    ("network", "Network"),
    ("device", "Device"),
    ("eds", "EDS"),
    ("library", "Library"),
    ("setup", "Setup"),
]

# Feature key → (workspace, stack index or None).
FEATURE_ROUTE: dict[str, tuple[str, Optional[int]]] = {
    "network": ("network", 0),
    "network_scan": ("network", 0),
    "network_nmt": ("network", 1),
    "monitor": ("network", 2),
    "device": ("device", 0),
    "od": ("device", 0),
    "pdo": ("device", 1),
    "eds": ("eds", 0),
    "eds_dict": ("eds", 0),
    "eds_device": ("eds", 1),
    "eds_check": ("eds", 2),
    "eds_editor": ("eds", 0),
    "library": ("library", 0),
    "lib_301": ("library", 0),
    "lib_402": ("library", 1),
    "profiles": ("library", 0),
    "setup": ("setup", 0),
}

FEATURE_TITLES: dict[str, str] = {
    "network": "Scan",
    "network_scan": "Scan",
    "network_nmt": "NMT",
    "monitor": "Monitor",
    "device": "OD",
    "od": "OD",
    "pdo": "PDO",
    "eds": "Dictionary",
    "eds_dict": "Dictionary",
    "eds_device": "Device",
    "eds_check": "Check",
    "library": "CiA 301",
    "lib_301": "CiA 301",
    "lib_402": "CiA 402",
    "setup": "Setup",
}

# Default leaf opened when activating a workspace with no open tabs.
_WORKSPACE_DEFAULT = {
    "network": "network_scan",
    "device": "od",
    "eds": "eds_dict",
    "library": "lib_301",
    "setup": "setup",
}


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("CANopen Suite")
        self.setMinimumSize(1100, 720)
        self.resize(1400, 900)

        self._context = context
        self.session = SharedSession(parent=self)
        self._log_rows: list = []
        self._log_table: Optional[QTableWidget] = None
        self._pages = {}
        self._workspace_stacks: dict = {}
        self._workspace_features: dict = {}
        self._sidebars: dict = {}
        self._active_feature = "eds_dict"
        # Open editor tabs per workspace: list of feature keys (order = tab order).
        self._open_tabs: dict[str, list[str]] = {
            k: [] for k, _ in NAV_PAGES}
        self._tab_bar: Optional[QTabBar] = None
        self._tab_guard = False
        self._chrome_park = QWidget(self)
        self._chrome_park.hide()
        self._chrome_park.setAttribute(
            Qt.WidgetAttribute.WA_DontShowOnScreen, True)

        vscode_theme.apply(self)
        plugin_shell.attach_status_bar(self, "Ready")
        plugin_shell.wire_close_deactivates(self, PLUGIN_ID)

        # Side Bar on; OUTPUT hidden by default; activity always visible.
        self._wb = suite_chrome.build_workbench(
            self, NAV_PAGES, title="CANopen Suite", panel_title="OUTPUT",
            panel_visible=False, sidebar_visible=True,
            side_bar_enabled=True, side_bar_visible=True, lock_activity=True,
            side_bar_width=220)
        self.stack = self._wb.stack
        # Side Bar column: moderate width, do not collapse to zero.
        hs = getattr(self._wb, "h_splitter", None)
        if hs is not None:
            hs.setChildrenCollapsible(False)
            try:
                hs.setCollapsible(0, False)
            except Exception:
                pass

        self._build_output_panel()
        self.session.set_log_fn(self._log_row)

        from pages import (
            eds_editor, library, monitor, network, object_dict, pdo, setup,
            workspace_sidebar,
        )

        self._pages["network"] = network.build(self, self.session, self._log_row)
        self._pages["monitor"] = monitor.build(self, self.session, self._log_row)
        self._pages["od"] = object_dict.build(self, self.session, self._log_row)
        self._pages["pdo"] = pdo.build(self, self.session, self._log_row)
        self._pages["eds"] = eds_editor.build(self, self.session, self._log_row)
        self._pages["library"] = library.build(self, self.session, self._log_row)
        self._pages["setup"] = setup.build(self, self.session, self._log_row)

        self._sidebars["network"] = workspace_sidebar.build_section_sidebar(
            self, "Network", workspace_sidebar.NETWORK_SECTIONS,
            nested=workspace_sidebar.NETWORK_NESTED)
        self._sidebars["device"] = workspace_sidebar.build_section_sidebar(
            self, "Device", workspace_sidebar.DEVICE_SECTIONS)
        self._sidebars["eds"] = workspace_sidebar.build_section_sidebar(
            self, "EDS", workspace_sidebar.EDS_SECTIONS,
            nested=workspace_sidebar.EDS_NESTED)
        self._sidebars["library"] = workspace_sidebar.build_section_sidebar(
            self, "Library", workspace_sidebar.LIBRARY_SECTIONS,
            nested=workspace_sidebar.LIBRARY_NESTED)
        self._sidebars["setup"] = workspace_sidebar.build_section_sidebar(
            self, "Setup", workspace_sidebar.SETUP_SECTIONS)

        # Activity stack: Network embeds Scan/NMT + Monitor.
        self.stack.addWidget(self._build_feature_workspace("network", [
            ("network_scan", self._pages["network"]),
            ("network_nmt", self._pages["network"]),
            ("monitor", self._pages["monitor"]),
        ]))
        # Network Scan/NMT share one page with an inner stack — patch indices.
        self._wire_network_inner()
        self.stack.addWidget(self._build_feature_workspace("device", [
            ("od", self._pages["od"]),
            ("pdo", self._pages["pdo"]),
        ]))
        self.stack.addWidget(self._build_feature_workspace("eds", [
            ("eds_dict", self._pages["eds"]),
            ("eds_device", self._pages["eds"]),
            ("eds_check", self._pages["eds"]),
        ]))
        self._wire_eds_inner()
        self.stack.addWidget(self._build_feature_workspace("library", [
            ("lib_301", self._pages["library"]),
            ("lib_402", self._pages["library"]),
        ]))
        self._wire_library_inner()
        self.stack.addWidget(self._build_feature_workspace("setup", [
            ("setup", self._pages["setup"]),
        ]))

        context.on_frame(self.session.on_frame)

        saved = state_store.load_state(PLUGIN_ID, default={}) or {}
        self._restore_state(saved)
        goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
        page = start_page or goto.get("start_page") or saved.get("nav_page") or "eds_dict"
        if goto:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        aliases = {
            "log": "eds_dict",
            "eds_editor": "eds_dict",
            "object_dict": "od",
            "profiles": "lib_301",
            "eds": "eds_dict",
            "network": "network_scan",
            "library": "lib_301",
            "device": "od",
        }
        if page == "log":
            self._wb.set_panel_visible(True)
        page = aliases.get(page, page)
        self.goto_page(page if page in FEATURE_ROUTE else "eds_dict")

        # Defaults after restore: Side Bar on; OUTPUT stays off unless user opened it.
        self._wb.set_sidebar_visible(True)
        if not saved.get("panel_visible"):
            self._wb.set_panel_visible(False)

        suite_chrome.bind_nav_shortcuts(self, NAV_PAGES, self.goto_page)
        plugin_shell.bind_shortcut(
            self, "Ctrl+J",
            lambda: self._wb.set_panel_visible(not self._wb.is_panel_visible()))
        plugin_shell.bind_shortcut(
            self, "Ctrl+B",
            lambda: self._wb.set_sidebar_visible(
                not self._wb.is_sidebar_visible()))
        plugin_shell.bind_shortcut(
            self, "Ctrl+Shift+E",
            lambda: self._wb.set_maximized(not self._wb.is_maximized()))
        self._log_row(
            "SYS", "-", b"",
            "Side Bar opens editor tabs · Ctrl+B Side Bar · Ctrl+J OUTPUT")

    # ------------------------------------------------------------------
    def _build_feature_workspace(
            self, workspace: str,
            features: list[tuple[str, QWidget]]) -> QStackedWidget:
        """Outer stack per activity — may share the same page widget once."""
        stack = QStackedWidget()
        stack.setObjectName("SuiteEditorStack")
        keys = []
        seen: dict[int, int] = {}
        for key, page in features:
            pid = id(page)
            if pid in seen:
                # Same widget already in stack — record key → that index.
                keys.append(key)
                continue
            seen[pid] = stack.count()
            stack.addWidget(page)
            keys.append(key)
        # Rebuild index map: each feature key → unique widget index.
        self._workspace_stacks[workspace] = stack
        self._workspace_features[workspace] = keys
        # Store feature → stack index for shared widgets.
        fmap = {}
        idx = 0
        placed = {}
        for key, page in features:
            pid = id(page)
            if pid not in placed:
                placed[pid] = idx
                idx += 1
            fmap[key] = placed[pid]
        stack._feature_index = fmap  # type: ignore[attr-defined]
        return stack

    def _wire_network_inner(self):
        """Network page has Scan|NMT inner stack; Monitor is sibling widget."""
        net = self._pages["network"]
        outer = self._workspace_stacks["network"]
        # Outer: [network_page, monitor] — network_page.select_view for Scan/NMT.
        fmap = {"network_scan": 0, "network_nmt": 0, "network": 0, "monitor": 1}
        outer._feature_index = fmap  # type: ignore[attr-defined]

        def apply(key: str):
            if key == "monitor":
                outer.setCurrentIndex(1)
                return
            outer.setCurrentIndex(0)
            if hasattr(net, "select_view"):
                net.select_view(key)

        outer._apply_feature = apply  # type: ignore[attr-defined]

    def _wire_eds_inner(self):
        eds = self._pages["eds"]
        outer = self._workspace_stacks["eds"]
        outer._feature_index = {
            "eds_dict": 0, "eds": 0, "eds_device": 0, "eds_check": 0,
        }

        def apply(key: str):
            outer.setCurrentIndex(0)
            if hasattr(eds, "select_view"):
                eds.select_view(key)

        outer._apply_feature = apply  # type: ignore[attr-defined]

    def _wire_library_inner(self):
        lib = self._pages["library"]
        outer = self._workspace_stacks["library"]
        outer._feature_index = {"lib_301": 0, "library": 0, "lib_402": 0}

        def apply(key: str):
            outer.setCurrentIndex(0)
            if hasattr(lib, "select_view"):
                lib.select_view(key)

        outer._apply_feature = apply  # type: ignore[attr-defined]

    def _on_workbench_page(self, key: str):
        """Activity bar changed — mount Side Bar + ensure a tab is open."""
        sb = self._sidebars.get(key)
        if sb is not None:
            self._wb.set_side_bar_widget(sb)
        opens = self._open_tabs.get(key) or []
        if not opens:
            default = _WORKSPACE_DEFAULT.get(key, key)
            self._open_feature_tab(default, activate=True)
        else:
            self._mount_tab_bar(key)
            self._activate_feature(opens[-1] if opens else key)
        try:
            from _shared import activity_snapshot
            activity_snapshot.update(
                active_plugin="canopen-suite", active_page=key)
        except Exception:
            pass

    def _build_output_panel(self):
        self._wb.panel_tools.addStretch(1)
        export_btn = QPushButton("Export")
        export_btn.setObjectName("GhostButton")
        export_btn.setFixedHeight(22)
        export_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        export_btn.setToolTip("Export OUTPUT as CSV")
        clear_btn = QPushButton("Clear")
        clear_btn.setObjectName("GhostButton")
        clear_btn.setFixedHeight(22)
        clear_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        clear_btn.setToolTip("Clear OUTPUT")
        codicons.set_button(export_btn, "export", size=12)
        codicons.set_button(clear_btn, "clear", size=12)
        self._wb.panel_tools.addWidget(export_btn)
        self._wb.panel_tools.addWidget(clear_btn)
        table = suite_chrome.make_output_table()
        self._log_table = table
        self._wb.panel_body.addWidget(table, 1)
        export_btn.clicked.connect(self.export_log)
        clear_btn.clicked.connect(self.clear_log)

    def _mount_tab_bar(self, workspace: str):
        """Closable chrome tabs = open Side Bar features for this workspace."""
        opens = list(self._open_tabs.get(workspace) or [])
        bar = QTabBar()
        bar.setObjectName("SuiteEditorTabs")
        bar.setDrawBase(False)
        bar.setExpanding(False)
        bar.setDocumentMode(True)
        bar.setTabsClosable(True)
        bar.setMovable(True)
        for feat in opens:
            bar.addTab(FEATURE_TITLES.get(feat, feat))
            bar.setTabData(bar.count() - 1, feat)
        if self._active_feature in opens:
            bar.setCurrentIndex(opens.index(self._active_feature))
        elif opens:
            bar.setCurrentIndex(len(opens) - 1)

        def _changed(idx: int):
            if self._tab_guard or idx < 0:
                return
            feat = bar.tabData(idx)
            if feat:
                self._activate_feature(str(feat))

        def _close(idx: int):
            if idx < 0 or idx >= bar.count():
                return
            feat = bar.tabData(idx)
            if not feat:
                return
            self._close_feature_tab(str(feat))

        bar.currentChanged.connect(_changed)
        bar.tabCloseRequested.connect(_close)
        self._tab_bar = bar
        self._wb.set_editor_tabs(bar)

    def _open_feature_tab(self, feature: str, *, activate: bool = True):
        route = FEATURE_ROUTE.get(feature)
        if not route:
            return
        workspace, _ = route
        opens = self._open_tabs.setdefault(workspace, [])
        if feature not in opens:
            opens.append(feature)
        if activate:
            self._active_feature = feature
        # Switch activity if needed
        if self._wb.current_page() != workspace:
            self._wb.goto_page(workspace)
        self._mount_tab_bar(workspace)
        self._activate_feature(feature)
        sb = self._sidebars.get(workspace)
        if sb is not None and hasattr(sb, "select_section"):
            try:
                sb.select_section(feature)
            except Exception:
                pass

    def _close_feature_tab(self, feature: str):
        route = FEATURE_ROUTE.get(feature)
        if not route:
            return
        workspace, _ = route
        opens = self._open_tabs.get(workspace) or []
        if feature in opens:
            opens.remove(feature)
        self._open_tabs[workspace] = opens
        if not opens:
            # Re-open workspace default so the editor is never empty.
            default = _WORKSPACE_DEFAULT.get(workspace, feature)
            self._open_feature_tab(default, activate=True)
            return
        nxt = opens[-1]
        self._mount_tab_bar(workspace)
        self._activate_feature(nxt)

    def _activate_feature(self, feature: str):
        route = FEATURE_ROUTE.get(feature)
        if not route:
            return
        workspace, _idx = route
        self._active_feature = feature
        # Ensure activity page
        page_idx = {k: i for i, (k, _) in enumerate(NAV_PAGES)}
        wi = page_idx.get(workspace, 0)
        if self.stack.currentIndex() != wi:
            self.stack.setCurrentIndex(wi)
        outer = self._workspace_stacks.get(workspace)
        if outer is not None:
            apply = getattr(outer, "_apply_feature", None)
            if callable(apply):
                apply(feature)
            else:
                fmap = getattr(outer, "_feature_index", {}) or {}
                si = fmap.get(feature, 0)
                if 0 <= si < outer.count():
                    outer.setCurrentIndex(si)
        # Sync tab selection without re-entry
        if self._tab_bar is not None:
            self._tab_guard = True
            try:
                for i in range(self._tab_bar.count()):
                    if self._tab_bar.tabData(i) == feature:
                        self._tab_bar.setCurrentIndex(i)
                        break
            finally:
                self._tab_guard = False
        sb = self._sidebars.get(workspace)
        if sb is not None and hasattr(sb, "select_section"):
            try:
                sb.select_section(feature)
            except Exception:
                pass
        plugin_shell.set_status(
            self, FEATURE_TITLES.get(feature, feature), 1200)

    def _log_row(self, direction, can_id, pdu, note, color=None):
        ts = time.time()
        self._log_rows.append((ts, direction, can_id, pdu, note, color))
        if len(self._log_rows) > 5000:
            self._log_rows = self._log_rows[-2000:]
        if self._log_table is None:
            return
        tstr = (time.strftime("%H:%M:%S", time.localtime(ts))
                + ".%03d" % int(ts % 1 * 1000))
        if isinstance(pdu, (bytes, bytearray)):
            hex_str = " ".join("%02X" % b for b in pdu[:48])
            if len(pdu) > 48:
                hex_str += " ..."
        else:
            hex_str = str(pdu)
        idstr = ("0x%X" % can_id) if isinstance(can_id, int) else str(can_id)
        colors = {
            "TX": QColor(vscode_theme.TX),
            "RX": QColor(vscode_theme.RX),
            "ERR": QColor(vscode_theme.ERR),
            "SYS": QColor(vscode_theme.TEXT_MUTED),
        }
        c = colors.get(color or direction, QColor(vscode_theme.TEXT_MUTED))
        row = self._log_table.rowCount()
        self._log_table.insertRow(row)
        for col, text in enumerate((tstr, direction, idstr, hex_str, note or "")):
            item = QTableWidgetItem(text)
            item.setForeground(c)
            self._log_table.setItem(row, col, item)
        while self._log_table.rowCount() > 2000:
            self._log_table.removeRow(0)
        self._log_table.scrollToBottom()
        if isinstance(can_id, int):
            plugin_shell.set_status(
                self, "%s 0x%X  %s" % (direction, can_id, (note or "")[:48]), 0)
        elif note:
            plugin_shell.set_status(self, str(note)[:64], 0)

    def clear_log(self):
        self._log_rows.clear()
        if self._log_table is not None:
            self._log_table.setRowCount(0)

    def export_log(self):
        rows = []
        for ts, direction, can_id, pdu, note, _color in self._log_rows:
            tstr = (time.strftime("%H:%M:%S", time.localtime(ts))
                    + ".%03d" % int(ts % 1 * 1000))
            if isinstance(pdu, (bytes, bytearray)):
                hex_str = " ".join("%02X" % b for b in pdu)
            else:
                hex_str = str(pdu)
            idstr = ("0x%X" % can_id) if isinstance(can_id, int) else str(can_id)
            rows.append([tstr, direction, idstr, hex_str, note or ""])
        path = plugin_shell.export_csv(
            self, ["Time", "Dir", "CAN ID", "PDU", "Note"], rows,
            "canopen_suite_log.csv")
        if path:
            plugin_shell.set_status(self, "Exported %s" % path, 4000)

    def goto_page(self, key: str):
        if key == "log":
            self._wb.set_panel_visible(True)
            return
        aliases = {
            "eds_editor": "eds_dict",
            "object_dict": "od",
            "profiles": "lib_301",
            "eds": "eds_dict",
            "network": "network_scan",
            "library": "lib_301",
            "device": "od",
        }
        key = aliases.get(key, key)
        if key in FEATURE_ROUTE:
            self._open_feature_tab(key, activate=True)
        elif key in dict(NAV_PAGES):
            # Activity-only: open default leaf
            self._wb.goto_page(key)
            self._on_workbench_page(key)
        self._persist()

    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        state_store.save_state(PLUGIN_ID, {
            "node_id": self.session.node_id,
            "eds_path": self.session.eds_path,
            "nav_page": self._active_feature or "eds_dict",
            "open_tabs": {k: list(v) for k, v in self._open_tabs.items()},
            "geometry_hex": geo,
            "sidebar_visible": self._wb.is_sidebar_visible(),
            "panel_visible": self._wb.is_panel_visible(),
            "splitter_v": self._wb.v_splitter.sizes(),
        })

    def _restore_state(self, saved: dict):
        if not saved:
            return
        try:
            if "node_id" in saved:
                self.session.set_node_id(int(saved["node_id"]))
            eds = saved.get("eds_path") or ""
            if eds and os.path.isfile(eds):
                self.session.load_eds(eds)
            # Geometry restore skipped when we showMaximized on open.
            sizes = saved.get("splitter_v")
            if sizes and isinstance(sizes, list) and len(sizes) == 2:
                # Only apply if panel was visible; otherwise keep collapsed.
                if saved.get("panel_visible"):
                    self._wb.v_splitter.setSizes(
                        [int(sizes[0]), int(sizes[1])])
            ot = saved.get("open_tabs")
            if isinstance(ot, dict):
                for k, v in ot.items():
                    if k in self._open_tabs and isinstance(v, list):
                        self._open_tabs[k] = [
                            x for x in v if x in FEATURE_ROUTE]
            if saved.get("panel_visible"):
                self._wb.set_panel_visible(True)
            if saved.get("sidebar_visible") is False:
                self._wb.set_sidebar_visible(False)
        except (TypeError, ValueError, OSError):
            pass

    def closeEvent(self, event):
        self._persist()
        super().closeEvent(event)

    def shutdown(self):
        self._persist()
        self.session.shutdown()
