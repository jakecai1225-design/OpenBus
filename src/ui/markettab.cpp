#include "markettab.h"

#include "core/driver/driverregistry.h"
#include "core/plugin/plugininfo.h"
#include "core/plugin/pluginmanager.h"
#include "ui/thememanager.h"
#include "utils/svg_icon.h"

#include <QButtonGroup>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
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
#include <QSplitter>
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
    auto *label = new QLabel(ch);
    label->setFixedSize(size, size);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(
        QStringLiteral("background: #3a3d41; border-radius: %1px; color: #aaaaaa;"
                       " font-weight: bold;").arg(size / 6));
    QFont f = label->font();
    f.setPointSize(qMax(size / 3, 9));
    label->setFont(f);
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
//  构造 / UI 构建
// ============================================================

MarketTab::MarketTab(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    rebuildList();
    showPlaceholder(QStringLiteral("在左侧选择驱动或插件查看详情"));
    MarketIndex::instance()->refresh();   // 异步加载市场索引
}

void MarketTab::buildUi()
{
    m_nam = new QNetworkAccessManager(this);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // ---- 工具栏：搜索 + 筛选 + 刷新 + 安装菜单 ----
    auto *bar = new QHBoxLayout;
    bar->setSpacing(6);

    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(
        QStringLiteral("搜索驱动与插件（型号 / 厂商 / 关键词）"));
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &MarketTab::onSearchChanged);
    bar->addWidget(m_searchEdit, 1);

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

    auto *refreshBtn = new QToolButton;
    refreshBtn->setText(QStringLiteral("⟳ 刷新"));
    refreshBtn->setToolTip(QStringLiteral("重新拉取市场索引与本地已装列表"));
    connect(refreshBtn, &QToolButton::clicked, this, &MarketTab::onRefreshClicked);
    bar->addWidget(refreshBtn);

    auto *installBtn = new QToolButton;
    installBtn->setText(QStringLiteral("⋯ 安装"));
    installBtn->setToolTip(QStringLiteral("从本地包文件安装（.odp 驱动 / .opk 插件）"));
    connect(installBtn, &QToolButton::clicked, this, &MarketTab::onInstallFromFile);
    bar->addWidget(installBtn);

    root->addLayout(bar);

    // ---- 状态行 + 下载进度条 ----
    m_progress = new QProgressBar;
    m_progress->setTextVisible(false);
    m_progress->setMaximumHeight(3);
    m_progress->setRange(0, 100);
    m_progress->setVisible(false);
    root->addWidget(m_progress);

    m_marketStatus = new QLabel(QStringLiteral("市场加载中…"));
    m_marketStatus->setStyleSheet(QStringLiteral("color: #9d9d9d;"));
    root->addWidget(m_marketStatus);

    // ---- 主体：左列表 / 右详情 ----
    auto *listHost = new QWidget;
    m_listLay = new QVBoxLayout(listHost);
    m_listLay->setContentsMargins(0, 0, 4, 0);
    m_listLay->setSpacing(2);
    m_listLay->addStretch(1);
    m_listArea = new QScrollArea;
    m_listArea->setWidgetResizable(true);
    m_listArea->setWidget(listHost);
    m_listArea->setFrameShape(QFrame::NoFrame);
    m_listArea->setMinimumWidth(300);

    auto *detailHost = new QWidget;
    m_detailLay = new QVBoxLayout(detailHost);
    m_detailLay->setContentsMargins(8, 0, 4, 0);
    m_detailLay->setSpacing(8);
    m_detailArea = new QScrollArea;
    m_detailArea->setWidgetResizable(true);
    m_detailArea->setWidget(detailHost);
    m_detailArea->setFrameShape(QFrame::NoFrame);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(m_listArea);
    splitter->addWidget(m_detailArea);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 360, 900 });
    root->addWidget(splitter, 1);

    // ---- 数据源信号 ----
    connect(MarketIndex::instance(), &MarketIndex::loaded,
            this, &MarketTab::onMarketLoaded);
    // 安装/禁用/卸载（含 scanAndLoad）后自动刷新已装分组
    connect(DriverRegistry::instance(), &DriverRegistry::driversChanged,
            this, &MarketTab::refreshInstalled);
}

// ============================================================
//  工具栏动作
// ============================================================

void MarketTab::focusSearch()
{
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
//  左栏列表（三分组聚合）
// ============================================================

void MarketTab::addSectionLabel(const QString &title)
{
    // 插入到尾部 stretch 之前
    m_listLay->insertWidget(m_listLay->count() - 1, makeSectionLabel(title));
}

FrameRow *MarketTab::makeRow(const MarketItem &item, const QString &title,
                             const QString &meta, const QString &status)
{
    auto *row = new FrameRow;
    row->item = item;

    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(8, 6, 8, 6);
    lay->setSpacing(8);

    row->iconLabel = makeIconPlaceholder(
        item.kind == MarketItem::InstalledPlugin || item.kind == MarketItem::MarketPlugin
            ? QStringLiteral("P") : QStringLiteral("D"),
        32);
    lay->addWidget(row->iconLabel);

    auto *tbox = new QVBoxLayout;
    tbox->setSpacing(0);
    auto *titleLabel = new QLabel(title);
    QFont bold = titleLabel->font();
    bold.setBold(true);
    titleLabel->setFont(bold);
    auto *metaLabel = new QLabel(meta);
    QFont small = metaLabel->font();
    small.setPointSize(qMax(small.pointSize() - 1, 1));
    metaLabel->setFont(small);
    metaLabel->setStyleSheet(QStringLiteral("color: #9d9d9d;"));
    // 单行截断（VS Code 条目风格）
    const QFontMetrics fm(metaLabel->font());
    metaLabel->setText(fm.elidedText(meta, Qt::ElideRight, 340));
    tbox->addWidget(titleLabel);
    tbox->addWidget(metaLabel);
    lay->addLayout(tbox, 1);

    if (!status.isEmpty()) {
        auto *statusLabel = new QLabel(status);
        statusLabel->setStyleSheet(QStringLiteral("color: #9d9d9d;"));
        statusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lay->addWidget(statusLabel);
    }

    // 已装插件：行内启停小按钮（方案 §13.9 决策保留）
    if (item.kind == MarketItem::InstalledPlugin) {
        auto *pm = PluginManager::instance();
        const bool enabled = pm->isPluginEnabled(item.id);
        const bool activated = pm->isPluginActivated(item.id);
        auto *btn = new QToolButton;
        btn->setFixedSize(52, 24);
        btn->setText(!enabled ? QStringLiteral("启用")
                     : activated ? QStringLiteral("停止")
                                 : QStringLiteral("启动"));
        const QString name = item.id;
        connect(btn, &QToolButton::clicked, this, [this, name, enabled, activated]() {
            if (!enabled)
                emit pluginToggleRequested(name, true);
            else if (activated)
                emit pluginDeactivateRequested(name);
            else
                emit pluginActivateRequested(name);
        });
        lay->addWidget(btn);
    }

    // 非按钮子控件鼠标事件穿透 → 行点击
    row->iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    for (QLabel *l : row->findChildren<QLabel *>())
        l->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    row->setOnClick([this, item]() { selectItem(item); });
    return row;
}

void MarketTab::rebuildList()
{
    // 清空旧行（保留重新追加的 stretch 语义：先全清，尾部再补 stretch）
    clearLayout(m_listLay);
    m_listLay->addStretch(1);
    const auto insertBeforeStretch = [this](QWidget *w) {
        m_listLay->insertWidget(m_listLay->count() - 1, w);
    };

    const QString text = m_searchEdit->text();
    const bool wantDrivers = m_filterAll->isChecked() || m_filterDrivers->isChecked();
    const bool wantPlugins = m_filterAll->isChecked() || m_filterPlugins->isChecked();
    int shown = 0;

    // ---- 分组：已安装（驱动 + 插件混合） ----
    int installedCount = 0;
    if (wantDrivers) {
        for (const auto &e : MarketModel::collectInstalledDrivers()) {
            if (!MarketIndex::matchWords(text, e.searchFields))
                continue;
            if (installedCount == 0)
                addSectionLabel(QStringLiteral("已安装"));
            auto *row = makeRow(e.item, e.title, e.meta, e.status);
            loadRowIcon(row, e.marketIcon);
            insertBeforeStretch(row);
            ++installedCount;
            ++shown;
        }
    }
    if (wantPlugins) {
        for (const auto &e : MarketModel::collectInstalledPlugins()) {
            if (!MarketIndex::matchWords(text, e.searchFields))
                continue;
            if (installedCount == 0)
                addSectionLabel(QStringLiteral("已安装"));
            auto *row = makeRow(e.item, e.title, e.meta, e.status);
            const QPixmap localIcon = MarketModel::pluginIconLocal(e.item.id);
            if (!localIcon.isNull())
                row->iconLabel->setPixmap(
                    localIcon.scaled(32, 32, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation));
            else
                loadRowIcon(row, e.marketIcon);
            insertBeforeStretch(row);
            ++installedCount;
            ++shown;
        }
    }

    // ---- 分组：驱动市场（drivers[]，按驱动聚合） ----
    int driverMarket = 0;
    if (wantDrivers) {
        for (const auto &e : MarketModel::collectMarketDrivers()) {
            if (!MarketIndex::matchWords(text, e.searchFields))
                continue;
            if (driverMarket == 0)
                addSectionLabel(QStringLiteral("驱动市场"));
            auto *row = makeRow(e.item, e.title, e.meta, e.status);
            loadRowIcon(row, e.marketIcon);
            insertBeforeStretch(row);
            ++driverMarket;
            ++shown;
        }
    }

    // ---- 分组：插件市场（plugins[]） ----
    int pluginMarket = 0;
    if (wantPlugins) {
        for (const auto &e : MarketModel::collectMarketPlugins()) {
            if (!MarketIndex::matchWords(text, e.searchFields))
                continue;
            if (pluginMarket == 0)
                addSectionLabel(QStringLiteral("插件市场"));
            auto *row = makeRow(e.item, e.title, e.meta, e.status);
            loadRowIcon(row, e.marketIcon);
            insertBeforeStretch(row);
            ++pluginMarket;
            ++shown;
        }
    }

    updateRowStyles();
    if (shown == 0)
        showPlaceholder(QStringLiteral("没有匹配的条目"));
}

void MarketTab::selectItem(const MarketItem &item)
{
    m_current = item;
    updateRowStyles();
    showDetail(item);
}

void MarketTab::updateRowStyles()
{
    for (int i = 0; i < m_listLay->count(); ++i) {
        auto *w = m_listLay->itemAt(i)->widget();
        if (w && w->property("marketRow").toBool()) {
            auto *row = static_cast<FrameRow *>(w);
            row->setSelected(row->item == m_current);
        }
    }
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
    clearDetail();
    auto *label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral("color: #777777;"));
    m_detailLay->addWidget(label, 1);
}

void MarketTab::showDetail(const MarketItem &item)
{
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
        img->setStyleSheet(QStringLiteral("background: #2a2d2e; border-radius: 4px;"
                                          " color: #777;"));
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
//  图标 / 图片（磁盘缓存 + 网络异步在 MarketModel 共享层，方案 §13.10）
// ============================================================

void MarketTab::loadRowIcon(FrameRow *row, const QString &relPath)
{
    if (relPath.isEmpty())
        return;
    MarketModel::fetchMarketPixmap(MarketIndex::instance()->resolveUrl(relPath),
                [row](const QPixmap &pm) {
                    QPointer<FrameRow> g(row);
                    if (g && g->iconLabel)
                        g->iconLabel->setPixmap(
                            pm.scaled(32, 32, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation));
                });
}

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
