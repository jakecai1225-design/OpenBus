# -*- coding: utf-8 -*-
"""dbc-lint — static DBC checks (naming, overlap, bounds, consistency).

Offline tool with optional CI mode (--ci) emitting JSON + SARIF.
"""

from __future__ import annotations

import json
import os
import re
import sys

_PLUGIN_DIR = os.path.dirname(os.path.abspath(__file__))
_PLUGINS_ROOT = os.path.dirname(_PLUGIN_DIR)
if _PLUGINS_ROOT not in sys.path:
    sys.path.insert(0, _PLUGINS_ROOT)

from _shared import dbcparse, dbc_picker, plugin_shell, state_store

PLUGIN_ID = "dbc-lint"

RESERVED = {
    "class", "struct", "union", "enum", "typedef", "static", "const",
    "void", "int", "long", "short", "float", "double", "char", "if",
    "else", "for", "while", "switch", "case", "return", "break",
}

# Default rule catalog: enable / severity / suppress (location substrings).
DEFAULT_RULES = {
    "naming": {
        "enabled": True,
        "severity": "error",
        "suppress": [],
        "title": "Invalid identifier characters",
    },
    "reserved": {
        "enabled": True,
        "severity": "error",
        "suppress": [],
        "title": "C reserved word",
    },
    "overlap": {
        "enabled": True,
        "severity": "error",
        "suppress": [],
        "title": "Signal bit overlap",
    },
    "bounds": {
        "enabled": True,
        "severity": "error",
        "suppress": [],
        "title": "Signal exceeds DLC",
    },
    "minmax_factor": {
        "enabled": True,
        "severity": "warning",
        "suppress": [],
        "title": "min/max vs factor/offset",
    },
    "missing_cycle": {
        "enabled": True,
        "severity": "info",
        "suppress": [],
        "title": "Missing GenMsgCycleTime",
    },
    "missing_comment": {
        "enabled": True,
        "severity": "info",
        "suppress": [],
        "title": "Missing comment",
    },
    "missing_valuetable": {
        "enabled": True,
        "severity": "info",
        "suppress": [],
        "title": "Missing value table",
    },
    "parse_warning": {
        "enabled": True,
        "severity": "warning",
        "suppress": [],
        "title": "Parser warning",
    },
}

_NAME_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
_MAX_NAME_LEN = 64

_win = None


def _plugin_dir() -> str:
    return _PLUGIN_DIR


def _rules_paths() -> list[str]:
    """Prefer plugin-local rules.json, then state_store copy."""
    return [
        os.path.join(_plugin_dir(), "rules.json"),
        state_store.state_path(PLUGIN_ID, "rules.json"),
    ]


def load_rules() -> dict:
    rules = {k: dict(v) for k, v in DEFAULT_RULES.items()}
    for path in _rules_paths():
        if not os.path.isfile(path):
            continue
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except (OSError, json.JSONDecodeError):
            continue
        if not isinstance(data, dict):
            continue
        for rid, cfg in data.items():
            if rid not in rules or not isinstance(cfg, dict):
                continue
            if "enabled" in cfg:
                rules[rid]["enabled"] = bool(cfg["enabled"])
            if "severity" in cfg and cfg["severity"] in ("error", "warning", "info"):
                rules[rid]["severity"] = cfg["severity"]
            if "suppress" in cfg and isinstance(cfg["suppress"], list):
                rules[rid]["suppress"] = [str(x) for x in cfg["suppress"]]
        break
    return rules


def save_rules(rules: dict, prefer_plugin: bool = True) -> str:
    payload = {}
    for rid, cfg in rules.items():
        payload[rid] = {
            "enabled": bool(cfg.get("enabled", True)),
            "severity": cfg.get("severity", "warning"),
            "suppress": list(cfg.get("suppress") or []),
        }
    if prefer_plugin:
        path = os.path.join(_plugin_dir(), "rules.json")
        try:
            with open(path, "w", encoding="utf-8") as f:
                json.dump(payload, f, indent=2, ensure_ascii=False)
            return path
        except OSError:
            pass
    return state_store.save_state(PLUGIN_ID, payload, "rules.json")


def _signal_bits(sig) -> set[int]:
    bits: set[int] = set()
    if sig.bit_length <= 0:
        return bits
    if sig.little_endian:
        for i in range(sig.bit_length):
            bits.add(sig.start_bit + i)
    else:
        byte = sig.start_bit >> 3
        bit = sig.start_bit & 7
        for _ in range(sig.bit_length):
            bits.add(byte * 8 + bit)
            bit += 1
            if bit == 8:
                bit = 0
                byte += 1
    return bits


def _raw_range(sig) -> tuple[float, float]:
    if not sig.is_signed:
        return 0.0, float((1 << sig.bit_length) - 1) if sig.bit_length > 0 else 0.0
    if sig.bit_length <= 1:
        return -1.0, 0.0
    half = 1 << (sig.bit_length - 1)
    return float(-half), float(half - 1)


def lint_dbc(db, rules: dict | None = None) -> list[dict]:
    """Return list of finding dicts with severity, location, rule, message."""
    rules = rules or load_rules()
    findings: list[dict] = []

    def _emit(rule_id: str, location: str, message: str, default_sev: str | None = None):
        cfg = rules.get(rule_id) or {}
        if not cfg.get("enabled", True):
            return
        for pat in cfg.get("suppress") or []:
            if pat and (pat in location or pat in message):
                return
        sev = cfg.get("severity") or default_sev or "warning"
        findings.append({
            "severity": sev,
            "location": location,
            "rule": rule_id,
            "message": message,
            "title": cfg.get("title") or rule_id,
        })

    for cid, m in db.messages.items():
        loc = "message 0x%X %s" % (cid, m.name)
        if not _NAME_RE.match(m.name) or len(m.name) > _MAX_NAME_LEN:
            _emit("naming", loc, "Invalid message name %r (length<=%d)" % (m.name, _MAX_NAME_LEN))
        if m.name.lower() in RESERVED:
            _emit("reserved", loc, "Message name is a C reserved word: %s" % m.name)
        if m.cycle_time == 0 and "GenMsgCycleTime" not in (m.attributes or {}):
            _emit("missing_cycle", loc, "No GenMsgCycleTime (event frame?)")
        if not m.comment:
            _emit("missing_comment", loc, "Message has no comment")

        bit_owner: dict[int, str] = {}
        bits_available = m.dlc * 8
        for s in m.signals:
            sloc = "%s / signal %s" % (loc, s.name)
            if not _NAME_RE.match(s.name) or len(s.name) > _MAX_NAME_LEN:
                _emit("naming", sloc, "Invalid signal name %r" % s.name)
            if s.name.lower() in RESERVED:
                _emit("reserved", sloc, "Signal name is a C reserved word: %s" % s.name)

            occupied = _signal_bits(s)
            if any(b < 0 or b >= bits_available for b in occupied) or (
                not occupied and s.bit_length > 0
            ):
                _emit(
                    "bounds",
                    sloc,
                    "start %d + length %d exceeds DLC %d*8" % (
                        s.start_bit, s.bit_length, m.dlc),
                )
            for b in occupied:
                if b in bit_owner:
                    _emit(
                        "overlap",
                        sloc,
                        "bit %d overlaps signal %s" % (b, bit_owner[b]),
                    )
                    break
                bit_owner[b] = s.name

            if s.factor and s.factor != 0:
                raw_min, raw_max = _raw_range(s)
                phys_min = raw_min * s.factor + s.offset
                phys_max = raw_max * s.factor + s.offset
                lo, hi = (phys_min, phys_max) if phys_min <= phys_max else (phys_max, phys_min)
                tol_lo = abs(lo) * 0.01
                tol_hi = abs(hi) * 0.01
                if s.minimum < lo - tol_lo or s.minimum > hi + tol_hi:
                    _emit(
                        "minmax_factor",
                        sloc,
                        "min %g outside representable [%g, %g]" % (s.minimum, lo, hi),
                    )
                if s.maximum > hi + tol_hi or s.maximum < lo - tol_lo:
                    _emit(
                        "minmax_factor",
                        sloc,
                        "max %g outside representable [%g, %g]" % (s.maximum, lo, hi),
                    )
                if s.minimum > s.maximum:
                    _emit(
                        "minmax_factor",
                        sloc,
                        "min %g > max %g" % (s.minimum, s.maximum),
                        default_sev="error",
                    )

            if s.value_table:
                for val, _desc in s.value_table.items():
                    # Value tables are discrete enums; compare against phys min/max loosely.
                    if s.minimum != s.maximum and (val < s.minimum or val > s.maximum):
                        _emit(
                            "minmax_factor",
                            sloc,
                            "value-table entry %s outside [%g, %g]" % (val, s.minimum, s.maximum),
                        )
            elif s.bit_length <= 2 and not s.comment:
                _emit(
                    "missing_valuetable",
                    sloc,
                    "<=2-bit signal should define a VAL_ table",
                )
            if not s.comment and s.bit_length > 2:
                _emit("missing_comment", sloc, "Signal has no comment")

    for w in db.warnings[:80]:
        _emit("parse_warning", "file", w)

    return findings


def findings_to_rows(findings: list[dict]) -> list[list]:
    return [[f["severity"], f["location"], f["rule"], f["message"]] for f in findings]


def to_sarif(findings: list[dict], dbc_path: str = "") -> dict:
    rules_seen = {}
    results = []
    for f in findings:
        rid = f["rule"]
        if rid not in rules_seen:
            rules_seen[rid] = {
                "id": rid,
                "name": rid,
                "shortDescription": {"text": f.get("title") or rid},
            }
        level = {"error": "error", "warning": "warning", "info": "note"}.get(
            f["severity"], "warning"
        )
        results.append({
            "ruleId": rid,
            "level": level,
            "message": {"text": f["message"]},
            "locations": [{
                "physicalLocation": {
                    "artifactLocation": {"uri": dbc_path or "dbc"},
                    "region": {"snippet": {"text": f["location"]}},
                }
            }],
        })
    return {
        "version": "2.1.0",
        "$schema": "https://json.schemastore.org/sarif-2.1.0.json",
        "runs": [{
            "tool": {
                "driver": {
                    "name": "dbc-lint",
                    "informationUri": "https://github.com/openbus",
                    "rules": list(rules_seen.values()),
                }
            },
            "results": results,
        }],
    }


def run_ci(dbc_path: str, out_base: str | None = None) -> int:
    rules = load_rules()
    db = dbcparse.parse_file(dbc_path)
    findings = lint_dbc(db, rules)
    payload = {
        "file": dbc_path,
        "messages": len(db.messages),
        "findings": findings,
        "counts": {
            "error": sum(1 for f in findings if f["severity"] == "error"),
            "warning": sum(1 for f in findings if f["severity"] == "warning"),
            "info": sum(1 for f in findings if f["severity"] == "info"),
        },
    }
    sarif = to_sarif(findings, dbc_path)
    if out_base:
        base = out_base
        if base.lower().endswith(".json"):
            base = base[:-5]
        elif base.lower().endswith(".sarif"):
            base = base[:-6]
        json_path = base + ".json"
        sarif_path = base + ".sarif"
        with open(json_path, "w", encoding="utf-8") as f:
            json.dump(payload, f, indent=2, ensure_ascii=False)
        with open(sarif_path, "w", encoding="utf-8") as f:
            json.dump(sarif, f, indent=2, ensure_ascii=False)
        print("Wrote %s and %s" % (json_path, sarif_path))
    else:
        print(json.dumps(payload, indent=2, ensure_ascii=False))
        print("---SARIF---")
        print(json.dumps(sarif, indent=2, ensure_ascii=False))
    return 1 if payload["counts"]["error"] else 0


def activate(context):
    global _win
    import sin

    try:
        from PyQt6.QtGui import QColor
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QMessageBox, QHeaderView,
            QDialog, QDialogButtonBox, QFormLayout, QCheckBox, QComboBox,
            QLineEdit, QScrollArea,
        )
    except ImportError:
        sin.output.append("dbc-lint requires PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("DBC Lint")
    win.resize(980, 640)
    plugin_shell.attach_status_bar(win, "Ready — load a DBC to lint")
    _win = win

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    load_btn = QPushButton("Lint DBC…")
    rules_btn = QPushButton("Rules…")
    export_btn = QPushButton("Export CSV")
    top.addWidget(load_btn)
    top.addWidget(rules_btn)
    top.addStretch(1)
    top.addWidget(export_btn)
    layout.addLayout(top)
    layout.addWidget(plugin_shell.help_label(
        "Checks naming, bit overlap/bounds, min/max vs factor, missing cycle/comment/VAL_. "
        "rules.json next to the plugin (or state_store) controls enable/severity/suppress. "
        "CI: python main.py --ci file.dbc [--out report]"))

    empty = plugin_shell.empty_state_label(
        "No findings yet.\nLoad a workspace or local DBC to run static checks.")
    layout.addWidget(empty)

    summary = QLabel("")
    summary.setStyleSheet("font-weight:bold;")
    summary.hide()
    layout.addWidget(summary)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Severity", "Location", "Rule", "Message"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    tree.hide()
    layout.addWidget(tree, 1)

    state = {"findings": [], "path": "", "rules": load_rules()}

    def _show_results(path: str, findings: list[dict], msg_count: int):
        state["findings"] = findings
        state["path"] = path
        tree.clear()
        colors = {
            "error": QColor("#c62828"),
            "warning": QColor("#ef6c00"),
            "info": QColor("#1565c0"),
        }
        for f in findings:
            item = QTreeWidgetItem([
                f["severity"], f["location"], f["rule"], f["message"]])
            item.setForeground(0, colors.get(f["severity"], QColor("#37474f")))
            tree.addTopLevelItem(item)
        n_err = sum(1 for i in findings if i["severity"] == "error")
        n_warn = sum(1 for i in findings if i["severity"] == "warning")
        n_info = sum(1 for i in findings if i["severity"] == "info")
        name = os.path.basename(path)
        summary.setText(
            "%s: %d messages | errors %d | warnings %d | info %d%s"
            % (name, msg_count, n_err, n_warn, n_info,
               " | OK" if n_err == 0 else " | has errors"))
        summary.show()
        empty.hide()
        tree.show()
        plugin_shell.set_status(
            win, "Linted %s — %d finding(s)" % (name, len(findings)), 5000)

    def _on_load():
        path = dbc_picker.pick_dbc(win, "Select DBC to lint")
        if not path:
            return
        db = dbcparse.parse_file(path)
        if not db.messages:
            QMessageBox.warning(win, "DBC Lint", "No messages in file")
            return
        findings = lint_dbc(db, state["rules"])
        _show_results(path, findings, len(db.messages))

    def _on_rules():
        dlg = QDialog(win)
        dlg.setWindowTitle("Lint rules")
        dlg.resize(520, 480)
        form = QFormLayout(dlg)
        editors = {}
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        inner = QWidget()
        inner_form = QFormLayout(inner)
        for rid, cfg in state["rules"].items():
            row = QWidget()
            hl = QHBoxLayout(row)
            hl.setContentsMargins(0, 0, 0, 0)
            en = QCheckBox("on")
            en.setChecked(bool(cfg.get("enabled", True)))
            sev = QComboBox()
            sev.addItems(["error", "warning", "info"])
            sev.setCurrentText(cfg.get("severity", "warning"))
            sup = QLineEdit(",".join(cfg.get("suppress") or []))
            sup.setPlaceholderText("suppress substrings, comma-separated")
            hl.addWidget(en)
            hl.addWidget(sev)
            hl.addWidget(sup, 1)
            inner_form.addRow("%s — %s" % (rid, cfg.get("title", "")), row)
            editors[rid] = (en, sev, sup)
        scroll.setWidget(inner)
        form.addRow(scroll)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save | QDialogButtonBox.StandardButton.Cancel)
        form.addRow(buttons)
        buttons.accepted.connect(dlg.accept)
        buttons.rejected.connect(dlg.reject)
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return
        for rid, (en, sev, sup) in editors.items():
            state["rules"][rid]["enabled"] = en.isChecked()
            state["rules"][rid]["severity"] = sev.currentText()
            state["rules"][rid]["suppress"] = [
                p.strip() for p in sup.text().split(",") if p.strip()]
        path = save_rules(state["rules"])
        plugin_shell.set_status(win, "Rules saved to %s" % path, 4000)
        if state["path"]:
            db = dbcparse.parse_file(state["path"])
            _show_results(state["path"], lint_dbc(db, state["rules"]), len(db.messages))

    def _on_export():
        findings = state["findings"]
        if not findings:
            QMessageBox.information(win, "DBC Lint", "Run a lint first")
            return
        path = plugin_shell.export_csv(
            win,
            ["severity", "location", "rule", "message"],
            findings_to_rows(findings),
            "dbc_lint.csv",
        )
        if path:
            plugin_shell.set_status(win, "Exported %s" % path, 4000)

    load_btn.clicked.connect(_on_load)
    rules_btn.clicked.connect(_on_rules)
    export_btn.clicked.connect(_on_export)
    plugin_shell.bind_shortcut(win, "Ctrl+O", _on_load)
    plugin_shell.bind_shortcut(win, "Ctrl+S", _on_export)

    context.register_command(
        "dbcLint.open", plugin_shell.bind_raise(win), "Database: DBC Lint")

    win.show()
    sin.output.append("dbc-lint loaded (naming / overlap / bounds / CI JSON+SARIF)")


def deactivate():
    global _win
    _win = None
    try:
        import sin
        sin.output.append("dbc-lint deactivated")
    except Exception:
        pass


def _cli_main(argv: list[str]) -> int:
    args = list(argv)
    out_base = None
    if "--out" in args:
        i = args.index("--out")
        if i + 1 >= len(args):
            print("error: --out requires a path", file=sys.stderr)
            return 2
        out_base = args[i + 1]
        del args[i:i + 2]
    args = [a for a in args if a != "--ci"]
    dbc_files = [a for a in args if not a.startswith("-")]
    if not dbc_files:
        print("usage: main.py --ci <file.dbc> [--out report]", file=sys.stderr)
        return 2
    return run_ci(dbc_files[0], out_base)


if __name__ == "__main__" or "--ci" in sys.argv:
    if __name__ == "__main__" or any(a.endswith(".dbc") for a in sys.argv[1:]):
        raise SystemExit(_cli_main(sys.argv[1:]))
