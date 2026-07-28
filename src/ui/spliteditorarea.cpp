#include "spliteditorarea.h"
#include <QMenu>
#include <QAction>
#include <QTabBar>
#include <QVBoxLayout>

SplitEditorArea::SplitEditorArea(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_rootSplitter = new QSplitter(Qt::Horizontal, this);
    layout->addWidget(m_rootSplitter);

    m_firstTabs = createTabWidget();
    m_rootSplitter->addWidget(m_firstTabs);
}

QTabWidget *SplitEditorArea::createTabWidget()
{
    auto *tabs = new QTabWidget(this);
    tabs->setTabsClosable(false);
    tabs->setMovable(true);
    tabs->setDocumentMode(true);

    // 右键标签栏 → 上下文菜单
    tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabs->tabBar(), &QWidget::customContextMenuRequested,
            this, [this, tabs](const QPoint &pos) {
        int idx = tabs->tabBar()->tabAt(pos);
        if (idx >= 0)
            onTabBarContextMenu(idx, pos);
    });

    // 当前页变化时转发信号
    connect(tabs, &QTabWidget::currentChanged, this, [this](int idx) {
        emit currentChanged(idx);
    });

    return tabs;
}

int SplitEditorArea::addTab(QWidget *widget, const QString &label)
{
    // 添加到当前活跃的 TabWidget，如果没有则添加到第一个
    QTabWidget *target = activeTabWidget();
    if (!target)
        target = m_firstTabs;
    return target->addTab(widget, label);
}

QWidget *SplitEditorArea::currentWidget() const
{
    auto *tabs = activeTabWidget();
    return tabs ? tabs->currentWidget() : nullptr;
}

QTabWidget *SplitEditorArea::activeTabWidget() const
{
    // 找到有焦点的或最后激活的 QTabWidget
    for (auto *w : m_rootSplitter->findChildren<QTabWidget *>()) {
        if (w->hasFocus())
            return w;
    }
    // 默认返回第一个有标签页的
    auto all = allTabWidgets();
    for (auto *tw : all) {
        if (tw->count() > 0)
            return tw;
    }
    return m_firstTabs;
}

QList<QTabWidget *> SplitEditorArea::allTabWidgets() const
{
    return m_rootSplitter->findChildren<QTabWidget *>();
}

void SplitEditorArea::onTabBarContextMenu(int index, const QPoint &pos)
{
    // 找到发出请求的 QTabWidget
    auto *bar = qobject_cast<QTabBar *>(sender());
    if (!bar)
        return;
    auto *tabs = qobject_cast<QTabWidget *>(bar->parentWidget());
    if (!tabs)
        return;

    auto *menu = new QMenu(this);

    auto *splitRight = menu->addAction("向右拆分");
    auto *splitDown = menu->addAction("向下拆分");

    menu->addSeparator();
    auto *closeSplit = menu->addAction("关闭此拆分组");
    closeSplit->setEnabled(tabs->count() > 0);

    auto *chosen = menu->exec(tabs->tabBar()->mapToGlobal(pos));
    if (chosen == splitRight) {
        splitTab(tabs, index, Qt::Horizontal);
    } else if (chosen == splitDown) {
        splitTab(tabs, index, Qt::Vertical);
    } else if (chosen == closeSplit) {
        // 移走所有标签页到第一个 TabWidget
        while (tabs->count() > 0) {
            QWidget *w = tabs->widget(0);
            QString text = tabs->tabText(0);
            tabs->removeTab(0);
            if (tabs != m_firstTabs)
                m_firstTabs->addTab(w, text);
            else
                delete w; // 不能删除唯一的 TabWidget 里的内容
        }
        if (tabs != m_firstTabs) {
            tabs->deleteLater();
            removeEmptySplits();
        }
    }
    menu->deleteLater();
}

void SplitEditorArea::splitTab(QTabWidget *source, int index, Qt::Orientation orient)
{
    // 取出当前标签页的 widget
    QWidget *widget = source->widget(index);
    QString label = source->tabText(index);
    source->removeTab(index);

    // 创建新的 TabWidget 并把 widget 放进去
    auto *newTabs = createTabWidget();
    newTabs->addTab(widget, label);

    // 找到 source 所在的 QSplitter
    QSplitter *split = parentSplitter(source);
    if (!split) {
        // source 不在任何 splitter 里（不应该发生）
        source->addTab(widget, label);
        return;
    }

    if (split->orientation() == orient) {
        // 方向一致，直接追加
        int idx = split->indexOf(source);
        split->insertWidget(idx + 1, newTabs);
    } else {
        // 方向不一致，需要创建新的嵌套 splitter
        int idx = split->indexOf(source);
        auto *newSplit = new QSplitter(orient, this);
        split->replaceWidget(idx, newSplit);
        newSplit->addWidget(source);
        newSplit->addWidget(newTabs);
        newSplit->setSizes({500, 500});
    }
}

void SplitEditorArea::removeEmptySplits()
{
    // 递归清理：如果 QSplitter 只有一个子项且不是 QTabWidget，
    // 则用该子项替换 QSplitter 本身
    auto splitters = m_rootSplitter->findChildren<QSplitter *>();
    for (auto *split : splitters) {
        if (split == m_rootSplitter)
            continue;
        if (split->count() == 1) {
            QWidget *only = split->widget(0);
            QSplitter *parent = parentSplitter(split);
            if (parent) {
                int idx = parent->indexOf(split);
                parent->replaceWidget(idx, only);
                split->deleteLater();
            }
        }
    }
}

QSplitter *SplitEditorArea::parentSplitter(QWidget *w) const
{
    auto *p = w->parentWidget();
    while (p) {
        if (auto *s = qobject_cast<QSplitter *>(p))
            return s;
        p = p->parentWidget();
    }
    return nullptr;
}
