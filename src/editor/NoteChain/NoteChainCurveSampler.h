#pragma once

// NoteChainMath.h — 数学工具：贝塞尔、距离、triplet、坐标转换、曲线采样
// 基于 Python: time_math.py, batch_commit.py (float_beat_to_triplet), note_chain_assist.py 采样函数

#include "NoteChainCommon.h"
#include <QtMath>
#include <QPointF>
#include <QVariantMap>
#include <algorithm>
#include <numeric>

namespace NoteChain {

// ====== 基础工具 ======
inline double ncClamp(double v, double lo, double hi) { return qBound(lo, v, hi); }
inline double ncDist(double x1, double y1, double x2, double y2) {
    double dx = x2 - x1, dy = y2 - y1; return qSqrt(dx * dx + dy * dy);
}
inline double ncPtSegDist(double px, double py, double x1, double y1, double x2, double y2) {
    double vx = x2 - x1, vy = y2 - y1, wx = px - x1, wy = py - y1;
    double c1 = vx * wx + vy * wy;
    if (c1 <= 0.0) return ncDist(px, py, x1, y1);
    double c2 = vx * vx + vy * vy;
    if (c2 <= 1e-12) return ncDist(px, py, x1, y1);
    double t = ncClamp(c1 / c2, 0.0, 1.0);
    return ncDist(px, py, x1 + t * vx, y1 + t * vy);
}
inline bool ncPtInRect(double px, double py, double rx0, double ry0, double rx1, double ry1) {
    return px >= rx0 && px <= rx1 && py >= ry0 && py <= ry1;
}
inline QPointF ncCubicPt(const QPointF &p0, const QPointF &p1, const QPointF &p2, const QPointF &p3, double t) {
    double u = 1.0 - t, tt = t * t, uu = u * u, uuu = uu * u, ttt = tt * t;
    return QPointF(uuu * p0.x() + 3.0 * uu * t * p1.x() + 3.0 * u * tt * p2.x() + ttt * p3.x(),
                   uuu * p0.y() + 3.0 * uu * t * p1.y() + 3.0 * u * tt * p2.y() + ttt * p3.y());
}
inline QVector<int> floatBeatToTriplet(double beat, int den) {
    den = qMax(1, den); if (beat < 0.0) beat = 0.0;
    int ticks = static_cast<int>(qRound(beat * den)), beatNum = ticks / den, num = ticks % den;
    if (num < 0) { num += den; beatNum -= 1; }
    // Note generation deliberately preserves the requested density
    // denominator.  This matches Python batch_commit.float_beat_to_triplet;
    // sidecar serialization has its own reduced-fraction conversion.
    return {beatNum, num, den};
}
inline double tripletToFloat(const QVector<int> &tri) {
    if (tri.size() < 3 || tri[2] == 0) return 0.0;
    return static_cast<double>(tri[0]) + static_cast<double>(tri[1]) / static_cast<double>(tri[2]);
}
inline QString ncNormalizeShape(const QString &s) {
    return (s.trimmed().toLower() == QLatin1String("polyline")) ? QStringLiteral("polyline") : QStringLiteral("curve");
}
inline QVector<SampledPoint> sampleSegment(const Anchor &a0, const Anchor &a1, const QString &shape, int n) {
    QVector<SampledPoint> pts; n = qMax(1, n);
    bool isPoly = (ncNormalizeShape(shape) == QLatin1String("polyline"));
    for (int j = 0; j <= n; ++j) {
        double t = static_cast<double>(j) / n; SampledPoint pt;
        if (isPoly) { pt.laneX = a0.laneX + (a1.laneX - a0.laneX) * t; pt.beat = a0.beat + (a1.beat - a0.beat) * t; }
        else {
            QPointF cp = ncCubicPt(QPointF(a0.laneX, a0.beat), a0.outAbs(), a1.inAbs(), QPointF(a1.laneX, a1.beat), t);
            pt.laneX = cp.x(); pt.beat = cp.y();
        } pts.append(pt);
    } return pts;
}

inline QVector<SampledPoint> normalizeSamplesByBeat(const QVector<SampledPoint> &samples) {
    QVector<SampledPoint> ordered = samples;
    std::sort(ordered.begin(), ordered.end(), [](const SampledPoint &a, const SampledPoint &b) {
        if (qAbs(a.beat - b.beat) <= 1e-12)
            return a.laneX < b.laneX;
        return a.beat < b.beat;
    });
    QVector<SampledPoint> out;
    out.reserve(ordered.size());
    constexpr double eps = 1e-6;
    for (const SampledPoint &point : ordered) {
        if (out.isEmpty() || qAbs(point.beat - out.last().beat) > eps) {
            out.append(point);
        } else {
            // Python intentionally folds duplicate-beat samples pairwise.
            out.last().laneX = (out.last().laneX + point.laneX) * 0.5;
        }
    }
    return out;
}

inline double laneXAtBeat(const QVector<SampledPoint> &samplesByBeat, double beat) {
    if (samplesByBeat.isEmpty())
        return 0.0;
    if (beat <= samplesByBeat.first().beat)
        return samplesByBeat.first().laneX;
    if (beat >= samplesByBeat.last().beat)
        return samplesByBeat.last().laneX;
    auto upper = std::lower_bound(samplesByBeat.cbegin(), samplesByBeat.cend(), beat,
                                  [](const SampledPoint &point, double value) {
                                      return point.beat < value;
                                  });
    if (upper == samplesByBeat.cbegin())
        return upper->laneX;
    const SampledPoint &right = *upper;
    const SampledPoint &left = *(upper - 1);
    const double span = right.beat - left.beat;
    if (qAbs(span) <= 1e-9)
        return left.laneX;
    const double t = (beat - left.beat) / span;
    return left.laneX + (right.laneX - left.laneX) * t;
}

// ====== CanvasProjection (chart ↔ canvas) ======
struct CanvasProjection {
    double lmargin = 64, rmargin = 64, available = 512, laneW = 512;
    double ch = 800, scrollB = 0, visRange = 8; bool flip = false;
    double lx2x(double lx) const {
        lx = ncClamp(lx, 0.0, qMax(1.0, laneW));
        return lmargin + (lx / qMax(1.0, laneW)) * qMax(1.0, available);
    }
    double b2y(double b) const {
        const double t = (b - scrollB) / qMax(1e-6, visRange);
        return flip ? qMax(1.0, ch) - t * qMax(1.0, ch) : t * qMax(1.0, ch);
    }
    double x2lx(double x) const {
        x = ncClamp(x, lmargin, lmargin + qMax(1.0, available));
        return ((x - lmargin) / qMax(1.0, available)) * qMax(1.0, laneW);
    }
    double y2b(double y) const {
        const double height = qMax(1.0, ch);
        const double t = flip ? (1.0 - y / height) : y / height;
        return scrollB + t * qMax(1e-6, visRange);
    }
    QPointF chartToCanvas(const QPointF &point) const { return {lx2x(point.x()), b2y(point.y())}; }
    QPointF canvasToChart(const QPointF &point) const { return {x2lx(point.x()), y2b(point.y())}; }
    bool containsEditableCanvasPoint(const QPointF &point) const {
        return point.x() >= lmargin && point.x() <= lmargin + qMax(1.0, available)
            && point.y() >= 0.0 && point.y() <= qMax(1.0, ch);
    }
};
} // namespace NoteChain
