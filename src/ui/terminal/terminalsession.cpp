#include "ui/terminal/terminalsession.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextCursor>

namespace {

QString stripAnsi(QString s)
{
    // Minimal CSI / OSC strip so shell noise stays readable.
    static const QRegularExpression csi(QStringLiteral("\x1b\\[[0-9;?]*[ -/]*[@-~]"));
    static const QRegularExpression osc(QStringLiteral("\x1b\\][^\x07\x1b]*(?:\x07|\x1b\\\\)"));
    static const QRegularExpression other(QStringLiteral("\x1b[@-_]|[\x00-\x08\x0b\x0c\x0e-\x1f]"));
    s.remove(csi);
    s.remove(osc);
    s.remove(other);
    return s;
}

} // namespace

TerminalSession::TerminalSession(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setObjectName(QStringLiteral("TerminalOutput"));
    setUndoRedoEnabled(false);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setMaximumBlockCount(8000);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);
    setFont(mono);

    appendSystem(QStringLiteral("OpenBus terminal v2.0.0"));
    appendSystem(QStringLiteral("Type in this view and press Enter.  help | bash | powershell"));
    writePrompt();
}

TerminalSession::~TerminalSession()
{
    killShell();
}

void TerminalSession::setMode(Mode mode)
{
    if (m_mode == mode)
        return;
    if (mode == Mode::Shell) {
        startShell();
    } else {
        killShell();
        m_mode = Mode::Repl;
        appendSystem(QStringLiteral("Switched to OpenBus REPL"));
        writePrompt();
        emit titleChanged(QStringLiteral("OpenBus"));
    }
}

void TerminalSession::appendSystem(const QString &text)
{
    moveCursor(QTextCursor::End);
    if (!document()->isEmpty() && !toPlainText().endsWith(QLatin1Char('\n')))
        insertPlainText(QStringLiteral("\n"));
    insertPlainText(text);
    if (!text.endsWith(QLatin1Char('\n')))
        insertPlainText(QStringLiteral("\n"));
    m_promptPos = textCursor().position();
    ensureCursorInInput();
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void TerminalSession::clearScreen()
{
    clear();
    m_promptPos = 0;
    if (m_mode == Mode::Repl) {
        appendSystem(QStringLiteral("OpenBus terminal — cleared"));
        writePrompt();
    } else {
        appendSystem(QStringLiteral("(shell still running — output cleared)"));
    }
}

void TerminalSession::focusInput()
{
    setFocus(Qt::OtherFocusReason);
    ensureCursorInInput();
}

void TerminalSession::killShell()
{
    if (!m_shell)
        return;
    m_shell->disconnect(this);
    if (m_shell->state() != QProcess::NotRunning) {
        m_shell->kill();
        m_shell->waitForFinished(500);
    }
    delete m_shell;
    m_shell = nullptr;
    m_shellCarry.clear();
}

void TerminalSession::writePrompt()
{
    moveCursor(QTextCursor::End);
    if (!document()->isEmpty() && !toPlainText().endsWith(QLatin1Char('\n')))
        insertPlainText(QStringLiteral("\n"));
    const QString prompt = (m_mode == Mode::Repl)
        ? QStringLiteral("> ")
        : QStringLiteral("$ ");
    insertPlainText(prompt);
    m_promptPos = textCursor().position();
    ensureCursorInInput();
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void TerminalSession::ensureCursorInInput()
{
    QTextCursor c = textCursor();
    if (c.position() < m_promptPos) {
        c.setPosition(document()->characterCount() - 1);
        setTextCursor(c);
    }
}

int TerminalSession::inputStartPos() const
{
    return m_promptPos;
}

QString TerminalSession::currentInput() const
{
    QTextCursor c(document());
    c.setPosition(m_promptPos);
    c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    return c.selectedText().replace(QChar(0x2029), QLatin1Char('\n'));
}

void TerminalSession::replaceCurrentInput(const QString &text)
{
    QTextCursor c(document());
    c.setPosition(m_promptPos);
    c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    c.insertText(text);
    m_promptPos = qMin(m_promptPos, document()->characterCount() - 1);
    ensureCursorInInput();
}

void TerminalSession::submitReplLine()
{
    const QString line = currentInput().trimmed();
    moveCursor(QTextCursor::End);
    insertPlainText(QStringLiteral("\n"));
    m_promptPos = textCursor().position();

    if (!line.isEmpty()) {
        if (m_history.isEmpty() || m_history.last() != line)
            m_history.append(line);
        m_historyIndex = -1;
        m_historyDraft.clear();

        if (line == QLatin1String("bash") || line == QLatin1String("shell")) {
            startShell();
            return;
        }
        if (line == QLatin1String("powershell") || line == QLatin1String("pwsh")) {
            killShell();
            m_mode = Mode::Shell;
            m_shell = new QProcess(this);
            connect(m_shell, &QProcess::readyReadStandardOutput, this, &TerminalSession::onShellReadyRead);
            connect(m_shell, &QProcess::readyReadStandardError, this, &TerminalSession::onShellReadyRead);
            connect(m_shell, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    this, &TerminalSession::onShellFinished);
            m_shell->setProcessChannelMode(QProcess::MergedChannels);
            m_shell->setWorkingDirectory(QDir::currentPath());
            m_shell->start(QStringLiteral("powershell.exe"),
                           {QStringLiteral("-NoLogo"), QStringLiteral("-NoExit")});
            if (!m_shell->waitForStarted(3000)) {
                appendSystem(QStringLiteral("Failed to start PowerShell"));
                killShell();
                m_mode = Mode::Repl;
                writePrompt();
                return;
            }
            emit titleChanged(QStringLiteral("powershell"));
            appendSystem(QStringLiteral("PowerShell started — type 'exit' to return to REPL"));
            return;
        }
        if (line == QLatin1String("cls") || line == QLatin1String("clear-term")) {
            clearScreen();
            return;
        }
        emit commandEntered(line);
    }
    writePrompt();
}

QString TerminalSession::resolveShellProgram(QStringList *args) const
{
    args->clear();
    const QStringList candidates = {
        QStringLiteral("C:/msys64/usr/bin/bash.exe"),
        QStringLiteral("C:/msys64/ucrt64/bin/bash.exe"),
        QStringLiteral("C:/msys64/mingw64/bin/bash.exe"),
    };
    for (const QString &p : candidates) {
        if (QFileInfo::exists(p)) {
            *args = {QStringLiteral("-l"), QStringLiteral("-i")};
            return p;
        }
    }
    const QByteArray envBash = qgetenv("SHELL");
    if (!envBash.isEmpty() && QFileInfo::exists(QString::fromLocal8Bit(envBash))) {
        *args = {QStringLiteral("-l"), QStringLiteral("-i")};
        return QString::fromLocal8Bit(envBash);
    }
    return QString();
}

void TerminalSession::startShell()
{
    killShell();
    QStringList args;
    const QString prog = resolveShellProgram(&args);
    if (prog.isEmpty()) {
        appendSystem(QStringLiteral("No bash found (install MSYS2 or use 'powershell')."));
        m_mode = Mode::Repl;
        writePrompt();
        return;
    }

    m_mode = Mode::Shell;
    m_shell = new QProcess(this);
    connect(m_shell, &QProcess::readyReadStandardOutput, this, &TerminalSession::onShellReadyRead);
    connect(m_shell, &QProcess::readyReadStandardError, this, &TerminalSession::onShellReadyRead);
    connect(m_shell, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &TerminalSession::onShellFinished);
    m_shell->setProcessChannelMode(QProcess::MergedChannels);
    m_shell->setWorkingDirectory(QDir::currentPath());

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    env.insert(QStringLiteral("MSYSTEM"), QStringLiteral("UCRT64"));
    m_shell->setProcessEnvironment(env);

    m_shell->start(prog, args);
    if (!m_shell->waitForStarted(3000)) {
        appendSystem(QStringLiteral("Failed to start: %1").arg(prog));
        killShell();
        m_mode = Mode::Repl;
        writePrompt();
        return;
    }
    emit titleChanged(QStringLiteral("bash"));
    appendSystem(QStringLiteral("bash (%1) — type 'exit' to return to REPL").arg(prog));
}

void TerminalSession::onShellReadyRead()
{
    if (!m_shell)
        return;
    m_shellCarry.append(m_shell->readAll());
    stripAndAppend(m_shellCarry);
}

void TerminalSession::stripAndAppend(QByteArray &buf)
{
    // Keep trailing incomplete UTF-8 sequence in carry.
    int keep = 0;
    const int n = buf.size();
    if (n > 0) {
        int i = n - 1;
        int cont = 0;
        while (i >= 0 && (buf.at(i) & 0xC0) == 0x80) {
            --i;
            ++cont;
        }
        if (i >= 0) {
            const unsigned char lead = static_cast<unsigned char>(buf.at(i));
            int need = 0;
            if ((lead & 0xE0) == 0xC0) need = 2;
            else if ((lead & 0xF0) == 0xE0) need = 3;
            else if ((lead & 0xF8) == 0xF0) need = 4;
            if (need && cont + 1 < need)
                keep = cont + 1;
        }
    }
    const QByteArray chunk = buf.left(buf.size() - keep);
    buf = buf.right(keep);
    if (chunk.isEmpty())
        return;

    QString text = stripAnsi(QString::fromUtf8(chunk));
    text.replace(QLatin1Char('\r'), QString());
    if (text.isEmpty())
        return;

    moveCursor(QTextCursor::End);
    insertPlainText(text);
    m_promptPos = textCursor().position();
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void TerminalSession::onShellFinished(int exitCode, QProcess::ExitStatus)
{
    appendSystem(QStringLiteral("Shell exited (%1) — back to OpenBus REPL").arg(exitCode));
    if (m_shell) {
        m_shell->deleteLater();
        m_shell = nullptr;
    }
    m_mode = Mode::Repl;
    emit titleChanged(QStringLiteral("OpenBus"));
    writePrompt();
}

void TerminalSession::keyPressEvent(QKeyEvent *e)
{
    if (m_mode == Mode::Shell && m_shell && m_shell->state() == QProcess::Running) {
        // Forward keys to shell; local echo comes from PTY-less pipe (may duplicate).
        if (e->key() == Qt::Key_C && e->modifiers() == Qt::ControlModifier) {
            m_shell->write("\x03");
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_D && e->modifiers() == Qt::ControlModifier) {
            m_shell->write("\x04");
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_L && e->modifiers() == Qt::ControlModifier) {
            clearScreen();
            e->accept();
            return;
        }
        const QString t = e->text();
        if (!t.isEmpty()) {
            m_shell->write(t.toUtf8());
            // Soft local echo for pipes without PTY
            moveCursor(QTextCursor::End);
            if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
                insertPlainText(QStringLiteral("\n"));
            else if (e->key() == Qt::Key_Backspace) {
                QTextCursor c = textCursor();
                c.deletePreviousChar();
                setTextCursor(c);
            } else {
                insertPlainText(t);
            }
            m_promptPos = textCursor().position();
            e->accept();
            return;
        }
        QPlainTextEdit::keyPressEvent(e);
        return;
    }

    // ---- REPL mode ----
    if (e->key() == Qt::Key_C && e->modifiers() == Qt::ControlModifier) {
        replaceCurrentInput(QString());
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_L && e->modifiers() == Qt::ControlModifier) {
        clearScreen();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Up) {
        if (m_history.isEmpty()) {
            e->accept();
            return;
        }
        if (m_historyIndex < 0) {
            m_historyDraft = currentInput();
            m_historyIndex = m_history.size() - 1;
        } else if (m_historyIndex > 0) {
            --m_historyIndex;
        }
        replaceCurrentInput(m_history.at(m_historyIndex));
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Down) {
        if (m_historyIndex < 0) {
            e->accept();
            return;
        }
        if (m_historyIndex + 1 >= m_history.size()) {
            m_historyIndex = -1;
            replaceCurrentInput(m_historyDraft);
        } else {
            ++m_historyIndex;
            replaceCurrentInput(m_history.at(m_historyIndex));
        }
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Home) {
        QTextCursor c = textCursor();
        c.setPosition(m_promptPos);
        setTextCursor(c);
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        submitReplLine();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Backspace) {
        if (textCursor().position() <= m_promptPos) {
            e->accept();
            return;
        }
    }
    if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Backspace
        || e->key() == Qt::Key_Delete) {
        // fall through after guard
    } else if (!e->text().isEmpty() && textCursor().position() < m_promptPos) {
        ensureCursorInInput();
    }

    // Block editing above prompt
    if (textCursor().selectionStart() < m_promptPos
        && (e->key() == Qt::Key_Backspace || e->key() == Qt::Key_Delete
            || !e->text().isEmpty())) {
        ensureCursorInInput();
        if (e->key() == Qt::Key_Backspace && textCursor().position() <= m_promptPos) {
            e->accept();
            return;
        }
    }

    QPlainTextEdit::keyPressEvent(e);
    if (textCursor().position() < m_promptPos)
        ensureCursorInInput();
}

void TerminalSession::mousePressEvent(QMouseEvent *e)
{
    QPlainTextEdit::mousePressEvent(e);
    // Allow selection anywhere; typing jumps back to input.
}

void TerminalSession::contextMenuEvent(QContextMenuEvent *e)
{
    QMenu menu(this);
    menu.addAction(QStringLiteral("Copy"), this, [this]() {
        QApplication::clipboard()->setText(textCursor().selectedText());
    });
    menu.addAction(QStringLiteral("Paste"), this, [this]() {
        ensureCursorInInput();
        insertPlainText(QApplication::clipboard()->text());
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Clear"), this, &TerminalSession::clearScreen);
    if (m_mode == Mode::Repl) {
        menu.addAction(QStringLiteral("Start bash"), this, [this]() { startShell(); });
        menu.addAction(QStringLiteral("Start PowerShell"), this, [this]() {
            replaceCurrentInput(QStringLiteral("powershell"));
            submitReplLine();
        });
    } else {
        menu.addAction(QStringLiteral("Kill shell / back to REPL"), this, [this]() {
            killShell();
            m_mode = Mode::Repl;
            emit titleChanged(QStringLiteral("OpenBus"));
            writePrompt();
        });
    }
    menu.exec(e->globalPos());
}
