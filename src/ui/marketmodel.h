#ifndef MARKETMODEL_H
#define MARKETMODEL_H

#include <QFrame>
#include <QMetaType>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <functional>

class QLabel;

/// 统一市场条目标识（四类：已装驱动/已装插件/市场驱动/市场插件）
struct MarketItem {
    enum Kind { InstalledDriver, InstalledPlugin, MarketDriver, MarketPlugin };
    Kind kind = MarketDriver;
    QString id;   ///< driverId 或插件名

    bool operator==(const MarketItem &o) const { return kind == o.kind && id == o.id; }
};

/// 允许 MarketItem 经 QVariant 跨模块接口传递（IBusinessModule::invoke）
Q_DECLARE_METATYPE(MarketItem)

/// 市场列表行组件（VS Code 扩展列表风格）。
/// 无 Q_OBJECT（用动态属性 marketRow 标记，选中态刷新以 property 识别）。
class FrameRow : public QFrame {
public:
    explicit FrameRow(QWidget *parent = nullptr) : QFrame(parent)
    {
        setObjectName(QStringLiteral("marketRow"));
        setProperty("marketRow", true);
    }
    using ClickCb = std::function<void()>;
    void setOnClick(ClickCb cb) { m_cb = std::move(cb); }
    void setSelected(bool sel)
    {
        setStyleSheet(sel
            ? QStringLiteral("QFrame#marketRow { background-color: rgba(86,156,214,0.22);"
                             " border-radius: 4px; }")
            : QStringLiteral("QFrame#marketRow { background-color: transparent; }"));
    }

    MarketItem item;
    QLabel *iconLabel = nullptr;

protected:
    void mousePressEvent(QMouseEvent *) override { if (m_cb) m_cb(); }

private:
    ClickCb m_cb;
};

/// 行条目聚合数据（市场页列表与侧边栏迷你市场共用，方案 §13.10）
struct MarketEntryData {
    MarketItem item;
    QString title;          ///< 行标题（驱动 displayName / 插件名）
    QString meta;           ///< 行元信息（"v1.0 · ZLG · 可用" / 摘要）
    QString status;         ///< 右侧状态词（空 = 不显示）
    QString marketIcon;     ///< 市场索引 icon 相对路径（可空；已装插件优先本地图标）
    QStringList searchFields;   ///< 多词 AND 搜索匹配字段
};

/// 统一市场数据聚合（DriverRegistry / PluginManager / MarketIndex → 行条目）
namespace MarketModel {

QVector<MarketEntryData> collectInstalledDrivers();
QVector<MarketEntryData> collectInstalledPlugins();
QVector<MarketEntryData> collectMarketDrivers();
QVector<MarketEntryData> collectMarketPlugins();

/// 已装插件本地图标（iconFilePath），无则返回空 QPixmap
QPixmap pluginIconLocal(const QString &name);

/// 市场图标/图片异步加载（磁盘缓存 AppData/market-cache；回调守卫由调用方处理）
void fetchMarketPixmap(const QUrl &url, const std::function<void(const QPixmap &)> &cb);

} // namespace MarketModel

/// 插件图标绘制（本地图片加载 + 按名称着色首字母头像兜底）
namespace PluginUi {
QPixmap pluginIconPixmap(const QString &iconPath, const QString &name, int size);
}

#endif // MARKETMODEL_H
