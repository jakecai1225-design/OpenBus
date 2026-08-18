#ifndef MARKETINDEX_H
#define MARKETINDEX_H

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * @brief 设备市场索引 — market.json 双索引（drivers + devices）的加载与检索
 *
 * 数据源（doc/驱动系统方案.md §8.1）：
 *   - 开发/演示：<源码根>/build/market/market.json（scripts/make_market.py 生成）
 *   - 发布：OSS 静态托管 market.json（URL 可配置，字段结构一致）
 *
 * 搜索：型号 / 厂商 / 摘要 / 标签 / keywords 客户端匹配（多词 AND，
 * 不区分大小写），无需服务端支持。
 */
class MarketIndex : public QObject
{
    Q_OBJECT
public:
    /// 市场驱动条目（.odp 下载描述）
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
    };

    /// 市场设备条目（图文详情数据）
    struct DeviceInfo {
        QString driverId;
        QString model;
        QString vendor;
        int type = 0;
        QString summary;
        QStringList tags;
        QStringList images;  ///< 相对 market base 的图片路径（首图为主图）
        QString intro;       ///< Markdown 介绍
        QJsonObject specs;   ///< 规格表（channels/canFd/maxBaud/timestamp/interface/power）
        QString keywords;    ///< 检索补充关键词
    };

    static MarketIndex *instance();

    /// 拉取并解析 market.json（异步；完成后 emit loaded）
    void refresh();

    bool isLoaded() const { return m_loaded; }
    QString lastError() const { return m_lastError; }
    QString updated() const { return m_updated; }

    QList<DeviceInfo> devices() const { return m_devices; }
    /// 多词 AND 检索（型号/厂商/摘要/标签/keywords，不区分大小写；空词返回全部）
    QList<DeviceInfo> search(const QString &text) const;
    /// driverId → 市场驱动条目（无则返回空 id 的条目）
    DriverInfo driverById(const QString &id) const;

    /// 相对路径（package/images）→ 绝对 URL（基于 market base）
    QUrl resolveUrl(const QString &relative) const;

    /// 市场源定位：build/market（开发）→ market/（发布布局）→ 官方占位 URL
    static QUrl defaultMarketUrl();

signals:
    void loaded(bool ok, const QString &error);

private:
    explicit MarketIndex(QObject *parent = nullptr);
    void setMarketUrl(const QUrl &url) { m_marketUrl = url; }
    void onReplyFinished(QNetworkReply *reply);

    QNetworkAccessManager *m_nam = nullptr;
    QUrl m_marketUrl;              ///< market.json 地址
    QString m_base;                ///< 资源基址（market.json base 字段）
    QString m_updated;
    QList<DriverInfo> m_drivers;
    QList<DeviceInfo> m_devices;
    bool m_loaded = false;
    QString m_lastError;
};

#endif // MARKETINDEX_H
