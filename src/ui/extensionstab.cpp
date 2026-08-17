#include "extensionstab.h"
#include "core/plugin/pluginmanager.h"
#include "core/plugin/plugininfo.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QMenu>

// ============================================================
//  ExtensionsTab
// ============================================================

ExtensionsTab::ExtensionsTab(QWidget *parent)
    : QWidget(parent)
    , m_pm(PluginManager::instance())
{
    setupUi();
    refresh();
}

void ExtensionsTab::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // ---- 工具栏 ----
    auto *toolbar = new QHBoxLayout;
    m_refreshBtn = new QPushButton(QStringLiteral("刷新"));
    m_refreshBtn->setFixedWidth(80);
    connect(m_refreshBtn, &QPushButton::clicked, this, &ExtensionsTab::onRefreshClicked);
    toolbar->addWidget(m_refreshBtn);

    // G9: 安装 .opk 插件包
    auto *installBtn = new QPushButton(QStringLiteral("安装 .opk..."));
    installBtn->setFixedWidth(110);
    installBtn->setToolTip(QStringLiteral("从 .opk 插件包安装（可由 plugin_tool.py pack 打包）"));
    connect(installBtn, &QPushButton::clicked, this, &ExtensionsTab::onInstallOpk);
    toolbar->addWidget(installBtn);

    toolbar->addStretch();
    m_pluginCount = new QLabel;
    m_pluginCount->setStyleSheet("color: #888;");
    toolbar->addWidget(m_pluginCount);
    layout->addLayout(toolbar);

    // ---- 插件表格 ----
    m_table = new QTableWidget;
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("插件名称"),
        QStringLiteral("版本"),
        QStringLiteral("作者"),
        QStringLiteral("描述"),
        QStringLiteral("状态"),
        QStringLiteral("操作")
    });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);

    connect(m_table, &QTableWidget::cellDoubleClicked,
            this, &ExtensionsTab::onItemDoubleClicked);

    // G9: 右键菜单（卸载插件）
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, &ExtensionsTab::onTableContextMenu);

    layout->addWidget(m_table, 1);
}

void ExtensionsTab::refresh()
{
    populateTable();
}

void ExtensionsTab::onRefreshClicked()
{
    refresh();
}

void ExtensionsTab::populateTable()
{
    m_table->setRowCount(0);

    if (!m_pm) {
        m_pluginCount->setText(QStringLiteral("插件系统未初始化"));
        return;
    }

    const auto plugins = m_pm->discoveredPlugins();
    m_table->setRowCount(plugins.size());

    for (int i = 0; i < plugins.size(); ++i) {
        const auto &info = plugins[i];

        // 名称
        m_table->setItem(i, 0, new QTableWidgetItem(info.name));
        // 版本
        m_table->setItem(i, 1, new QTableWidgetItem(info.version));
        // 作者
        m_table->setItem(i, 2, new QTableWidgetItem(info.author));
        // 描述
        m_table->setItem(i, 3, new QTableWidgetItem(info.description));

        // 状态
        bool enabled = m_pm->isPluginEnabled(info.name);
        bool activated = m_pm->isPluginActivated(info.name);

        QString statusText;
        QColor statusColor;
        if (!enabled) {
            statusText = QStringLiteral("已禁用");
            statusColor = QColor("#888888");
        } else if (activated) {
            statusText = QStringLiteral("● 运行中");
            statusColor = QColor("#4ec9b0");
        } else {
            statusText = QStringLiteral("已就绪");
            statusColor = QColor("#569cd6");
        }
        auto *statusItem = new QTableWidgetItem(statusText);
        statusItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(i, 4, statusItem);

        // 操作按钮
        auto *btn = new QPushButton;
        btn->setFixedWidth(64);
        if (!enabled) {
            btn->setText(QStringLiteral("启用"));
            btn->setProperty("action", "enable");
        } else if (activated) {
            btn->setText(QStringLiteral("停止"));
            btn->setProperty("action", "stop");
        } else {
            btn->setText(QStringLiteral("启动"));
            btn->setProperty("action", "start");
        }

        connect(btn, &QPushButton::clicked, this,
                [this, name = info.name, btn]() {
                    QString action = btn->property("action").toString();
                    if (action == "stop")
                        emit pluginDeactivateRequested(name);
                    else if (action == "start")
                        emit pluginActivateRequested(name);
                    else if (action == "enable")
                        emit pluginToggleRequested(name, true);
                });

        m_table->setCellWidget(i, 5, btn);

        m_table->item(i, 4)->setForeground(statusColor);
    }

    // 统计
    int running = 0, ready = 0, disabled = 0;
    for (const auto &info : plugins) {
        if (!m_pm->isPluginEnabled(info.name))
            disabled++;
        else if (m_pm->isPluginActivated(info.name))
            running++;
        else
            ready++;
    }
    m_pluginCount->setText(
        QStringLiteral("共 %1 个插件 — 运行中: %2  已就绪: %3  已禁用: %4")
            .arg(plugins.size()).arg(running).arg(ready).arg(disabled));
}

void ExtensionsTab::onItemDoubleClicked(int row, int /*col*/)
{
    auto *item = m_table->item(row, 0);
    if (!item) return;
    QString name = item->text();
    if (!name.isEmpty())
        emit pluginActivateRequested(name);
}

// ============================================================
//  G9: 插件包安装 / 卸载
// ============================================================

void ExtensionsTab::onInstallOpk()
{
    QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择插件包"), QString(),
        QStringLiteral("openbus 插件包 (*.opk);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    QString err = m_pm ? m_pm->installPackage(path)
                       : QStringLiteral("插件系统未初始化");
    if (!err.isEmpty())
        QMessageBox::warning(this, QStringLiteral("安装插件"), err);
    else
        QMessageBox::information(this, QStringLiteral("安装插件"),
                                 QStringLiteral("插件安装成功"));
    refresh();
}

void ExtensionsTab::onTableContextMenu(const QPoint &pos)
{
    auto *item = m_table->itemAt(pos);
    if (!item) return;
    const int row = item->row();
    auto *nameItem = m_table->item(row, 0);
    if (!nameItem) return;
    const QString name = nameItem->text();

    QMenu menu(this);
    QAction *uninstallAct = menu.addAction(QStringLiteral("卸载"));
    QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (chosen != uninstallAct)
        return;

    if (QMessageBox::question(this, QStringLiteral("卸载插件"),
                              QStringLiteral("确定卸载插件 %1？").arg(name))
        != QMessageBox::Yes)
        return;

    QString err = m_pm ? m_pm->uninstallPlugin(name)
                       : QStringLiteral("插件系统未初始化");
    if (!err.isEmpty())
        QMessageBox::warning(this, QStringLiteral("卸载插件"), err);
    refresh();
}
