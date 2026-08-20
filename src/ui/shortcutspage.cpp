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
    // 与帮助菜单"快捷键"对话框同一份文案（MainWindow::showShortcuts）
    return QStringLiteral(
        "播放 / 暂停      Space\n"
        "停止             Ctrl+S\n"
        "录制             Ctrl+R\n"
        "打开文件         Ctrl+O\n"
        "打开工程         Ctrl+Shift+O\n"
        "清空 Trace       Ctrl+L\n"
        "切换左侧栏       Ctrl+B\n"
        "切换底部栏       Ctrl+J\n"
        "向右拆分         Ctrl+\\\n"
        "关闭拆分组       Ctrl+W\n");
}
