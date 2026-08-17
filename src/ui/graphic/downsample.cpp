#include "downsample.h"

#include <algorithm>

namespace graphic {

/**
 * @brief 二分查找第一个 t >= key 的索引，找不到返回 n
 */
static int lowerBound(const RingBuffer<Sample> &raw, double key)
{
    int lo = 0, hi = raw.size();
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (raw.at(mid).t < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

QVector<Sample> downsample(const RingBuffer<Sample> &raw,
                           double t1, double t2,
                           int targetPoints,
                           Strategy strategy)
{
    QVector<Sample> out;
    const int n = raw.size();
    if (n == 0 || t2 <= t1)
        return out;

    // 定位 [t1, t2] 区间：[first, last) 为严格落在区间内的点
    int first = lowerBound(raw, t1);
    int last = lowerBound(raw, t2);  // 第一个 t >= t2

    // 区间外两侧各外延 1 点，保证阶梯线边缘（跳变沿）渲染正确
    if (first > 0)
        first--;

    if (last < n - 1)
        last++;  // 含 t2 之后的第 1 个点
    // last 为开区间边界：处理 [first, last)
    const int inRange = last - first;
    if (inRange <= 0)
        return out;

    // 数据量本身不多 → 原样透传
    if (inRange <= targetPoints || targetPoints < 2) {
        out.reserve(inRange);
        for (int i = first; i < last; ++i)
            out.append(raw.at(i));
        return out;
    }

    switch (strategy) {
    case Strategy::MinMax: {
        // 每桶保留 min/max 两点（保轮廓）
        const int bucketCount = std::max(1, targetPoints / POINTS_PER_PIXEL);
        const double bucketSpan = (t2 - t1) / bucketCount;
        out.reserve(bucketCount * 2 + 2);
        int i = first;
        for (int b = 0; b < bucketCount && i < last; ++b) {
            const double bStart = t1 + b * bucketSpan;
            const double bEnd = bStart + bucketSpan;
            double minV = 0, maxV = 0, minT = 0, maxT = 0;
            bool has = false;
            // 跳过桶前的外延点（属于上一个桶区间）
            while (i < last && raw.at(i).t < bStart)
                ++i;
            while (i < last && raw.at(i).t < bEnd) {
                const Sample &s = raw.at(i);
                if (!has) {
                    minV = maxV = s.v;
                    minT = maxT = s.t;
                    has = true;
                } else {
                    if (s.v < minV) { minV = s.v; minT = s.t; }
                    if (s.v > maxV) { maxV = s.v; maxT = s.t; }
                }
                ++i;
            }
            if (!has)
                continue;
            if (minT <= maxT) {
                out.append({minT, minV});
                if (maxT > minT)
                    out.append({maxT, maxV});
            } else {
                out.append({maxT, maxV});
                out.append({minT, minV});
            }
        }
        break;
    }
    case Strategy::Avg: {
        const int bucketCount = std::max(1, targetPoints / POINTS_PER_PIXEL);
        const double bucketSpan = (t2 - t1) / bucketCount;
        out.reserve(bucketCount + 2);
        int i = first;
        for (int b = 0; b < bucketCount && i < last; ++b) {
            const double bStart = t1 + b * bucketSpan;
            const double bEnd = bStart + bucketSpan;
            double sum = 0;
            int cnt = 0;
            double firstT = 0;
            while (i < last && raw.at(i).t < bStart)
                ++i;
            while (i < last && raw.at(i).t < bEnd) {
                const Sample &s = raw.at(i);
                if (cnt == 0)
                    firstT = s.t;
                sum += s.v;
                ++cnt;
                ++i;
            }
            if (cnt > 0)
                out.append({firstT, sum / cnt});
        }
        break;
    }
    case Strategy::First: {
        const int bucketCount = std::max(1, targetPoints / POINTS_PER_PIXEL);
        const double bucketSpan = (t2 - t1) / bucketCount;
        out.reserve(bucketCount + 2);
        int i = first;
        for (int b = 0; b < bucketCount && i < last; ++b) {
            const double bStart = t1 + b * bucketSpan;
            const double bEnd = bStart + bucketSpan;
            while (i < last && raw.at(i).t < bStart)
                ++i;
            if (i < last && raw.at(i).t < bEnd)
                out.append(raw.at(i));
            while (i < last && raw.at(i).t < bEnd)
                ++i;
        }
        break;
    }
    case Strategy::Decimate: {
        // 每 N 取 1
        const int stride = std::max(1, inRange / targetPoints);
        out.reserve(inRange / stride + 2);
        for (int i = first; i < last; i += stride)
            out.append(raw.at(i));
        // 保证最后一个点（最新数据）不丢
        if (out.isEmpty() || out.last().t != raw.at(last - 1).t)
            out.append(raw.at(last - 1));
        break;
    }
    }

    // 兜底：保证两个外延端点存在（第一个桶可能整体为空导致首点缺失）
    if (!out.isEmpty()) {
        const Sample &head = raw.at(first);
        const Sample &tail = raw.at(last - 1);
        if (out.first().t != head.t)
            out.prepend(head);
        if (out.last().t != tail.t)
            out.append(tail);
    }
    return out;
}

} // namespace graphic
