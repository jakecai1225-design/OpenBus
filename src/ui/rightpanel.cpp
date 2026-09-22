#include "rightpanel.h"
#include "core/bookmarkmanager.h"
#include "core/dbcmanager.h"
#include "ui/thememanager.h"
#include "utils/svg_icon.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QLineEdit>
#include <QToolButton>
#include <QFrame>
#include <QEvent>
#include <QKeyEvent>

#include "core/signalrelay.h"

namespace {

QString hexData(const QByteArray &data)
{
    QStringList parts;
    parts.reserve(data.size());
    for (unsigned char b : data)
        parts << QString::asprintf("%02X", b);
    return parts.join(QLatin1Char(' '));
}

} // namespace

QString RightPanel::WatchEntry::displayName() const
{
    if (signalName.isEmpty())
        return QStringLiteral("0x%1").arg(canId, 0, 16).toUpper();
    return QStringLiteral("0x%1.%2")
        .arg(canId, 0, 16)
        .arg(signalName)
        .toUpper();
}

RightPanel::RightPanel(QWidget *parent)
    : QTabWidget(parent)
{
    setObjectName(QStringLiteral("RightPanel"));
    setDocumentMode(true);

    // ---- Inspector ----
    auto *inspectorPage = new QWidget(this);
    auto *inspLay = new QVBoxLayout(inspectorPage);
    inspLay->setContentsMargins(0, 0, 0, 0);
    inspLay->setSpacing(0);

    auto *inspHeader = new QWidget(inspectorPage);
    inspHeader->setObjectName(QStringLiteral("RightAssistHeader"));
    auto *inspHeaderLay = new QHBoxLayout(inspHeader);
    inspHeaderLay->setContentsMargins(8, 4, 4, 4);
    inspHeaderLay->setSpacing(4);
    auto *inspTitle = new QLabel(QStringLiteral("INSPECTOR"), inspHeader);
    inspTitle->setObjectName(QStringLiteral("RightAssistTitle"));
    inspHeaderLay->addWidget(inspTitle, 1);
    m_addWatchBtn = new QPushButton(inspHeader);
    m_addWatchBtn->setObjectName(QStringLiteral("RightAssistAction"));
    m_addWatchBtn->setIcon(svgIcon(QStringLiteral(":/icons/plus.svg"),
                                   ThemeManager::instance()->currentTheme().text, 14));
    m_addWatchBtn->setToolTip(QStringLiteral("Add selected CAN ID to Watch"));
    m_addWatchBtn->setFlat(true);
    m_addWatchBtn->setFixedSize(22, 22);
    m_addWatchBtn->setEnabled(false);
    inspHeaderLay->addWidget(m_addWatchBtn);
    inspLay->addWidget(inspHeader);

    m_inspectorText = new QPlainTextEdit(inspectorPage);
    m_inspectorText->setObjectName(QStringLiteral("RightInspectorText"));
    m_inspectorText->setReadOnly(true);
    m_inspectorText->setFrameShape(QFrame::NoFrame);
    inspLay->addWidget(m_inspectorText, 1);

    auto *aiBar = new QHBoxLayout;
    aiBar->setContentsMargins(8, 4, 8, 8);
    m_aiAgentBtn = new QPushButton(QStringLiteral("Open AI Agent"), inspectorPage);
    m_aiAgentBtn->setObjectName(QStringLiteral("SidePanelButton"));
    m_aiAgentBtn->setToolTip(QStringLiteral("Activate the ai-agent plugin"));
    aiBar->addWidget(m_aiAgentBtn);
    aiBar->addStretch();
    inspLay->addLayout(aiBar);

    addTab(inspectorPage, QStringLiteral("Inspector"));
    renderInspectorEmpty();

    connect(m_addWatchBtn, &QPushButton::clicked, this, [this]() {
        if (m_hasInspection)
            addWatchCanId(m_lastFrame.id);
    });
    connect(m_aiAgentBtn, &QPushButton::clicked, this, &RightPanel::openAiAgentRequested);

    // ---- Bookmarks ----
    auto *bmPage = new QWidget(this);
    auto *bmLay = new QVBoxLayout(bmPage);
    bmLay->setContentsMargins(0, 0, 0, 0);
    bmLay->setSpacing(0);

    auto *bmHeader = new QWidget(bmPage);
    bmHeader->setObjectName(QStringLiteral("RightAssistHeader"));
    auto *bmHeaderLay = new QHBoxLayout(bmHeader);
    bmHeaderLay->setContentsMargins(8, 4, 4, 4);
    auto *bmTitle = new QLabel(QStringLiteral("BOOKMARKS"), bmHeader);
    bmTitle->setObjectName(QStringLiteral("RightAssistTitle"));
    bmHeaderLay->addWidget(bmTitle, 1);
    auto *bmClearBtn = new QToolButton(bmHeader);
    bmClearBtn->setObjectName(QStringLiteral("ExplorerSectionAction"));
    bmClearBtn->setAutoRaise(true);
    bmClearBtn->setIcon(svgIcon(QStringLiteral(":/icons/clear-all.svg"),
                                ThemeManager::instance()->currentTheme().text, 14));
    bmClearBtn->setToolTip(QStringLiteral("Clear all bookmarks"));
    bmClearBtn->setFixedSize(22, 22);
    bmHeaderLay->addWidget(bmClearBtn);
    bmLay->addWidget(bmHeader);

    m_bookmarkList = new QListWidget(bmPage);
    m_bookmarkList->setObjectName(QStringLiteral("RightBookmarkList"));
    m_bookmarkList->setAlternatingRowColors(false);
    m_bookmarkList->setFrameShape(QFrame::NoFrame);
    bmLay->addWidget(m_bookmarkList, 1);

    addTab(bmPage, QStringLiteral("Bookmarks"));

    connect(m_bookmarkList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) {
        if (!item)
            return;
        emit bookmarkJumped(item->data(Qt::UserRole).toInt());
    });
    connect(bmClearBtn, &QToolButton::clicked, this, [this]() {
        if (m_bookmarkMgr)
            m_bookmarkMgr->clear();
    });

    // ---- Watch ----
    auto *watchPage = new QWidget(this);
    auto *watchLay = new QVBoxLayout(watchPage);
    watchLay->setContentsMargins(0, 0, 0, 0);
    watchLay->setSpacing(0);

    auto *watchHeader = new QWidget(watchPage);
    watchHeader->setObjectName(QStringLiteral("RightAssistHeader"));
    auto *watchHeaderLay = new QHBoxLayout(watchHeader);
    watchHeaderLay->setContentsMargins(8, 4, 4, 4);
    auto *watchTitle = new QLabel(QStringLiteral("WATCH"), watchHeader);
    watchTitle->setObjectName(QStringLiteral("RightAssistTitle"));
    watchHeaderLay->addWidget(watchTitle, 1);
    watchLay->addWidget(watchHeader);

    auto *addRow = new QHBoxLayout;
    addRow->setContentsMargins(8, 4, 8, 4);
    addRow->setSpacing(4);
    m_watchIdEdit = new QLineEdit(watchPage);
    m_watchIdEdit->setPlaceholderText(QStringLiteral("CAN ID (e.g. 0x123)"));
    auto *watchAddBtn = new QToolButton(watchPage);
    watchAddBtn->setObjectName(QStringLiteral("ExplorerSectionAction"));
    watchAddBtn->setAutoRaise(true);
    watchAddBtn->setIcon(svgIcon(QStringLiteral(":/icons/plus.svg"),
                                 ThemeManager::instance()->currentTheme().text, 14));
    watchAddBtn->setToolTip(QStringLiteral("Add CAN ID to Watch"));
    watchAddBtn->setFixedSize(22, 22);
    addRow->addWidget(m_watchIdEdit, 1);
    addRow->addWidget(watchAddBtn);
    watchLay->addLayout(addRow);

    m_watchList = new QListWidget(watchPage);
    m_watchList->setObjectName(QStringLiteral("RightWatchList"));
    m_watchList->setFrameShape(QFrame::NoFrame);
    m_watchList->installEventFilter(this);
    watchLay->addWidget(m_watchList, 1);

    addTab(watchPage, QStringLiteral("Watch"));

    auto addFromEdit = [this]() {
        const QString t = m_watchIdEdit->text().trimmed();
        if (t.isEmpty())
            return;
        bool ok = false;
        quint32 id = t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                         ? t.mid(2).toUInt(&ok, 16)
                         : t.toUInt(&ok, 0);
        if (!ok)
            return;
        addWatchCanId(id);
        m_watchIdEdit->clear();
    };
    connect(watchAddBtn, &QToolButton::clicked, this, addFromEdit);
    connect(m_watchIdEdit, &QLineEdit::returnPressed, this, addFromEdit);

    auto *themeRelay = new SignalRelay(this);
    themeRelay->fire0 = [this, bmClearBtn, watchAddBtn]() {
        const QString c = ThemeManager::instance()->currentTheme().text;
        m_addWatchBtn->setIcon(svgIcon(QStringLiteral(":/icons/plus.svg"), c, 14));
        bmClearBtn->setIcon(svgIcon(QStringLiteral(":/icons/clear-all.svg"), c, 14));
        watchAddBtn->setIcon(svgIcon(QStringLiteral(":/icons/plus.svg"), c, 14));
    };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            themeRelay, SLOT(fire()));

    setMinimumWidth(220);
    setMaximumWidth(480);
}

void RightPanel::setBookmarkManager(BookmarkManager *mgr)
{
    m_bookmarkMgr = mgr;
    if (m_bookmarkMgr) {
        connect(m_bookmarkMgr, SIGNAL(bookmarkAdded(Bookmark)),
                this, SLOT(refreshBookmarks()));
        connect(m_bookmarkMgr, SIGNAL(bookmarkRemoved(int)),
                this, SLOT(refreshBookmarks()));
        connect(m_bookmarkMgr, SIGNAL(cleared()),
                this, SLOT(refreshBookmarks()));
        refreshBookmarks();
    }
}

void RightPanel::setDbcManager(DbcManager *mgr)
{
    m_dbcMgr = mgr;
}

void RightPanel::refreshBookmarks()
{
    m_bookmarkList->clear();
    if (!m_bookmarkMgr)
        return;

    for (const auto &bm : m_bookmarkMgr->bookmarks()) {
        auto *item = new QListWidgetItem(
            QStringLiteral("#%1  %2").arg(bm.frameIndex).arg(bm.note));
        item->setData(Qt::UserRole, bm.frameIndex);
        item->setToolTip(QStringLiteral("Double-click to jump"));
        if (bm.color.isValid())
            item->setForeground(bm.color);
        m_bookmarkList->addItem(item);
    }
}

void RightPanel::inspectFrame(const CanFrame &frame)
{
    m_lastFrame = frame;
    m_hasInspection = true;
    m_addWatchBtn->setEnabled(true);
    renderInspectorFrame(frame);
    setCurrentIndex(0);
}

void RightPanel::clearInspection()
{
    m_hasInspection = false;
    m_addWatchBtn->setEnabled(false);
    renderInspectorEmpty();
}

void RightPanel::renderInspectorEmpty()
{
    m_inspectorText->setPlainText(
        QStringLiteral(
            "Select a Trace row to inspect frame details.\n\n"
            "Tips:\n"
            "  • Trace: click a frame\n"
            "  • Use + to pin the CAN ID to Watch\n"
            "  • Open AI Agent for assisted analysis"));
}

void RightPanel::renderInspectorFrame(const CanFrame &frame)
{
    QStringList lines;
    lines << QStringLiteral("Time      %1 s").arg(frame.timestamp, 0, 'f', 6);
    lines << QStringLiteral("ID        0x%1%2")
                 .arg(frame.id, frame.extended ? 8 : 3, 16, QLatin1Char('0'))
                 .arg(frame.extended ? QStringLiteral(" (ext)") : QString())
                 .toUpper();
    lines << QStringLiteral("Channel   %1").arg(frame.channel);
    lines << QStringLiteral("Dir       %1")
                 .arg(frame.direction == CanFrame::Tx ? QStringLiteral("Tx")
                                                     : QStringLiteral("Rx"));
    lines << QStringLiteral("Type      %1")
                 .arg(frame.fd ? QStringLiteral("CAN FD") : QStringLiteral("CAN"));
    if (frame.fd) {
        lines << QStringLiteral("BRS       %1")
                     .arg(frame.bitrateSwitch ? QStringLiteral("yes")
                                              : QStringLiteral("no"));
        lines << QStringLiteral("ESI       %1")
                     .arg(frame.errorState ? QStringLiteral("yes")
                                           : QStringLiteral("no"));
    }
    lines << QStringLiteral("DLC       %1 (%2 bytes)")
                 .arg(frame.dlc)
                 .arg(frame.data.size());
    lines << QStringLiteral("Data      %1").arg(hexData(frame.data));

    if (m_dbcMgr) {
        const auto decoded = m_dbcMgr->decodeFrame(frame.id, frame.data);
        if (!decoded.isEmpty()) {
            lines << QString();
            lines << QStringLiteral("Signals");
            for (const auto &s : decoded) {
                QString line = QStringLiteral("  %1 = %2")
                                   .arg(s.name)
                                   .arg(s.physValue, 0, 'g', 8);
                if (!s.unit.isEmpty())
                    line += QLatin1Char(' ') + s.unit;
                if (!s.valueDesc.isEmpty())
                    line += QStringLiteral(" (%1)").arg(s.valueDesc);
                lines << line;
            }
        }
    }

    m_inspectorText->setPlainText(lines.join(QLatin1Char('\n')));
}

void RightPanel::addWatchCanId(quint32 canId)
{
    for (const auto &w : m_watches) {
        if (w.canId == canId && w.signalName.isEmpty())
            return;
    }
    WatchEntry e;
    e.canId = canId;
    e.lastValue = QStringLiteral("—");
    m_watches.append(e);
    rebuildWatchList();
    setCurrentIndex(2);
}

void RightPanel::addWatchSignal(quint32 canId, const QString &signalName)
{
    if (signalName.isEmpty()) {
        addWatchCanId(canId);
        return;
    }
    for (const auto &w : m_watches) {
        if (w.canId == canId && w.signalName == signalName)
            return;
    }
    WatchEntry e;
    e.canId = canId;
    e.signalName = signalName;
    e.lastValue = QStringLiteral("—");
    m_watches.append(e);
    rebuildWatchList();
    setCurrentIndex(2);
}

void RightPanel::rebuildWatchList()
{
    m_watchList->clear();
    for (int i = 0; i < m_watches.size(); ++i) {
        const auto &w = m_watches[i];
        auto *item = new QListWidgetItem(
            QStringLiteral("%1   %2").arg(w.displayName(), w.lastValue));
        item->setData(Qt::UserRole, i);
        item->setToolTip(QStringLiteral("Right-click or Delete key to remove"));
        m_watchList->addItem(item);
    }
}

void RightPanel::updateFromFrames(const QVector<CanFrame> &frames)
{
    if (m_watches.isEmpty() || frames.isEmpty())
        return;

    bool dirty = false;
    for (const CanFrame &f : frames) {
        for (auto &w : m_watches) {
            if (w.canId != f.id)
                continue;
            if (w.signalName.isEmpty()) {
                const QString v = hexData(f.data);
                if (w.lastValue != v) {
                    w.lastValue = v;
                    dirty = true;
                }
            } else if (m_dbcMgr) {
                double phys = 0.0;
                if (m_dbcMgr->decodeSignal(f.id, w.signalName, f.data, phys)) {
                    const QString v = QString::number(phys, 'g', 8);
                    if (w.lastValue != v) {
                        w.lastValue = v;
                        dirty = true;
                    }
                }
            }
        }
    }
    if (dirty)
        rebuildWatchList();
}

bool RightPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_watchList && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace) {
            const int row = m_watchList->currentRow();
            if (row >= 0 && row < m_watches.size()) {
                m_watches.removeAt(row);
                rebuildWatchList();
                return true;
            }
        }
    }
    return QTabWidget::eventFilter(watched, event);
}
