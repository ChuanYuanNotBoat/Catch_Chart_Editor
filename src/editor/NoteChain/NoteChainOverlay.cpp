// NoteChainOverlay.cpp — Canvas 叠加层渲染实现

#include "NoteChainOverlay.h"

#include <QtMath>

namespace NoteChain {

void NoteChainOverlay::render(QPainter *painter, const NoteChainState &state,
                               double /*scrollBeat*/, double /*visibleBeatRange*/)
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
        drawCurveSegment(painter, a0, a1, shape);
    }

    // 绘制锚点和控制柄
    for (auto it = anchorMap.begin(); it != anchorMap.end(); ++it) {
        const Anchor &anchor = it.value();
        bool selected = state.isSelected(anchor.id);
        drawAnchor(painter, anchor, selected);
        drawHandles(painter, anchor, selected);
    }
}

void NoteChainOverlay::drawAnchor(QPainter *painter, const Anchor &anchor, bool selected)
{
    QColor color = selected ? anchorSelectedColor() : anchorColor();
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawEllipse(QPointF(anchor.x, anchor.y), kAnchorRadius, kAnchorRadius);
}

void NoteChainOverlay::drawHandles(QPainter *painter, const Anchor &anchor, bool selected)
{
    QPointF anchorPos(anchor.x, anchor.y);
    QPointF inAbs  = anchor.handleInAbs();
    QPointF outAbs = anchor.handleOutAbs();

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
                                         const QString &shape)
{
    QVector<SampledPoint> pts = NoteChainCurveSampler::sampleSegment(a0, a1, shape);

    if (pts.size() < 2)
        return;

    bool isPolyline = (shape == QLatin1String(kShapePolyline));
    QColor col = isPolyline ? polylineColor() : curveColor();
    QPen pen(col, isPolyline ? 1.5 : 2.5);
    painter->setPen(pen);

    for (int i = 0; i < pts.size() - 1; ++i) {
        painter->drawLine(QPointF(pts[i].laneX, pts[i].beat),
                          QPointF(pts[i+1].laneX, pts[i+1].beat));
    }
}

} // namespace NoteChain