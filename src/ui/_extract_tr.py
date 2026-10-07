# -*- coding: utf-8 -*-
"""Extract MarketTab::tr / tr(...) English source strings from markettab.cpp."""
from pathlib import Path
import re

text = Path(r"D:\code\openbus\sin\src\ui\markettab.cpp").read_text(encoding="utf-8")

# Match tr("...") and MarketTab::tr("...") including concatenated adjacent string literals
pattern = re.compile(
    r'(?:MarketTab::)?tr\s*\(\s*((?:"(?:\\.|[^"\\])*"\s*)+)\)',
    re.MULTILINE,
)

def unquote_concat(s: str) -> str:
    parts = re.findall(r'"(?:\\.|[^"\\])*"', s)
    out = []
    for p in parts:
        body = p[1:-1]
        body = body.encode("utf-8").decode("unicode_escape")
        out.append(body)
    return "".join(out)

seen = []
for m in pattern.finditer(text):
    s = unquote_concat(m.group(1))
    if s not in seen:
        seen.append(s)

for s in seen:
    print(s)
print("---")
print(f"total unique: {len(seen)}")
