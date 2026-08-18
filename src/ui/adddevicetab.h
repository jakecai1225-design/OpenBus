#ifndef ADDDEVICETAB_H
#define ADDDEVICETAB_H

#include <QWidget>
#include <QJsonObject>

#include "core/driver/marketindex.h"

class QListWidget;
class QListWidgetItem;
class QTableWidget;
class QLabel;
class QPushButton;
class QLineEdit;
class QStackedWidget;
class QProgressBar;
class QNetworkAccessManager;
class QNetworkReply;
class QFormLayout;
class QTextBrowser;

/**
 * @brief 新增设备标签页 — 设备市场 + 已安装驱动管理（doc/驱动系统方案.md §8.4）
 *
 * 双视图（顶部切换）：
 *   - 市场：搜索 + 设备卡片列表 + 图文详情（主图/规格表/Markdown 介绍）+
 *     安装闭环（下载 .odp → sha256 → 确认弹窗 → 安装 → 热加载）；
 *     图片经磁盘缓存（AppData/market-cache），市场不可达时显示错误与重试
 *   - 已安装：驱动列表 + 详情（设备型号表）+ 禁用/卸载 + 从 .odp 离线安装
 *
 * 数据源 MarketIndex / DriverRegistry；安装经 scripts/driver_tool.py。
 */
class AddDeviceTab : public QWidget
{
    Q_OBJECT
public:
    explicit AddDeviceTab(QWidget *parent = nullptr);

    /// 重新拉取已安装驱动列表（保留原选中，条目已删则回退首项）
    void refreshDrivers();

signals:
    /// 驱动安装成功（Registry 已热加载，设备树随之刷新）
    void driverInstalled(const QString &driverId);
    /// 外置驱动卸载完成
    void driverUninstalled(const QString &driverId);

private slots:
    // ---- 已安装页 ----
    void onInstallOdp();       // 「从 .odp 安装…」
    void onUninstallClicked();
    void onToggleEnabled();
    void onDriverSelected(QListWidgetItem *current);

    // ---- 市场页 ----
    void onMarketLoaded(bool ok, const QString &error);
    void onSearchChanged(const QString &text);
    void onMarketDeviceSelected(QListWidgetItem *current);
    void onInstallFromMarket();
    void onRefreshClicked();   // 刷新市场索引 + 已装列表

private:
    // 与 PluginManager 相同的定位逻辑（scripts 目录 / Python 解释器）
    static QString findAppBaseDir();
    static QString findPythonExecutable();
    /// 同步执行 driver_tool.py；成功返回空串且 *result 为输出 JSON 对象
    QString runDriverTool(const QStringList &args, QJsonObject *result);

    void buildMarketPage();    // 市场页 UI（stack index 0）
    void buildInstalledPage(); // 已安装页 UI（stack index 1）
    void showEntry(const QString &driverId);          // 已装详情
    void rebuildMarketList();                          // 市场卡片列表（含已装标记）
    void showMarketDevice(const MarketIndex::DeviceInfo &dev);
    void fetchImage(const QUrl &url);                  // 磁盘缓存 + 网络异步
    QString imageCachePath(const QUrl &url) const;
    /// 共用安装链：sha256 校验 → 确认弹窗 → driver_tool install → 热加载
    void installOdpFile(const QString &odpPath, const QString &expectedSha,
                        const QString &driverId);
    /// 本地安装状态文案（"已安装 v1.0.0（内置）" / "未安装"）
    QString marketStateText(const QString &driverId) const;
    /// driverId 是否已在本地（任意可用状态均算）
    bool isDriverInstalled(const QString &driverId) const;

    QStackedWidget *m_stack = nullptr;

    // ---- 市场页 ----
    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_marketStatus = nullptr;
    QListWidget *m_marketList = nullptr;
    QLabel *m_imageLabel = nullptr;
    QLabel *m_devTitle = nullptr;       // 型号（大字）
    QLabel *m_devMeta = nullptr;        // 厂商 · 标签 · 本地状态
    QFormLayout *m_specsForm = nullptr; // 规格表（动态填充）
    QTextBrowser *m_introBrowser = nullptr;  // Markdown 介绍
    QLabel *m_pkgInfoLabel = nullptr;   // 驱动包版本/大小/更新时间/授权
    QPushButton *m_installBtn = nullptr;
    QProgressBar *m_progress = nullptr;
    MarketIndex::DeviceInfo m_currentDev;
    QNetworkAccessManager *m_nam = nullptr;       // 市场图片 + 驱动包下载
    QNetworkReply *m_activeReply = nullptr;       // 当前图片请求（切换详情时 abort）

    // ---- 已安装页 ----
    QListWidget *m_driverList = nullptr;
    QLabel *m_nameLabel = nullptr;         // 驱动显示名（大字）
    QLabel *m_idLabel = nullptr;
    QLabel *m_versionLabel = nullptr;
    QLabel *m_sourceLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QTableWidget *m_deviceTable = nullptr; // 支持的设备型号表
    QPushButton *m_uninstallBtn = nullptr;
    QPushButton *m_toggleEnabledBtn = nullptr;

    QString m_selectedDriverId;  // 当前选中已装条目的 driverId
    QString m_pythonExe;         // 缓存的 Python 解释器路径
};

#endif // ADDDEVICETAB_H
