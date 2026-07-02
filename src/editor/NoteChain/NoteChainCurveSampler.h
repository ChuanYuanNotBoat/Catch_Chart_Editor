#pragma once

// NoteChainCurveSampler.h — 贝塞尔曲线数学 + 曲线采样 → 生成 Note 列表
// 对应 Python modular/math/bezier.py、modular/math/geometry.py、modular/sampling/curve_sampler.py

#include "NoteChainState.h"

#include <QVector>
#include <QPointF>
#include <QRectF>

namespace NoteChain {

struct SampledPoint
{
    double laneX = 0.0; // 轨道位置
    double beat  = 0.0; // 节拍时间
};

class NoteChainCurveSampler
{
public:
    /// 三次贝塞尔曲线点 (0 ≤ t ≤ 1)
    static QPointF cubicPoint(const QPointF &p0, const QPointF &p1,
                              const QPointF &p2, const QPointF &p3, double t);

    /// 两点距离
    static double distance(double x1, double y1, double x2, double y2);

    /// 点到线段的距离
    static double pointToSegmentDistance(double px, double py,
                                         double x1, double y1,
                                         double x2, double y2);

    /// 点是否在矩形内
    static bool pointInRect(double px, double py,
                            double rx, double ry,
                            double rw, double rh);

    /// 钳制值
    static double clamp(double value, double lo, double hi);

    /// 从 State 采样整条曲线的采样点列表
    /// @param state 曲线状态
    /// @param samplesPerSegment 每段采样点数
    static QVector<SampledPoint> sampleCurve(const NoteChainState &state,
                                             int samplesPerSegment = 24);

    /// 采样单段曲线
    static QVector<SampledPoint> sampleSegment(const Anchor &a0, const Anchor &a1,
                                               const QString &shape = QString(),
                                               int samplesPerSegment = 24);

    /// 从曲线生成 Note 列表（按时间排序，去重）
    /// 返回 (laneX, beat) 列表，可用于生成 CatchNote
    static QVector<SampledPoint> generateNotesFromCurve(const NoteChainState &state,
                                                        int samplesPerSegment = 24);

    /// 矩形碰撞检测（用于框选）
    static QRectF makeNormalizedRect(double x1, double y1, double x2, double y2);
};

} // namespace NoteChain