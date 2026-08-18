#include "adddevicetab.h"

#include "core/driver/driverregistry.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTemporaryFile>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <algorithm>

// ============================================================
//  新增设备标签页 — 设备市场 + 已安装驱动管理（方案 §8.4）
// ============================================================

namespace {
// 1234567 → "1.2 MB"
QString formatBytes(qint64 n)
{
    if (n < 1024)
        return QStringLiteral("%1 B").arg(n);
    if (n < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(n / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(n / (1024.0 * 1024.0), 0, 'f', 1);
}

// "1.2" < "1.10"（按点分数字逐段比较）
bool versionLessThan(const QString &a, const QString &b)
{
    const auto pa = a.split(QLatin1Char('.'));
    const auto pb = b.split(QLatin1Char('.'));
    const int n = std::max(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        const int x = i < pa.size() ? pa.at(i).toInt() : 0;
        const int y = i < pb.size() ? pb.at(i).toInt() : 0;
        if (x != y)
            return x < y;
    }
    return false;
}

// 规格键 → 中文标签（未知键原样展示）
QString specLabelText(const QString &key)
{
    static const struct { const char *key; const char *label; } kLabels[] = {
        { "channels",  "通道数" },
        { "canFd",     "CAN FD" },
        { "maxBaud",   "最高波特率" },
        { "timestamp", "硬件时间戳" },
        { "interface", "接口" },
        { "power",     "供电" },
    };
    for (const auto &l : kLabels) {
        if (key == QLatin1String(l.key))
            return QString::fromUtf8(l.label);
    }
    return key;
}

// 规格值展示：bool → 支持/不支持；channels → "N × 通道"；其余原样
QString specValueText(const QString &key, const QJsonValue &v)
{
    if (v.isBool())
        return v.toBool() ? QStringLiteral("支持") : QStringLiteral("不支持");
    if (v.isDouble()) {
        if (key == QLatin1String("channels"))
            return QStringLiteral("%1 × 通道").arg(static_cast<int>(v.toDouble()));
        return QString::number(v.toDouble());
    }
    return v.toString();
}
} // namespace

AddDeviceTab::AddDeviceTab(QWidget *parent)
    : QWidget(parent)
{
    m_nam = new QNetworkAccessManager(this);

    // ---- 顶部工具栏：视图切换 + 全局操作 ----
    auto *marketBtn = new QPushButton(QStringLiteral("市场"));
    marketBtn->setCheckable(true);
    marketBtn->setChecked(true);
    auto *installedBtn = new QPushButton(QStringLiteral("已安装"));
    installedBtn->setCheckable(true);

    auto *views = new QButtonGroup(this);
    views->setExclusive(true);
    views->addButton(marketBtn, 0);
    views->addButton(installedBtn, 1);

    auto *hint = new QLabel(QStringLiteral(
        "从设备市场浏览并安装驱动，或从 .odp 驱动包离线安装；"
        "安装后设备即刻出现在左侧「设备连接」面板。"));
    hint->setStyleSheet(QStringLiteral("color: #888;"));

    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(marketBtn);
    toolbar->addWidget(installedBtn);
    toolbar->addSpacing(12);
    toolbar->addWidget(hint, 1);
    toolbar->addStretch();

    auto *refreshBtn = new QPushButton(QStringLiteral("刷新"));
    refreshBtn->setToolTip(QStringLiteral("重新拉取市场索引并重扫本地驱动"));
    connect(refreshBtn, &QPushButton::clicked, this, &AddDeviceTab::onRefreshClicked);
    toolbar->addWidget(refreshBtn);

    auto *offlineBtn = new QPushButton(QStringLiteral("从 .odp 安装…"));
    offlineBtn->setToolTip(QStringLiteral(
        "选择 .odp 驱动包离线安装（可由 scripts/driver_tool.py pack 打包）"));
    connect(offlineBtn, &QPushButton::clicked, this, &AddDeviceTab::onInstallOdp);
    toolbar->addWidget(offlineBtn);

    // ---- 双视图 ----
    m_stack = new QStackedWidget;
    buildMarketPage();    // index 0
    buildInstalledPage(); // index 1
    connect(views, &QButtonGroup::idClicked,
            m_stack, &QStackedWidget::setCurrentIndex);

    auto *root = new QVBoxLayout(this);
    root->addLayout(toolbar);
    root->addWidget(m_stack, 1);

    // 市场索引加载完成 → 刷新市场列表与状态
    connect(MarketIndex::instance(), &MarketIndex::loaded,
            this, &AddDeviceTab::onMarketLoaded);
    // Registry 变化（安装/卸载/禁用/热加载）→ 已装列表 + 市场已装标记 + 详情按钮态
    connect(DriverRegistry::instance(), &DriverRegistry::driversChanged, this, [this]() {
        refreshDrivers();
        rebuildMarketList();
        if (!m_currentDev.model.isEmpty())
            showMarketDevice(m_currentDev);
    });

    refreshDrivers();
    MarketIndex::instance()->refresh();
}

// ============================================================
//  市场页（stack index 0）
// ============================================================

void AddDeviceTab::buildMarketPage()
{
    auto *page = new QWidget;

    // 顶部：搜索框 + 状态
    auto *topRow = new QHBoxLayout;
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(
        QStringLiteral("搜索设备型号、厂商、关键词…（多词为「与」关系）"));
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &AddDeviceTab::onSearchChanged);
    topRow->addWidget(m_searchEdit, 1);

    m_marketStatus = new QLabel(QStringLiteral("正在加载市场索引…"));
    m_marketStatus->setStyleSheet(QStringLiteral("color: #888;"));
    topRow->addWidget(m_marketStatus);

    // 左侧：设备卡片列表
    m_marketList = new QListWidget;
    m_marketList->setMinimumWidth(240);
    m_marketList->setMaximumWidth(340);
    connect(m_marketList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *cur, QListWidgetItem *) { onMarketDeviceSelected(cur); });

    // 右侧：图文详情
    auto *detail = new QWidget;
    auto *detailLay = new QVBoxLayout(detail);

    m_imageLabel = new QLabel;
    m_imageLabel->setFixedHeight(260);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet(
        QStringLiteral("background: #1a2b4a; color: #6a86ad; border-radius: 4px;"));
    m_imageLabel->setText(QStringLiteral("设备主图"));
    detailLay->addWidget(m_imageLabel);

    m_devTitle = new QLabel(QStringLiteral("未选择设备"));
    QFont bold = m_devTitle->font();
    bold.setBold(true);
    bold.setPointSize(bold.pointSize() + 2);
    m_devTitle->setFont(bold);
    detailLay->addWidget(m_devTitle);

    m_devMeta = new QLabel;
    m_devMeta->setWordWrap(true);
    m_devMeta->setStyleSheet(QStringLiteral("color: #888;"));
    detailLay->addWidget(m_devMeta);

    m_pkgInfoLabel = new QLabel;
    m_pkgInfoLabel->setStyleSheet(QStringLiteral("color: #666;"));
    detailLay->addWidget(m_pkgInfoLabel);

    auto *specBox = new QGroupBox(QStringLiteral("规格"));
    m_specsForm = new QFormLayout(specBox);
    specBox->setMaximumWidth(340);

    m_introBrowser = new QTextBrowser;
    m_introBrowser->setOpenExternalLinks(true);
    m_introBrowser->setPlaceholderText(QStringLiteral("设备介绍"));

    auto *contentRow = new QHBoxLayout;
    contentRow->addWidget(specBox);
    contentRow->addWidget(m_introBrowser, 1);
    detailLay->addLayout(contentRow, 1);

    m_progress = new QProgressBar;
    m_progress->setVisible(false);
    m_installBtn = new QPushButton(QStringLiteral("安装驱动"));
    m_installBtn->setEnabled(false);
    m_installBtn->setToolTip(QStringLiteral(
        "下载 .odp 驱动包 → sha256 校验 → 安装并热加载（无需重启）"));
    connect(m_installBtn, &QPushButton::clicked, this, &AddDeviceTab::onInstallFromMarket);

    auto *bottomRow = new QHBoxLayout;
    bottomRow->addWidget(m_progress, 1);
    bottomRow->addWidget(m_installBtn);
    detailLay->addLayout(bottomRow);

    auto *body = new QHBoxLayout;
    body->addWidget(m_marketList);
    body->addWidget(detail, 1);

    auto *root = new QVBoxLayout(page);
    root->addLayout(topRow);
    root->addLayout(body, 1);

    m_stack->addWidget(page); // index 0
}

void AddDeviceTab::onMarketLoaded(bool ok, const QString &error)
{
    if (!ok) {
        m_marketStatus->setText(QStringLiteral("市场加载失败: %1").arg(error));
        rebuildMarketList();
        return;
    }
    rebuildMarketList();
}

void AddDeviceTab::onSearchChanged(const QString &text)
{
    Q_UNUSED(text);
    rebuildMarketList();
}

void AddDeviceTab::rebuildMarketList()
{
    if (!m_marketList)
        return;

    const auto devices = MarketIndex::instance()->search(m_searchEdit->text());
    m_marketList->blockSignals(true);
    m_marketList->clear();
    for (const auto &d : devices) {
        auto *item = new QListWidgetItem(m_marketList);
        // 已安装条目前缀 ✓，与详情页安装按钮态一致
        const QString prefix = isDriverInstalled(d.driverId)
                                   ? QStringLiteral("✓ ") : QString();
        item->setText(QStringLiteral("%1%2\n%3 · %4")
                          .arg(prefix, d.model, d.vendor, d.summary));
        item->setData(Qt::UserRole, d.model);
        item->setSizeHint(QSize(0, 48));
    }
    m_marketList->blockSignals(false);

    if (m_marketList->count() > 0)
        m_marketList->setCurrentRow(0);
    else
        onMarketDeviceSelected(nullptr);

    // 状态文案
    const auto *idx = MarketIndex::instance();
    if (!idx->isLoaded()) {
        m_marketStatus->setText(idx->lastError().isEmpty()
                                    ? QStringLiteral("市场索引未加载")
                                    : QStringLiteral("市场加载失败: %1").arg(idx->lastError()));
    } else {
        const QString updatedSuffix = idx->updated().isEmpty()
                                          ? QString()
                                          : QStringLiteral(" · 更新于 %1").arg(idx->updated());
        m_marketStatus->setText(QStringLiteral("%1 个设备%2")
                                     .arg(m_marketList->count())
                                     .arg(updatedSuffix));
    }
}

void AddDeviceTab::onMarketDeviceSelected(QListWidgetItem *current)
{
    if (!current) {
        m_currentDev = MarketIndex::DeviceInfo();
        showMarketDevice(m_currentDev);
        return;
    }
    const QString model = current->data(Qt::UserRole).toString();
    for (const auto &d : MarketIndex::instance()->devices()) {
        if (d.model == model) {
            m_currentDev = d;
            showMarketDevice(d);
            return;
        }
    }
}

void AddDeviceTab::showMarketDevice(const MarketIndex::DeviceInfo &dev)
{
    const bool has = !dev.model.isEmpty();

    // 标题 / 概要行 / 包信息
    m_devTitle->setText(has ? dev.model : QStringLiteral("未选择设备"));
    if (has) {
        QStringList metaParts{ dev.vendor };
        if (!dev.tags.isEmpty())
            metaParts << dev.tags.join(QStringLiteral(" / "));
        metaParts << marketStateText(dev.driverId);
        m_devMeta->setText(metaParts.join(QStringLiteral(" · ")));
    } else {
        m_devMeta->setText(QStringLiteral(
            "从左侧列表选择设备查看图文详情。\n"
            "若市场加载失败，请点击右上角「刷新」重试。"));
    }
    m_pkgInfoLabel->clear();

    // 规格表（动态重建；已知键按固定序，未知键追加）
    while (m_specsForm->count() > 0) {
        QLayoutItem *child = m_specsForm->takeAt(0);
        delete child->widget();
        delete child;
    }
    if (has) {
        const QStringList known{ QStringLiteral("channels"), QStringLiteral("canFd"),
                                 QStringLiteral("maxBaud"), QStringLiteral("timestamp"),
                                 QStringLiteral("interface"), QStringLiteral("power") };
        QStringList keys;
        for (const auto &k : known) {
            if (dev.specs.contains(k))
                keys << k;
        }
        for (auto it = dev.specs.constBegin(); it != dev.specs.constEnd(); ++it) {
            if (!keys.contains(it.key()))
                keys << it.key();
        }
        for (const auto &k : keys) {
            auto *valueLabel = new QLabel(specValueText(k, dev.specs.value(k)));
            valueLabel->setWordWrap(true);
            m_specsForm->addRow(specLabelText(k), valueLabel);
        }
    }

    // 介绍
    m_introBrowser->setMarkdown(
        has ? dev.intro
            : QStringLiteral("安装说明\n"
                             "----\n"
                             "- **市场安装**：点击右下角「安装驱动」，自动下载 .odp "
                             "并校验 sha256 后安装；\n"
                             "- **离线安装**：顶部「从 .odp 安装…」选择本地驱动包；\n"
                             "- 已安装驱动可在「已安装」页禁用或卸载。"));

    // 主图
    m_imageLabel->setPixmap(QPixmap());
    if (has && !dev.images.isEmpty()) {
        fetchImage(MarketIndex::instance()->resolveUrl(dev.images.first()));
    } else {
        if (m_activeReply) {
            m_activeReply->abort();
            m_activeReply = nullptr;
        }
        m_imageLabel->setText(QStringLiteral("暂无图片"));
    }

    // 安装按钮态
    m_installBtn->setEnabled(false);
    if (!has)
        return;

    const auto drv = MarketIndex::instance()->driverById(dev.driverId);
    if (drv.id.isEmpty()) {
        m_installBtn->setText(QStringLiteral("不可安装"));
        return;
    }
    m_pkgInfoLabel->setText(QStringLiteral("驱动包 %1 v%2 · %3%4").arg(
        drv.name, drv.version, formatBytes(drv.size),
        drv.updatedAt.isEmpty() ? QString()
                                : QStringLiteral(" · 更新于 %1").arg(drv.updatedAt)));

    if (!isDriverInstalled(dev.driverId)) {
        m_installBtn->setText(QStringLiteral("安装驱动"));
        m_installBtn->setEnabled(true);
        return;
    }
    // 已安装：本地版本 ≥ 市场版本 → 已安装；否则可更新
    QString localVersion;
    for (const auto &e : DriverRegistry::instance()->drivers()) {
        if (e.driverId == dev.driverId) {
            localVersion = e.version;
            break;
        }
    }
    if (!versionLessThan(localVersion, drv.version)) {
        m_installBtn->setText(QStringLiteral("已安装 ✓"));
    } else {
        m_installBtn->setText(QStringLiteral("更新到 v%1").arg(drv.version));
        m_installBtn->setEnabled(true);
    }
}

void AddDeviceTab::fetchImage(const QUrl &url)
{
    // 弃置旧请求（详情快速切换）
    if (m_activeReply) {
        m_activeReply->abort();
        m_activeReply = nullptr;
    }

    // 磁盘缓存命中 → 直接显示
    const QString cache = imageCachePath(url);
    QPixmap pm(cache);
    if (!pm.isNull()) {
        m_imageLabel->setPixmap(
            pm.scaled(460, 259, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        return;
    }

    m_imageLabel->setText(QStringLiteral("图片加载中…"));
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = m_nam->get(req);
    m_activeReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        reply->deleteLater();
        if (m_activeReply == reply)
            m_activeReply = nullptr;
        // 竞态防护：详情已切换到其他设备 → 丢弃本次结果
        if (m_currentDev.images.isEmpty()
            || MarketIndex::instance()->resolveUrl(m_currentDev.images.first()) != url)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            m_imageLabel->setText(QStringLiteral("图片加载失败"));
            return;
        }
        const QByteArray data = reply->readAll();
        QPixmap img;
        if (!img.loadFromData(data)) {
            m_imageLabel->setText(QStringLiteral("图片格式不支持"));
            return;
        }
        // 写入磁盘缓存（失败不影响显示）
        QFile f(imageCachePath(url));
        if (f.open(QIODevice::WriteOnly))
            f.write(data);
        m_imageLabel->setPixmap(
            img.scaled(460, 259, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    });
}

QString AddDeviceTab::imageCachePath(const QUrl &url) const
{
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(
        url.toString().toUtf8(), QCryptographicHash::Sha1).toHex());
    QString suffix = QFileInfo(url.path()).suffix();
    if (suffix.isEmpty())
        suffix = QStringLiteral("img");
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/market-cache");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/") + hash + QLatin1Char('.') + suffix;
}

void AddDeviceTab::onInstallFromMarket()
{
    if (m_currentDev.model.isEmpty())
        return;
    const auto drv = MarketIndex::instance()->driverById(m_currentDev.driverId);
    if (drv.id.isEmpty() || drv.package.isEmpty())
        return;

    m_installBtn->setEnabled(false);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setVisible(true);

    QNetworkRequest req(MarketIndex::instance()->resolveUrl(drv.package));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 cur, qint64 total) {
                if (total > 0) {
                    m_progress->setRange(0, 100);
                    m_progress->setValue(int(cur * 100 / total));
                } else {
                    m_progress->setRange(0, 0); // 不定进度
                }
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply, drv]() {
        reply->deleteLater();
        m_progress->setVisible(false);
        m_installBtn->setEnabled(true);
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, QStringLiteral("安装驱动"),
                QStringLiteral("驱动包下载失败: %1").arg(reply->errorString()));
            return;
        }

        // sha256 校验（market.json 声明的期望值）
        const QByteArray data = reply->readAll();
        if (!drv.sha256.isEmpty()) {
            const QString actual = QString::fromLatin1(
                QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
            if (actual.compare(drv.sha256, Qt::CaseInsensitive) != 0) {
                QMessageBox::warning(this, QStringLiteral("安装驱动"),
                    QStringLiteral("驱动包校验失败（sha256 不匹配），已中止安装。\n"
                                   "请「刷新」市场索引后重试。"));
                return;
            }
        }

        // 落临时文件后走共用安装链（临时文件随本作用域销毁）
        QTemporaryFile tmp(QDir::temp().absoluteFilePath(
            QStringLiteral("openbus-driver-XXXXXX.odp")));
        if (!tmp.open()) {
            QMessageBox::warning(this, QStringLiteral("安装驱动"),
                QStringLiteral("无法创建临时文件: %1").arg(tmp.errorString()));
            return;
        }
        tmp.write(data);
        tmp.flush();
        installOdpFile(tmp.fileName(), drv.sha256, drv.id);
    });
}

QString AddDeviceTab::marketStateText(const QString &driverId) const
{
    for (const auto &e : DriverRegistry::instance()->drivers()) {
        if (e.driverId == driverId) {
            const QString src = e.builtin ? QStringLiteral("内置")
                                          : QStringLiteral("已安装");
            return QStringLiteral("本地: %1 v%2").arg(src, e.version);
        }
    }
    return QStringLiteral("未安装");
}

bool AddDeviceTab::isDriverInstalled(const QString &driverId) const
{
    for (const auto &e : DriverRegistry::instance()->drivers()) {
        if (e.driverId == driverId)
            return true;
    }
    return false;
}

// ============================================================
//  已安装页（stack index 1）
// ============================================================

void AddDeviceTab::buildInstalledPage()
{
    auto *page = new QWidget;

    // 左侧：已安装驱动列表
    m_driverList = new QListWidget;
    m_driverList->setMinimumWidth(240);
    m_driverList->setMaximumWidth(340);
    connect(m_driverList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *cur, QListWidgetItem *) { onDriverSelected(cur); });

    // 右侧：驱动详情
    auto *detail = new QWidget;
    auto *detailLay = new QVBoxLayout(detail);

    m_nameLabel = new QLabel(QStringLiteral("—"));
    QFont bold = m_nameLabel->font();
    bold.setBold(true);
    bold.setPointSize(bold.pointSize() + 2);
    m_nameLabel->setFont(bold);
    detailLay->addWidget(m_nameLabel);

    m_idLabel = new QLabel;
    m_versionLabel = new QLabel;
    m_sourceLabel = new QLabel;
    m_statusLabel = new QLabel;
    m_statusLabel->setWordWrap(true);
    auto *meta = new QFormLayout;
    meta->addRow(QStringLiteral("驱动 ID:"), m_idLabel);
    meta->addRow(QStringLiteral("版本:"), m_versionLabel);
    meta->addRow(QStringLiteral("来源:"), m_sourceLabel);
    meta->addRow(QStringLiteral("状态:"), m_statusLabel);
    detailLay->addLayout(meta);

    auto *tableTitle = new QLabel(QStringLiteral("支持的设备型号"));
    QFont bold2 = tableTitle->font();
    bold2.setBold(true);
    tableTitle->setFont(bold2);
    detailLay->addWidget(tableTitle);

    m_deviceTable = new QTableWidget(0, 4);
    m_deviceTable->setHorizontalHeaderLabels(
        {QStringLiteral("型号名称"), QStringLiteral("设备类型"),
         QStringLiteral("通道数"), QStringLiteral("CAN FD")});
    m_deviceTable->horizontalHeader()->setStretchLastSection(true);
    m_deviceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_deviceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deviceTable->verticalHeader()->setVisible(false);
    detailLay->addWidget(m_deviceTable, 1);

    m_uninstallBtn = new QPushButton(QStringLiteral("卸载此驱动"));
    m_uninstallBtn->setEnabled(false);
    m_uninstallBtn->setToolTip(QStringLiteral(
        "仅外置驱动可卸载；已加载的 DLL 在重启程序前仍驻留内存（方案 §7.4）"));
    connect(m_uninstallBtn, &QPushButton::clicked, this, &AddDeviceTab::onUninstallClicked);
    m_toggleEnabledBtn = new QPushButton(QStringLiteral("禁用此驱动"));
    m_toggleEnabledBtn->setEnabled(false);
    m_toggleEnabledBtn->setToolTip(QStringLiteral(
        "禁用后设备树隐藏且不参与枚举/创建，重启后不加载（方案 §7.4）"));
    connect(m_toggleEnabledBtn, &QPushButton::clicked, this,
            &AddDeviceTab::onToggleEnabled);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(m_toggleEnabledBtn);
    btnRow->addWidget(m_uninstallBtn);
    detailLay->addLayout(btnRow);

    auto *body = new QHBoxLayout;
    body->addWidget(m_driverList);
    body->addWidget(detail, 1);

    auto *root = new QVBoxLayout(page);
    root->addLayout(body, 1);

    m_stack->addWidget(page); // index 1
}

void AddDeviceTab::refreshDrivers()
{
    const QString keepId = m_selectedDriverId;
    m_driverList->blockSignals(true);
    m_driverList->clear();

    const auto entries = DriverRegistry::instance()->drivers();
    int restoreRow = -1;
    for (const auto &e : entries) {
        auto *item = new QListWidgetItem(m_driverList);
        const QString source = e.builtin ? QStringLiteral("内置") : QStringLiteral("外置");
        const QString status = e.enabled
                                   ? (e.available ? QStringLiteral("可用")
                                                  : QStringLiteral("不可用"))
                                   : QStringLiteral("已禁用");
        const QString version = e.version.isEmpty()
                                    ? QStringLiteral("-") : e.version;
        item->setText(QStringLiteral("%1\nv%2 · %3 · %4")
                          .arg(e.displayName, version, source, status));
        item->setData(Qt::UserRole, e.driverId);
        item->setSizeHint(QSize(0, 48));
        if (e.driverId == keepId)
            restoreRow = m_driverList->count() - 1;
    }
    m_driverList->blockSignals(false);

    // 恢复原选中；条目已卸载则回退首项（空列表保持清空）
    if (restoreRow >= 0)
        m_driverList->setCurrentRow(restoreRow);
    else if (m_driverList->count() > 0)
        m_driverList->setCurrentRow(0);
    else
        m_selectedDriverId.clear();
    onDriverSelected(m_driverList->currentItem());
}

void AddDeviceTab::onDriverSelected(QListWidgetItem *current)
{
    if (!current) {
        m_selectedDriverId.clear();
        showEntry(QString());
        return;
    }
    m_selectedDriverId = current->data(Qt::UserRole).toString();
    showEntry(m_selectedDriverId);
}

void AddDeviceTab::showEntry(const QString &driverId)
{
    m_nameLabel->setText(QStringLiteral("—"));
    m_idLabel->clear();
    m_versionLabel->clear();
    m_sourceLabel->clear();
    m_statusLabel->clear();
    m_deviceTable->setRowCount(0);
    m_uninstallBtn->setEnabled(false);
    m_toggleEnabledBtn->setEnabled(false);
    m_toggleEnabledBtn->setText(QStringLiteral("禁用此驱动"));

    if (driverId.isEmpty())
        return;

    const auto entries = DriverRegistry::instance()->drivers();
    const auto it = std::find_if(entries.cbegin(), entries.cend(),
                                 [&driverId](const auto &e) { return e.driverId == driverId; });
    if (it == entries.cend())
        return;
    const auto &e = *it;

    m_nameLabel->setText(e.displayName);
    m_idLabel->setText(e.driverId);
    m_versionLabel->setText(e.version);
    m_sourceLabel->setText(e.builtin
                               ? QStringLiteral("内置（随主程序静态编译）")
                               : QStringLiteral("外置驱动包（%1）").arg(e.installDir));
    m_statusLabel->setText(e.available
                               ? QStringLiteral("✓ 可用")
                               : QStringLiteral("✗ %1")
                                     .arg(e.disabledReason.isEmpty()
                                              ? QStringLiteral("不可用")
                                              : e.disabledReason));
    if (!e.enabled)
        m_statusLabel->setText(QStringLiteral("✗ 已禁用"));

    for (const auto &d : e.devices) {
        const auto obj = d.toObject();
        const int row = m_deviceTable->rowCount();
        m_deviceTable->insertRow(row);
        m_deviceTable->setItem(row, 0,
            new QTableWidgetItem(obj.value(QStringLiteral("name")).toString()));
        m_deviceTable->setItem(row, 1,
            new QTableWidgetItem(QString::number(obj.value(QStringLiteral("type")).toInt())));
        m_deviceTable->setItem(row, 2,
            new QTableWidgetItem(QString::number(obj.value(QStringLiteral("channels")).toInt())));
        m_deviceTable->setItem(row, 3,
            new QTableWidgetItem(obj.value(QStringLiteral("canFd")).toBool()
                                     ? QStringLiteral("支持") : QStringLiteral("—")));
    }

    m_uninstallBtn->setEnabled(!e.builtin);
    m_toggleEnabledBtn->setEnabled(true);
    m_toggleEnabledBtn->setText(e.enabled ? QStringLiteral("禁用此驱动")
                                          : QStringLiteral("启用此驱动"));
}

void AddDeviceTab::onInstallOdp()
{
    const QString odp = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择驱动包"), QString(),
        QStringLiteral("openbus 驱动包 (*.odp);;所有文件 (*)"));
    if (odp.isEmpty())
        return;
    // 离线包无期望 sha（pack 生成的 CHECKSUMS 由 install 阶段在包内自校验）
    installOdpFile(odp, QString(), QString());
}

void AddDeviceTab::onUninstallClicked()
{
    if (m_selectedDriverId.isEmpty())
        return;
    const auto ret = QMessageBox::question(
        this, QStringLiteral("卸载驱动"),
        QStringLiteral("确定卸载驱动 %1？\n\n"
                       "若其 DLL 已被本次运行加载，重启程序后将彻底清理（方案 §7.4）。")
            .arg(m_selectedDriverId));
    if (ret != QMessageBox::Yes)
        return;

    const QString id = m_selectedDriverId;
    const QString err = DriverRegistry::instance()->uninstallExternal(id);
    if (!err.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("卸载驱动"), err);
        return;
    }
    // driversChanged 已触发列表刷新（条目移除后回退首项）
    emit driverUninstalled(id);
}

void AddDeviceTab::onToggleEnabled()
{
    if (m_selectedDriverId.isEmpty())
        return;

    // 取当前状态取反（driversChanged 会触发列表/设备树同步刷新）
    bool toEnable = true;
    for (const auto &e : DriverRegistry::instance()->drivers()) {
        if (e.driverId == m_selectedDriverId) {
            toEnable = !e.enabled;
            break;
        }
    }
    DriverRegistry::instance()->setDriverEnabled(m_selectedDriverId, toEnable);
    // refreshDrivers 由 driversChanged 自动触发，这里仅同步详情按钮文案
    showEntry(m_selectedDriverId);
}

void AddDeviceTab::onRefreshClicked()
{
    MarketIndex::instance()->refresh();
    DriverRegistry::instance()->scanAndLoad();
    refreshDrivers();
}

// ============================================================
//  共用安装链：sha256 → 确认 → driver_tool install → 热加载
// ============================================================

void AddDeviceTab::installOdpFile(const QString &odpPath, const QString &expectedSha,
                                  const QString &driverId)
{
    Q_UNUSED(driverId); // id 以包内 driver.json 为准（validate 读取）

    // 期望 sha 非空 → 先校验（市场下载路径；离线路径依赖包内 CHECKSUMS）
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

    // 热加载（scanAndLoad 内部 emit driversChanged → 本页/设备树自动刷新）
    DriverRegistry::instance()->scanAndLoad();
    m_selectedDriverId = id;  // 安装后自动选中新驱动
    refreshDrivers();
    emit driverInstalled(id);
    QMessageBox::information(this, QStringLiteral("安装驱动"),
        QStringLiteral("驱动 %1 v%2 安装成功，已加载。")
            .arg(id, ires.value(QStringLiteral("version")).toString()));
}

// ============================================================
//  driver_tool.py 调用（与 PluginManager 安装 .opk 同一 QProcess 模式）
// ============================================================

QString AddDeviceTab::findAppBaseDir()
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

QString AddDeviceTab::findPythonExecutable()
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

QString AddDeviceTab::runDriverTool(const QStringList &args, QJsonObject *result)
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
