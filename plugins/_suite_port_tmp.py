# -*- coding: utf-8 -*-
"""Re-port suite pages from original plugins with clean build() unwrap."""
from __future__ import annotations

import ast
import os
import re

BASE = r"d:/openbus/openbus_20260727/sin/plugins"

TRANSFORMS = [
    ("autosar-nm-monitor", "protocol-hub/pages/nm.py", "nm", "AUTOSAR NM"),
    ("iso-tp-monitor", "protocol-hub/pages/isotp.py", "isotp", "ISO-TP"),
    ("isobus-monitor", "protocol-hub/pages/isobus.py", "isobus", "ISOBUS"),
    ("nmea2000-decoder", "protocol-hub/pages/nmea2000.py", "nmea2000", "NMEA2000"),
    ("gbt27930-monitor", "protocol-hub/pages/gbt27930.py", "gbt27930", "GBT27930"),
    ("xcp-monitor", "protocol-hub/pages/xcp.py", "xcp", "XCP"),
    ("can-bit-timing", "bus-utilities/pages/bit_timing.py", "bit_timing", "Bit Timing"),
    ("can-gateway", "bus-utilities/pages/gateway.py", "gateway", "Gateway"),
    ("j1939-analyzer", "j1939-suite/pages/analyzer.py", "analyzer", "J1939 Analyzer"),
    ("obd2-scanner", "obd-suite/pages/scanner.py", "scanner", "OBD Scanner"),
]


def extract_activate_body(src: str) -> tuple[str, str]:
    """Return (preamble, dedented activate body without trailing show/output)."""
    m = re.search(r"\ndef activate\(context\):\n", src)
    if not m:
        raise RuntimeError("no activate")
    preamble = src[: m.start()]
    rest = src[m.end() :]
    m2 = re.search(r"\ndef deactivate\(\):", rest)
    if m2:
        rest = rest[: m2.start()]

    # Dedent one level (4 spaces typical)
    lines = []
    for ln in rest.splitlines():
        if ln.startswith("    "):
            lines.append(ln[4:])
        else:
            lines.append(ln)
    body = "\n".join(lines).rstrip() + "\n"

    # Drop trailing win.show() / sin.output.append(...) blocks (multi-line safe)
    body = re.sub(
        r"\n\s*win\.show\(\)\s*\n\s*sin\.output\.append\([\s\S]*?\)\s*$",
        "\n",
        body,
    )
    body = re.sub(r"\n\s*win\.show\(\)\s*$", "\n", body)
    return preamble, body


def port_body(body: str) -> str:
    # Window -> root widget
    body = re.sub(
        r"win = sin\.ui\.create_window\([^\n]+\)\n"
        r"win\.resize\([^\n]+\)\n"
        r"plugin_shell\.attach_status_bar\(win,[^\n]+\)\n"
        r"(?:_win = win\n)?"
        r"\n"
        r"central = QWidget\(\)\n"
        r"win\.setCentralWidget\(central\)\n"
        r"layout = QVBoxLayout\(central\)",
        "root = QWidget(parent)\nlayout = QVBoxLayout(root)",
        body,
    )

    reps = [
        ("plugin_shell.set_status(win,", "plugin_shell.set_status(parent,"),
        ("QMessageBox.information(win,", "QMessageBox.information(parent,"),
        ("QMessageBox.warning(win,", "QMessageBox.warning(parent,"),
        ("QMessageBox.question(\n                win,", "QMessageBox.question(\n                parent,"),
        ("QMessageBox.question(\n            win,", "QMessageBox.question(\n            parent,"),
        ("QFileDialog.getSaveFileName(\n            win,", "QFileDialog.getSaveFileName(\n            parent,"),
        ("QFileDialog.getOpenFileName(\n            win,", "QFileDialog.getOpenFileName(\n            parent,"),
        ("dbc_picker.pick_dbc(win,", "dbc_picker.pick_dbc(parent,"),
        ("plugin_shell.export_csv(\n            win,", "plugin_shell.export_csv(\n            parent,"),
        ("plugin_shell.bind_shortcut(win,", "plugin_shell.bind_shortcut(parent,"),
        ("QTimer(win)", "QTimer(root)"),
        ("IsotpClient(_send_frame, qt_parent=win)", "IsotpClient(_send_frame, qt_parent=root)"),
        ("context.on_frame(_on_frame)", "session.on_bus_frame(_on_frame)"),
        ("context.on_frame(on_frame)", "session.on_bus_frame(on_frame)"),
    ]
    for a, b in reps:
        body = body.replace(a, b)

    # Remove command registration blocks
    body = re.sub(
        r"\nraise_fn = plugin_shell\.bind_raise\(win\)\n"
        r"context\.register_command\(\n"
        r'    "[^"]+", raise_fn, "[^"]+"\)\n',
        "\n",
        body,
    )
    body = re.sub(
        r"\ncontext\.register_command\(\n"
        r'    "[^"]+",\n'
        r"    plugin_shell\.bind_raise\(win\),\n"
        r'    "[^"]+",\n'
        r"\)\n",
        "\n",
        body,
    )
    body = re.sub(
        r"\ncontext\.register_command\(\n"
        r'    "[^"]+", plugin_shell\.bind_raise\(win\), "[^"]+"\)\n',
        "\n",
        body,
    )

    # Any remaining bare win references (dialogs etc.)
    body = re.sub(r"\bwin\b", "parent", body)

    # Drop global _win if present at start of build body
    body = re.sub(r"^global _win\n\n", "", body)

    body = body.rstrip() + "\n\nreturn root\n"
    return body


def transform(src_plugin, dest_rel, page_key, title):
    src = os.path.join(BASE, src_plugin, "main.py")
    dest = os.path.join(BASE, dest_rel.replace("/", os.sep))
    suite_id = dest_rel.split("/")[0]

    with open(src, "r", encoding="utf-8", errors="replace") as f:
        raw = f.read()

    preamble, body = extract_activate_body(raw)
    preamble = re.sub(
        r'^"""[\s\S]*?"""',
        f'"""{title} workspace page for {suite_id}."""',
        preamble,
        count=1,
    )
    preamble = re.sub(
        r'PLUGIN_ID = "[^"]+"',
        f'SUITE_ID = "{suite_id}"\nPAGE_KEY = "{page_key}"',
        preamble,
    )
    preamble = preamble.replace("PLUGIN_ID", "SUITE_ID")

    body = port_body(body)
    body = body.replace("PLUGIN_ID", "SUITE_ID")

    # Indent body under build()
    indented = "\n".join(("    " + ln if ln.strip() else ln) for ln in body.splitlines())
    out = preamble.rstrip() + "\n\n\ndef build(parent, session, log_fn):\n" + indented + "\n"

    try:
        ast.parse(out)
    except SyntaxError as e:
        print("FAIL", dest, e)
        with open(dest + ".broken", "w", encoding="utf-8", newline="\n") as f:
            f.write(out)
        return False

    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with open(dest, "w", encoding="utf-8", newline="\n") as f:
        f.write(out)
    print("ok", dest)
    return True


def main():
    ok = 0
    for row in TRANSFORMS:
        if transform(*row):
            ok += 1
    print("done", ok, "/", len(TRANSFORMS))


if __name__ == "__main__":
    main()
