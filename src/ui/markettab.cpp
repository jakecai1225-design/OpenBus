#include "markettab.h"
#include "core/signalrelay.h"   // DEF-08：字符串信号 → lambda 桥接
#include "flowlayout.h"         // 首页卡片网格流式换行（marketplace 网页版版式）

#include "core/driver/driverregistry.h"
#include "core/appconfig.h"
#include "core/plugin/plugininfo.h"
#include "core/plugin/pluginmanager.h"
#include "ui/thememanager.h"
#include "utils/svg_icon.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTemporaryFile>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

// FrameRow / MarketItem / 四源聚合与图标缓存在 ui/marketmodel.h 共享层
//（方案 §13.10，与侧边栏迷你市场同源同风格）

namespace {

QString formatBytes(qint64 n)
{
    if (n < 1024)
        return QStringLiteral("%1 B").arg(n);
    if (n < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(n / 1024);
    return QStringLiteral("%1.%2 MB")
        .arg(n / (1024 * 1024))
        .arg((n % (1024 * 1024)) * 10 / (1024 * 1024));
}

/// 语义化版本比较（a < b），按点分段数字比较
bool versionLessThan(const QString &a, const QString &b)
{
    const auto sa = a.split(QLatin1Char('.'));
    const auto sb = b.split(QLatin1Char('.'));
    const int n = qMax(sa.size(), sb.size());
    for (int i = 0; i < n; ++i) {
        const int va = i < sa.size() ? sa.at(i).toInt() : 0;
        const int vb = i < sb.size() ? sb.at(i).toInt() : 0;
        if (va != vb)
            return va < vb;
    }
    return false;
}

/// 清空布局（删除全部子 widget/item；deleteLater 保证挂起回调可用 QPointer 守卫）
void clearLayout(QVBoxLayout *lay)
{
    while (lay->count()) {
        QLayoutItem *child = lay->takeAt(0);
        if (child->widget())
            child->widget()->deleteLater();
        delete child;
    }
}

QLabel *makeTitleLabel(const QString &text)
{
    auto *label = new QLabel(text);
    QFont bold = label->font();
    bold.setBold(true);
    bold.setPointSize(bold.pointSize() + 2);
    label->setFont(bold);
    label->setWordWrap(true);
    return label;
}

QLabel *makeSubLabel(const QString &text)
{
    auto *label = new QLabel(text);
    QFont small = label->font();
    small.setPointSize(qMax(small.pointSize() - 1, 1));
    label->setFont(small);
    label->setStyleSheet(QStringLiteral("color: #9d9d9d;"));
    label->setWordWrap(true);
    return label;
}

QLabel *makeSectionLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setStyleSheet(
        QStringLiteral("color: #888888; font-weight: bold; padding: 6px 4px 2px 4px;"));
    return label;
}

QLabel *makeIconPlaceholder(const QString &ch, int size)
{
    // 与 sidebar 迷你市场统一的兑底：PluginUi 彩色首字母头像
    // （market icon 异步加载完成前/加载失败时作为美观占位）
    auto *label = new QLabel;
    label->setFixedSize(size, size);
    label->setAlignment(Qt::AlignCenter);
    label->setPixmap(PluginUi::pluginIconPixmap(QString(), ch, size));
    return label;
}

QTableWidget *makeDeviceTable(const QStringList &headers)
{
    auto *table = new QTableWidget(0, headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->setVisible(false);
    table->setMaximumHeight(220);
    return table;
}

QTextBrowser *makeMarkdownBrowser()
{
    auto *browser = new QTextBrowser;
    browser->setOpenExternalLinks(true);
    browser->setFrameShape(QFrame::NoFrame);
    return browser;
}

} // namespace

// ============================================================
//  市场卡片（VS Code marketplace 网页版卡片：图标 + 名称/厂商 +
//  摘要 + 免费 徽标 + 安装/更新按钮；整卡点击进详情）
// ============================================================

/// 卡片聚合数据（rebuildList 从 MarketIndex 直取，含安装动作所需字段）
struct CardData {
    MarketItem item;
    QString title;        ///< 名称
    QString vendor;       ///< 厂商 / 发布者
    QString version;
    QString updatedAt;    ///< ISO 日期（可空）
    QString summary;      ///< 一句话摘要（可空）
    QString icon;         ///< 市场图标相对路径（可空）
    qint64 size = 0;      ///< 包大小（字节）
    bool isDriver = true;
    QString package;      ///< .odp / .opk 相对路径
    QString sha256;
    QStringList searchFields;
};

/// 市场卡片 — 无 Q_OBJECT（点击回调模式，同 FrameRow；按钮子控件自带点击语义）
class MarketCard : public QFrame {
public:
    explicit MarketCard(QWidget *parent = nullptr) : QFrame(parent)
    {
        setObjectName(QStringLiteral("marketCard"));
        setFixedSize(400, 116);
        setCursor(Qt::PointingHandCursor);
        // 主题中性配色：半透明描边/悬停，深浅主题均可用（视觉规范：无黑白块）
        setStyleSheet(QStringLiteral(
            "QFrame#marketCard { background: transparent;"
            " border: 1px solid rgba(128,128,128,0.35); border-radius: 6px; }"
            "QFrame#marketCard:hover { background: rgba(86,156,214,0.10);"
            " border: 1px solid rgba(86,156,214,0.75); }"));
    }
    using ClickCb = std::function<void()>;
    void setOnClick(ClickCb cb) { m_cb = std::move(cb); }

    QLabel *iconLabel = nullptr;
    QLabel *nameLabel = nullptr;
    QLabel *vendorLabel = nullptr;
    QLabel *descLabel = nullptr;
    QLabel *metaLabel = nullptr;
    QPushButton *actionBtn = nullptr;

protected:
    void mousePressEvent(QMouseEvent *) override { if (m_cb) m_cb(); }

private:
    ClickCb m_cb;
};

namespace {

/// 分区标题（marketplace 网页版 Featured/Most Popular 量级：加粗放大）
QLabel *makeMarketSectionLabel(const QString &text)
{
    auto *label = new QLabel(text);
    QFont big = label->font();
    big.setBold(true);
    big.setPointSize(big.pointSize() + 1);
    label->setFont(big);
    label->setStyleSheet(
        QStringLiteral("padding: 14px 2px 6px 2px;"));
    return label;
}

/// 单行截断标签（卡片固定宽 → 按可用像素 elide，避免中英文混排溢出）
QLabel *makeElidedLabel(const QString &text, int widthPx, bool bold = false,
                        const QString &color = QString())
{
    auto *label = new QLabel;
    if (bold) {
        QFont f = label->font();
        f.setBold(true);
        label->setFont(f);
    }
    if (!color.isEmpty())
        label->setStyleSheet(QStringLiteral("color: %1;").arg(color));
    const QFontMetrics fm(label->font());
    label->setText(fm.elidedText(text, Qt::ElideRight, widthPx));
    return label;
}

/// 市场卡片工厂：onOpen = 整卡点击（进详情）；onInstall = 安装/更新按钮
MarketCard *makeMarketCard(const CardData &d, const std::function<void()> &onOpen,
                           const std::function<void()> &onInstall)
{
    auto *card = new MarketCard;
    card->actionBtn = nullptr;

    auto *lay = new QHBoxLayout(card);
    lay->setContentsMargins(12, 10, 12, 10);
    lay->setSpacing(10);

    // 左：图标（加载前彩色首字母头像兜底，同列表行/迷你市场）
    card->iconLabel = makeIconPlaceholder(d.title.left(1).toUpper(), 48);
    lay->addWidget(card->iconLabel);

    // 中：名称 / 厂商·版本 / 摘要 / 更新·大小·类别
    const int textWidth = 400 - 24 /*margins*/ - 48 /*icon*/ - 10 - 74 /*右侧*/ - 10;
    auto *vbox = new QVBoxLayout;
    vbox->setSpacing(1);
    card->nameLabel = makeElidedLabel(d.title, textWidth, true);
    vbox->addWidget(card->nameLabel);
    card->vendorLabel = makeElidedLabel(
        QStringLiteral("%1 · v%2").arg(d.vendor, d.version), textWidth,
        false, QStringLiteral("#9d9d9d"));
    vbox->addWidget(card->vendorLabel);
    if (!d.summary.isEmpty()) {
        card->descLabel = makeElidedLabel(d.summary, textWidth,
                                          false, QStringLiteral("#9d9d9d"));
        vbox->addWidget(card->descLabel);
    }
    QStringList meta;
    if (!d.updatedAt.isEmpty())
        meta << QStringLiteral("更新 %1").arg(d.updatedAt);
    if (d.size > 0)
        meta << formatBytes(d.size);
    meta << (d.isDriver ? QStringLiteral("驱动") : QStringLiteral("插件"));
    card->metaLabel = makeElidedLabel(meta.join(QStringLiteral(" · ")),
                                      textWidth, false,
                                      QStringLiteral("#9d9d9d"));
    vbox->addWidget(card->metaLabel);
    vbox->addStretch(1);
    lay->addLayout(vbox, 1);

    // 右：「免费」徽标 + 安装/更新/已安装（marketplace 卡片免费徽标 + 一键安装）
    auto *right = new QVBoxLayout;
    right->setSpacing(6);
    auto *badge = new QLabel(QStringLiteral("免费"));
    badge->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    badge->setStyleSheet(QStringLiteral(
        "color: #9d9d9d; border: 1px solid rgba(128,128,128,0.45);"
        " border-radius: 3px; padding: 1px 8px;"));
    right->addWidget(badge, 0, Qt::AlignRight | Qt::AlignTop);
    right->addStretch(1);
    // 安装态三形态文字由调用方决定（安装 vN / 更新 / 已安装），onInstall 空则不建按钮
    card->actionBtn = new QPushButton;
    card->actionBtn->setFixedHeight(26);
    card->actionBtn->setMinimumWidth(64);
    if (onInstall) {
        card->actionBtn->setText(QStringLiteral("安装"));
        QObject::connect(card->actionBtn, &QPushButton::clicked,
                         card, onInstall);
    } else {
        card->actionBtn->setText(QStringLiteral("已安装"));
        card->actionBtn->setEnabled(false);
    }
    right->addWidget(card->actionBtn, 0, Qt::AlignRight | Qt::AlignBottom);
    lay->addLayout(right);

    // 非按钮子控件鼠标事件穿透 → 整卡点击
    card->iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    for (QLabel *l : card->findChildren<QLabel *>())
        l->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    card->setOnClick(onOpen);
    return card;
}

/// 卡片图标异步加载（48px；磁盘缓存 + 网络取数在 MarketModel 共享层）
void loadCardIcon(QLabel *iconLabel, const QString &relPath)
{
    if (relPath.isEmpty() || !iconLabel)
        return;
    MarketModel::fetchMarketPixmap(MarketIndex::instance()->resolveUrl(relPath),
                [iconLabel](const QPixmap &pm) {
                    QPointer<QLabel> g(iconLabel);
                    if (g)
                        g->setPixmap(pm.scaled(48, 48, Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation));
                });
}

} // namespace

// ============================================================
//  构造 / UI 构建
// ============================================================

MarketTab::MarketTab(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    rebuildList();
    MarketIndex::instance()->refresh();   // 异步加载市场索引
}

void MarketTab::buildUi()
{
    m_nam = new QNetworkAccessManager(this);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // ---- 工具栏：筛选 + 排序 + 刷新 + 安装菜单（大搜索框移首页 hero，marketplace 版式） ----
    auto *bar = new QHBoxLayout;
    bar->setSpacing(6);

    m_filterAll = new QToolButton;
    m_filterAll->setText(QStringLiteral("全部"));
    m_filterAll->setCheckable(true);
    m_filterAll->setChecked(true);
    m_filterDrivers = new QToolButton;
    m_filterDrivers->setText(QStringLiteral("驱动"));
    m_filterDrivers->setCheckable(true);
    m_filterPlugins = new QToolButton;
    m_filterPlugins->setText(QStringLiteral("插件"));
    m_filterPlugins->setCheckable(true);
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    group->addButton(m_filterAll);
    group->addButton(m_filterDrivers);
    group->addButton(m_filterPlugins);
    connect(group, &QButtonGroup::idClicked,
            this, &MarketTab::onSearchChanged);
    bar->addWidget(m_filterAll);
    bar->addWidget(m_filterDrivers);
    bar->addWidget(m_filterPlugins);

    // 排序（marketplace 网页版筛选/排序控件对齐）
    m_sortCombo = new QComboBox;
    m_sortCombo->addItems({ QStringLiteral("默认排序"),
                            QStringLiteral("最近更新"),
                            QStringLiteral("名称") });
    m_sortCombo->setToolTip(QStringLiteral("首页卡片排列顺序"));
    connect(m_sortCombo, &QComboBox::currentIndexChanged,
            this, &MarketTab::onSearchChanged);
    bar->addWidget(m_sortCombo);

    bar->addStretch(1);

    // ---- 市场源（插件系统方案 §六：切换 market.json 来源，持久化 settings.json） ----
    auto *sourceBtn = new QToolButton;
    sourceBtn->setIcon(svgIcon(":/icons/database.svg",
                               ThemeManager::instance()->currentTheme().text, 14));
    sourceBtn->setText(QStringLiteral("市场源"));
    sourceBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    sourceBtn->setPopupMode(QToolButton::InstantPopup);
    sourceBtn->setToolTip(QStringLiteral(
        "market.json 索引来源（openbus_appstore 开发源 / 自定义 URL / 本地文件）"));
    auto *sourceMenu = new QMenu(sourceBtn);
    // 每次展开重建：显示当前源 + 可选项（避免陈旧状态）
    connect(sourceMenu, &QMenu::aboutToShow, this, [this, sourceMenu]() {
        sourceMenu->clear();
        const QString cur = AppConfig::instance()->getString(
            QStringLiteral("market.url"));
        auto *head = sourceMenu->addAction(
            cur.isEmpty()
                ? QStringLiteral("当前源：默认（自动定位）")
                : QStringLiteral("当前源：%1").arg(cur));
        head->setEnabled(false);
        sourceMenu->addSeparator();

        // 应用新源：持久化 + 刷新索引（旧详情失效，回到首页）
        const auto applySource = [this](const QString &url) {
            AppConfig::instance()->set(QStringLiteral("market.url"), url);
            AppConfig::instance()->save();
            MarketIndex::instance()->setMarketUrl(
                url.isEmpty() ? MarketIndex::defaultMarketUrl() : QUrl(url));
            m_current = MarketItem();
            m_marketStatus->setText(QStringLiteral("市场加载中…"));
            MarketIndex::instance()->refresh();
        };

        auto *defAct = sourceMenu->addAction(
            QStringLiteral("默认（自动定位本地 market/ 或官方源）"));
        connect(defAct, &QAction::triggered, this,
                [applySource]() { applySource(QString()); });
        auto *devAct = sourceMenu->addAction(QStringLiteral(
            "openbus 应用市场 · 开发 (127.0.0.1:5173)"));
        connect(devAct, &QAction::triggered, this, [applySource]() {
            applySource(QStringLiteral("http://127.0.0.1:5173/market/market.json"));
        });
        auto *officialAct = sourceMenu->addAction(QStringLiteral(
            "openbus 应用市场 · 官方 (sin.org.cn)"));
        connect(officialAct, &QAction::triggered, this, [applySource]() {
            applySource(QStringLiteral("http://sin.org.cn/market/market.json"));
        });
        auto *customAct = sourceMenu->addAction(QStringLiteral("自定义 URL…"));
        connect(customAct, &QAction::triggered, this, [this, applySource]() {
            const QString curUrl = MarketIndex::instance()->marketUrl().toString();
            bool ok = false;
            const QString url = QInputDialog::getText(
                this, QStringLiteral("市场源"),
                QStringLiteral("market.json 地址（http(s):// 或 file:///）:"),
                QLineEdit::Normal, curUrl, &ok);
            if (ok && !url.trimmed().isEmpty())
                applySource(url.trimmed());
        });
        auto *localAct = sourceMenu->addAction(
            QStringLiteral("选择本地 market.json…"));
        connect(localAct, &QAction::triggered, this, [this, applySource]() {
            const QString file = QFileDialog::getOpenFileName(
                this, QStringLiteral("选择 market.json"), QString(),
                QStringLiteral("市场索引 (market.json);;所有文件 (*)"));
            if (!file.isEmpty())
                applySource(QUrl::fromLocalFile(file).toString());
        });
    });
    sourceBtn->setMenu(sourceMenu);
    // 主题切换 → 重刷按钮图标颜色（DEF-08 字符串信号）
    auto *sourceBtnRelay = new SignalRelay(this);
    sourceBtnRelay->fire0 = [sourceBtn]() {
        sourceBtn->setIcon(svgIcon(":/icons/database.svg",
                                  ThemeManager::instance()->currentTheme().text, 14));
    };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            sourceBtnRelay, SLOT(fire()));
    bar->addWidget(sourceBtn);

    auto *refreshBtn = new QToolButton;
    refreshBtn->setIcon(svgIcon(":/icons/refresh.svg",
                                ThemeManager::instance()->currentTheme().text, 14));
    refreshBtn->setText(QStringLiteral("刷新"));
    refreshBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    refreshBtn->setToolTip(QStringLiteral("重新拉取市场索引与本地已装列表"));
    connect(refreshBtn, &QToolButton::clicked, this, &MarketTab::onRefreshClicked);
    // 主题切换 → 重刷按钮图标颜色（DEF-08 字符串信号）
    auto *refreshBtnRelay = new SignalRelay(this);
    refreshBtnRelay->fire0 = [refreshBtn]() {
        refreshBtn->setIcon(svgIcon(":/icons/refresh.svg",
                                    ThemeManager::instance()->currentTheme().text, 14));
    };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            refreshBtnRelay, SLOT(fire()));
    bar->addWidget(refreshBtn);

    auto *installBtn = new QToolButton;
    installBtn->setIcon(svgIcon(":/icons/kebab.svg",
                                 ThemeManager::instance()->currentTheme().text, 14));
    installBtn->setText(QStringLiteral("安装"));
    installBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    installBtn->setToolTip(QStringLiteral("从本地包文件安装（.odp 驱动 / .opk 插件）"));
    connect(installBtn, &QToolButton::clicked, this, &MarketTab::onInstallFromFile);
    // 主题切换 → 重刷按钮图标颜色（DEF-08 字符串信号）
    auto *installBtnRelay = new SignalRelay(this);
    installBtnRelay->fire0 = [installBtn]() {
        installBtn->setIcon(svgIcon(":/icons/kebab.svg",
                                  ThemeManager::instance()->currentTheme().text, 14));
    };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            installBtnRelay, SLOT(fire()));
    bar->addWidget(installBtn);

    root->addLayout(bar);

    // ---- 下载进度条（状态行移首页 hero 下方） ----
    m_progress = new QProgressBar;
    m_progress->setTextVisible(false);
    m_progress->setMaximumHeight(3);
    m_progress->setRange(0, 100);
    m_progress->setVisible(false);
    root->addWidget(m_progress);

    // ---- 首页：hero（居中大标题 + 大搜索框，marketplace 网页版）+ 卡片网格 ----
    auto *listHost = new QWidget;
    m_listLay = new QVBoxLayout(listHost);
    m_listLay->setContentsMargins(12, 4, 12, 12);
    m_listLay->setSpacing(0);
    m_listLay->addStretch(1);
    m_listArea = new QScrollArea;
    m_listArea->setWidgetResizable(true);
    m_listArea->setWidget(listHost);
    m_listArea->setFrameShape(QFrame::NoFrame);

    auto *hero = new QWidget;
    auto *heroLay = new QVBoxLayout(hero);
    heroLay->setContentsMargins(24, 28, 24, 8);
    heroLay->setSpacing(10);

    auto *heroTitle = new QLabel(QStringLiteral("openbus 扩展市场"));
    QFont heroFont = heroTitle->font();
    heroFont.setBold(true);
    heroFont.setPointSize(heroFont.pointSize() + 6);
    heroTitle->setFont(heroFont);
    heroLay->addWidget(heroTitle, 0, Qt::AlignHCenter);

    // 居中大搜索框 + 强调色搜索按钮（marketplace 首页 hero 搜索行）
    auto *searchWrap = new QWidget;
    auto *searchRow = new QHBoxLayout(searchWrap);
    searchRow->setContentsMargins(0, 0, 0, 0);
    searchRow->setSpacing(6);
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(
        QStringLiteral("搜索驱动与插件（型号 / 厂商 / 关键词）"));
    m_searchEdit->setClearButtonEnabled(true);
    // 原生清除按钮 × 不随主题（深色下不可见）→ 换主题色 SVG 图标
    applyClearButtonIcon(m_searchEdit, ThemeManager::instance()->currentTheme().text);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &MarketTab::onSearchChanged);
    QFont searchFont = m_searchEdit->font();
    searchFont.setPointSize(searchFont.pointSize() + 1);
    m_searchEdit->setFont(searchFont);
    m_searchEdit->setFixedHeight(32);
    m_searchEdit->setMinimumWidth(460);
    searchRow->addWidget(m_searchEdit);

    m_searchBtn = new QPushButton;
    m_searchBtn->setText(QStringLiteral("搜索"));
    m_searchBtn->setFixedHeight(32);
    const auto applySearchBtnStyle = [this]() {
        const Theme &t = ThemeManager::instance()->currentTheme();
        m_searchBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; color: #ffffff; border: none;"
            " border-radius: 3px; padding: 0 18px; font-weight: bold; }"
            "QPushButton:hover { background: %2; }"
            "QPushButton:pressed { background: %2; }")
            .arg(t.accent, t.accentHover));
        m_searchBtn->setIcon(svgIcon(":/icons/search.svg",
                                     QStringLiteral("#ffffff"), 14));
    };
    applySearchBtnStyle();
    const auto submitSearch = [this]() {
        onSearchChanged();
        m_searchEdit->clearFocus();   // 收起输入焦点，视线回到结果区
        m_listArea->verticalScrollBar()->setValue(0);
    };
    connect(m_searchBtn, &QPushButton::clicked, this, submitSearch);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, submitSearch);
    // 主题切换 → 重刷搜索按钮配色/图标（DEF-08 字符串信号）
    auto *searchBtnRelay = new SignalRelay(this);
    searchBtnRelay->fire0 = applySearchBtnStyle;
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            searchBtnRelay, SLOT(fire()));
    searchRow->addWidget(m_searchBtn);
    heroLay->addWidget(searchWrap, 0, Qt::AlignHCenter);

    m_marketStatus = new QLabel(QStringLiteral("市场加载中…"));
    m_marketStatus->setStyleSheet(QStringLiteral("color: #9d9d9d;"));
    heroLay->addWidget(m_marketStatus, 0, Qt::AlignHCenter);

    auto *homePage = new QWidget;
    auto *homeLay = new QVBoxLayout(homePage);
    homeLay->setContentsMargins(0, 0, 0, 0);
    homeLay->setSpacing(0);
    homeLay->addWidget(hero);
    homeLay->addWidget(m_listArea, 1);

    // ---- 详情页：「← 返回市场」+ 详情滚动区 ----
    auto *detailPage = new QWidget;
    auto *detailLay = new QVBoxLayout(detailPage);
    detailLay->setContentsMargins(0, 4, 0, 0);
    detailLay->setSpacing(6);
    auto *backBtn = new QToolButton;
    backBtn->setText(QStringLiteral("← 返回市场"));
    backBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    backBtn->setToolTip(QStringLiteral("回到市场首页（搜索 / 浏览卡片）"));
    connect(backBtn, &QToolButton::clicked, this, [this]() {
        m_stack->setCurrentIndex(0);
    });
    auto *backRow = new QHBoxLayout;
    backRow->setContentsMargins(4, 0, 0, 0);
    backRow->addWidget(backBtn);
    backRow->addStretch(1);
    detailLay->addLayout(backRow);

    auto *detailHost = new QWidget;
    m_detailLay = new QVBoxLayout(detailHost);
    m_detailLay->setContentsMargins(8, 0, 8, 0);
    m_detailLay->setSpacing(8);
    m_detailArea = new QScrollArea;
    m_detailArea->setWidgetResizable(true);
    m_detailArea->setWidget(detailHost);
    m_detailArea->setFrameShape(QFrame::NoFrame);
    detailLay->addWidget(m_detailArea, 1);

    // ---- 双页堆叠：0 = 市场首页 / 1 = 详情页 ----
    m_stack = new QStackedWidget;
    m_stack->addWidget(homePage);
    m_stack->addWidget(detailPage);
    root->addWidget(m_stack, 1);

    // ---- 数据源信号（DEF-08 字符串信号：data.dll 类跨 DLL connect）----
    connect(MarketIndex::instance(), SIGNAL(loaded(bool,QString)),
            this, SLOT(onMarketLoaded(bool,QString)));
    // 安装/禁用/卸载（含 scanAndLoad）后自动刷新已装分组
    connect(DriverRegistry::instance(), SIGNAL(driversChanged()),
            this, SLOT(refreshInstalled()));
}

// ============================================================
//  工具栏动作
// ============================================================

void MarketTab::focusSearch()
{
    m_stack->setCurrentIndex(0);   // 搜索框在首页 hero（marketplace 版式）
    m_searchEdit->setFocus(Qt::ShortcutFocusReason);
    m_searchEdit->selectAll();
}

void MarketTab::revealItem(const MarketItem &item)
{
    // 清空搜索词与筛选，保证目标行可见（setText 可能不触发 textChanged，
    // 下面统一 rebuildList）
    m_searchEdit->setText(QString());
    m_filterAll->setChecked(true);
    m_filterDrivers->setChecked(false);
    m_filterPlugins->setChecked(false);
    rebuildList();
    selectItem(item);
}

void MarketTab::installLocalFile(const QString &path)
{
    if (path.endsWith(QStringLiteral(".opk"), Qt::CaseInsensitive)) {
        const QString err = PluginManager::instance()->installPackage(path);
        if (!err.isEmpty())
            QMessageBox::warning(this, QStringLiteral("安装插件"), err);
        else
            QMessageBox::information(this, QStringLiteral("安装插件"),
                                     QStringLiteral("插件安装成功"));
        refreshInstalled();
    } else {
        installOdpFile(path, QString());   // 离线包依赖包内 CHECKSUMS 自校验
    }
}

void MarketTab::onSearchChanged()
{
    rebuildList();
}

void MarketTab::onRefreshClicked()
{
    MarketIndex::instance()->refresh();
    DriverRegistry::instance()->scanAndLoad();   // driversChanged → refreshInstalled
    refreshInstalled();
}

void MarketTab::onInstallFromFile()
{
    QMenu menu(this);
    QAction *odpAct = menu.addAction(QStringLiteral("从 .odp 安装驱动…"));
    QAction *opkAct = menu.addAction(QStringLiteral("从 .opk 安装插件…"));
    QAction *chosen = menu.exec(QCursor::pos());
    if (chosen == odpAct) {
        const QString odp = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择驱动包"), QString(),
            QStringLiteral("openbus 驱动包 (*.odp);;所有文件 (*)"));
        if (!odp.isEmpty())
            installLocalFile(odp);
    } else if (chosen == opkAct) {
        const QString opk = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择插件包"), QString(),
            QStringLiteral("openbus 插件包 (*.opk);;所有文件 (*)"));
        if (!opk.isEmpty())
            installLocalFile(opk);
    }
}

void MarketTab::onMarketLoaded(bool ok, const QString &error)
{
    auto *idx = MarketIndex::instance();
    if (!ok) {
        m_marketStatus->setText(QStringLiteral("市场加载失败: %1").arg(error));
    } else {
        m_marketStatus->setText(
            QStringLiteral("市场更新于 %1 · %2 个驱动 / %3 个插件")
                .arg(idx->updated())
                .arg(idx->drivers().size())
                .arg(idx->plugins().size()));
    }
    rebuildList();
    if (!m_current.id.isEmpty())
        showDetail(m_current);   // 市场数据到位后刷新当前详情
}

// ============================================================
//  首页卡片网格（分区：精选推荐 / 最近更新 / 搜索结果单区）
// ============================================================

void MarketTab::rebuildList()
{
    // 清空旧分区（尾部再补 stretch 保持分区顶对齐）
    clearLayout(m_listLay);
    m_listLay->addStretch(1);
    const auto addWidget = [this](QWidget *w) {
        m_listLay->insertWidget(m_listLay->count() - 1, w);
    };
    // 分区 = 标题 + FlowLayout 卡片流（等宽卡片随视口自动换行，marketplace 网格）
    const auto addSection = [this, &addWidget](const QString &title,
                                              const QVector<CardData> &entries) {
        if (entries.isEmpty())
            return;
        addWidget(makeMarketSectionLabel(title));
        auto *flowHost = new QWidget;
        auto *flow = new FlowLayout(flowHost, 0, 12, 12);
        for (const auto &d : entries) {
            // 安装态三形态（与详情页按钮组一致：安装 / 更新 / 已安装）
            const QString local = d.isDriver
                                      ? installedDriverVersion(d.item.id)
                                      : installedPluginVersion(d.item.id);
            const bool canUpdate =
                !local.isEmpty() && versionLessThan(local, d.version);
            std::function<void()> onInstall;
            if (local.isEmpty() || canUpdate) {
                const QUrl url = MarketIndex::instance()->resolveUrl(d.package);
                const QString sha = d.sha256;
                const QString id = d.item.id;
                const bool isPlugin = !d.isDriver;
                onInstall = [this, url, sha, id, isPlugin]() {
                    downloadAndInstall(url, sha, id, isPlugin);
                };
            }
            const MarketItem item = d.item;
            auto *card = makeMarketCard(
                d, [this, item]() { selectItem(item); }, onInstall);
            if (onInstall)
                card->actionBtn->setText(
                    local.isEmpty() ? QStringLiteral("安装")
                                    : QStringLiteral("更新"));
            loadCardIcon(card->iconLabel, d.icon);
            flow->addWidget(card);
        }
        addWidget(flowHost);
    };

    const QString text = m_searchEdit->text();
    const bool wantDrivers = m_filterAll->isChecked() || m_filterDrivers->isChecked();
    const bool wantPlugins = m_filterAll->isChecked() || m_filterPlugins->isChecked();

    // ---- 聚合市场条目（MarketIndex 直取：卡片需 vendor/version/updatedAt/package，
    //      MarketEntryData 不携带；搜索字段组成与 MarketModel::collect* 一致） ----
    auto *idx = MarketIndex::instance();
    QVector<CardData> drivers, plugins;
    if (wantDrivers) {
        for (const auto &drv : idx->drivers()) {
            CardData d;
            d.item = { MarketItem::MarketDriver, drv.id };
            d.title = drv.name;
            d.vendor = drv.vendor;
            d.version = drv.version;
            d.updatedAt = drv.updatedAt;
            d.summary = drv.summary;
            d.icon = drv.icon;
            d.size = drv.size;
            d.isDriver = true;
            d.package = drv.package;
            d.sha256 = drv.sha256;
            d.searchFields = { drv.name, drv.vendor, drv.summary,
                               drv.keywords, drv.id };
            for (const auto &v : drv.devices)
                d.searchFields << v.toObject()
                                  .value(QStringLiteral("model")).toString();
            if (MarketIndex::matchWords(text, d.searchFields))
                drivers.append(d);
        }
    }
    if (wantPlugins) {
        for (const auto &p : idx->plugins()) {
            CardData d;
            d.item = { MarketItem::MarketPlugin, p.id };
            d.title = p.name;
            d.vendor = p.publisher;
            d.version = p.version;
            d.updatedAt = p.updatedAt;
            d.summary = p.description;
            d.icon = p.icon;
            d.size = p.size;
            d.isDriver = false;
            d.package = p.package;
            d.sha256 = p.sha256;
            d.searchFields = { p.name, p.id, p.publisher, p.description,
                               p.keywords, p.tags.join(QLatin1Char(' ')) };
            if (MarketIndex::matchWords(text, d.searchFields))
                plugins.append(d);
        }
    }

    // 排序（ISO 日期字符串字典序即时间序，空值沉底；名称本地化感知）
    const auto byUpdated = [](const CardData &a, const CardData &b) {
        if (a.updatedAt.isEmpty())
            return false;
        if (b.updatedAt.isEmpty())
            return true;
        return a.updatedAt > b.updatedAt;
    };
    const auto byName = [](const CardData &a, const CardData &b) {
        return a.title.localeAwareCompare(b.title) < 0;
    };

    QVector<CardData> all = drivers + plugins;
    const int sortIdx = m_sortCombo->currentIndex();

    // ---- 搜索结果单区（marketplace 搜索结果页式样） ----
    if (!text.isEmpty()) {
        if (sortIdx == 1)
            std::stable_sort(all.begin(), all.end(), byUpdated);
        else if (sortIdx == 2)
            std::stable_sort(all.begin(), all.end(), byName);
        if (all.isEmpty()) {
            auto *empty = new QLabel(QStringLiteral("没有匹配的条目"));
            empty->setAlignment(Qt::AlignCenter);
            empty->setStyleSheet(
                QStringLiteral("color: #777777; padding: 24px;"));
            addWidget(empty);
            return;
        }
        addSection(QStringLiteral("与 “%1” 匹配的 %2 个结果")
                       .arg(text).arg(all.size()), all);
        return;
    }

    // ---- 浏览态分区（默认 = 精选推荐 + 最近更新；排序切换 = 全部条目单区） ----
    if (sortIdx == 1) {
        std::stable_sort(all.begin(), all.end(), byUpdated);
        addSection(QStringLiteral("全部条目 · 最近更新"), all);
    } else if (sortIdx == 2) {
        std::stable_sort(all.begin(), all.end(), byName);
        addSection(QStringLiteral("全部条目 · 按名称"), all);
    } else {
        // 精选推荐：驱动/插件交错取前 6（无运营位数据前的确定性策展）
        QVector<CardData> featured;
        int i = 0, j = 0;
        while (featured.size() < 6 && (i < drivers.size() || j < plugins.size())) {
            if (i < drivers.size())
                featured.append(drivers.at(i++));
            if (j < plugins.size())
                featured.append(plugins.at(j++));
        }
        addSection(QStringLiteral("精选推荐"), featured);

        std::stable_sort(all.begin(), all.end(), byUpdated);
        addSection(QStringLiteral("最近更新"), all);
    }
}

void MarketTab::selectItem(const MarketItem &item)
{
    m_current = item;
    showDetail(item);
}

// ============================================================
//  刷新 / 详情区控制
// ============================================================

void MarketTab::refreshInstalled()
{
    rebuildList();
    if (!m_current.id.isEmpty())
        showDetail(m_current);
}

void MarketTab::clearDetail()
{
    clearLayout(m_detailLay);
}

void MarketTab::showPlaceholder(const QString &text)
{
    m_stack->setCurrentIndex(1);   // 占位也属详情页（如「该驱动已卸载」）
    clearDetail();
    auto *label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral("color: #777777;"));
    m_detailLay->addWidget(label, 1);
}

void MarketTab::showDetail(const MarketItem &item)
{
    m_stack->setCurrentIndex(1);   // 首页卡片点击 / 联动定位 → 进详情页
    switch (item.kind) {
    case MarketItem::MarketDriver: {
        const auto d = MarketIndex::instance()->driverById(item.id);
        if (d.id.isEmpty())
            showPlaceholder(QStringLiteral("该驱动不在当前市场索引中"));
        else
            showMarketDriver(d);
        break;
    }
    case MarketItem::InstalledDriver:
        showInstalledDriver(item.id);
        break;
    case MarketItem::MarketPlugin: {
        const auto p = MarketIndex::instance()->pluginById(item.id);
        if (p.id.isEmpty())
            showPlaceholder(QStringLiteral("该插件不在当前市场索引中"));
        else
            showMarketPlugin(p);
        break;
    }
    case MarketItem::InstalledPlugin:
        showInstalledPlugin(item.id);
        break;
    }
}

// ============================================================
//  详情：市场驱动（图文 + 设备简表 + readme + 安装/更新）
// ============================================================

void MarketTab::showMarketDriver(const MarketIndex::DriverInfo &drv)
{
    clearDetail();

    // 头部：图标 + 名称/厂商/版本 + 摘要
    auto *head = new QWidget;
    auto *hlay = new QHBoxLayout(head);
    hlay->setSpacing(12);
    auto *icon = makeIconPlaceholder(QStringLiteral("D"), 48);
    hlay->addWidget(icon);
    MarketModel::fetchMarketPixmap(MarketIndex::instance()->resolveUrl(drv.icon),
                [icon](const QPixmap &pm) {
                    QPointer<QLabel> g(icon);
                    if (g)
                        g->setPixmap(pm.scaled(48, 48, Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation));
                });
    auto *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->addWidget(makeTitleLabel(drv.name));
    vbox->addWidget(makeSubLabel(
        QStringLiteral("%1 · v%2 · 更新 %3")
            .arg(drv.vendor, drv.version, drv.updatedAt)));
    if (!drv.summary.isEmpty())
        vbox->addWidget(makeSubLabel(drv.summary));
    hlay->addLayout(vbox, 1);
    m_detailLay->addWidget(head);

    // 操作按钮（未安装 / 可更新 / 已安装）
    const QString local = installedDriverVersion(drv.id);
    auto *btnRow = new QWidget;
    auto *blay = new QHBoxLayout(btnRow);
    if (local.isEmpty()) {
        auto *btn = new QPushButton(
            QStringLiteral("安装 v%1").arg(drv.version));
        connect(btn, &QPushButton::clicked, this, [this, drv]() {
            downloadAndInstall(MarketIndex::instance()->resolveUrl(drv.package),
                               drv.sha256, drv.id, false);
        });
        blay->addWidget(btn);
    } else if (versionLessThan(local, drv.version)) {
        auto *btn = new QPushButton(
            QStringLiteral("更新到 v%1（当前 v%2）").arg(drv.version, local));
        connect(btn, &QPushButton::clicked, this, [this, drv]() {
            downloadAndInstall(MarketIndex::instance()->resolveUrl(drv.package),
                               drv.sha256, drv.id, false);
        });
        blay->addWidget(btn);
    } else {
        auto *btn = new QPushButton(
            svgIcon(":/icons/check.svg",
                    ThemeManager::instance()->currentTheme().text, 14),
            QStringLiteral("已安装 v%1").arg(local));
        btn->setEnabled(false);
        blay->addWidget(btn);
    }
    blay->addStretch(1);
    m_detailLay->addWidget(btnRow);

    // 代表图
    if (!drv.image.isEmpty()) {
        auto *img = new QLabel(QStringLiteral("图片加载中…"));
        img->setAlignment(Qt::AlignCenter);
        img->setMinimumHeight(230);
        img->setStyleSheet(QStringLiteral(
            "background: %1; border: 1px solid %2; border-radius: 4px; color: %3;")
            .arg(ThemeManager::instance()->currentTheme().panelBg,
                 ThemeManager::instance()->currentTheme().border,
                 ThemeManager::instance()->currentTheme().textDim));
        m_detailLay->addWidget(img);
        MarketModel::fetchMarketPixmap(MarketIndex::instance()->resolveUrl(drv.image),
                    [img](const QPixmap &pm) {
                        QPointer<QLabel> g(img);
                        if (g)
                            g->setPixmap(pm.scaled(480, 270, Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation));
                    });
    }

    // 设备简表
    if (!drv.devices.isEmpty()) {
        m_detailLay->addWidget(makeSectionLabel(QStringLiteral("支持设备")));
        auto *table = makeDeviceTable(
            { QStringLiteral("型号"), QStringLiteral("通道"),
              QStringLiteral("CAN FD"), QStringLiteral("最高波特率"),
              QStringLiteral("时间戳") });
        for (const auto &v : drv.devices) {
            const auto obj = v.toObject();
            const int row = table->rowCount();
            table->insertRow(row);
            const auto cell = [](const QString &t) {
                return new QTableWidgetItem(t);
            };
            auto *modelCell = cell(obj.value(QStringLiteral("model")).toString());
            modelCell->setToolTip(
                obj.value(QStringLiteral("summary")).toString());
            table->setItem(row, 0, modelCell);
            table->setItem(row, 1, cell(QString::number(
                obj.value(QStringLiteral("channels")).toInt())));
            table->setItem(row, 2, cell(
                obj.value(QStringLiteral("canFd")).toBool()
                    ? QStringLiteral("支持") : QStringLiteral("—")));
            table->setItem(row, 3, cell(
                obj.value(QStringLiteral("maxBaud")).toString()));
            table->setItem(row, 4, cell(
                obj.value(QStringLiteral("timestamp")).toString()));
        }
        m_detailLay->addWidget(table);
    }

    m_detailLay->addWidget(makeSubLabel(
        QStringLiteral("包大小 %1 · 授权: %2 · 要求应用 ≥ v%3")
            .arg(formatBytes(drv.size), drv.license, drv.minAppVersion)));

    auto *browser = makeMarkdownBrowser();
    browser->setMarkdown(drv.readme);
    m_detailLay->addWidget(browser, 1);
}

// ============================================================
//  详情：已装驱动（状态 + 设备型号表 + 禁用/卸载）
// ============================================================

void MarketTab::showInstalledDriver(const QString &driverId)
{
    const auto entries = DriverRegistry::instance()->drivers();
    const auto it = std::find_if(entries.cbegin(), entries.cend(),
                                 [&driverId](const auto &e) {
                                     return e.driverId == driverId;
                                 });
    if (it == entries.cend()) {
        showPlaceholder(QStringLiteral("该驱动已卸载"));
        return;
    }
    const auto &e = *it;

    clearDetail();

    auto *head = new QWidget;
    auto *hlay = new QHBoxLayout(head);
    hlay->setSpacing(12);
    auto *icon = makeIconPlaceholder(QStringLiteral("D"), 48);
    hlay->addWidget(icon);
    const auto marketDrv = MarketIndex::instance()->driverById(driverId);
    if (!marketDrv.icon.isEmpty()) {
        MarketModel::fetchMarketPixmap(MarketIndex::instance()->resolveUrl(marketDrv.icon),
                    [icon](const QPixmap &pm) {
                        QPointer<QLabel> g(icon);
                        if (g)
                            g->setPixmap(pm.scaled(48, 48, Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation));
                    });
    }
    auto *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->addWidget(makeTitleLabel(e.displayName));
    vbox->addWidget(makeSubLabel(
        QStringLiteral("%1 · v%2").arg(e.driverId,
            e.version.isEmpty() ? QStringLiteral("-") : e.version)));
    hlay->addLayout(vbox, 1);
    m_detailLay->addWidget(head);

    auto *status = new QLabel(
        e.enabled
            ? (e.available
                   ? QStringLiteral("可用")
                   : e.disabledReason.isEmpty()
                         ? QStringLiteral("不可用") : e.disabledReason)
            : QStringLiteral("已禁用"));
    status->setStyleSheet(
        e.enabled && e.available
            ? QStringLiteral("color: #4ec9b0;")
            : QStringLiteral("color: #d7ba7d;"));
    m_detailLay->addWidget(status);

    m_detailLay->addWidget(makeSubLabel(
        e.builtin ? QStringLiteral("来源: 内置（随主程序静态编译）")
                  : QStringLiteral("来源: 外置驱动包（%1）").arg(e.installDir)));

    if (!e.devices.isEmpty()) {
        m_detailLay->addWidget(makeSectionLabel(QStringLiteral("支持的设备型号")));
        auto *table = makeDeviceTable(
            { QStringLiteral("型号名称"), QStringLiteral("设备类型"),
              QStringLiteral("通道数"), QStringLiteral("CAN FD") });
        for (const auto &d : e.devices) {
            const auto obj = d.toObject();
            const int row = table->rowCount();
            table->insertRow(row);
            const auto cell = [](const QString &t) {
                return new QTableWidgetItem(t);
            };
            table->setItem(row, 0, cell(
                obj.value(QStringLiteral("name")).toString()));
            table->setItem(row, 1, cell(QString::number(
                obj.value(QStringLiteral("type")).toInt())));
            table->setItem(row, 2, cell(QString::number(
                obj.value(QStringLiteral("channels")).toInt())));
            table->setItem(row, 3, cell(
                obj.value(QStringLiteral("canFd")).toBool()
                    ? QStringLiteral("支持") : QStringLiteral("—")));
        }
        m_detailLay->addWidget(table);
    }

    auto *btnRow = new QWidget;
    auto *blay = new QHBoxLayout(btnRow);
    blay->addStretch(1);
    auto *toggleBtn = new QPushButton(
        e.enabled ? QStringLiteral("禁用此驱动") : QStringLiteral("启用此驱动"));
    toggleBtn->setToolTip(QStringLiteral(
        "禁用后设备树隐藏且不参与枚举/创建，重启后不加载（方案 §7.4）"));
    connect(toggleBtn, &QPushButton::clicked, this, [this, driverId]() {
        toggleDriverEnabled(driverId);
    });
    blay->addWidget(toggleBtn);
    auto *uninstallBtn = new QPushButton(QStringLiteral("卸载此驱动"));
    uninstallBtn->setEnabled(!e.builtin);
    uninstallBtn->setToolTip(QStringLiteral(
        "仅外置驱动可卸载；已加载的 DLL 在重启程序前仍驻留内存（方案 §7.4）"));
    connect(uninstallBtn, &QPushButton::clicked, this, [this, driverId]() {
        uninstallDriver(driverId);
    });
    blay->addWidget(uninstallBtn);
    m_detailLay->addWidget(btnRow);
    m_detailLay->addStretch(1);
}

// ============================================================
//  详情：市场插件（说明 + 安装/更新）
// ============================================================

void MarketTab::showMarketPlugin(const MarketIndex::PluginInfo &plug)
{
    clearDetail();

    auto *head = new QWidget;
    auto *hlay = new QHBoxLayout(head);
    hlay->setSpacing(12);
    auto *icon = makeIconPlaceholder(QStringLiteral("P"), 48);
    hlay->addWidget(icon);
    MarketModel::fetchMarketPixmap(MarketIndex::instance()->resolveUrl(plug.icon),
                [icon](const QPixmap &pm) {
                    QPointer<QLabel> g(icon);
                    if (g)
                        g->setPixmap(pm.scaled(48, 48, Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation));
                });
    auto *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->addWidget(makeTitleLabel(plug.name));
    vbox->addWidget(makeSubLabel(
        QStringLiteral("%1 · v%2 · 更新 %3")
            .arg(plug.publisher, plug.version, plug.updatedAt)));
    if (!plug.tags.isEmpty())
        vbox->addWidget(makeSubLabel(
            QStringLiteral("标签: %1")
                .arg(plug.tags.join(QStringLiteral("  ·  ")))));
    hlay->addLayout(vbox, 1);
    m_detailLay->addWidget(head);

    const QString local = installedPluginVersion(plug.id);
    auto *btnRow = new QWidget;
    auto *blay = new QHBoxLayout(btnRow);
    if (local.isEmpty()) {
        auto *btn = new QPushButton(
            QStringLiteral("安装 v%1").arg(plug.version));
        connect(btn, &QPushButton::clicked, this, [this, plug]() {
            downloadAndInstall(MarketIndex::instance()->resolveUrl(plug.package),
                               plug.sha256, plug.id, true);
        });
        blay->addWidget(btn);
    } else if (versionLessThan(local, plug.version)) {
        auto *btn = new QPushButton(
            QStringLiteral("更新到 v%1（当前 v%2）").arg(plug.version, local));
        connect(btn, &QPushButton::clicked, this, [this, plug]() {
            downloadAndInstall(MarketIndex::instance()->resolveUrl(plug.package),
                               plug.sha256, plug.id, true);
        });
        blay->addWidget(btn);
    } else {
        auto *btn = new QPushButton(
            svgIcon(":/icons/check.svg",
                    ThemeManager::instance()->currentTheme().text, 14),
            QStringLiteral("已安装"));
        btn->setEnabled(false);
        blay->addWidget(btn);
    }
    blay->addStretch(1);
    m_detailLay->addWidget(btnRow);

    m_detailLay->addWidget(makeSubLabel(
        QStringLiteral("包大小 %1 · 要求应用 ≥ v%2")
            .arg(formatBytes(plug.size), plug.minAppVersion)));

    auto *browser = makeMarkdownBrowser();
    browser->setMarkdown(plug.readme);
    m_detailLay->addWidget(browser, 1);
}

// ============================================================
//  详情：已装插件（状态 + 启停/卸载）
// ============================================================

void MarketTab::showInstalledPlugin(const QString &name)
{
    const auto plugins = PluginManager::instance()->discoveredPlugins();
    const auto it = std::find_if(plugins.cbegin(), plugins.cend(),
                                 [&name](const auto &p) { return p.name == name; });
    if (it == plugins.cend()) {
        showPlaceholder(QStringLiteral("该插件已卸载"));
        return;
    }
    const auto &p = *it;

    clearDetail();
    auto *pm = PluginManager::instance();
    const bool enabled = pm->isPluginEnabled(name);
    const bool activated = pm->isPluginActivated(name);

    auto *head = new QWidget;
    auto *hlay = new QHBoxLayout(head);
    hlay->setSpacing(12);
    auto *icon = makeIconPlaceholder(QStringLiteral("P"), 48);
    const QString iconFile = p.iconFilePath();
    if (!iconFile.isEmpty()) {
        QPixmap pm48(iconFile);
        if (!pm48.isNull())
            icon->setPixmap(pm48.scaled(48, 48, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation));
    }
    hlay->addWidget(icon);
    auto *vbox = new QVBoxLayout;
    vbox->setSpacing(2);
    vbox->addWidget(makeTitleLabel(name));
    vbox->addWidget(makeSubLabel(
        QStringLiteral("v%1 · %2").arg(p.version, p.author)));
    hlay->addLayout(vbox, 1);
    m_detailLay->addWidget(head);

    auto *status = new QLabel(
        !enabled ? QStringLiteral("已禁用")
                 : activated ? QStringLiteral("运行中") : QStringLiteral("已就绪"));
    status->setStyleSheet(
        !enabled ? QStringLiteral("color: #888888;")
                 : activated ? QStringLiteral("color: #4ec9b0;")
                             : QStringLiteral("color: #569cd6;"));
    m_detailLay->addWidget(status);

    if (!p.description.isEmpty())
        m_detailLay->addWidget(makeSubLabel(p.description));

    auto *btnRow = new QWidget;
    auto *blay = new QHBoxLayout(btnRow);
    auto *actionBtn = new QPushButton(
        !enabled ? QStringLiteral("启用")
                 : activated ? QStringLiteral("停止运行") : QStringLiteral("启动"));
    connect(actionBtn, &QPushButton::clicked, this,
            [this, name, enabled, activated]() {
        if (!enabled)
            emit pluginToggleRequested(name, true);
        else if (activated)
            emit pluginDeactivateRequested(name);
        else
            emit pluginActivateRequested(name);
    });
    blay->addWidget(actionBtn);
    if (enabled) {
        auto *disableBtn = new QPushButton(QStringLiteral("禁用"));
        connect(disableBtn, &QPushButton::clicked, this, [this, name]() {
            emit pluginToggleRequested(name, false);
        });
        blay->addWidget(disableBtn);
    }
    auto *uninstallBtn = new QPushButton(QStringLiteral("卸载"));
    connect(uninstallBtn, &QPushButton::clicked, this, [this, name]() {
        uninstallPlugin(name);
    });
    blay->addWidget(uninstallBtn);
    blay->addStretch(1);
    m_detailLay->addWidget(btnRow);

    // 市场补充说明（有条目时）
    const auto market = MarketIndex::instance()->pluginById(name);
    if (!market.id.isEmpty() && !market.readme.isEmpty()) {
        m_detailLay->addWidget(makeSectionLabel(QStringLiteral("详细说明")));
        auto *browser = makeMarkdownBrowser();
        browser->setMarkdown(market.readme);
        m_detailLay->addWidget(browser, 1);
    } else {
        m_detailLay->addStretch(1);
    }
}

// ============================================================
//  图标 / 图片（磁盘缓存 + 网络异步在 MarketModel 共享层，方案 §13.10；
//  首页卡片图标 48px 经 loadCardIcon，详情大图经 fetchMarketPixmap）
// ============================================================

// ============================================================
//  安装 / 卸载
// ============================================================

void MarketTab::downloadAndInstall(const QUrl &url, const QString &expectedSha,
                                   const QString &id, bool isPlugin)
{
    const QString title = isPlugin ? QStringLiteral("安装插件")
                                   : QStringLiteral("安装驱动");
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setVisible(true);

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 cur, qint64 total) {
                if (total > 0) {
                    m_progress->setRange(0, 100);
                    m_progress->setValue(int(cur * 100 / total));
                } else {
                    m_progress->setRange(0, 0);   // 不定进度
                }
            });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, expectedSha, id, isPlugin, title]() {
        reply->deleteLater();
        m_progress->setVisible(false);
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, title,
                QStringLiteral("包下载失败: %1").arg(reply->errorString()));
            return;
        }

        // sha256 校验（market.json 声明的期望值）
        const QByteArray data = reply->readAll();
        if (!expectedSha.isEmpty()) {
            const QString actual = QString::fromLatin1(
                QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
            if (actual.compare(expectedSha, Qt::CaseInsensitive) != 0) {
                QMessageBox::warning(this, title,
                    QStringLiteral("包校验失败（sha256 不匹配），已中止安装。\n"
                                   "请「刷新」市场索引后重试。"));
                return;
            }
        }

        // 落临时文件后分流安装（临时文件随本作用域销毁）
        QTemporaryFile tmp(QDir::temp().absoluteFilePath(
            isPlugin ? QStringLiteral("openbus-pkg-XXXXXX.opk")
                     : QStringLiteral("openbus-pkg-XXXXXX.odp")));
        if (!tmp.open()) {
            QMessageBox::warning(this, title,
                QStringLiteral("无法创建临时文件: %1").arg(tmp.errorString()));
            return;
        }
        tmp.write(data);
        tmp.flush();

        if (isPlugin) {
            const QString err = PluginManager::instance()->installPackage(tmp.fileName());
            if (!err.isEmpty())
                QMessageBox::warning(this, title, err);
            else
                QMessageBox::information(
                    this, title, QStringLiteral("插件 %1 安装成功。").arg(id));
            refreshInstalled();
        } else {
            installOdpFile(tmp.fileName(), expectedSha);
        }
    });
}

void MarketTab::installOdpFile(const QString &odpPath, const QString &expectedSha)
{
    // 期望 sha 非空 → 先校验（市场下载路径已校验，此处兜底；离线包依赖包内 CHECKSUMS）
    if (!expectedSha.isEmpty()) {
        QFile f(odpPath);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, QStringLiteral("安装驱动"),
                QStringLiteral("无法读取驱动包: %1").arg(odpPath));
            return;
        }
        const QString actual = QString::fromLatin1(
            QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex());
        if (actual.compare(expectedSha, Qt::CaseInsensitive) != 0) {
            QMessageBox::warning(this, QStringLiteral("安装驱动"),
                QStringLiteral("驱动包校验失败（sha256 不匹配），已中止安装。"));
            return;
        }
    }

    // 预检（validate）读取包内 driver.json，取 id/version 做安装预览
    QJsonObject vres;
    QString err = runDriverTool({ QStringLiteral("validate"), odpPath }, &vres);
    if (!err.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("安装驱动"), err);
        return;
    }
    const QString id = vres.value(QStringLiteral("id")).toString();
    const QString version = vres.value(QStringLiteral("version")).toString();

    const auto ret = QMessageBox::question(
        this, QStringLiteral("安装驱动"),
        QStringLiteral("即将安装驱动 %1 v%2。\n\n"
                       "注意：驱动为原生插件，安装后将加载进主进程"
                       "（与内置驱动同级，保证低时延性能）。是否继续？").arg(id, version));
    if (ret != QMessageBox::Yes)
        return;

    QJsonObject ires;
    err = runDriverTool({ QStringLiteral("install"), odpPath,
                          DriverRegistry::driversRootDir() }, &ires);
    if (!err.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("安装驱动"), err);
        return;
    }

    // 热加载（scanAndLoad → driversChanged → 本页/设备树自动刷新）
    m_current = { MarketItem::InstalledDriver, id };   // 安装后自动选中新驱动
    DriverRegistry::instance()->scanAndLoad();
    refreshInstalled();
    emit driverInstalled(id);
    QMessageBox::information(this, QStringLiteral("安装驱动"),
        QStringLiteral("驱动 %1 v%2 安装成功，已加载。")
            .arg(id, ires.value(QStringLiteral("version")).toString()));
}

void MarketTab::uninstallDriver(const QString &driverId)
{
    const auto ret = QMessageBox::question(
        this, QStringLiteral("卸载驱动"),
        QStringLiteral("确定卸载驱动 %1？\n\n"
                       "若其 DLL 已被本次运行加载，重启程序后将彻底清理（方案 §7.4）。")
            .arg(driverId));
    if (ret != QMessageBox::Yes)
        return;

    const QString err = DriverRegistry::instance()->uninstallExternal(driverId);
    if (!err.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("卸载驱动"), err);
        return;
    }
    // driversChanged → refreshInstalled（条目移除后详情自动回退占位）
    emit driverUninstalled(driverId);
}

void MarketTab::toggleDriverEnabled(const QString &driverId)
{
    bool toEnable = true;
    for (const auto &e : DriverRegistry::instance()->drivers()) {
        if (e.driverId == driverId) {
            toEnable = !e.enabled;
            break;
        }
    }
    DriverRegistry::instance()->setDriverEnabled(driverId, toEnable);
    // driversChanged → refreshInstalled 自动刷新列表/详情/设备树
}

void MarketTab::uninstallPlugin(const QString &name)
{
    const auto ret = QMessageBox::question(
        this, QStringLiteral("卸载插件"),
        QStringLiteral("确定卸载插件 %1？").arg(name));
    if (ret != QMessageBox::Yes)
        return;
    const QString err = PluginManager::instance()->uninstallPlugin(name);
    if (!err.isEmpty())
        QMessageBox::warning(this, QStringLiteral("卸载插件"), err);
    refreshInstalled();
}

// ============================================================
//  本地状态查询
// ============================================================

QString MarketTab::installedDriverVersion(const QString &driverId) const
{
    for (const auto &e : DriverRegistry::instance()->drivers()) {
        if (e.driverId == driverId)
            return e.version;
    }
    return QString();
}

QString MarketTab::installedPluginVersion(const QString &id) const
{
    const auto plugins = PluginManager::instance()->discoveredPlugins();
    for (const auto &p : plugins) {
        if (p.name == id)
            return p.version;
    }
    return QString();
}

// ============================================================
//  driver_tool.py 调用（与 PluginManager 安装 .opk 同一 QProcess 模式）
// ============================================================

QString MarketTab::findAppBaseDir()
{
    // 可执行文件目录（发布版）→ 源码根（开发版）→ 兜底 exeDir
    const QString exeDir = QCoreApplication::applicationDirPath();
    if (QDir(exeDir + QStringLiteral("/plugins")).exists()
        || QDir(exeDir + QStringLiteral("/sdk")).exists())
        return exeDir;

    const QString sourceRoot =
        QDir(QDir(exeDir).absoluteFilePath(QStringLiteral("../.."))).absolutePath();
    if (QDir(sourceRoot + QStringLiteral("/plugins")).exists()
        || QDir(sourceRoot + QStringLiteral("/sdk")).exists())
        return sourceRoot;

    return exeDir;
}

QString MarketTab::findPythonExecutable()
{
    QStringList candidates;
    const QString envPython =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("SIN_PYTHON"));
    if (!envPython.isEmpty())
        candidates << envPython;

    // 捆绑运行时 Python（<exeDir>/runtime/python/python.exe，打包方案 §6.1）——
    // 与 PluginManager::findPythonExecutable 同一候选顺序，优先于系统 Python
    const QString bundled = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("runtime/python/python.exe"));
    if (QFileInfo::exists(bundled))
        candidates << QDir::toNativeSeparators(bundled);

    candidates << QStringLiteral("python3")
               << QStringLiteral("python")
               << QStringLiteral("py");

    for (const auto &cmd : candidates) {
        QProcess proc;
        proc.start(cmd, { QStringLiteral("-c"),
                          QStringLiteral("import sys; print(sys.executable)") });
        if (proc.waitForFinished(3000) && proc.exitCode() == 0) {
            const QString path =
                QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
            if (!path.isEmpty() && QFileInfo::exists(path))
                return path;
        }
    }
    return QString();
}

QString MarketTab::runDriverTool(const QStringList &args, QJsonObject *result)
{
    if (m_pythonExe.isEmpty())
        m_pythonExe = findPythonExecutable();
    if (m_pythonExe.isEmpty())
        return QStringLiteral("未找到 Python 解释器");

    const QString toolPath = QDir(findAppBaseDir())
                                 .filePath(QStringLiteral("scripts/driver_tool.py"));
    if (!QFileInfo::exists(toolPath))
        return QStringLiteral("驱动工具不存在: %1").arg(toolPath);

    QProcess proc;
    proc.start(m_pythonExe, QStringList{ toolPath } + args);
    if (!proc.waitForFinished(60000)) {
        proc.kill();
        return QStringLiteral("驱动工具执行超时");
    }

    const QByteArray out = proc.readAllStandardOutput().trimmed();
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(out, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject())
        return QStringLiteral("驱动工具输出异常: %1")
                   .arg(QString::fromUtf8(out).left(300));

    *result = doc.object();
    if (!result->value(QStringLiteral("ok")).toBool())
        return result->value(QStringLiteral("error")).toString(QStringLiteral("操作失败"));
    return QString();
}
