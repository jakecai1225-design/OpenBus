#ifndef MARKETINDEX_H
#define MARKETINDEX_H

#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * @brief 统一插件市场索引 — market.json schema 2（方案 §13.4）的加载与检索
 *
 * v2 结构：drivers[] 按驱动聚合（内嵌 devices 简表 + 图文/说明/关键词），
 *          plugins[] 为 Python 插件（.opk 下载描述）；顶层 devices[] 退役。
 *
 * 数据源：
 *   - 开发/演示：<源码根>/build/market/market.json（scripts/make_market.py 生成）
 *   - 发布：OSS 静态托管 market.json（URL 可配置，字段结构一致）
 *
 * 列表过滤（驱动/插件名称、厂商、关键词等多词 AND 匹配）由 UI 层使用
 * matchWords() 完成，无需服务端支持。
 */
class MarketIndex : public QObject
{
    Q_OBJECT
public:
    /// 市场驱动条目（.odp 下载描述 + 聚合图文/设备简表）
    struct DriverInfo {
        QString id;
        QString name;
        QString vendor;
        QString version;
        QString package;     ///< 相对 market base 的 .odp 路径
        QString sha256;
        qint64 size = 0;
        QString license;
        QString updatedAt;
        QString minAppVersion;
        QString abiVersion;

        // ---- v2 聚合扩展 ----
        QString icon;        ///< 列表/详情图标（相对 base，可空）
        QString image;       ///< 详情代表图（相对 base，可空）
        QString summary;     ///< 一句话摘要
        QString readme;      ///< Markdown 说明（内联）
        QString keywords;    ///< 检索关键词（空格分隔）
        QJsonArray devices;  ///< 设备简表 [{model,channels,canFd,maxBaud,timestamp[,summary]}]
    };

    /// 市场插件条目（.opk 下载描述）
    struct PluginInfo {
        QString id;
        QString name;        ///< 显示名（中文标题）
        QString publisher;
        QString version;
        QString description; ///< 一句话描述
        QString icon;        ///< 相对 base，可空
        QString package;     ///< 相对 market base 的 .opk 路径
        QString sha256;
        qint64 size = 0;
        QString readme;      ///< Markdown 说明（内联）
        QStringList tags;
        QString keywords;    ///< 检索关键词（空格分隔）
        QString minAppVersion;
        QString updatedAt;
    };

    static MarketIndex *instance();

    /// 拉取并解析 market.json（异步；完成后 emit loaded）
    void refresh();

    bool isLoaded() const { return m_loaded; }
    QString lastError() const { return m_lastError; }
    QString updated() const { return m_updated; }

    QList<DriverInfo> drivers() const { return m_drivers; }
    QList<PluginInfo> plugins() const { return m_plugins; }
    /// id → 市场条目（无则返回空 id 的条目）
    DriverInfo driverById(const QString &id) const;
    PluginInfo pluginById(const QString &id) const;

    /// 当前市场索引地址（market.json）
    QUrl marketUrl() const { return m_marketUrl; }
    /// 切换市场源（设置项 market.url；空串 → 恢复自动定位）
    /// 注：不持久化，由调用方（MarketTab）写 AppConfig 并 save()
    void setMarketUrl(const QUrl &url) { m_marketUrl = url; }

    /// 相对路径（package/icon/image）→ 绝对 URL（基于 market base）
    QUrl resolveUrl(const QString &relative) const;

    /// 多词 AND 文本匹配（空词/空文本返回 true；不区分大小写）— 列表过滤工具
    static bool matchWords(const QString &text, const QStringList &fields);

    /// 市场源定位：build/market（开发）→ market/（发布布局）→ 官方占位 URL
    static QUrl defaultMarketUrl();

signals:
    void loaded(bool ok, const QString &error);

private:
    explicit MarketIndex(QObject *parent = nullptr);
    void onReplyFinished(QNetworkReply *reply);

    QNetworkAccessManager *m_nam = nullptr;
    QUrl m_marketUrl;              ///< market.json 地址
    QString m_base;                ///< 资源基址（market.json base 字段）
    QString m_updated;
    QList<DriverInfo> m_drivers;
    QList<PluginInfo> m_plugins;
    bool m_loaded = false;
    QString m_lastError;
};

#endif // MARKETINDEX_H
