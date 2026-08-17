#include "extensionstab.h"
#include "core/plugin/pluginmanager.h"
#include "core/plugin/plugininfo.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QTimer>
#include <QFileDialog>
#include <QMessageBox>
#include <QMenu>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

// ---- 工具函数 ----

static QString formatBytes(quint64 bytes)
{
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024 * 1024 * 1024)
        return QString::number(bytes / (1024.0 * 1024), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 2) + " GB";
}

// ============================================================
//  ExtensionsTab
// ============================================================

ExtensionsTab::ExtensionsTab(QWidget *parent)
    : QWidget(parent)
    , m_pm(PluginManager::instance())
{
    setupUi();

    // 资源监控定时器（2 秒采样一次）
    m_resourceTimer = new QTimer(this);
    m_resourceTimer->setInterval(2000);
    connect(m_resourceTimer, &QTimer::timeout, this, &ExtensionsTab::onResourceTimer);
    m_resourceTimer->start();

    refresh();
}

ExtensionsTab::~ExtensionsTab()
{
    if (m_resourceTimer)
        m_resourceTimer->stop();
}

void ExtensionsTab::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // ---- 宿主进程信息面板 ----
    auto *hostFrame = new QFrame;
    hostFrame->setObjectName("HostInfoFrame");
    hostFrame->setFrameShape(QFrame::StyledPanel);
    hostFrame->setStyleSheet(
        "QFrame#HostInfoFrame { background: #2a2a2a; border: 1px solid #3c3c3c; border-radius: 4px; }"
        "QLabel { color: #ccc; }");

    auto *hostLayout = new QGridLayout(hostFrame);
    hostLayout->setContentsMargins(12, 8, 12, 8);
    hostLayout->setHorizontalSpacing(12);
    hostLayout->setVerticalSpacing(4);

    auto makeTitle = [](const QString &t) {
        auto *l = new QLabel(t);
        l->setStyleSheet("color: #888; font-size: 11px;");
        return l;
    };

    // 第一行
    int row = 0;
    hostLayout->addWidget(makeTitle(QStringLiteral("宿主状态")), row, 0);
    m_hostStatus = new QLabel(QStringLiteral("—"));
    m_hostStatus->setStyleSheet("font-weight: bold;");
    hostLayout->addWidget(m_hostStatus, row, 1);

    hostLayout->addWidget(makeTitle(QStringLiteral("PID")), row, 2);
    m_hostPid = new QLabel(QStringLiteral("—"));
    hostLayout->addWidget(m_hostPid, row, 3);

    hostLayout->addWidget(makeTitle(QStringLiteral("CPU")), row, 4);
    m_cpuLabel = new QLabel(QStringLiteral("—"));
    m_cpuLabel->setStyleSheet("font-weight: bold; color: #4ec9b0;");
    hostLayout->addWidget(m_cpuLabel, row, 5);

    // 第二行
    row++;
    hostLayout->addWidget(makeTitle(QStringLiteral("内存")), row, 0);
    m_memLabel = new QLabel(QStringLiteral("—"));
    m_memLabel->setStyleSheet("font-weight: bold; color: #569cd6;");
    hostLayout->addWidget(m_memLabel, row, 1);

    hostLayout->addWidget(makeTitle(QStringLiteral("磁盘读")), row, 2);
    m_diskReadLabel = new QLabel(QStringLiteral("—"));
    hostLayout->addWidget(m_diskReadLabel, row, 3);

    hostLayout->addWidget(makeTitle(QStringLiteral("磁盘写")), row, 4);
    m_diskWriteLabel = new QLabel(QStringLiteral("—"));
    hostLayout->addWidget(m_diskWriteLabel, row, 5);

    // 第三行
    row++;
    hostLayout->addWidget(makeTitle(QStringLiteral("Python")), row, 0);
    m_pythonPath = new QLabel(QStringLiteral("—"));
    m_pythonPath->setStyleSheet("color: #888;");
    hostLayout->addWidget(m_pythonPath, row, 1, 1, 3);

    m_stopHostBtn = new QPushButton(QStringLiteral("停止宿主"));
    m_stopHostBtn->setStyleSheet(
        "QPushButton { background: #3c1c1c; color: #f44747; border: 1px solid #5a2a2a; padding: 3px 12px; }"
        "QPushButton:hover { background: #4c2c2c; }"
        "QPushButton:disabled { color: #666; background: #2a2a2a; border-color: #333; }");
    connect(m_stopHostBtn, &QPushButton::clicked, this, [this]() {
        emit hostStopRequested();
    });
    hostLayout->addWidget(m_stopHostBtn, row, 5);

    layout->addWidget(hostFrame);

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
    updateHostInfo();
    populateTable();
    sampleProcessResources();
}

void ExtensionsTab::onRefreshClicked()
{
    m_firstSample = true;  // 重置采样状态
    refresh();
}

void ExtensionsTab::onResourceTimer()
{
    sampleProcessResources();
    updateHostInfo();
}

void ExtensionsTab::updateHostInfo()
{
    bool running = m_pm && m_pm->isHostRunning();
    m_hostStatus->setText(running ? QStringLiteral("运行中") : QStringLiteral("已停止"));
    m_hostStatus->setStyleSheet(
        running ? "color: #4ec9b0; font-weight: bold;" : "color: #f44747; font-weight: bold;");

    qint64 pid = m_pm ? m_pm->hostProcessId() : 0;
    m_hostPid->setText(pid > 0 ? QString::number(pid) : QStringLiteral("—"));

    m_stopHostBtn->setEnabled(running);

    if (m_pm) {
        QString pyPath = m_pm->pythonExecutable();
        m_pythonPath->setText(pyPath.isEmpty() ? QStringLiteral("未找到") : pyPath);
    }
}

void ExtensionsTab::sampleProcessResources()
{
#ifdef Q_OS_WIN
    if (!m_pm || !m_pm->isHostRunning()) {
        m_cpuLabel->setText("—");
        m_memLabel->setText("—");
        m_diskReadLabel->setText("—");
        m_diskWriteLabel->setText("—");
        m_firstSample = true;
        return;
    }

    qint64 pid = m_pm->hostProcessId();
    if (pid <= 0) return;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE,
                                   static_cast<DWORD>(pid));
    if (!hProcess) return;

    // ---- CPU 时间 ----
    FILETIME ftCreate, ftExit, ftKernel, ftUser;
    quint64 cpuTime = 0;
    if (GetProcessTimes(hProcess, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
        ULARGE_INTEGER kernel, user;
        kernel.LowPart = ftKernel.dwLowDateTime;
        kernel.HighPart = ftKernel.dwHighDateTime;
        user.LowPart = ftUser.dwLowDateTime;
        user.HighPart = ftUser.dwHighDateTime;
        cpuTime = kernel.QuadPart + user.QuadPart;
    }

    // 系统时间（墙钟）
    FILETIME ftNow;
    GetSystemTimeAsFileTime(&ftNow);
    ULARGE_INTEGER now;
    now.LowPart = ftNow.dwLowDateTime;
    now.HighPart = ftNow.dwHighDateTime;

    // ---- 内存 ----
    PROCESS_MEMORY_COUNTERS memCounters;
    quint64 memBytes = 0;
    if (GetProcessMemoryInfo(hProcess, &memCounters, sizeof(memCounters))) {
        memBytes = memCounters.WorkingSetSize;
    }

    // ---- 磁盘 I/O ----
    IO_COUNTERS ioCounters;
    quint64 diskRead = 0, diskWrite = 0;
    if (GetProcessIoCounters(hProcess, &ioCounters)) {
        diskRead = ioCounters.ReadTransferCount;
        diskWrite = ioCounters.WriteTransferCount;
    }

    CloseHandle(hProcess);

    if (m_firstSample) {
        // 首次采样只记录基准值，不显示变化
        m_prevCpuTime = cpuTime;
        m_prevWallTime = static_cast<qint64>(now.QuadPart);
        m_prevDiskRead = diskRead;
        m_prevDiskWrite = diskWrite;
        m_firstSample = false;

        m_cpuLabel->setText("0.0%");
        m_memLabel->setText(formatBytes(memBytes));
        m_diskReadLabel->setText(formatBytes(0));
        m_diskWriteLabel->setText(formatBytes(0));
        return;
    }

    // CPU 百分比 = (ΔCPU时间 / Δ墙钟时间) × 100 / 处理器数
    qint64 dCpu = static_cast<qint64>(cpuTime) - m_prevCpuTime;
    qint64 dWall = static_cast<qint64>(now.QuadPart) - m_prevWallTime;

    double cpuPercent = 0.0;
    if (dWall > 0) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        int numCpus = si.dwNumberOfProcessors;
        if (numCpus < 1) numCpus = 1;
        cpuPercent = (static_cast<double>(dCpu) / dWall) * 100.0 / numCpus;
        if (cpuPercent < 0) cpuPercent = 0;
    }

    // 磁盘 I/O 增量
    quint64 dRead = 0, dWrite = 0;
    if (diskRead >= m_prevDiskRead) dRead = diskRead - m_prevDiskRead;
    if (diskWrite >= m_prevDiskWrite) dWrite = diskWrite - m_prevDiskWrite;

    // 更新 UI
    m_cpuLabel->setText(QString("%1%").arg(cpuPercent, 0, 'f', 1));
    // CPU 颜色：低=绿，中=黄，高=红
    if (cpuPercent < 10)
        m_cpuLabel->setStyleSheet("font-weight: bold; color: #4ec9b0;");
    else if (cpuPercent < 50)
        m_cpuLabel->setStyleSheet("font-weight: bold; color: #dcdcaa;");
    else
        m_cpuLabel->setStyleSheet("font-weight: bold; color: #f44747;");

    m_memLabel->setText(formatBytes(memBytes));
    m_diskReadLabel->setText(formatBytes(dRead));
    m_diskWriteLabel->setText(formatBytes(dWrite));

    // 更新基准值
    m_prevCpuTime = cpuTime;
    m_prevWallTime = static_cast<qint64>(now.QuadPart);
    m_prevDiskRead = diskRead;
    m_prevDiskWrite = diskWrite;
#else
    // 非 Windows 平台：不支持
    m_cpuLabel->setText("N/A");
    m_memLabel->setText("N/A");
    m_diskReadLabel->setText("N/A");
    m_diskWriteLabel->setText("N/A");
#endif
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
