#include "welcomepage.h"

#include "core/sessionmanager.h"
#include "thememanager.h"
#include "utils/svg_icon.h"

#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QVariantMap>
#include <QFileInfo>
#include <QPainter>
#include <QPaintEvent>

namespace {

constexpr char kVersion[] = "1.0.0";

QString tipCardQss(const QString &border, const QString &bg)
{
    return QStringLiteral(
               "QFrame#WelcomeTip {"
               "  background-color: %1;"
               "  border: 1px solid %2;"
               "  border-radius: 6px;"
               "  padding: 12px;"
               "}")
        .arg(bg, border);
}

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
    scroll->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));

    auto *content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("WelcomeContent"));
    content->setAttribute(Qt::WA_TranslucentBackground);
    content->setAutoFillBackground(false);
    auto *lay = new QVBoxLayout(content);
    lay->setContentsMargins(48, 36, 48, 48);
    lay->setSpacing(28);
    lay->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    // ---- Hero ----
    auto *hero = new QWidget(content);
    auto *heroLay = new QVBoxLayout(hero);
    heroLay->setContentsMargins(0, 0, 0, 0);
    heroLay->setSpacing(6);

    m_heroTitle = new QLabel(QStringLiteral("openbus"), hero);
    QFont titleFont = m_heroTitle->font();
    titleFont.setPointSize(28);
    titleFont.setBold(true);
    m_heroTitle->setFont(titleFont);

    m_heroSub = new QLabel(
        QStringLiteral("CAN / CAN FD analysis — get started in a few clicks"), hero);
    m_heroSub->setWordWrap(true);

    m_versionLabel = new QLabel(QStringLiteral("Version %1").arg(QLatin1String(kVersion)), hero);

    heroLay->addWidget(m_heroTitle);
    heroLay->addWidget(m_heroSub);
    heroLay->addWidget(m_versionLabel);
    lay->addWidget(hero);

    // ---- Three columns: Start | Recent | Help ----
    auto *cols = new QWidget(content);
    auto *colsLay = new QHBoxLayout(cols);
    colsLay->setContentsMargins(0, 0, 0, 0);
    colsLay->setSpacing(40);
    colsLay->setAlignment(Qt::AlignTop);

    // Start
    auto *startBody = new QWidget;
    auto *startLay = new QVBoxLayout(startBody);
    startLay->setContentsMargins(0, 0, 0, 0);
    startLay->setSpacing(4);
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
    startLay->addStretch();
    colsLay->addWidget(makeSection(QStringLiteral("Start"), startBody), 1);

    // Recent
    auto *recentBody = new QWidget;
    m_recentLayout = new QVBoxLayout(recentBody);
    m_recentLayout->setContentsMargins(0, 0, 0, 0);
    m_recentLayout->setSpacing(4);
    colsLay->addWidget(makeSection(QStringLiteral("Recent"), recentBody), 1);

    // Help
    auto *helpBody = new QWidget;
    auto *helpLay = new QVBoxLayout(helpBody);
    helpLay->setContentsMargins(0, 0, 0, 0);
    helpLay->setSpacing(4);
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
    addHelp(QStringLiteral("About openbus"),
            QStringLiteral("Version and credits"),
            &WelcomePage::openAboutRequested);
    helpLay->addStretch();
    colsLay->addWidget(makeSection(QStringLiteral("Help"), helpBody), 1);

    lay->addWidget(cols);

    // ---- Walkthrough tip cards ----
    auto *tipsTitle = new QLabel(QStringLiteral("Walkthrough"), content);
    QFont tipFont = tipsTitle->font();
    tipFont.setPointSize(14);
    tipFont.setBold(true);
    tipsTitle->setFont(tipFont);
    lay->addWidget(tipsTitle);

    auto *tips = new QWidget(content);
    auto *tipsGrid = new QGridLayout(tips);
    tipsGrid->setContentsMargins(0, 0, 0, 0);
    tipsGrid->setHorizontalSpacing(16);
    tipsGrid->setVerticalSpacing(16);

    struct Tip {
        QString title;
        QString body;
    };
    const Tip tipList[] = {
        {QStringLiteral("1. Connect hardware"),
         QStringLiteral("Open Device Connection, scan for BUSMUST / ZLG / PEAK / "
                        "Candle / SLCAN, then Connect.")},
        {QStringLiteral("2. Start the flow"),
         QStringLiteral("Open CAN Flow and press Start so Trace and Graphic receive "
                        "live frames.")},
        {QStringLiteral("3. Trace & Graphic"),
         QStringLiteral("Use Trace for frame lists (overwrite mode, filters). Use "
                        "Graphic for signal waveforms linked to the list.")},
        {QStringLiteral("4. Load a DBC"),
         QStringLiteral("Import a DBC in the Database sidebar to decode names and "
                        "plot signals by definition.")},
    };
    for (int i = 0; i < 4; ++i) {
        auto *card = new QFrame(tips);
        card->setObjectName(QStringLiteral("WelcomeTip"));
        auto *cardLay = new QVBoxLayout(card);
        cardLay->setSpacing(6);
        auto *t = new QLabel(tipList[i].title, card);
        QFont tf = t->font();
        tf.setBold(true);
        t->setFont(tf);
        auto *b = new QLabel(tipList[i].body, card);
        b->setWordWrap(true);
        b->setObjectName(QStringLiteral("WelcomeTipBody"));
        cardLay->addWidget(t);
        cardLay->addWidget(b);
        tipsGrid->addWidget(card, i / 2, i % 2);
    }
    lay->addWidget(tips);
    lay->addStretch();

    scroll->setWidget(content);
    root->addWidget(scroll);
}

QFrame *WelcomePage::makeSection(const QString &title, QWidget *body)
{
    auto *frame = new QFrame;
    auto *lay = new QVBoxLayout(frame);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(10);
    auto *lab = new QLabel(title, frame);
    QFont f = lab->font();
    f.setPointSize(14);
    f.setBold(true);
    lab->setFont(f);
    lay->addWidget(lab);
    lay->addWidget(body);
    return frame;
}

QPushButton *WelcomePage::makeLinkButton(const QString &text, const QString &tip)
{
    auto *btn = new QPushButton(text);
    btn->setFlat(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(QStringLiteral(
        "QPushButton { text-align: left; padding: 4px 2px; border: none; }"
        "QPushButton:hover { text-decoration: underline; }"));
    if (!tip.isEmpty())
        btn->setToolTip(tip);
    return btn;
}

void WelcomePage::refreshRecent()
{
    if (!m_recentLayout)
        return;

    // Wipe and rebuild (empty label + path buttons + clear + stretch)
    while (QLayoutItem *it = m_recentLayout->takeAt(0)) {
        if (QWidget *w = it->widget())
            w->deleteLater();
        delete it;
    }

    const QVariantList items = SessionManager::instance()->recentItems();
    m_recentEmpty = new QLabel(QStringLiteral("No recent projects yet."));
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
    const Theme &t = ThemeManager::instance()->currentTheme();
    setStyleSheet(QStringLiteral(
                      "#WelcomePage, #WelcomeContent, #WelcomeScroll, #WelcomeViewport {"
                      "  background: transparent;"
                      "}"
                      "QLabel { color: %1; }"
                      "QLabel#WelcomeTipBody { color: %2; }"
                      "QPushButton { color: %3; background: transparent; }"
                      "QPushButton:hover { color: %4; }")
                      .arg(t.text, t.textDim, t.accent, t.accentHover));

    const QList<QFrame *> tips = findChildren<QFrame *>(QStringLiteral("WelcomeTip"));
    const QString cardQss = tipCardQss(t.border, t.panelBg);
    for (QFrame *f : tips)
        f->setStyleSheet(cardQss);

    if (m_versionLabel)
        m_versionLabel->setStyleSheet(QStringLiteral("color: %1;").arg(t.textDim));
    if (m_heroSub)
        m_heroSub->setStyleSheet(QStringLiteral("color: %1;").arg(t.textDim));
    rebuildWatermark();
}

void WelcomePage::rebuildWatermark()
{
    // Small, bold mark — VS Code empty-editor scale (~200–260px render)
    m_watermark = renderSvgPixmap(QStringLiteral(":/icons/spider-watermark.svg"),
                                  QStringLiteral("#5A6A78"), 256);
    update();
}

void WelcomePage::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const Theme &t = ThemeManager::instance()->currentTheme();
    p.fillRect(rect(), QColor(t.contentBg));

    if (m_watermark.isNull() || width() < 200 || height() < 200)
        return;

    // Centered, compact (VS Code–style empty editor watermark)
    const int side = qBound(160, qRound(qMin(width(), height()) * 0.22), 260);
    const int x = (width() - side) / 2;
    const int y = (height() - side) / 2;
    p.setOpacity(0.10);
    p.drawPixmap(QRect(x, y, side, side), m_watermark);
}
