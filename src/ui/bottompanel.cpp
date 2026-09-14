#include "bottompanel.h"
#include "thememanager.h"
#include "utils/svg_icon.h"

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
#include <QToolButton>
#include <QMenu>

BottomPanel::BottomPanel(QWidget *parent)
    : QTabWidget(parent)
{
    setObjectName("BottomPanel");

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);

    // ---- Terminal tab ----
    auto *termWidget = new QWidget(this);
    auto *termLayout = new QVBoxLayout(termWidget);
    termLayout->setContentsMargins(0, 0, 0, 0);
    termLayout->setSpacing(0);

    m_terminal = new QPlainTextEdit(termWidget);
    m_terminal->setObjectName("TerminalOutput");
    m_terminal->setReadOnly(true);
    m_terminal->setFont(mono);
    m_terminal->appendPlainText("openbus terminal v1.0.0");
    m_terminal->appendPlainText("Type a command and press Enter (help for commands)");
    termLayout->addWidget(m_terminal, 1);

    auto *inputBar = new QHBoxLayout;
    inputBar->setContentsMargins(0, 0, 0, 0);
    inputBar->setSpacing(0);
    auto *promptLabel = new QLabel(">  ", termWidget);
    promptLabel->setObjectName("TerminalPrompt");
    promptLabel->setContentsMargins(8, 4, 0, 4);
    m_cmdInput = new QLineEdit(termWidget);
    m_cmdInput->setFont(mono);
    m_cmdInput->setPlaceholderText("Type a command and press Enter (help)...");
    m_cmdInput->setFrame(false);
    inputBar->addWidget(promptLabel);
    inputBar->addWidget(m_cmdInput);
    termLayout->addLayout(inputBar);

    addTab(termWidget, QStringLiteral("Terminal"));

    // ---- Output tab ----
    m_output = new QPlainTextEdit(this);
    m_output->setObjectName("TerminalOutput");
    m_output->setReadOnly(true);
    m_output->setFont(mono);
    addTab(m_output, QStringLiteral("Output"));

    // ---- Problems tab ----
    auto *problemsWidget = new QWidget(this);
    auto *problemsLayout = new QVBoxLayout(problemsWidget);
    problemsLayout->setContentsMargins(0, 0, 0, 0);
    problemsLayout->setSpacing(0);

    m_problemsTable = new QTableWidget(0, 3, this);
    m_problemsTable->setHorizontalHeaderLabels({
        QStringLiteral("Severity"), QStringLiteral("Source"), QStringLiteral("Description")});
    m_problemsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_problemsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_problemsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_problemsTable->verticalHeader()->setVisible(false);
    m_problemsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    problemsLayout->addWidget(m_problemsTable);

    m_problemCount = new QLabel(QStringLiteral("0 problems"), this);
    m_problemCount->setObjectName("DimLabel");
    m_problemCount->setContentsMargins(10, 3, 10, 3);
    problemsLayout->addWidget(m_problemCount);

    addTab(problemsWidget, QStringLiteral("Problems"));

    // ---- Plugin output tab ----
    m_pluginOutput = new QPlainTextEdit(this);
    m_pluginOutput->setObjectName("TerminalOutput");
    m_pluginOutput->setReadOnly(true);
    m_pluginOutput->setFont(mono);
    addTab(m_pluginOutput, QStringLiteral("Extensions"));

    // VS Code-style corner toolbar: clear / more / close panel
    auto *corner = new QWidget(this);
    corner->setObjectName("PanelCornerBar");
    auto *cornerLay = new QHBoxLayout(corner);
    cornerLay->setContentsMargins(4, 0, 6, 0);
    cornerLay->setSpacing(0);

    auto makeCornerBtn = [corner](const QString &tip) {
        auto *b = new QToolButton(corner);
        b->setObjectName("PanelCornerBtn");
        b->setIconSize(QSize(14, 14));
        b->setAutoRaise(true);
        b->setToolTip(tip);
        return b;
    };
    m_clearBtn = makeCornerBtn(QStringLiteral("Clear"));
    m_moreBtn = makeCornerBtn(QStringLiteral("More Actions..."));
    m_closePanelBtn = makeCornerBtn(QStringLiteral("Close Panel"));
    cornerLay->addWidget(m_clearBtn);
    cornerLay->addWidget(m_moreBtn);
    cornerLay->addWidget(m_closePanelBtn);
    setCornerWidget(corner, Qt::TopRightCorner);

    connect(m_clearBtn, &QToolButton::clicked, this, [this]() {
        switch (currentIndex()) {
        case TabTerminal: m_terminal->clear(); break;
        case TabOutput: m_output->clear(); break;
        case TabProblems: clearProblems(); break;
        case TabPlugin: clearPluginOutput(); break;
        default: break;
        }
    });
    connect(m_moreBtn, &QToolButton::clicked, this, [this]() {
        QMenu menu(this);
        menu.addAction(QStringLiteral("Clear"), m_clearBtn, &QToolButton::click);
        menu.addSeparator();
        menu.addAction(QStringLiteral("Close Panel"), this, &BottomPanel::closeRequested);
        menu.exec(m_moreBtn->mapToGlobal(QPoint(0, m_moreBtn->height())));
    });
    connect(m_closePanelBtn, &QToolButton::clicked, this, &BottomPanel::closeRequested);

    refreshCornerIcons();
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            this, SLOT(refreshCornerIcons()));

    connect(m_cmdInput, &QLineEdit::returnPressed, this, &BottomPanel::onCommandReturnPressed);

    setMinimumHeight(120);
}

void BottomPanel::refreshCornerIcons()
{
    const QString c = ThemeManager::instance()->currentTheme().textDim;
    if (m_clearBtn)
        m_clearBtn->setIcon(svgIcon(":/icons/trash.svg", c, 14));
    if (m_moreBtn)
        m_moreBtn->setIcon(svgIcon(":/icons/kebab.svg", c, 14));
    if (m_closePanelBtn)
        m_closePanelBtn->setIcon(svgIcon(":/icons/close.svg", c, 14));
}

void BottomPanel::addProblem(int severity, const QString &source, const QString &message)
{
    int row = m_problemsTable->rowCount();
    m_problemsTable->insertRow(row);

    QString sevText;
    QColor sevColor;
    switch (severity) {
    case 0: sevText = "警告"; sevColor = QColor(0xCC, 0x88, 0x00); break;
    case 1: sevText = "错误"; sevColor = QColor(0xCC, 0x00, 0x00); break;
    default: sevText = "信息"; sevColor = QColor(0x00, 0x66, 0xCC); break;
    }

    auto *sevItem = new QTableWidgetItem(sevText);
    sevItem->setForeground(sevColor);
    m_problemsTable->setItem(row, 0, sevItem);
    m_problemsTable->setItem(row, 1, new QTableWidgetItem(source));
    m_problemsTable->setItem(row, 2, new QTableWidgetItem(message));

    m_problemCount->setText(QString("%1 problems").arg(m_problemsTable->rowCount()));
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

void BottomPanel::appendPluginOutput(const QString &text)
{
    m_pluginOutput->appendPlainText(text);
}

void BottomPanel::clearPluginOutput()
{
    m_pluginOutput->clear();
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
    m_terminal->appendPlainText(QString("[%1] > %2").arg(ts, cmd));

    emit commandEntered(cmd);
}
