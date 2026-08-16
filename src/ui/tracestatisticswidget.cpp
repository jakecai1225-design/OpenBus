#include "tracestatisticswidget.h"
#include "core/dbcmanager.h"

#include <QLabel>
#include <QTimer>
#include <QTreeWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHash>

#include <algorithm>
#include <cmath>

namespace {

/// 数值格式化 — 与信号解析面板一致（3 位小数）
QString fmtVal(double v)
{
    return QString::number(v, 'f', 3);
}

/// 时长格式化 — 自动选择 µs / ms / s 单位
QString fmtDuration(double seconds)
{
    const double v = std::abs(seconds);
    if (v < 1e-6)
        return QStringLiteral("0 µs");
    if (v < 1e-3)
        return QStringLiteral("%1 µs").arg(seconds * 1e6, 0, 'f', 1);
    if (v < 1.0)
        return QStringLiteral("%1 ms").arg(seconds * 1e3, 0, 'f', 3);
    return QStringLiteral("%1 s").arg(seconds, 0, 'f', 6);
}

/// 增量统计（min/max/avg/σ/首末值）
struct Stat {
    double min = 0.0, max = 0.0;
    double sum = 0.0, sumSq = 0.0;
    double first = 0.0, last = 0.0;
    int count = 0;

    void add(double v)
    {
        if (count == 0) {
            min = max = first = v;
        } else {
            min = std::min(min, v);
            max = std::max(max, v);
        }
        sum += v;
        sumSq += v * v;
        last = v;
        ++count;
    }

    double avg() const { return count ? sum / count : 0.0; }

    /// 样本标准差（n-1）；n<2 时无意义返回 0
    double sigma() const
    {
        if (count < 2)
            return 0.0;
        const double var = (sumSq - sum * sum / count) / (count - 1);
        return std::sqrt(std::max(0.0, var));
    }
};

/// 树列布局
enum { ColItem = 0, ColMin, ColMax, ColAvg, ColSigma, ColFirst, ColLast, ColNote, ColCount };

} // namespace

// ============================================================
//  TraceStatisticsWidget
// ============================================================

TraceStatisticsWidget::TraceStatisticsWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_summary = new QLabel(QStringLiteral("未选中帧 — 在 Trace 列表中选择帧后显示统计"), this);
    m_summary->setContentsMargins(6, 3, 6, 3);
    m_summary->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(m_summary);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(ColCount);
    m_tree->setHeaderLabels({
        QStringLiteral("项目"), QStringLiteral("最小"), QStringLiteral("最大"),
        QStringLiteral("平均"), QStringLiteral("σ 标准差"), QStringLiteral("首值"),
        QStringLiteral("末值"), QStringLiteral("单位/备注") });
    for (int c = 0; c < ColCount - 1; ++c)
        m_tree->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    layout->addWidget(m_tree, 1);

    // 选中变化防抖 100ms 后重算（G-F1 约定）
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(100);
    connect(m_debounce, &QTimer::timeout, this, &TraceStatisticsWidget::rebuild);
}

void TraceStatisticsWidget::setFrames(const QVector<CanFrame> &frames, bool truncated)
{
    m_pending = frames;
    m_pendingTruncated = truncated;
    m_debounce->start();
}

void TraceStatisticsWidget::rebuild()
{
    m_tree->clear();

    const int n = m_pending.size();
    if (n == 0) {
        m_summary->setText(QStringLiteral("未选中帧 — 在 Trace 列表中选择帧后显示统计"));
        return;
    }

    // 选中顺序与时间顺序无关 — 统一按时间排序
    QVector<CanFrame> frames = m_pending;
    std::sort(frames.begin(), frames.end(),
              [](const CanFrame &a, const CanFrame &b) { return a.timestamp < b.timestamp; });

    // ---- 摘要 ----
    QString sum = QStringLiteral("已选 %1 帧").arg(n);
    if (m_pendingTruncated)
        sum += QStringLiteral("（超出上限，已截断至 %1 帧）").arg(MaxFrames);
    m_summary->setText(sum);

    // ---- 通用构造 ----
    auto makeGroup = [this](const QString &title) {
        auto *item = new QTreeWidgetItem(m_tree, QStringList{title});
        QFont f = item->font(0);
        f.setBold(true);
        item->setFont(0, f);
        item->setFirstColumnSpanned(true);
        return item;
    };
    auto addRow = [](QTreeWidgetItem *group, const QString &name, const QStringList &vals) {
        QStringList cols;
        cols << name;
        for (int i = 0; i + 1 < ColCount; ++i)
            cols << (i < vals.size() ? vals.at(i) : QString());
        new QTreeWidgetItem(group, cols);
    };

    // ---- 时间组 ----
    auto *timeGroup = makeGroup(QStringLiteral("时间统计"));
    {
        const double t0 = frames.first().timestamp;
        const double t1 = frames.last().timestamp;

        addRow(timeGroup, QStringLiteral("帧数"), {QString::number(n)});
        addRow(timeGroup, QStringLiteral("时间跨度"), {fmtDuration(t1 - t0)});

        if (n >= 2) {
            Stat dt;
            for (int i = 1; i < n; ++i)
                dt.add(frames.at(i).timestamp - frames.at(i - 1).timestamp);
            addRow(timeGroup, QStringLiteral("相邻帧 Δt"),
                   {fmtDuration(dt.min), fmtDuration(dt.max), fmtDuration(dt.avg()),
                    fmtDuration(dt.sigma()), QString(), QString(), QStringLiteral("s")});
        }
    }

    // ---- 信号组（DBC 解码）----
    QHash<QString, Stat> sigStats;
    QHash<QString, QString> sigLabel;   // key → "报文名.信号名"
    QHash<QString, QString> sigUnit;
    if (m_dbcMgr) {
        for (const auto &f : frames) {
            const auto decoded = m_dbcMgr->decodeFrame(f.id, f.data);
            if (decoded.isEmpty())
                continue;
            const DbcMessage *msg = m_dbcMgr->findMessage(f.id);
            const QString msgName = msg ? msg->name
                                        : QStringLiteral("0x%1").arg(f.id, 0, 16).toUpper();
            for (const auto &ds : decoded) {
                const QString key = QStringLiteral("%1|%2").arg(f.id).arg(ds.name);
                sigStats[key].add(ds.physValue);
                if (!sigLabel.contains(key))
                    sigLabel.insert(key, msgName + QLatin1Char('.') + ds.name);
                if (!ds.unit.isEmpty() && !sigUnit.contains(key))
                    sigUnit.insert(key, ds.unit);
            }
        }
    }

    if (!sigStats.isEmpty()) {
        auto *sigGroup = makeGroup(QStringLiteral("信号统计（DBC 解码）"));
        QList<QString> keys = sigStats.keys();
        std::sort(keys.begin(), keys.end(), [&sigLabel](const QString &a, const QString &b) {
            return sigLabel.value(a) < sigLabel.value(b);
        });
        for (const QString &key : keys) {
            const Stat &st = sigStats.value(key);
            addRow(sigGroup, sigLabel.value(key),
                   {fmtVal(st.min), fmtVal(st.max), fmtVal(st.avg()), fmtVal(st.sigma()),
                    fmtVal(st.first), fmtVal(st.last), sigUnit.value(key)});
        }
    } else {
        // 无 DBC（或选中帧无 DBC 定义）— 按字节位置统计
        int maxLen = 0;
        for (const auto &f : frames)
            maxLen = qMax(maxLen, f.data.size());
        auto *byteGroup = makeGroup(QStringLiteral("字节统计（未加载 DBC 或报文未定义）"));
        for (int i = 0; i < maxLen; ++i) {
            Stat st;
            for (const auto &f : frames)
                if (i < f.data.size())
                    st.add(static_cast<quint8>(f.data.at(i)));
            // 字节值本身为整数 — min/max/首末用整数格式，avg/σ 保留小数
            addRow(byteGroup, QStringLiteral("字节 %1").arg(i),
                   {QString::number(int(st.min)), QString::number(int(st.max)),
                    fmtVal(st.avg()), fmtVal(st.sigma()),
                    QString::number(int(st.first)), QString::number(int(st.last)), QString()});
        }
    }

    m_tree->expandAll();
}
