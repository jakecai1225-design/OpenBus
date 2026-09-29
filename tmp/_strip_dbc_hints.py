# -*- coding: utf-8 -*-
"""One-shot: remove caption hint rows from DBC Studio pages."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2] / "plugins" / "dbc-studio" / "pages"
PAT = re.compile(
    r"\n[ \t]*hint = QLabel\(\n"
    r"(?:.*\n)*?"
    r"[ \t]*hint\.setStyleSheet\(\"color:#78909c;font-size:12px;\"\)\n"
    r"[ \t]*(?:bl|ml|cl|rl)\.addWidget\(hint\)\n",
    re.M,
)

def main():
    for name in (
        "compare.py", "merge.py", "library.py", "timing.py",
        "matrix.py", "export.py", "attributes.py", "value_tables.py",
    ):
        path = ROOT / name
        if not path.is_file():
            print("missing", name)
            continue
        src = path.read_text(encoding="utf-8")
        new, n = PAT.subn("\n", src, count=2)
        if n:
            path.write_text(new, encoding="utf-8")
            print("stripped", name, n)
        else:
            print("no match", name)

if __name__ == "__main__":
    main()
