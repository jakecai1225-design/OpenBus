# -*- coding: utf-8 -*-
from pathlib import Path

p = Path(r"D:\code\openbus\sin\src\ui\markettab.cpp")
text = p.read_text(encoding="utf-8")

replacements = [
    (
        'showPlaceholder(QStringLiteral("该驱动已卸载"));',
        'showPlaceholder(tr("This driver has been uninstalled"));',
    ),
    (
        '''                   ? QStringLiteral("可用")
                   : e.disabledReason.isEmpty()
                         ? QStringLiteral("不可用") : e.disabledReason)
            : QStringLiteral("已禁用"));''',
        '''                   ? tr("Available")
                   : e.disabledReason.isEmpty()
                         ? tr("Unavailable") : e.disabledReason)
            : tr("Disabled"));''',
    ),
    (
        '''        e.builtin ? QStringLiteral("来源: 内置（随主程序静态编译）")
                  : QStringLiteral("来源: 外置驱动包（%1）").arg(e.installDir)));''',
        '''        e.builtin ? tr("Source: Built-in (statically linked with the app)")
                  : tr("Source: External driver package (%1)").arg(e.installDir)));''',
    ),
    (
        '''        m_detailLay->addWidget(makeSectionLabel(QStringLiteral("支持的设备型号")));
        auto *table = makeDeviceTable(
            { QStringLiteral("型号名称"), QStringLiteral("设备类型"),
              QStringLiteral("通道数"), QStringLiteral("CAN FD") });''',
        '''        m_detailLay->addWidget(makeSectionLabel(tr("Supported device models")));
        auto *table = makeDeviceTable(
            { tr("Model name"), tr("Device type"),
              tr("Channels"), QStringLiteral("CAN FD") });''',
    ),
    (
        '? QStringLiteral("支持") : QStringLiteral("—")));\n'
        "        }\n"
        "        m_detailLay->addWidget(table);\n"
        "    }\n\n"
        "    auto *btnRow = new QWidget;\n"
        "    auto *blay = new QHBoxLayout(btnRow);\n"
        "    blay->addStretch(1);\n"
        "    auto *toggleBtn = new QPushButton(\n"
        '        e.enabled ? QStringLiteral("禁用此驱动") : QStringLiteral("启用此驱动"));\n'
        "    toggleBtn->setToolTip(QStringLiteral(\n"
        '        "禁用后设备树隐藏且不参与枚举/打开，重启后不加载（方案 §7.4）"));\n'
        "    connect(toggleBtn, &QPushButton::clicked, this, [this, driverId]() {\n"
        "        toggleDriverEnabled(driverId);\n"
        "    });\n"
        "    blay->addWidget(toggleBtn);\n"
        '    auto *uninstallBtn = new QPushButton(QStringLiteral("卸载此驱动"));\n'
        "    uninstallBtn->setEnabled(!e.builtin);\n"
        "    uninstallBtn->setToolTip(QStringLiteral(\n"
        '        "仅外置驱动可卸载；已加载的 DLL 在重启程序前仍驻留内存（方案 §7.4）"));\n',
        '? tr("Yes") : QStringLiteral("—")));\n'
        "        }\n"
        "        m_detailLay->addWidget(table);\n"
        "    }\n\n"
        "    auto *btnRow = new QWidget;\n"
        "    auto *blay = new QHBoxLayout(btnRow);\n"
        "    blay->addStretch(1);\n"
        "    auto *toggleBtn = new QPushButton(\n"
        '        e.enabled ? tr("Disable this driver") : tr("Enable this driver"));\n'
        "    toggleBtn->setToolTip(tr(\n"
        '        "When disabled, the driver is hidden from the device tree and skipped "\n'
        '        "for enumeration/open; it will not load after restart"));\n'
        "    connect(toggleBtn, &QPushButton::clicked, this, [this, driverId]() {\n"
        "        toggleDriverEnabled(driverId);\n"
        "    });\n"
        "    blay->addWidget(toggleBtn);\n"
        '    auto *uninstallBtn = new QPushButton(tr("Uninstall this driver"));\n'
        "    uninstallBtn->setEnabled(!e.builtin);\n"
        "    uninstallBtn->setToolTip(tr(\n"
        '        "Only external drivers can be uninstalled; a loaded DLL stays in memory "\n'
        '        "until the app restarts"));\n',
    ),
    # install / uninstall / tool messages
    (
        '''    const QString title = isPlugin ? QStringLiteral("安装插件")
                                   : QStringLiteral("安装驱动");''',
        '''    const QString title = isPlugin ? tr("Install Plugin")
                                   : tr("Install Driver");''',
    ),
    (
        'QStringLiteral("包下载失败: %1").arg(reply->errorString())',
        'tr("Package download failed: %1").arg(reply->errorString())',
    ),
    (
        '''                    QStringLiteral("包校验失败（sha256 不匹配），已中止安装。\\n"
                                   "请「刷新」市场索引后重试。"));''',
        '''                    tr("Package verification failed (sha256 mismatch); install aborted.\\n"
                       "Refresh the marketplace index and try again."));''',
    ),
    (
        'QStringLiteral("无法创建临时文件: %1").arg(tmp.errorString())',
        'tr("Cannot create temporary file: %1").arg(tmp.errorString())',
    ),
    (
        'QStringLiteral("插件 %1 安装成功。").arg(id)',
        'tr("Plugin %1 installed successfully.").arg(id)',
    ),
    (
        '''            QMessageBox::warning(this, QStringLiteral("安装驱动"),
                QStringLiteral("无法读取驱动包: %1").arg(odpPath));''',
        '''            QMessageBox::warning(this, tr("Install Driver"),
                tr("Cannot read driver package: %1").arg(odpPath));''',
    ),
    (
        '''            QMessageBox::warning(this, QStringLiteral("安装驱动"),
                QStringLiteral("驱动包校验失败（sha256 不匹配），已中止安装。"));''',
        '''            QMessageBox::warning(this, tr("Install Driver"),
                tr("Driver package verification failed (sha256 mismatch); install aborted."));''',
    ),
    (
        'QMessageBox::warning(this, QStringLiteral("安装驱动"), err);',
        'QMessageBox::warning(this, tr("Install Driver"), err);',
    ),
    (
        '''    const auto ret = QMessageBox::question(
        this, QStringLiteral("安装驱动"),
        QStringLiteral("即将安装驱动 %1 v%2。\\n\\n"
                       "注意：驱动为原生插件，安装后将加载进主进程"
                       "（与内置驱动同级，保证低时延性能）。是否继续？").arg(id, version));''',
        '''    const auto ret = QMessageBox::question(
        this, tr("Install Driver"),
        tr("About to install driver %1 v%2.\\n\\n"
           "Note: Drivers are native plugins and will be loaded into the main process "
           "after install (same level as built-in drivers for low latency). Continue?")
            .arg(id, version));''',
    ),
    (
        '''    QMessageBox::information(this, QStringLiteral("安装驱动"),
        QStringLiteral("驱动 %1 v%2 安装成功，已加载。")
            .arg(id, ires.value(QStringLiteral("version")).toString()));''',
        '''    QMessageBox::information(this, tr("Install Driver"),
        tr("Driver %1 v%2 installed and loaded successfully.")
            .arg(id, ires.value(QStringLiteral("version")).toString()));''',
    ),
    (
        '''    const auto ret = QMessageBox::question(
        this, QStringLiteral("卸载驱动"),
        QStringLiteral("确定卸载驱动 %1？\\n\\n"
                       "若其 DLL 已被本次运行加载，重启程序后将彻底清理（方案 §7.4）。")
            .arg(driverId));''',
        '''    const auto ret = QMessageBox::question(
        this, tr("Uninstall Driver"),
        tr("Uninstall driver %1?\\n\\n"
           "If its DLL is already loaded in this session, it will be fully cleaned up after restart.")
            .arg(driverId));''',
    ),
    (
        'QMessageBox::warning(this, QStringLiteral("卸载驱动"), err);',
        'QMessageBox::warning(this, tr("Uninstall Driver"), err);',
    ),
    (
        '''    const auto ret = QMessageBox::question(
        this, QStringLiteral("卸载插件"),
        QStringLiteral("确定卸载插件 %1？").arg(name));''',
        '''    const auto ret = QMessageBox::question(
        this, tr("Uninstall Plugin"),
        tr("Uninstall plugin %1?").arg(name));''',
    ),
    (
        'QMessageBox::warning(this, QStringLiteral("卸载插件"), err);',
        'QMessageBox::warning(this, tr("Uninstall Plugin"), err);',
    ),
    (
        'return QStringLiteral("未找到 Python 解释器");',
        'return tr("Python interpreter not found");',
    ),
    (
        'return QStringLiteral("驱动工具不存在: %1").arg(toolPath);',
        'return tr("Driver tool not found: %1").arg(toolPath);',
    ),
    (
        'return QStringLiteral("驱动工具执行超时");',
        'return tr("Driver tool timed out");',
    ),
    (
        '''        return QStringLiteral("驱动工具输出异常: %1")
                   .arg(QString::fromUtf8(out).left(300));''',
        '''        return tr("Unexpected driver tool output: %1")
                   .arg(QString::fromUtf8(out).left(300));''',
    ),
    (
        'return result->value(QStringLiteral("error")).toString(QStringLiteral("操作失败"));',
        'return result->value(QStringLiteral("error")).toString(tr("Operation failed"));',
    ),
]

missing = 0
for i, (old, new) in enumerate(replacements):
    if old not in text:
        print(f"MISSING [{i}]: {old[:80]!r}")
        missing += 1
    else:
        text = text.replace(old, new, 1)
        print(f"OK [{i}]")

p.write_text(text, encoding="utf-8")
print(f"done, missing={missing}")
