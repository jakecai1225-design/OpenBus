# -*- coding: utf-8 -*-
from pathlib import Path
import re

p = Path(r"D:\code\openbus\sin\src\ui\markettab.cpp")
text = p.read_text(encoding="utf-8")

for m in re.finditer(r'QStringLiteral\(\s*"([^"]*[\u4e00-\u9fff][^"]*)"', text):
    s = m.group(1)
    print("FOUND:", s)
    print("REPR:", repr(s))

# Replace the remaining tooltip by locating via unique ASCII anchors
old_start = 'toggleBtn->setToolTip(QStringLiteral('
idx = text.find(old_start)
if idx < 0:
    print("toggle tooltip block not found")
else:
    # Find closing ));
    end = text.find("));", idx)
    old = text[idx : end + 3]
    print("OLD BLOCK REPR:", repr(old))
    new = (
        'toggleBtn->setToolTip(tr(\n'
        '        "When disabled, the driver is hidden from the device tree and skipped "\n'
        '        "for enumeration/open; it will not load after restart"))'
    )
    text = text[:idx] + new + text[end + 3 :]
    p.write_text(text, encoding="utf-8")
    print("replaced toggle tooltip")

# Verify
text = p.read_text(encoding="utf-8")
left = re.findall(r'QStringLiteral\(\s*"([^"]*[\u4e00-\u9fff][^"]*)"', text)
print("Remaining CN QStringLiteral:", left)
left_tr = re.findall(r'(?<![A-Za-z_])tr\(\s*"([^"]*[\u4e00-\u9fff][^"]*)"', text)
print("CN in tr (unexpected):", left_tr)
