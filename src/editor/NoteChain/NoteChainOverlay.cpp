// NoteChainOverlay.cpp — Canvas 叠加层渲染实现

#include "NoteChainOverlay.h"

#include <QtMath>

namespace NoteChain {

void NoteChainOverlay::render(QPainter *painter, const NoteChainState &state,
                               double /*scrollBeat*/, double /*visibleBeatRange*/,
                               const ProjectX &projX, const ProjectY &projY)
{
    // 绘制曲线段
    QMap<int, Anchor> anchorMap = state.anchors();
    QVector<Link> linksList = state.links();

    for (const Link &link : linksList) {
        if (!anchorMap.contains(link.fromAnchorId) || !anchorMap.contains(link.toAnchorId))
            continue;
        Anchor a0 = anchorMap[link.fromAnchorId];
        Anchor a1 = anchorMap[link.toAnchorId];
        QString shape = state.segmentShape(link.fromAnchorId, link.toAnchorId);
        drawCurveSegment(painter, a0, a1, shape, projX, projY);
    }

    // 绘制锚点和控制柄
    for (auto it = anchorMap.begin(); it != anchorMap.end(); ++it) {
        const Anchor &anchor = it.value();
        bool selected = state.isSelected(anchor.id);
        drawAnchor(painter, anchor, selected, projX, projY);
        drawHandles(painter, anchor, selected, projX, projY);
    }
}

void NoteChainOverlay::drawAnchor(QPainter *painter, const Anchor &anchor, bool selected,
                                   const ProjectX &projX, const ProjectY &projY)
{
    QColor color = selected ? anchorSelectedColor() : anchorColor();
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    double cx = projX(anchor.laneX);
    double cy = projY(anchor.beat);
    painter->drawEllipse(QPointF(cx, cy), kAnchorRadius, kAnchorRadius);
}

void NoteChainOverlay::drawHandles(QPainter *painter, const Anchor &anchor, bool selected,
                                    const ProjectX &projX, const ProjectY &projY)
{
    double ax = projX(anchor.laneX);
    double ay = projY(anchor.beat);
    QPointF anchorPos(ax, ay);

    double inLane = anchor.laneX + anchor.handleInDx;
    double inBeat = anchor.beat + anchor.handleInDy;
    QPointF inAbs(projX(inLane), projY(inBeat));

    double outLane = anchor.laneX + anchor.handleOutDx;
    double outBeat = anchor.beat + anchor.handleOutDy;
    QPointF outAbs(projX(outLane), projY(outBeat));

    // 入控制柄
    if (anchor.hasHandleIn()) {
        QColor col = selected ? handleSelectedColor() : handleColor();
        painter->setPen(QPen(col, 1));
        painter->drawLine(anchorPos, inAbs);
        painter->setBrush(col);
        painter->drawEllipse(inAbs, kHandleRadius, kHandleRadius);
    }

    // 出控制柄
    if (anchor.hasHandleOut()) {
        QColor col = selected ? handleSelectedColor() : handleColor();
        painter->setPen(QPen(col, 1));
        painter->drawLine(anchorPos, outAbs);
        painter->setBrush(col);
        painter->drawEllipse(outAbs, kHandleRadius, kHandleRadius);
    }
}

void NoteChainOverlay::drawCurveSegment(QPainter *painter, const Anchor &a0, const Anchor &a1,
                                         const QString &shape,
                                         const ProjectX &projX, const ProjectY &projY)
{
    QVector<SampledPoint> pts = NoteChainCurveSampler::sampleSegment(a0, a1, shape);

    if (pts.size() < 2)
        return;

    bool isPolyline = (shape == QLatin1String(kShapePolyline));
    QColor col = isPolyline ? polylineColor() : curveColor();
    QPen pen(col, isPolyline ? 1.5 : 2.5);
    painter->setPen(pen);

    for (int i = 0; i < pts.size() - 1; ++i) {
        double x1 = projX(pts[i].laneX);
        double y1 = projY(pts[i].beat);
        double x2 = projX(pts[i+1].laneX);
        double y2 = projY(pts[i+1].beat);
        painter->drawLine(QPointF(x1, y1), QPointF(x2, y2));
    }
}

void NoteChainOverlay::drawLinkDragPreview(QPainter *painter,
                                            const Anchor &fromAnchor,
                                            double toLaneX, double toBeat,
                                            const ProjectX &projX, const ProjectY &projY)
{
    QColor col = linkPreviewColor();
    QPen pen(col, 2.0, Qt::DashLine);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    double fx = projX(fromAnchor.laneX);
    double fy = projY(fromAnchor.beat);
    double tx = projX(toLaneX);
    double ty = projY(toBeat);
    painter->drawLine(QPointF(fx, fy), QPointF(tx, ty));
}

} // namespace NoteChain