#include "tracediffwidget.h"
#include "core/dbcmanager.h"
#include "utils/canutils.h"

#include <QCheckBox>
#include <QLabel>
#include <QTimer>
#include <QTreeWidget>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <algorithm>

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

/// 树列布局
enum { ColItem = 0, ColA, ColB, ColNote, ColCount };

/// 变化高亮（字节不等 / 信号变化 / 关键字段不同）
void highlightChanged(QTreeWidgetItem *item)
{
    const QColor fg(0xD0, 0x20, 0x20);
    const QColor bg(0xFF, 0xE0, 0xE0);
    for (int c = 0; c < ColCount; ++c) {
        item->setForeground(c, fg);
        item->setBackground(c, bg);
    }
}

/// 单字节 Hex（越界显示 "--"）
QString byteHex(const QByteArray &data, int index)
{
    if (index < 0 || index >= data.size())
        return QStringLiteral("--");
    return QStringLiteral("%1")
        .arg(static_cast<quint8>(data.at(index)), 2, 16, QChar('0')).toUpper();
}

} // namespace

// ============================================================
//  TraceDiffWidget
// ============================================================

TraceDiffWidget::TraceDiffWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 顶部工具行：提示 + 仅显示变化项开关
    auto *bar = new QWidget(this);
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(6, 2, 6, 2);
    auto *hint = new QLabel(QStringLiteral("对比选中集合的首帧(A)与末帧(B)"), bar);
    hint->setObjectName(QStringLiteral("DimLabel"));
    barLayout->addWidget(hint);
    barLayout->addStretch(1);
    m_showChangedOnly = new QCheckBox(QStringLiteral("仅显示变化项"), bar);
    m_showChangedOnly->setChecked(true);
    m_showChangedOnly->setToolTip(
        QStringLiteral("信号级对比默认仅列出首末值不同的信号（CANoe 同款行为）"));
    barLayout->addWidget(m_showChangedOnly);
    layout->addWidget(bar);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(ColCount);
    m_tree->setHeaderLabels({QStringLiteral("项目"), QStringLiteral("帧 A（首帧）"),
                             QStringLiteral("帧 B（末帧）"), QStringLiteral("备注")});
    for (int c = 0; c < ColCount - 1; ++c)
        m_tree->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    layout->addWidget(m_tree, 1);

    // 选中变化防抖 100ms 后重算
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(100);
    connect(m_debounce, &QTimer::timeout, this, &TraceDiffWidget::rebuild);

    // 切换"仅显示变化项"立即重算（数据已在 m_pending，无需防抖）
    connect(m_showChangedOnly, &QCheckBox::toggled, this, &TraceDiffWidget::rebuild);
}

void TraceDiffWidget::setFrames(const QVector<CanFrame> &frames)
{
    m_pending = frames;
    m_debounce->start();
}

void TraceDiffWidget::rebuild()
{
    m_tree->clear();

    if (m_pending.size() < 2) {
        auto *hintItem = new QTreeWidgetItem(m_tree, QStringList{
            QStringLiteral("选中 ≥2 帧后对比首帧与末帧（恰好 2 帧即 A/B 对比）")});
        hintItem->setFirstColumnSpanned(true);
        hintItem->setDisabled(true);
        return;
    }

    // 首帧/末帧按时间排序确定（与选中顺序无关）
    QVector<CanFrame> frames = m_pending;
    std::sort(frames.begin(), frames.end(),
              [](const CanFrame &a, const CanFrame &b) { return a.timestamp < b.timestamp; });
    const CanFrame &fa = frames.first();
    const CanFrame &fb = frames.last();

    auto makeGroup = [this](const QString &title) {
        auto *item = new QTreeWidgetItem(m_tree, QStringList{title});
        QFont f = item->font(0);
        f.setBold(true);
        item->setFont(0, f);
        item->setFirstColumnSpanned(true);
        return item;
    };

    // ---- 概览 ----
    auto *ovGroup = makeGroup(QStringLiteral("概览"));
    {
        auto frameText = [](const CanFrame &f) {
            return QStringLiteral("%1  %2  DLC %3")
                .arg(CanUtils::formatTime(f.timestamp))
                .arg(CanUtils::formatId(f.id, f.extended))
                .arg(CanUtils::formatDlc(f.dlc, f.fd));
        };
        new QTreeWidgetItem(ovGroup, {QStringLiteral("帧 A"), frameText(fa), QString(), QString()});
        new QTreeWidgetItem(ovGroup, {QStringLiteral("帧 B"), QString(), frameText(fb), QString()});

        // 报文名（如有 DBC）
        if (m_dbcMgr) {
            const DbcMessage *ma = m_dbcMgr->findMessage(fa.id);
            const DbcMessage *mb = m_dbcMgr->findMessage(fb.id);
            if (ma || mb) {
                new QTreeWidgetItem(ovGroup, {QStringLiteral("报文名"),
                    ma ? ma->name : QStringLiteral("-"),
                    mb ? mb->name : QStringLiteral("-"), QString()});
            }
        }

        new QTreeWidgetItem(ovGroup, {QStringLiteral("首末 Δt"),
            fmtDuration(fb.timestamp - fa.timestamp), QString(), QString()});

        auto *rowId = new QTreeWidgetItem(ovGroup, {QStringLiteral("CAN ID"),
            fa.id == fb.id ? QStringLiteral("相同") : QStringLiteral("不同"),
            fa.id == fb.id ? QString() : QStringLiteral("信号级对比跳过"), QString()});
        if (fa.id != fb.id)
            highlightChanged(rowId);

        if (fa.dlc != fb.dlc || fa.data.size() != fb.data.size()) {
            auto *rowDlc = new QTreeWidgetItem(ovGroup, {QStringLiteral("DLC"),
                QString::number(fa.dlc), QString::number(fb.dlc),
                QStringLiteral("长度不同")});
            highlightChanged(rowDlc);
        } else {
            new QTreeWidgetItem(ovGroup, {QStringLiteral("DLC"),
                QString::number(fa.dlc), QString::number(fb.dlc), QString()});
        }
    }

    // ---- 字节级对比 ----
    {
        const int maxLen = qMax(fa.data.size(), fb.data.size());
        int diffBytes = 0;
        auto *byteGroup = makeGroup(QStringLiteral("字节级对比"));
        if (maxLen == 0) {
            auto *row = new QTreeWidgetItem(byteGroup,
                {QStringLiteral("(无数据)"), QString(), QString(), QString()});
            row->setDisabled(true);
        }
        for (int i = 0; i < maxLen; ++i) {
            const QString va = byteHex(fa.data, i);
            const QString vb = byteHex(fb.data, i);
            auto *row = new QTreeWidgetItem(byteGroup,
                {QStringLiteral("字节 %1").arg(i), va, vb, QString()});
            if (va != vb) {
                highlightChanged(row);
                row->setText(ColNote, QStringLiteral("≠"));
                ++diffBytes;
            }
        }
        byteGroup->setText(0, QStringLiteral("字节级对比（%1/%2 字节不同）")
                               .arg(diffBytes).arg(maxLen));
    }

    // ---- 信号级对比 ----
    {
        auto *sigGroup = makeGroup(QStringLiteral("信号级对比"));
        if (!m_dbcMgr) {
            auto *row = new QTreeWidgetItem(sigGroup,
                {QStringLiteral("未加载 DBC，跳过信号级对比"), QString(), QString(), QString()});
            row->setDisabled(true);
        } else if (fa.id != fb.id) {
            auto *row = new QTreeWidgetItem(sigGroup,
                {QStringLiteral("首帧与末帧 CAN ID 不同，跳过信号级对比"),
                 QString(), QString(), QString()});
            row->setDisabled(true);
        } else {
            const auto da = m_dbcMgr->decodeFrame(fa.id, fa.data);
            const auto db = m_dbcMgr->decodeFrame(fb.id, fb.data);

            // 按信号名合并两侧解码结果（A 顺序优先，B 独有信号追加）
            struct SigRow {
                QString name;
                bool hasA = false, hasB = false;
                double vA = 0.0, vB = 0.0;
                QString unit, noteA, noteB;
            };
            QVector<SigRow> rows;
            QHash<QString, int> pos;
            auto ensure = [&](const QString &name) -> SigRow & {
                const auto it = pos.constFind(name);
                if (it != pos.constEnd())
                    return rows[it.value()];
                pos.insert(name, rows.size());
                SigRow r;
                r.name = name;
                rows.append(r);
                return rows.last();
            };
            for (const auto &ds : da) {
                SigRow &r = ensure(ds.name);
                r.hasA = true;
                r.vA = ds.physValue;
                if (!ds.unit.isEmpty())
                    r.unit = ds.unit;
                r.noteA = ds.valueDesc;
            }
            for (const auto &ds : db) {
                SigRow &r = ensure(ds.name);
                r.hasB = true;
                r.vB = ds.physValue;
                if (!ds.unit.isEmpty())
                    r.unit = ds.unit;
                r.noteB = ds.valueDesc;
            }

            int changedSignals = 0;
            int shownRows = 0;
            for (const auto &r : rows) {
                const bool changed = r.hasA && r.hasB && !qFuzzyCompare(r.vA, r.vB);
                if (changed)
                    ++changedSignals;
                if (m_showChangedOnly->isChecked() && !changed)
                    continue;
                QString colA = r.hasA ? fmtVal(r.vA) : QStringLiteral("-");
                QString colB = r.hasB ? fmtVal(r.vB) : QStringLiteral("-");
                if (!r.unit.isEmpty()) {
                    if (r.hasA)
                        colA += QLatin1Char(' ') + r.unit;
                    if (r.hasB)
                        colB += QLatin1Char(' ') + r.unit;
                }
                QString note = changed ? QStringLiteral("变化")
                                       : (!r.noteB.isEmpty() ? r.noteB : r.noteA);
                auto *row = new QTreeWidgetItem(sigGroup,
                    {r.name, colA, colB, note});
                if (changed)
                    highlightChanged(row);
                else if (!r.hasA || !r.hasB)
                    row->setDisabled(true);   // 仅单侧存在的信号
                ++shownRows;
            }
            if (rows.isEmpty()) {
                auto *row = new QTreeWidgetItem(sigGroup,
                    {QStringLiteral("报文未在 DBC 中定义"), QString(), QString(), QString()});
                row->setDisabled(true);
            } else if (shownRows == 0) {
                auto *row = new QTreeWidgetItem(sigGroup,
                    {QStringLiteral("无变化项（%1 项全部相同）").arg(rows.size()),
                     QString(), QString(), QString()});
                row->setDisabled(true);
            }
            sigGroup->setText(0, QStringLiteral("信号级对比（%1/%2 项变化）")
                                  .arg(changedSignals).arg(rows.size()));
        }
    }

    m_tree->expandAll();
}
