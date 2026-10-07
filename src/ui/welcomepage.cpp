#include "welcomepage.h"

#include "core/sessionmanager.h"
#include "thememanager.h"
#include "utils/svg_icon.h"

#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QVariantMap>
#include <QFileInfo>
#include <QPainter>
#include <QPaintEvent>
#include <QRadialGradient>

namespace {

constexpr char kVersion[] = "1.10.4";

} // namespace

WelcomePage::WelcomePage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("WelcomePage"));
    setWindowTitle(QStringLiteral("Welcome"));
    setupUi();
    applyTheme();
    refreshRecent();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const QString &) { applyTheme(); });
}

void WelcomePage::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("WelcomeScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setAttribute(Qt::WA_TranslucentBackground);
    scroll->viewport()->setObjectName(QStringLiteral("WelcomeViewport"));
    scroll->viewport()->setAutoFillBackground(false);
    scroll->viewport()->setAttribute(Qt::WA_TranslucentBackground);

    auto *content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("WelcomeContent"));
    content->setAttribute(Qt::WA_TranslucentBackground);
    content->setAutoFillBackground(false);
    content->setMaximumWidth(980);

    auto *lay = new QVBoxLayout(content);
    lay->setContentsMargins(56, 40, 56, 56);
    lay->setSpacing(32);
    lay->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    // ---- Hero (brand-first) ----
    auto *hero = new QWidget(content);
    auto *heroLay = new QVBoxLayout(hero);
    heroLay->setContentsMargins(0, 0, 0, 0);
    heroLay->setSpacing(8);

    auto *brandRow = new QWidget(hero);
    auto *brandLay = new QHBoxLayout(brandRow);
    brandLay->setContentsMargins(0, 0, 0, 0);
    brandLay->setSpacing(12);
    brandLay->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto *mark = new QLabel(brandRow);
    mark->setObjectName(QStringLiteral("WelcomeBrandMark"));
    mark->setFixedSize(36, 36);
    mark->setPixmap(renderSvgPixmap(QStringLiteral(":/icons/spider-logo.svg"),
                                    QStringLiteral("#5A6A78"), 36));
    mark->setProperty("role", QStringLiteral("brandMark"));

    m_heroTitle = new QLabel(QStringLiteral("OpenBus"), brandRow);
    m_heroTitle->setObjectName(QStringLiteral("WelcomeHeroTitle"));
    QFont titleFont = m_heroTitle->font();
    titleFont.setPointSize(32);
    titleFont.setBold(true);
    titleFont.setLetterSpacing(QFont::AbsoluteSpacing, -0.5);
    m_heroTitle->setFont(titleFont);

    brandLay->addWidget(mark);
    brandLay->addWidget(m_heroTitle);

    m_heroSub = new QLabel(
        QStringLiteral("CAN / CAN FD analysis — get started in a few clicks"), hero);
    m_heroSub->setObjectName(QStringLiteral("WelcomeHeroSub"));
    m_heroSub->setWordWrap(true);

    m_versionLabel = new QLabel(QStringLiteral("Version %1").arg(QLatin1String(kVersion)), hero);
    m_versionLabel->setObjectName(QStringLiteral("WelcomeVersion"));

    heroLay->addWidget(brandRow);
    heroLay->addWidget(m_heroSub);
    heroLay->addWidget(m_versionLabel);
    lay->addWidget(hero);

    // ---- Two columns: Start + Walkthrough | Recent + Help ----
    auto *cols = new QWidget(content);
    auto *colsLay = new QHBoxLayout(cols);
    colsLay->setContentsMargins(0, 0, 0, 0);
    colsLay->setSpacing(56);
    colsLay->setAlignment(Qt::AlignTop);

    // Left: Start + Walkthrough
    auto *left = new QWidget(cols);
    auto *leftLay = new QVBoxLayout(left);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(28);

    auto *startBody = new QWidget;
    auto *startLay = new QVBoxLayout(startBody);
    startLay->setContentsMargins(0, 0, 0, 0);
    startLay->setSpacing(2);
    auto addStart = [&](const QString &text, const QString &tip, auto signal) {
        auto *btn = makeLinkButton(text, tip);
        connect(btn, &QPushButton::clicked, this, signal);
        startLay->addWidget(btn);
    };
    addStart(QStringLiteral("New Project…"),
             QStringLiteral("Create an empty project"),
             &WelcomePage::newProjectRequested);
    addStart(QStringLiteral("Open Project…"),
             QStringLiteral("Open an existing .obp / project file"),
             &WelcomePage::openProjectRequested);
    addStart(QStringLiteral("Connect Device"),
             QStringLiteral("Open Device Connection"),
             &WelcomePage::openDeviceRequested);
    addStart(QStringLiteral("CAN Flow"),
             QStringLiteral("Measurement / flow graph"),
             &WelcomePage::openFlowRequested);
    addStart(QStringLiteral("Open Trace"),
             QStringLiteral("Frame list view"),
             &WelcomePage::openTraceRequested);
    addStart(QStringLiteral("Open Graphic"),
             QStringLiteral("Signal waveform view"),
             &WelcomePage::openGraphicRequested);
    addStart(QStringLiteral("DBC Explorer"),
             QStringLiteral("Focus the DBC sidebar"),
             &WelcomePage::openDbcPanelRequested);
    addStart(QStringLiteral("Extension Market"),
             QStringLiteral("Drivers and plugins"),
             &WelcomePage::openMarketRequested);
    leftLay->addWidget(makeSection(QStringLiteral("Start"), startBody));

    // Walkthrough — flat rows (VS Code Getting Started), not heavy cards
    auto *walkBody = new QWidget;
    auto *walkLay = new QVBoxLayout(walkBody);
    walkLay->setContentsMargins(0, 0, 0, 0);
    walkLay->setSpacing(14);

    struct Tip {
        QString title;
        QString body;
    };
    const Tip tipList[] = {
        {QStringLiteral("Connect hardware"),
         QStringLiteral("Open Device Connection, scan for BUSMUST / ZLG / PEAK / "
                        "Candle / SLCAN, then Connect.")},
        {QStringLiteral("Start the flow"),
         QStringLiteral("Open CAN Flow and press Start so Trace and Graphic receive "
                        "live frames.")},
        {QStringLiteral("Trace & Graphic"),
         QStringLiteral("Use Trace for frame lists (overwrite mode, filters). Use "
                        "Graphic for signal waveforms linked to the list.")},
        {QStringLiteral("Load a DBC"),
         QStringLiteral("Import a DBC in the Database sidebar to decode names and "
                        "plot signals by definition.")},
    };
    for (const Tip &tip : tipList) {
        auto *row = new QFrame(walkBody);
        row->setObjectName(QStringLiteral("WelcomeTip"));
        auto *rowLay = new QVBoxLayout(row);
        rowLay->setContentsMargins(0, 0, 0, 0);
        rowLay->setSpacing(3);
        auto *t = new QLabel(tip.title, row);
        t->setObjectName(QStringLiteral("WelcomeTipTitle"));
        QFont tf = t->font();
        tf.setBold(true);
        t->setFont(tf);
        auto *b = new QLabel(tip.body, row);
        b->setWordWrap(true);
        b->setObjectName(QStringLiteral("WelcomeTipBody"));
        rowLay->addWidget(t);
        rowLay->addWidget(b);
        walkLay->addWidget(row);
    }
    leftLay->addWidget(makeSection(QStringLiteral("Walkthrough"), walkBody));
    leftLay->addStretch();
    colsLay->addWidget(left, 3);

    // Right: Recent + Help
    auto *right = new QWidget(cols);
    auto *rightLay = new QVBoxLayout(right);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(28);

    auto *recentBody = new QWidget;
    m_recentLayout = new QVBoxLayout(recentBody);
    m_recentLayout->setContentsMargins(0, 0, 0, 0);
    m_recentLayout->setSpacing(2);
    rightLay->addWidget(makeSection(QStringLiteral("Recent"), recentBody));

    auto *helpBody = new QWidget;
    auto *helpLay = new QVBoxLayout(helpBody);
    helpLay->setContentsMargins(0, 0, 0, 0);
    helpLay->setSpacing(2);
    auto addHelp = [&](const QString &text, const QString &tip, auto signal) {
        auto *btn = makeLinkButton(text, tip);
        connect(btn, &QPushButton::clicked, this, signal);
        helpLay->addWidget(btn);
    };
    addHelp(QStringLiteral("Keyboard Shortcuts"),
            QStringLiteral("Open the shortcuts reference tab"),
            &WelcomePage::openShortcutsRequested);
    addHelp(QStringLiteral("Documentation"),
            QStringLiteral("Open online docs"),
            &WelcomePage::openDocsRequested);
    addHelp(QStringLiteral("Release Notes"),
            QStringLiteral("What is new in this build"),
            &WelcomePage::openReleaseNotesRequested);
    addHelp(QStringLiteral("About OpenBus"),
            QStringLiteral("Version and credits"),
            &WelcomePage::openAboutRequested);
    rightLay->addWidget(makeSection(QStringLiteral("Help"), helpBody));
    rightLay->addStretch();
    colsLay->addWidget(right, 2);

    lay->addWidget(cols);
    lay->addStretch();

    // Center the max-width content block in the scroll area
    auto *shell = new QWidget;
    shell->setObjectName(QStringLiteral("WelcomeShell"));
    shell->setAttribute(Qt::WA_TranslucentBackground);
    shell->setAutoFillBackground(false);
    auto *shellLay = new QHBoxLayout(shell);
    shellLay->setContentsMargins(0, 0, 0, 0);
    shellLay->addStretch();
    shellLay->addWidget(content);
    shellLay->addStretch();

    scroll->setWidget(shell);
    root->addWidget(scroll);
}

QFrame *WelcomePage::makeSection(const QString &title, QWidget *body)
{
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("WelcomeSection"));
    auto *lay = new QVBoxLayout(frame);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(12);
    auto *lab = new QLabel(title, frame);
    lab->setObjectName(QStringLiteral("WelcomeSectionTitle"));
    QFont f = lab->font();
    f.setPointSize(13);
    f.setBold(true);
    lab->setFont(f);
    lay->addWidget(lab);
    lay->addWidget(body);
    return frame;
}

QPushButton *WelcomePage::makeLinkButton(const QString &text, const QString &tip)
{
    auto *btn = new QPushButton(text);
    btn->setObjectName(QStringLiteral("WelcomeLinkBtn"));
    btn->setFlat(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFocusPolicy(Qt::StrongFocus);
    if (!tip.isEmpty())
        btn->setToolTip(tip);
    return btn;
}

void WelcomePage::refreshRecent()
{
    if (!m_recentLayout)
        return;

    while (QLayoutItem *it = m_recentLayout->takeAt(0)) {
        if (QWidget *w = it->widget())
            w->deleteLater();
        delete it;
    }

    const QVariantList items = SessionManager::instance()->recentItems();
    m_recentEmpty = new QLabel(QStringLiteral("No recent projects yet."));
    m_recentEmpty->setObjectName(QStringLiteral("WelcomeRecentEmpty"));
    m_recentEmpty->setWordWrap(true);
    m_recentEmpty->setVisible(items.isEmpty());
    m_recentLayout->addWidget(m_recentEmpty);

    int shown = 0;
    for (const QVariant &v : items) {
        if (shown >= 10)
            break;
        const QVariantMap m = v.toMap();
        const QString path = m.value(QStringLiteral("path")).toString();
        if (path.isEmpty())
            continue;
        QString name = m.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            name = QFileInfo(path).fileName();
        auto *btn = makeLinkButton(name, path);
        connect(btn, &QPushButton::clicked, this, [this, path]() {
            emit openRecentRequested(path);
        });
        m_recentLayout->addWidget(btn);
        ++shown;
    }

    auto *clearBtn = makeLinkButton(QStringLiteral("Clear Recent"),
                                    QStringLiteral("Remove all recent project entries"));
    connect(clearBtn, &QPushButton::clicked, this, &WelcomePage::clearRecentRequested);
    m_recentLayout->addWidget(clearBtn);
    m_recentLayout->addStretch();
}

void WelcomePage::applyTheme()
{
    rebuildWatermark();

    const Theme &t = ThemeManager::instance()->currentTheme();
    // Refresh brand mark tint with theme
    if (auto *mark = findChild<QLabel *>(QStringLiteral("WelcomeBrandMark"))) {
        mark->setPixmap(renderSvgPixmap(QStringLiteral(":/icons/spider-logo.svg"),
                                        t.accent, 36));
    }
}

void WelcomePage::rebuildWatermark()
{
    const Theme &t = ThemeManager::instance()->currentTheme();
    // Soft slate from theme — large render for crisp HiDPI watermark
    m_watermark = renderSvgPixmap(QStringLiteral(":/icons/spider-watermark.svg"),
                                  t.textDim, 320);
    update();
}

void WelcomePage::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const Theme &t = ThemeManager::instance()->currentTheme();
    const QColor bg(t.contentBg);
    p.fillRect(rect(), bg);

    // Soft atmospheric wash (keeps OpenBus theme, not flat white)
    {
        QRadialGradient wash(width() * 0.72, height() * 0.28, qMax(width(), height()) * 0.55);
        QColor c1(t.accent);
        c1.setAlpha(18);
        QColor c2(bg);
        c2.setAlpha(0);
        wash.setColorAt(0.0, c1);
        wash.setColorAt(1.0, c2);
        p.fillRect(rect(), wash);
    }

    if (m_watermark.isNull() || width() < 200 || height() < 200)
        return;

    // Large faint mark — bottom-right brand plane (VS Code empty-editor energy)
    const int side = qBound(220, qRound(qMin(width(), height()) * 0.42), 420);
    const int x = width() - side - qMax(24, width() / 18);
    const int y = height() - side - qMax(16, height() / 20);
    p.setOpacity(0.09);
    p.drawPixmap(QRect(x, y, side, side), m_watermark);
}
