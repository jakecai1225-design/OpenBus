#ifndef GRAPHIC_DOWNSAMPLE_H
#define GRAPHIC_DOWNSAMPLE_H

#include <QVector>
#include "utils/ringbuffer.h"

/**
 * @brief 视口降采样 — O(视口宽) 恒定渲染成本
 *
 * 原始数据 (百万点) → 视口范围 [t1,t2] 每像素列保留 min/max → 显示数据 (≈ 2×像素宽)
 * 波形轮廓无损；卡尺测量始终对原始数据插值，不受降采样影响。
 */
namespace graphic {

/// 采样点（时间 + 值）
struct Sample {
    double t = 0.0;
    double v = 0.0;
};

/// 降采样策略
enum class Strategy {
    MinMax,   ///< 每桶保留 min/max 两点（保轮廓，推荐）
    Avg,      ///< 每桶平均
    First,    ///< 每桶首点
    Decimate  ///< 每 N 取 1
};

/// 每像素目标点数（MinMax 策略下每桶 2 点）
constexpr int POINTS_PER_PIXEL = 2;

/**
 * 对环形缓冲 [t1,t2] 区间降采样
 * @param raw 原始数据（按 t 升序）
 * @param t1/t2 视口时间范围
 * @param targetPoints 目标点数（≈ 2 × 视口像素宽）
 * @param strategy 抽稀策略
 * @return 显示数据（含区间两侧各 1 个外延点，保证阶梯线边缘渲染正确）
 */
QVector<Sample> downsample(const RingBuffer<Sample> &raw,
                           double t1, double t2,
                           int targetPoints,
                           Strategy strategy = Strategy::MinMax);

} // namespace graphic

#endif // GRAPHIC_DOWNSAMPLE_H
