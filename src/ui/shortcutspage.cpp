#include "shortcutspage.h"

#include <QVBoxLayout>
#include <QTextBrowser>

ShortcutsPage::ShortcutsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *browser = new QTextBrowser(this);
    browser->setPlainText(shortcutsText());
    browser->setObjectName("TerminalOutput");
    layout->addWidget(browser);
}

QString ShortcutsPage::shortcutsText()
{
    // Shared with Help → Keyboard Shortcuts (MainWindow::showShortcuts)
    return QStringLiteral(
        "Play / Pause          Space\n"
        "Stop                  Ctrl+S\n"
        "Record                Ctrl+R\n"
        "Open File             Ctrl+O\n"
        "Open Project          Ctrl+Shift+O\n"
        "Clear Trace           Ctrl+L\n"
        "Toggle Side Bar       Ctrl+B\n"
        "Toggle Panel          Ctrl+J\n"
        "Split Right           Ctrl+\\\n"
        "Close Group           Ctrl+W\n");
}
