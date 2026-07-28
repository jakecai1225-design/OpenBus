#include "rightpanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>

RightPanel::RightPanel(QWidget *parent)
    : QTabWidget(parent)
{
    setObjectName("RightPanel");

    // ---- AI 对话标签页 ----
    auto *chatWidget = new QWidget(this);
    auto *chatLayout = new QVBoxLayout(chatWidget);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(0);

    m_chatMessages = new QPlainTextEdit(chatWidget);
    m_chatMessages->setReadOnly(true);
    m_chatMessages->setPlaceholderText("AI 助手对话区...");
    chatLayout->addWidget(m_chatMessages, 1);

    auto *inputBar = new QHBoxLayout;
    inputBar->setContentsMargins(4, 4, 4, 4);
    m_chatInput = new QLineEdit(chatWidget);
    m_chatInput->setPlaceholderText("输入消息...");
    auto *sendBtn = new QPushButton("发送", chatWidget);
    inputBar->addWidget(m_chatInput, 1);
    inputBar->addWidget(sendBtn);
    chatLayout->addLayout(inputBar);

    addTab(chatWidget, "AI 对话");

    // 初始消息
    appendAiMessage("AI", "你好，我是 sin AI 助手，可以帮你分析 CAN 报文。");

    connect(sendBtn, &QPushButton::clicked, this, [this]() {
        QString text = m_chatInput->text().trimmed();
        if (text.isEmpty()) return;
        m_chatInput->clear();
        appendAiMessage("我", text);
        emit aiMessageSent(text);
    });
    connect(m_chatInput, &QLineEdit::returnPressed, sendBtn, &QPushButton::click);

    // ---- 快捷按钮标签页 ----
    auto *quickWidget = new QWidget(this);
    auto *quickLayout = new QVBoxLayout(quickWidget);
    quickLayout->setContentsMargins(8, 8, 8, 8);
    quickLayout->setSpacing(8);

    // 录制组
    auto *recGroup = new QGroupBox("录制", quickWidget);
    auto *recLayout = new QHBoxLayout(recGroup);
    m_recordBtn = new QPushButton("● 开始录制", recGroup);
    m_stopRecBtn = new QPushButton("⏹ 停止录制", recGroup);
    recLayout->addWidget(m_recordBtn);
    recLayout->addWidget(m_stopRecBtn);
    quickLayout->addWidget(recGroup);

    // 回放组
    auto *playGroup = new QGroupBox("回放", quickWidget);
    auto *playLayout = new QHBoxLayout(playGroup);
    m_playBtn = new QPushButton("▶ 播放", playGroup);
    m_pauseBtn = new QPushButton("⏸ 暂停", playGroup);
    m_stopBtn = new QPushButton("⏹ 停止", playGroup);
    playLayout->addWidget(m_playBtn);
    playLayout->addWidget(m_pauseBtn);
    playLayout->addWidget(m_stopBtn);
    quickLayout->addWidget(playGroup);

    // Trace 组
    auto *traceGroup = new QGroupBox("Trace", quickWidget);
    auto *traceLayout = new QVBoxLayout(traceGroup);
    m_clearTraceBtn = new QPushButton("清空 Trace", traceGroup);
    m_autoScrollBtn = new QPushButton("☑ 自动滚动", traceGroup);
    m_autoScrollBtn->setCheckable(true);
    m_autoScrollBtn->setChecked(true);
    traceLayout->addWidget(m_clearTraceBtn);
    traceLayout->addWidget(m_autoScrollBtn);
    quickLayout->addWidget(traceGroup);

    // 设备组
    auto *devGroup = new QGroupBox("设备", quickWidget);
    auto *devLayout = new QHBoxLayout(devGroup);
    m_connectBtn = new QPushButton("🔗 连接", devGroup);
    m_disconnectBtn = new QPushButton("✂ 断开", devGroup);
    devLayout->addWidget(m_connectBtn);
    devLayout->addWidget(m_disconnectBtn);
    quickLayout->addWidget(devGroup);

    quickLayout->addStretch();
    addTab(quickWidget, "快捷按钮");

    // 信号连接
    connect(m_recordBtn, &QPushButton::clicked, this, &RightPanel::recordRequested);
    connect(m_stopRecBtn, &QPushButton::clicked, this, &RightPanel::stopRecordRequested);
    connect(m_playBtn, &QPushButton::clicked, this, &RightPanel::playRequested);
    connect(m_pauseBtn, &QPushButton::clicked, this, &RightPanel::pauseRequested);
    connect(m_stopBtn, &QPushButton::clicked, this, &RightPanel::stopRequested);
    connect(m_clearTraceBtn, &QPushButton::clicked, this, &RightPanel::clearTraceRequested);
    connect(m_autoScrollBtn, &QPushButton::toggled, this, [this](bool on) {
        m_autoScrollBtn->setText(on ? "☑ 自动滚动" : "☐ 自动滚动");
        emit autoScrollToggled(on);
    });
    connect(m_connectBtn, &QPushButton::clicked, this, &RightPanel::connectRequested);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &RightPanel::disconnectRequested);

    setMinimumWidth(200);
    setMaximumWidth(400);
}

void RightPanel::appendAiMessage(const QString &role, const QString &text)
{
    m_chatMessages->appendPlainText(QString("[%1] %2").arg(role, text));
}
