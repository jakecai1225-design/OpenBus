# -*- coding: utf-8 -*-
from pathlib import Path

p = Path(r"D:\code\openbus\sin\src\ui\markettab.cpp")
text = p.read_text(encoding="utf-8")

# Remaining UI Chinese in showInstalledDriver + leftover install warning
pairs = [
    ('QStringLiteral("支持")', 'tr("Yes")'),
    ('QStringLiteral("禁用此驱动")', 'tr("Disable this driver")'),
    ('QStringLiteral("启用此驱动")', 'tr("Enable this driver")'),
    (
        'toggleBtn->setToolTip(QStringLiteral(\n'
        '        "禁用后设备树隐藏且不参与枚举/打开，重启后不加载（方案 §7.4）"));',
        'toggleBtn->setToolTip(tr(\n'
        '        "When disabled, the driver is hidden from the device tree and skipped "\n'
        '        "for enumeration/open; it will not load after restart"));',
    ),
    ('QStringLiteral("卸载此驱动")', 'tr("Uninstall this driver")'),
    (
        'uninstallBtn->setToolTip(QStringLiteral(\n'
        '        "仅外置驱动可卸载；已加载的 DLL 在重启程序前仍驻留内存（方案 §7.4）"));',
        'uninstallBtn->setToolTip(tr(\n'
        '        "Only external drivers can be uninstalled; a loaded DLL stays in memory "\n'
        '        "until the app restarts"));',
    ),
    (
        'QMessageBox::warning(this, QStringLiteral("安装驱动"), err);',
        'QMessageBox::warning(this, tr("Install Driver"), err);',
    ),
]

for i, (old, new) in enumerate(pairs):
    count = text.count(old)
    if count == 0:
        print(f"MISSING [{i}] count=0: {old[:70]!r}")
    else:
        text = text.replace(old, new)
        print(f"OK [{i}] replaced {count}")

p.write_text(text, encoding="utf-8")

# Verify no user-visible Chinese left in QStringLiteral / tr candidates
import re
ui_cn = re.findall(
    r'(?:QStringLiteral|tr)\s*\(\s*"([^"]*[\u4e00-\u9fff][^"]*)"',
    text,
)
print("Remaining CN in QStringLiteral/tr:", ui_cn)
