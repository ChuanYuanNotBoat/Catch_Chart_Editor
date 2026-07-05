// NoteChainCurveSampler.cpp — 贝塞尔曲线数学与采样实现

#include "NoteChainCurveSampler.h"

#include <QtMath>
#include <QSet>
#include <algorithm>

namespace NoteChain {

// ---- 基础数学 ----

QPointF NoteChainCurveSampler::cubicPoint(const QPointF &p0, const QPointF &p1,
                                           const QPointF &p2, const QPointF &p3, double t)
{
    double t2 = t * t;
    double t3 = t2 * t;
    double u  = 1.0 - t;
    double u2 = u * u;
    double u3 = u2 * u;

    double x = u3 * p0.x() + 3.0 * u2 * t * p1.x() + 3.0 * u * t2 * p2.x() + t3 * p3.x();
    double y = u3 * p0.y() + 3.0 * u2 * t * p1.y() + 3.0 * u * t2 * p2.y() + t3 * p3.y();
    return QPointF(x, y);
}

double NoteChainCurveSampler::distance(double x1, double y1, double x2, double y2)
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    return qSqrt(dx * dx + dy * dy);
}

double NoteChainCurveSampler::pointToSegmentDistance(double px, double py,
                                                      double x1, double y1,
                                                      double x2, double y2)
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    double lenSq = dx * dx + dy * dy;

    if (lenSq < 1e-12)
        return distance(px, py, x1, y1);

    double t = qBound(0.0, ((px - x1) * dx + (py - y1) * dy) / lenSq, 1.0);
    double projX = x1 + t * dx;
    double projY = y1 + t * dy;
    return distance(px, py, projX, projY);
}

bool NoteChainCurveSampler::pointInRect(double px, double py,
                                         double rx, double ry,
                                         double rw, double rh)
{
    return px >= rx && px <= rx + rw && py >= ry && py <= ry + rh;
}

double NoteChainCurveSampler::clamp(double value, double lo, double hi)
{
    return qBound(lo, value, hi);
}

QRectF NoteChainCurveSampler::makeNormalizedRect(double x1, double y1, double x2, double y2)
{
    double rx = qMin(x1, x2);
    double ry = qMin(y1, y2);
    double rw = qAbs(x2 - x1);
    double rh = qAbs(y2 - y1);
    return QRectF(rx, ry, rw, rh);
}

// ---- 曲线采样 ----

QVector<SampledPoint> NoteChainCurveSampler::sampleSegment(const Anchor &a0, const Anchor &a1,
                                                            const QString &shape,
                                                            int samplesPerSegment)
{
    QVector<SampledPoint> pts;

    bool isPolyline = (shape == QLatin1String(kShapePolyline));
    int steps = qMax(1, samplesPerSegment);

    // p0 = a0.position, p3 = a1.position
    QPointF p0(a0.laneX, a0.beat);
    QPointF p3(a1.laneX, a1.beat);

    if (isPolyline) {
        // 折线：线性插值
        for (int j = 0; j <= steps; ++j) {
            double t = static_cast<double>(j) / steps;
            SampledPoint pt;
            pt.laneX = p0.x() + (p3.x() - p0.x()) * t;
            pt.beat  = p0.y() + (p3.y() - p0.y()) * t;
            pts.append(pt);
        }
    } else {
        // 贝塞尔曲线：a0.out → a1.in 作为控制点
        QPointF p1 = a0.handleOutAbs();
        QPointF p2 = a1.handleInAbs();

        for (int j = 0; j <= steps; ++j) {
            double t = static_cast<double>(j) / steps;
            QPointF cp = cubicPoint(p0, p1, p2, p3, t);
            SampledPoint pt;
            pt.laneX = cp.x();
            pt.beat  = cp.y();
            pts.append(pt);
        }
    }

    return pts;
}

QVector<SampledPoint> NoteChainCurveSampler::sampleCurve(const NoteChainState &state,
                                                          int samplesPerSegment)
{
    QVector<SampledPoint> allPts;
    QMap<int, Anchor> anchorMap = state.anchors();
    QVector<Link> linksList = state.links();

    struct SegInfo {
        int id0, id1;
        Anchor a0, a1;
        QString shape;
    };
    QVector<SegInfo> segments;

    for (const Link &link : linksList) {
        if (!anchorMap.contains(link.fromAnchorId) || !anchorMap.contains(link.toAnchorId))
            continue;
        SegInfo seg;
        seg.id0 = link.fromAnchorId;
        seg.id1 = link.toAnchorId;
        seg.a0 = anchorMap[link.fromAnchorId];
        seg.a1 = anchorMap[link.toAnchorId];
        seg.shape = state.segmentShape(link.fromAnchorId, link.toAnchorId);
        segments.append(seg);
    }

    // 按 beat 时间排序 (P2-4 fix)
    std::sort(segments.begin(), segments.end(), [](const SegInfo &s1, const SegInfo &s2) {
        return s1.a0.beat < s2.a0.beat;
    });

    for (const SegInfo &seg : segments) {
        QVector<SampledPoint> segPts = sampleSegment(seg.a0, seg.a1, seg.shape, samplesPerSegment);
        if (!allPts.isEmpty() && !segPts.isEmpty())
            segPts.removeFirst();
        allPts.append(segPts);
    }

    return allPts;
}

QVector<SampledPoint> NoteChainCurveSampler::generateNotesFromCurve(const NoteChainState &state,
                                                                     int /*samplesPerSegment*/)
{
    QVector<SampledPoint> allPts;
    QMap<int, Anchor> anchorMap = state.anchors();
    QVector<Link> linksList = state.links();

    for (const Link &link : linksList) {
        if (!anchorMap.contains(link.fromAnchorId) || !anchorMap.contains(link.toAnchorId))
            continue;

        Anchor a0 = anchorMap[link.fromAnchorId];
        Anchor a1 = anchorMap[link.toAnchorId];
        QString shape = state.segmentShape(link.fromAnchorId, link.toAnchorId);
        int density = state.segmentDenominator(link.fromAnchorId, link.toAnchorId);
        if (density <= 0)
            density = kDefaultSegmentDenominator;

        // 按 segment density 在 beat 轴上以 1/den 步进插值 (P2-5 fix)
        double beatRange = qAbs(a1.beat - a0.beat);
        int steps = qMax(1, static_cast<int>(beatRange * density));

        for (int j = 0; j <= steps; ++j) {
            double t = static_cast<double>(j) / steps;
            SampledPoint pt;
            if (shape == QLatin1String(kShapePolyline)) {
                pt.laneX = a0.laneX + (a1.laneX - a0.laneX) * t;
                pt.beat = a0.beat + (a1.beat - a0.beat) * t;
            } else {
                QPointF p0(a0.laneX, a0.beat);
                QPointF p1 = a0.handleOutAbs();
                QPointF p2 = a1.handleInAbs();
                QPointF p3(a1.laneX, a1.beat);
                QPointF cp = cubicPoint(p0, p1, p2, p3, t);
                pt.laneX = cp.x();
                pt.beat = cp.y();
            }
            allPts.append(pt);
        }
    }

    if (allPts.isEmpty())
        return {};

    // 按 beat 排序
    std::sort(allPts.begin(), allPts.end(), [](const SampledPoint &a, const SampledPoint &b) {
        if (qAbs(a.beat - b.beat) < 1e-6)
            return a.laneX < b.laneX;
        return a.beat < b.beat;
    });

    // 去重
    QVector<SampledPoint> result;
    const double eps = 1e-6;
    for (const SampledPoint &pt : allPts) {
        if (result.isEmpty()) {
            result.append(pt);
            continue;
        }
        SampledPoint &last = result.last();
        if (qAbs(pt.beat - last.beat) <= eps) {
            last.laneX = (last.laneX + pt.laneX) * 0.5;
        } else {
            result.append(pt);
        }
    }

    return result;
}

} // namespace NoteChain