#include "bottompanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QDateTime>
#include <QFontDatabase>
#include <QHeaderView>

BottomPanel::BottomPanel(QWidget *parent)
    : QTabWidget(parent)
{
    setObjectName("BottomPanel");

    // ---- Problems 标签页 ----
    auto *problemsWidget = new QWidget(this);
    auto *problemsLayout = new QVBoxLayout(problemsWidget);
    problemsLayout->setContentsMargins(0, 0, 0, 0);
    problemsLayout->setSpacing(0);

    m_problemsTable = new QTableWidget(0, 3, this);
    m_problemsTable->setHorizontalHeaderLabels({"严重度", "来源", "描述"});
    m_problemsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_problemsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_problemsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_problemsTable->verticalHeader()->setVisible(false);
    m_problemsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    problemsLayout->addWidget(m_problemsTable);

    m_problemCount = new QLabel("0 个问题", this);
    m_problemCount->setContentsMargins(8, 2, 8, 2);
    problemsLayout->addWidget(m_problemCount);

    addTab(problemsWidget, "Problems");

    // ---- Terminal 标签页 ----
    m_terminal = new QPlainTextEdit(this);
    m_terminal->setReadOnly(true);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);
    m_terminal->setFont(mono);
    m_terminal->appendPlainText("sin 终端 v0.1.0");
    m_terminal->appendPlainText("输入命令在 Command 标签页中执行");
    addTab(m_terminal, "Terminal");

    // ---- Output 标签页 ----
    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setFont(mono);
    addTab(m_output, "Output");

    // ---- Command 标签页 ----
    auto *cmdWidget = new QWidget(this);
    auto *cmdLayout = new QVBoxLayout(cmdWidget);
    cmdLayout->setContentsMargins(0, 0, 0, 0);
    cmdLayout->setSpacing(0);

    m_cmdOutput = new QPlainTextEdit(cmdWidget);
    m_cmdOutput->setReadOnly(true);
    m_cmdOutput->setFont(mono);
    cmdLayout->addWidget(m_cmdOutput);

    auto *inputBar = new QHBoxLayout;
    inputBar->setContentsMargins(4, 2, 4, 2);
    auto *promptLabel = new QLabel(">", cmdWidget);
    promptLabel->setStyleSheet("font-family: monospace; font-weight: bold; color: #4a90d9;");
    m_cmdInput = new QLineEdit(cmdWidget);
    m_cmdInput->setFont(mono);
    m_cmdInput->setPlaceholderText("输入命令后按 Enter 执行 (help 查看帮助)...");
    inputBar->addWidget(promptLabel);
    inputBar->addWidget(m_cmdInput);
    cmdLayout->addLayout(inputBar);

    addTab(cmdWidget, "Command");

    connect(m_cmdInput, &QLineEdit::returnPressed, this, &BottomPanel::onCommandReturnPressed);

    // 默认高度
    setMinimumHeight(120);
}

void BottomPanel::addProblem(int severity, const QString &source, const QString &message)
{
    int row = m_problemsTable->rowCount();
    m_problemsTable->insertRow(row);

    QString sevText;
    QColor sevColor;
    switch (severity) {
    case 0: sevText = "⚠ 警告"; sevColor = QColor(0xCC, 0x88, 0x00); break;
    case 1: sevText = "✕ 错误"; sevColor = QColor(0xCC, 0x00, 0x00); break;
    default: sevText = "ℹ 信息"; sevColor = QColor(0x00, 0x66, 0xCC); break;
    }

    auto *sevItem = new QTableWidgetItem(sevText);
    sevItem->setForeground(sevColor);
    m_problemsTable->setItem(row, 0, sevItem);
    m_problemsTable->setItem(row, 1, new QTableWidgetItem(source));
    m_problemsTable->setItem(row, 2, new QTableWidgetItem(message));

    m_problemCount->setText(QString("%1 个问题").arg(m_problemsTable->rowCount()));
}

void BottomPanel::appendOutput(const QString &text)
{
    QString ts = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    m_output->appendPlainText(QString("[%1] %2").arg(ts, text));
}

void BottomPanel::appendTerminal(const QString &text)
{
    m_terminal->appendPlainText(text);
}

void BottomPanel::clearProblems()
{
    m_problemsTable->setRowCount(0);
    m_problemCount->setText("0 个问题");
}

void BottomPanel::onCommandReturnPressed()
{
    QString cmd = m_cmdInput->text().trimmed();
    if (cmd.isEmpty()) return;

    m_cmdInput->clear();

    QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_cmdOutput->appendPlainText(QString("[%1] > %2").arg(ts, cmd));

    emit commandEntered(cmd);
}
