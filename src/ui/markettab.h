#ifndef MARKETTAB_H
#define MARKETTAB_H

#include <QWidget>
#include <QJsonObject>
#include <QUrl>

#include "core/driver/marketindex.h"
#include "marketmodel.h"   // MarketItem / FrameRow / MarketModel 共享层（方案 §13.10）

class QLabel;
class QLineEdit;
class QToolButton;
class QPushButton;
class QProgressBar;
class QScrollArea;
class QVBoxLayout;
class QTableWidget;
class QTextBrowser;
class QNetworkAccessManager;
class QNetworkReply;

/**
 * @brief 插件市场标签页 — 统一市场（VS Code 扩展市场风格，doc/驱动系统方案.md §13.5）
 *
 * 左栏三分组条目列表（已安装 = 驱动 + 插件混合，含行内启停小按钮；
 * 驱动市场 = drivers[] 按驱动聚合；插件市场 = plugins[]），支持多词 AND
 * 搜索与全部/驱动/插件筛选；右栏详情页四形态（图文/设备简表/Markdown 说明
 * + 操作按钮组：安装/更新/启停/禁用/卸载）。
 *
 * 数据源聚合：DriverRegistry（已装驱动）/ PluginManager（已装插件）/
 * MarketIndex（市场驱动 + 插件，schema 2）。
 * 安装链：下载 → sha256 → 驱动 driver_tool install 热加载 / 插件
 * PluginManager::installPackage；图片经磁盘缓存（AppData/market-cache）。
 */
class MarketTab : public QWidget
{
    Q_OBJECT
public:
    explicit MarketTab(QWidget *parent = nullptr);

    /// 重取已装驱动/插件并重建列表（保留选中并刷新详情）
    void refreshInstalled();
    /// 聚焦搜索框并全选（设备树「＋新增设备」跳转联动，方案 §13.6）
    void focusSearch();
    /// 定位并展示指定条目（清空搜索/筛选 → 重建列表 → 选中展示详情；
    /// 侧边栏迷你市场与设备树联动的统一入口，方案 §13.10）
    void revealItem(const MarketItem &item);
    /// 离线安装本地包（.odp 驱动 / .opk 插件，与「⋯ 安装」同一链路与确认文案）
    void installLocalFile(const QString &path);

signals:
    /// 驱动安装成功（Registry 已热加载，driversChanged 触发设备树刷新）
    void driverInstalled(const QString &driverId);
    /// 外置驱动卸载完成
    void driverUninstalled(const QString &driverId);
    /// 插件操作请求（转 PluginManager，由 MainWindow 连接）
    void pluginActivateRequested(const QString &name);
    void pluginDeactivateRequested(const QString &name);
    void pluginToggleRequested(const QString &name, bool enable);

private slots:
    void onMarketLoaded(bool ok, const QString &error);
    void onSearchChanged();
    void onRefreshClicked();
    void onInstallFromFile();      // 「⋯ 安装」菜单：.odp 驱动 / .opk 插件

private:
    // ---- 工具（与 PluginManager 相同的定位逻辑，迁移自 AddDeviceTab） ----
    static QString findAppBaseDir();
    static QString findPythonExecutable();
    QString runDriverTool(const QStringList &args, QJsonObject *result);

    // ---- 列表 ----
    void buildUi();
    void rebuildList();            // 依据搜索词 + 筛选 + 四数据源重建左栏
    FrameRow *makeRow(const MarketItem &item, const QString &title,
                      const QString &meta, const QString &status);
    void addSectionLabel(const QString &title);
    void selectItem(const MarketItem &item);
    void updateRowStyles();        // 依据 m_current 刷新行选中样式

    // ---- 详情（右栏动态重建） ----
    void clearDetail();
    void showPlaceholder(const QString &text);
    void showDetail(const MarketItem &item);       // 分发四形态
    void showMarketDriver(const MarketIndex::DriverInfo &drv);
    void showInstalledDriver(const QString &driverId);
    void showMarketPlugin(const MarketIndex::PluginInfo &plug);
    void showInstalledPlugin(const QString &name);

    // ---- 图标/图片 ----
    void loadRowIcon(FrameRow *row, const QString &relPath);   // 市场 icon（32）

    // ---- 安装/卸载 ----
    /// 市场下载安装（进度条 → sha256 → 驱动 installOdpFile / 插件 installPackage）
    void downloadAndInstall(const QUrl &url, const QString &expectedSha,
                            const QString &id, bool isPlugin);
    /// 驱动共用安装链：sha256 → validate 预览 → 确认 → install → 热加载
    void installOdpFile(const QString &odpPath, const QString &expectedSha);
    void uninstallDriver(const QString &driverId);
    void toggleDriverEnabled(const QString &driverId);
    void uninstallPlugin(const QString &name);

    // ---- 本地状态查询 ----
    QString installedDriverVersion(const QString &driverId) const;
    QString installedPluginVersion(const QString &id) const;

    // ---- UI ----
    QLineEdit *m_searchEdit = nullptr;
    QToolButton *m_filterAll = nullptr;
    QToolButton *m_filterDrivers = nullptr;
    QToolButton *m_filterPlugins = nullptr;
    QLabel *m_marketStatus = nullptr;   // 市场源状态（更新日期 / 错误）
    QProgressBar *m_progress = nullptr; // 下载进度（顶部细条，默认隐藏）
    QScrollArea *m_listArea = nullptr;
    QVBoxLayout *m_listLay = nullptr;   // 三分组条目（尾 stretch）
    QScrollArea *m_detailArea = nullptr;
    QVBoxLayout *m_detailLay = nullptr; // 详情动态内容

    MarketItem m_current;               // 当前选中条目
    QNetworkAccessManager *m_nam = nullptr;   // 市场 icon/图片 + 包下载
    QString m_pythonExe;                // 缓存的 Python 解释器路径
};

#endif // MARKETTAB_H
