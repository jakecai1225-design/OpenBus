"""DBC file picker: prefer host workspace list, else file dialog."""

from __future__ import annotations

from typing import Optional

from PyQt6.QtWidgets import QFileDialog, QInputDialog, QWidget


def pick_dbc(parent: Optional[QWidget] = None, title: str = "Select DBC") -> Optional[str]:
    """Return a DBC path or None if the user cancels."""
    files: list[str] = []
    try:
        import sin

        files = list(sin.workspace.get_dbc_files() or [])
    except Exception:
        files = []

    files = [f for f in files if f]
    if len(files) == 1:
        return files[0]
    if len(files) > 1:
        labels = files[:]
        labels.append("Browse…")
        choice, ok = QInputDialog.getItem(
            parent, title, "Workspace DBC files:", labels, 0, False
        )
        if not ok:
            return None
        if choice != "Browse…":
            return choice

    path, _ = QFileDialog.getOpenFileName(
        parent, title, "", "DBC (*.dbc);;All files (*)"
    )
    return path or None
