#include "sidebarpanels.h"
#include "core/dbcmanager.h"
#include "core/cansimulator.h"
#include "ui/graphicview.h"
#include "ui/signalconfigdialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTreeWidget>
#include <QListWidget>
#include <QListWidgetItem>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QSlider>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QHeaderView>
#include <QCollator>
#include <QDateTime>
#include <QInputDialog>
#include <QMessageBox>
#include <QFrame>
#include <QMouseEvent>
#include <QFontDatabase>
#include <QFile>
#include <QTextStream>
#include <QEvent>

// ============================================================
//  CollapsibleSection
// ============================================================

CollapsibleSection::CollapsibleSection(const QString &title, QWidget *parent)
    : QWidget(parent), m_expanded(true)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_toggleBtn = new QLabel(title, this);
    m_toggleBtn->setObjectName("CollapsibleTitle");
    m_toggleBtn->setContentsMargins(8, 4, 8, 4);
    m_toggleBtn->setCursor(Qt::PointingHandCursor);
    m_toggleBtn->setText((m_expanded ? "▼ " : "▶ ") + title);
    layout->addWidget(m_toggleBtn);

    m_content = new QWidget(this);
    m_content->setObjectName("CollapsibleContent");
    auto *contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    layout->addWidget(m_content);

    m_toggleBtn->installEventFilter(this);
}

void CollapsibleSection::setContent(QWidget *widget)
{
    auto *cl = qobject_cast<QVBoxLayout *>(m_content->layout());
    if (cl) {
        // remove old widget if any
        QLayoutItem *item;
        while ((item = cl->takeAt(0)) != nullptr) {
            delete item;
        }
        cl->addWidget(widget);
    }
}

void CollapsibleSection::setExpanded(bool expanded)
{
    m_expanded = expanded;
    m_content->setVisible(expanded);
    // update title text
    QString title = m_toggleBtn->text();
    title.remove(0, 2); // remove prefix
    m_toggleBtn->setText((m_expanded ? "▼ " : "▶ ") + title);
}

void CollapsibleSection::onToggle()
{
    setExpanded(!m_expanded);
}

bool CollapsibleSection::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_toggleBtn && event->type() == QEvent::MouseButtonPress) {
        onToggle();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

// ============================================================
//  SidePanel 基类
// ============================================================

SidePanel::SidePanel(const QString &title, QWidget *parent)
    : QWidget(parent), m_contentLayout(nullptr)
{
    setupTitle(title);
}

void SidePanel::setupTitle(const QString &title)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *titleBar = new QLabel(title, this);
    titleBar->setObjectName("SidePanelTitle");
    titleBar->setContentsMargins(8, 6, 8, 6);
    layout->addWidget(titleBar);

    // 内容容器
    auto *contentWidget = new QWidget(this);
    m_contentLayout = new QVBoxLayout(contentWidget);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);
    layout->addWidget(contentWidget, 1);
}

// ============================================================
//  ProjectPanel — 项目上下文管理器
// ============================================================

ProjectPanel::ProjectPanel(QWidget *parent)
    : SidePanel("工程管理", parent)
{
    auto *cl = contentLayout();

    // 项目列表
    m_projectList = new QListWidget(this);
    m_projectList->setObjectName("ProjectList");
    cl->addWidget(m_projectList, 1);

    // 按钮栏
    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(4, 4, 4, 4);
    auto *newBtn = new QPushButton("新建", this);
    auto *saveBtn = new QPushButton("保存", this);
    auto *delBtn = new QPushButton("删除", this);
    btnBar->addWidget(newBtn);
    btnBar->addWidget(saveBtn);
    btnBar->addWidget(delBtn);
    cl->addLayout(btnBar);

    // 默认创建一个项目
    ProjectContext defaultProj;
    defaultProj.name = "默认工程";
    m_projects.append(defaultProj);
    m_currentIndex = 0;
    refreshList();

    connect(newBtn, &QPushButton::clicked, this, &ProjectPanel::onNewProject);
    connect(saveBtn, &QPushButton::clicked, this, &ProjectPanel::onSaveProject);
    connect(delBtn, &QPushButton::clicked, this, &ProjectPanel::onDeleteProject);
    connect(m_projectList, &QListWidget::currentRowChanged,
            this, &ProjectPanel::onProjectSelected);
    connect(m_projectList, &QListWidget::itemDoubleClicked,
            this, &ProjectPanel::onItemDoubleClicked);
}

void ProjectPanel::refreshList()
{
    m_projectList->clear();
    for (int i = 0; i < m_projects.size(); ++i) {
        auto *item = new QListWidgetItem(m_projects[i].name);
        item->setData(Qt::UserRole, i);
        if (i == m_currentIndex)
            item->setSelected(true);
        m_projectList->addItem(item);
    }
    if (m_currentIndex >= 0 && m_currentIndex < m_projectList->count())
        m_projectList->setCurrentRow(m_currentIndex);
}

void ProjectPanel::onNewProject()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, "新建工程",
        "工程名称:", QLineEdit::Normal,
        QString("工程 %1").arg(m_projects.size() + 1), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    ProjectContext proj;
    proj.name = name.trimmed();
    m_projects.append(proj);
    m_currentIndex = m_projects.size() - 1;
    refreshList();
    emit projectCreated(proj.name);
    emit projectSwitched(m_currentIndex);
}

void ProjectPanel::onSaveProject()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_projects.size()) return;

    QString path = QFileDialog::getSaveFileName(
        this, "保存工程", m_projects[m_currentIndex].name + ".sinproj",
        "sin 工程文件 (*.sinproj);;所有文件 (*.*)");
    if (path.isEmpty()) return;

    // 简单文本格式保存
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "保存工程", "无法创建文件: " + path);
        return;
    }
    QTextStream out(&file);
    const auto &p = m_projects[m_currentIndex];
    out << "name=" << p.name << "\n";
    out << "[dbc]\n";
    for (const auto &f : p.dbcFiles)
        out << f << "\n";
    out << "[record]\n";
    for (const auto &f : p.recordFiles)
        out << f << "\n";
    out << "[layout]\n";
    out << p.layoutConfig << "\n";
    file.close();
}

void ProjectPanel::onDeleteProject()
{
    if (m_projects.size() <= 1) {
        QMessageBox::information(this, "删除工程", "至少保留一个工程");
        return;
    }
    if (m_currentIndex < 0) return;

    auto reply = QMessageBox::question(this, "删除工程",
        QString("确定删除工程 \"%1\"?").arg(m_projects[m_currentIndex].name));
    if (reply != QMessageBox::Yes) return;

    m_projects.removeAt(m_currentIndex);
    m_currentIndex = qMax(0, m_currentIndex - 1);
    refreshList();
    emit projectSwitched(m_currentIndex);
}

void ProjectPanel::onProjectSelected(int row)
{
    if (row < 0 || row >= m_projects.size()) return;
    m_currentIndex = row;
    emit projectSwitched(m_currentIndex);
}

void ProjectPanel::onItemDoubleClicked(QListWidgetItem *item)
{
    // 双击项目项可触发打开关联文件
    Q_UNUSED(item);
    // 未来可扩展
}

// ============================================================
//  DbcPanel
// ============================================================

DbcPanel::DbcPanel(QWidget *parent)
    : SidePanel("DBC 数据库", parent)
{
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(16);
    m_tree->setColumnCount(1);

    auto *cl = contentLayout();
    cl->addWidget(m_tree);

    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(4, 4, 4, 4);
    auto *importBtn = new QPushButton("导入 DBC", this);
    btnBar->addWidget(importBtn);
    btnBar->addStretch();
    cl->addLayout(btnBar);

    connect(importBtn, &QPushButton::clicked, this, &DbcPanel::onImportDbc);
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &DbcPanel::onItemDoubleClicked);

    auto *hint = new QTreeWidgetItem(m_tree, {"（点击导入 DBC 文件）"});
    hint->setFlags(Qt::NoItemFlags);
}

void DbcPanel::setDbcManager(DbcManager *mgr)
{
    m_dbcMgr = mgr;
    if (m_dbcMgr) {
        connect(m_dbcMgr, &DbcManager::dbcLoaded, this, [this](const QString &) { refreshTree(); });
        connect(m_dbcMgr, &DbcManager::dbcUnloaded, this, [this](const QString &) { refreshTree(); });
    }
    refreshTree();
}

void DbcPanel::onImportDbc()
{
    QString path = QFileDialog::getOpenFileName(
        this, "导入 DBC 文件", {}, "DBC 文件 (*.dbc);;所有文件 (*.*)");
    if (path.isEmpty() || !m_dbcMgr)
        return;

    if (!m_dbcMgr->loadDbc(path))
        m_tree->addTopLevelItem(new QTreeWidgetItem({QString("加载失败: %1").arg(path)}));
}

void DbcPanel::refreshTree()
{
    m_tree->clear();
    if (!m_dbcMgr) return;

    for (const auto &file : m_dbcMgr->files()) {
        auto *fileItem = new QTreeWidgetItem(m_tree, {file.fileName});
        fileItem->setIcon(0, style()->standardIcon(QStyle::SP_FileDialogListView));

        for (const auto &msg : file.messages) {
            QString msgText = QString("0x%1  %2  (%3 bytes)")
                .arg(msg.id, 0, 16).toUpper()
                .arg(msg.name)
                .arg(msg.dlc);
            auto *msgItem = new QTreeWidgetItem(fileItem, {msgText});
            msgItem->setIcon(0, style()->standardIcon(QStyle::SP_ArrowRight));
            msgItem->setData(0, Qt::UserRole, msg.id);

            for (const auto &sig : msg.signalList) {
                QString sigText = QString("%1  [%2:%3]  (%4,%5) %6")
                    .arg(sig.name)
                    .arg(sig.startBit)
                    .arg(sig.bitLength)
                    .arg(sig.factor)
                    .arg(sig.offset)
                    .arg(sig.unit);
                auto *sigItem = new QTreeWidgetItem(msgItem, {sigText});
                sigItem->setData(0, Qt::UserRole + 0, msg.id);
                sigItem->setData(0, Qt::UserRole + 1, sig.name);
            }
        }
    }
}

void DbcPanel::onItemDoubleClicked(QTreeWidgetItem *item, int)
{
    QString sigName = item->data(0, Qt::UserRole + 1).toString();
    if (!sigName.isEmpty()) {
        quint32 canId = item->data(0, Qt::UserRole + 0).toUInt();
        emit signalDoubleClicked(canId, sigName);
    }
}

// ============================================================
//  TraceConfigPanel（含回放控制折叠区）
// ============================================================

TraceConfigPanel::TraceConfigPanel(QWidget *parent)
    : SidePanel("Trace 配置", parent)
{
    auto *cl = contentLayout();

    // ---- 回放控制折叠区 ----
    auto *playbackSection = new CollapsibleSection("回放控制", this);

    auto *playbackWidget = new QWidget(this);
    auto *pbLayout = new QVBoxLayout(playbackWidget);
    pbLayout->setContentsMargins(4, 4, 4, 4);
    pbLayout->setSpacing(4);

    // 播放按钮行
    auto *btnRow = new QHBoxLayout;
    m_playBtn = new QPushButton("▶ 播放", this);
    m_pauseBtn = new QPushButton("⏸ 暂停", this);
    m_stopBtn = new QPushButton("⏹ 停止", this);
    m_playBtn->setEnabled(false);
    m_pauseBtn->setEnabled(false);
    m_stopBtn->setEnabled(false);
    btnRow->addWidget(m_playBtn);
    btnRow->addWidget(m_pauseBtn);
    btnRow->addWidget(m_stopBtn);
    pbLayout->addLayout(btnRow);

    // 进度滑块
    auto *seekRow = new QHBoxLayout;
    seekRow->addWidget(new QLabel("位置:", this));
    m_seekSlider = new QSlider(Qt::Horizontal, this);
    m_seekSlider->setMinimum(0);
    m_seekSlider->setMaximum(1000);
    seekRow->addWidget(m_seekSlider, 1);
    pbLayout->addLayout(seekRow);

    // 速度
    auto *speedRow = new QHBoxLayout;
    speedRow->addWidget(new QLabel("速度:", this));
    m_speedCombo = new QComboBox(this);
    m_speedCombo->addItem("0.25x", 0.25);
    m_speedCombo->addItem("0.5x", 0.5);
    m_speedCombo->addItem("1x", 1.0);
    m_speedCombo->addItem("2x", 2.0);
    m_speedCombo->addItem("4x", 4.0);
    m_speedCombo->addItem("8x", 8.0);
    m_speedCombo->setCurrentIndex(2);
    speedRow->addWidget(m_speedCombo, 1);
    pbLayout->addLayout(speedRow);

    playbackSection->setContent(playbackWidget);
    cl->addWidget(playbackSection);

    // ---- 列显示折叠区 ----
    auto *colSection = new CollapsibleSection("显示列", this);

    auto *colWidget = new QWidget(this);
    auto *colLayout = new QVBoxLayout(colWidget);
    colLayout->setContentsMargins(8, 4, 8, 4);
    colLayout->setSpacing(2);

    m_chkTime  = new QCheckBox("Time", colWidget);     m_chkTime->setChecked(true);
    m_chkCh    = new QCheckBox("Channel", colWidget);   m_chkCh->setChecked(true);
    m_chkDir   = new QCheckBox("Direction", colWidget); m_chkDir->setChecked(true);
    m_chkId    = new QCheckBox("ID", colWidget);        m_chkId->setChecked(true);
    m_chkDlc   = new QCheckBox("DLC", colWidget);       m_chkDlc->setChecked(true);
    m_chkData  = new QCheckBox("Data", colWidget);      m_chkData->setChecked(true);
    m_chkFlags = new QCheckBox("Flags", colWidget);     m_chkFlags->setChecked(true);

    for (auto *cb : {m_chkTime, m_chkCh, m_chkDir, m_chkId, m_chkDlc, m_chkData, m_chkFlags}) {
        colLayout->addWidget(cb);
        connect(cb, &QCheckBox::toggled, this, &TraceConfigPanel::columnsChanged);
    }

    colSection->setContent(colWidget);
    cl->addWidget(colSection);

    // ---- 过滤器预设折叠区 ----
    auto *presetSection = new CollapsibleSection("过滤器预设", this);

    m_presets = new QListWidget(this);
    m_presets->addItem("全部报文");
    m_presets->addItem("CAN FD 帧 (fd)");
    m_presets->addItem("扩展帧 (ext)");
    m_presets->addItem("仅接收 (rx)");
    m_presets->addItem("DLC > 8");
    m_presets->addItem("ID 0x100");
    m_presets->addItem("ID 0x200");

    presetSection->setContent(m_presets);
    cl->addWidget(presetSection, 1);

    // ---- 信号连接 ----
    connect(m_playBtn, &QPushButton::clicked, this, &TraceConfigPanel::playRequested);
    connect(m_pauseBtn, &QPushButton::clicked, this, &TraceConfigPanel::pauseRequested);
    connect(m_stopBtn, &QPushButton::clicked, this, &TraceConfigPanel::stopRequested);

    connect(m_speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this](int idx) {
        emit speedChanged(m_speedCombo->itemData(idx).toDouble());
    });

    connect(m_seekSlider, &QSlider::sliderMoved, this, [this](int value) {
        emit seekChanged(value / 1000.0);
    });

    connect(m_presets, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        static const QStringList filters = {
            "", "fd", "ext", "rx", "dlc > 8", "id == 0x100", "id == 0x200"
        };
        int row = m_presets->row(item);
        if (row >= 0 && row < filters.size())
            emit filterPresetApplied(filters[row]);
    });
}

void TraceConfigPanel::setPlayerLoaded(bool loaded, bool playing)
{
    m_playBtn->setEnabled(loaded && !playing);
    m_pauseBtn->setEnabled(playing);
    m_stopBtn->setEnabled(loaded);
}

// ============================================================
//  GraphicConfigPanel
// ============================================================

GraphicConfigPanel::GraphicConfigPanel(QWidget *parent)
    : SidePanel("Graphic 配置", parent)
{
    auto *cl = contentLayout();

    m_signalList = new QListWidget(this);
    cl->addWidget(m_signalList, 1);

    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(4, 4, 4, 4);
    auto *addBtn = new QPushButton("添加", this);
    auto *rmBtn = new QPushButton("删除", this);
    auto *clrBtn = new QPushButton("清空", this);
    btnBar->addWidget(addBtn);
    btnBar->addWidget(rmBtn);
    btnBar->addWidget(clrBtn);
    cl->addLayout(btnBar);

    connect(addBtn, &QPushButton::clicked, this, &GraphicConfigPanel::onAddSignal);
    connect(rmBtn, &QPushButton::clicked, this, &GraphicConfigPanel::onRemoveSignal);
    connect(clrBtn, &QPushButton::clicked, this, &GraphicConfigPanel::onClearSignals);
}

void GraphicConfigPanel::setGraphicView(GraphicView *view)
{
    m_graphicView = view;
    refreshList();
}

void GraphicConfigPanel::onAddSignal()
{
    if (!m_graphicView) return;
    SignalConfigDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        GraphicView::Signal sig;
        sig.name = dlg.signalName();
        sig.canId = dlg.canId();
        sig.extended = dlg.isExtended();
        sig.byteOffset = dlg.byteOffset();
        sig.bitLength = dlg.bitLength();
        sig.bigEndian = dlg.isBigEndian();
        m_graphicView->addSignal(sig);
        refreshList();
    }
}

void GraphicConfigPanel::onRemoveSignal()
{
    if (!m_graphicView) return;
    int row = m_signalList->currentRow();
    if (row >= 0) {
        m_graphicView->removeSignal(row);
        refreshList();
    }
}

void GraphicConfigPanel::onClearSignals()
{
    if (!m_graphicView) return;
    m_graphicView->clearSignals();
    refreshList();
}

void GraphicConfigPanel::refreshList()
{
    m_signalList->clear();
    if (!m_graphicView) return;
    for (const auto &sig : m_graphicView->signalConfigs()) {
        QString text = QString("%1  [0x%2:%3]")
            .arg(sig.name)
            .arg(sig.canId, 0, 16).toUpper()
            .arg(sig.byteOffset);
        auto *item = new QListWidgetItem(text);
        item->setForeground(sig.color);
        m_signalList->addItem(item);
    }
}

// ============================================================
//  DevicePanel（含录制控制折叠区）
// ============================================================

DevicePanel::DevicePanel(QWidget *parent)
    : SidePanel("设备连接", parent)
{
    auto *cl = contentLayout();

    // ---- 录制控制折叠区 ----
    auto *recordSection = new CollapsibleSection("录制控制", this);

    auto *recordWidget = new QWidget(this);
    auto *recLayout = new QVBoxLayout(recordWidget);
    recLayout->setContentsMargins(4, 4, 4, 4);
    recLayout->setSpacing(4);

    m_recordBtn = new QPushButton("● 开始录制", recordWidget);
    m_recordBtn->setCheckable(true);
    recLayout->addWidget(m_recordBtn);

    m_autoScrollChk = new QCheckBox("自动滚动", recordWidget);
    m_autoScrollChk->setChecked(true);
    recLayout->addWidget(m_autoScrollChk);

    auto *clearBtn = new QPushButton("清空 Trace", recordWidget);
    recLayout->addWidget(clearBtn);

    recordSection->setContent(recordWidget);
    cl->addWidget(recordSection);

    // ---- 设备连接折叠区 ----
    auto *connSection = new CollapsibleSection("设备连接", this);

    auto *connWidget = new QWidget(this);
    auto *connLayout = new QVBoxLayout(connWidget);
    connLayout->setContentsMargins(4, 4, 4, 4);
    connLayout->setSpacing(4);

    // 设备类型
    auto *devLayout = new QHBoxLayout;
    devLayout->addWidget(new QLabel("设备:", connWidget));
    m_deviceCombo = new QComboBox(connWidget);
    m_deviceCombo->addItem("模拟器 (内置)");
    m_deviceCombo->addItem("PCAN-USB");
    m_deviceCombo->addItem("Kvaser");
    m_deviceCombo->addItem("Vector VN1630");
    m_deviceCombo->addItem("SocketCAN");
    devLayout->addWidget(m_deviceCombo, 1);
    connLayout->addLayout(devLayout);

    // 通道
    auto *chLayout = new QHBoxLayout;
    chLayout->addWidget(new QLabel("通道:", connWidget));
    m_channelCombo = new QComboBox(connWidget);
    m_channelCombo->addItem("1");
    m_channelCombo->addItem("2");
    chLayout->addWidget(m_channelCombo, 1);
    connLayout->addLayout(chLayout);

    // 波特率
    auto *brLayout = new QHBoxLayout;
    brLayout->addWidget(new QLabel("波特率:", connWidget));
    m_baudCombo = new QComboBox(connWidget);
    m_baudCombo->addItem("500 Kbps", 500000);
    m_baudCombo->addItem("250 Kbps", 250000);
    m_baudCombo->addItem("1 Mbps", 1000000);
    m_baudCombo->addItem("125 Kbps", 125000);
    m_baudCombo->addItem("800 Kbps", 800000);
    brLayout->addWidget(m_baudCombo, 1);
    connLayout->addLayout(brLayout);

    auto *btnBar = new QHBoxLayout;
    m_connectBtn = new QPushButton("连接", connWidget);
    m_disconnectBtn = new QPushButton("断开", connWidget);
    m_disconnectBtn->setEnabled(false);
    btnBar->addWidget(m_connectBtn);
    btnBar->addWidget(m_disconnectBtn);
    connLayout->addLayout(btnBar);

    m_statusLabel = new QLabel("● 未连接", connWidget);
    m_statusLabel->setContentsMargins(4, 0, 4, 4);
    connLayout->addWidget(m_statusLabel);

    connSection->setContent(connWidget);
    cl->addWidget(connSection, 1);

    // ---- 信号连接 ----
    connect(m_connectBtn, &QPushButton::clicked, this, &DevicePanel::onConnect);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &DevicePanel::onDisconnect);
    connect(m_recordBtn, &QPushButton::toggled, this, &DevicePanel::onRecord);
    connect(clearBtn, &QPushButton::clicked, this, &DevicePanel::onClear);
    connect(m_autoScrollChk, &QCheckBox::stateChanged, this, &DevicePanel::onAutoScroll);
}

void DevicePanel::setSimulator(CanSimulator *sim)
{
    m_simulator = sim;
}

void DevicePanel::setRecording(bool recording)
{
    m_recordBtn->blockSignals(true);
    m_recordBtn->setChecked(recording);
    m_recordBtn->setText(recording ? "■ 停止录制" : "● 开始录制");
    m_recordBtn->blockSignals(false);
}

void DevicePanel::onConnect()
{
    QString device = m_deviceCombo->currentText();
    int baudrate = m_baudCombo->currentData().toInt();
    int channel = m_channelCombo->currentText().toInt();

    if (m_deviceCombo->currentIndex() == 0 && m_simulator) {
        m_simulator->setChannel(static_cast<quint8>(channel));
        m_simulator->start();
    }

    m_connectBtn->setEnabled(false);
    m_disconnectBtn->setEnabled(true);
    m_statusLabel->setText(QString("● 已连接: %1 (Ch%2, %3 Kbps)")
        .arg(device).arg(channel).arg(baudrate / 1000));
    m_statusLabel->setStyleSheet("color: green;");

    emit deviceConnectRequested(device, baudrate);
}

void DevicePanel::onDisconnect()
{
    if (m_simulator)
        m_simulator->stop();

    m_connectBtn->setEnabled(true);
    m_disconnectBtn->setEnabled(false);
    m_statusLabel->setText("● 未连接");
    m_statusLabel->setStyleSheet("color: gray;");

    emit deviceDisconnectRequested();
}

void DevicePanel::onRecord()
{
    bool on = m_recordBtn->isChecked();
    m_recordBtn->setText(on ? "■ 停止录制" : "● 开始录制");
    emit recordToggled(on);
}

void DevicePanel::onClear()
{
    emit clearRequested();
}

void DevicePanel::onAutoScroll(int state)
{
    emit autoScrollToggled(state == Qt::Checked);
}

// ============================================================
//  SideBar
// ============================================================

SideBar::SideBar(QWidget *parent)
    : QStackedWidget(parent)
{
    m_project = new ProjectPanel(this);
    m_dbc = new DbcPanel(this);
    m_traceConfig = new TraceConfigPanel(this);
    m_graphicConfig = new GraphicConfigPanel(this);
    m_device = new DevicePanel(this);

    addWidget(m_project);       // index 0 = Project
    addWidget(m_dbc);           // index 1 = Dbc
    addWidget(m_traceConfig);   // index 2 = Trace
    addWidget(m_graphicConfig); // index 3 = Graphic
    addWidget(m_device);        // index 4 = Device

    setCurrentIndex(0);
    setMinimumWidth(220);
    setMaximumWidth(400);
}

void SideBar::showPanel(int index)
{
    if (index < 0 || index >= count()) return;
    setCurrentIndex(index);
}

void SideBar::togglePanel(int index)
{
    Q_UNUSED(index);
}
