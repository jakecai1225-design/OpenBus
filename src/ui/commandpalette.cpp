#include "ui/commandpalette.h"

#include <QApplication>
#include <QEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScreen>
#include <QShowEvent>
#include <QVBoxLayout>

#include <algorithm>

CommandPalette::CommandPalette(QWidget *parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("CommandPalette"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFocusPolicy(Qt::StrongFocus);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(6);

    m_input = new QLineEdit(this);
    m_input->setObjectName(QStringLiteral("CommandPaletteInput"));
    m_input->setPlaceholderText(
        QStringLiteral("> command · @ plugin · # setting · or search files…"));
    m_input->setClearButtonEnabled(true);
    lay->addWidget(m_input);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("CommandPaletteList"));
    m_list->setUniformItemSizes(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lay->addWidget(m_list, 1);

    m_hint = new QLabel(
        QStringLiteral("Enter to run · Esc to dismiss · ↑↓ to navigate"), this);
    m_hint->setObjectName(QStringLiteral("CommandPaletteHint"));
    lay->addWidget(m_hint);

    setFixedWidth(560);
    setMinimumHeight(320);
    setMaximumHeight(480);

    connect(m_input, &QLineEdit::textChanged, this, &CommandPalette::onQueryChanged);
    connect(m_input, &QLineEdit::returnPressed, this, &CommandPalette::onActivateCurrent);
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *) {
        onActivateCurrent();
    });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *) {
        onActivateCurrent();
    });

    m_input->installEventFilter(this);
    m_list->installEventFilter(this);
}

void CommandPalette::setItems(const QVector<Item> &items)
{
    m_all = items;
    for (Item &it : m_all) {
        if (it.filterText.isEmpty()) {
            it.filterText = (kindLabel(it.kind) + QLatin1Char(' ')
                             + it.label + QLatin1Char(' ') + it.detail).toLower();
        } else {
            it.filterText = it.filterText.toLower();
        }
    }
    rebuildList();
}

void CommandPalette::openCentered(QWidget *anchor)
{
    QWidget *host = anchor ? anchor->window() : nullptr;
    QRect screen;
    if (host && host->screen())
        screen = host->screen()->availableGeometry();
    else if (QApplication::primaryScreen())
        screen = QApplication::primaryScreen()->availableGeometry();
    else
        screen = QRect(0, 0, 1280, 800);

    const int w = width();
    const int h = qMin(440, maximumHeight());
    resize(w, h);
    int x = screen.center().x() - w / 2;
    int y = screen.top() + screen.height() / 6;
    if (host) {
        const QPoint g = host->mapToGlobal(QPoint(0, 0));
        x = g.x() + (host->width() - w) / 2;
        y = g.y() + 48;
    }
    move(x, y);
    show();
    raise();
    m_input->setFocus(Qt::PopupFocusReason);
    m_input->selectAll();
}

void CommandPalette::openBelow(const QRect &globalAnchorRect)
{
    const int w = width();
    const int h = qMin(440, maximumHeight());
    resize(w, h);
    int x = globalAnchorRect.center().x() - w / 2;
    int y = globalAnchorRect.bottom() + 4;
    if (QScreen *sc = QApplication::screenAt(globalAnchorRect.center())) {
        const QRect ag = sc->availableGeometry();
        x = qBound(ag.left() + 8, x, ag.right() - w - 8);
        if (y + h > ag.bottom() - 8)
            y = globalAnchorRect.top() - h - 4;
    }
    move(x, y);
    show();
    raise();
    m_input->setFocus(Qt::PopupFocusReason);
    m_input->selectAll();
}

bool CommandPalette::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (obj == m_input) {
            if (ke->key() == Qt::Key_Down || ke->key() == Qt::Key_Up) {
                if (m_list->count() == 0)
                    return true;
                int row = m_list->currentRow();
                if (row < 0) row = 0;
                else if (ke->key() == Qt::Key_Down)
                    row = qMin(row + 1, m_list->count() - 1);
                else
                    row = qMax(row - 1, 0);
                m_list->setCurrentRow(row);
                return true;
            }
            if (ke->key() == Qt::Key_Escape) {
                hide();
                return true;
            }
        }
    }
    return QFrame::eventFilter(obj, event);
}

void CommandPalette::showEvent(QShowEvent *event)
{
    QFrame::showEvent(event);
    rebuildList();
}

void CommandPalette::hideEvent(QHideEvent *event)
{
    QFrame::hideEvent(event);
}

void CommandPalette::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QFrame::keyPressEvent(event);
}

void CommandPalette::onQueryChanged(const QString &)
{
    rebuildList();
}

void CommandPalette::onActivateCurrent()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_filtered.size())
        return;
    const int idx = m_filtered.at(row);
    if (idx < 0 || idx >= m_all.size())
        return;
    auto run = m_all.at(idx).run;
    hide();
    if (run)
        run();
}

QString CommandPalette::kindLabel(Kind k)
{
    switch (k) {
    case Kind::Command: return QStringLiteral("command");
    case Kind::File:    return QStringLiteral("file");
    case Kind::Plugin:  return QStringLiteral("plugin");
    case Kind::Setting: return QStringLiteral("setting");
    case Kind::View:    return QStringLiteral("view");
    }
    return QStringLiteral("item");
}

int CommandPalette::fuzzyScore(const QString &query, const QString &haystack)
{
    if (query.isEmpty())
        return 1;
    if (haystack.contains(query))
        return 1000 - haystack.indexOf(query);
    // subsequence match
    int qi = 0;
    int score = 0;
    int last = -2;
    for (int hi = 0; hi < haystack.size() && qi < query.size(); ++hi) {
        if (haystack.at(hi) == query.at(qi)) {
            score += (hi == last + 1) ? 5 : 1;
            last = hi;
            ++qi;
        }
    }
    return (qi == query.size()) ? score : 0;
}

void CommandPalette::rebuildList()
{
    m_list->clear();
    m_filtered.clear();

    QString raw = m_input->text().trimmed();
    Kind force = Kind::Command;
    bool forced = false;
    if (raw.startsWith(QLatin1Char('>'))) {
        force = Kind::Command;
        forced = true;
        raw = raw.mid(1).trimmed();
    } else if (raw.startsWith(QLatin1Char('@'))) {
        force = Kind::Plugin;
        forced = true;
        raw = raw.mid(1).trimmed();
    } else if (raw.startsWith(QLatin1Char('#'))) {
        force = Kind::Setting;
        forced = true;
        raw = raw.mid(1).trimmed();
    }
    const QString q = raw.toLower();

    struct Ranked { int idx; int score; };
    QVector<Ranked> ranked;
    ranked.reserve(m_all.size());

    for (int i = 0; i < m_all.size(); ++i) {
        const Item &it = m_all.at(i);
        if (forced && it.kind != force)
            continue;
        const int sc = fuzzyScore(q, it.filterText);
        if (sc <= 0 && !q.isEmpty())
            continue;
        ranked.push_back({i, sc});
    }

    std::sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b) {
        if (a.score != b.score) return a.score > b.score;
        return a.idx < b.idx;
    });

    const int limit = qMin(80, ranked.size());
    for (int i = 0; i < limit; ++i) {
        const int idx = ranked.at(i).idx;
        m_filtered.push_back(idx);
        const Item &it = m_all.at(idx);
        auto *row = new QListWidgetItem(m_list);
        const QString prefix = QStringLiteral("[%1] ").arg(kindLabel(it.kind));
        row->setText(prefix + it.label
                     + (it.detail.isEmpty()
                            ? QString()
                            : QStringLiteral("  —  ") + it.detail));
        row->setToolTip(it.detail.isEmpty() ? it.label : it.detail);
    }

    if (m_list->count() > 0)
        m_list->setCurrentRow(0);

    m_hint->setText(QStringLiteral("%1 results · Enter run · Esc dismiss")
                        .arg(m_list->count()));
}
