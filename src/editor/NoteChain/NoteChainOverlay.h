#pragma once

// NoteChainOverlay.h — Canvas 叠加层渲染
// 对应 Python modular/rendering/overlay.py

#include "NoteChainState.h"
#include "NoteChainCurveSampler.h"

#include <QPainter>
#include <QColor>

namespace NoteChain {

class NoteChainOverlay
{
public:
    static void render(QPainter *painter, const NoteChainState &state,
                       double scrollBeat, double visibleBeatRange);

    static QColor anchorColor()        { return QColor(255, 180, 60); }
    static QColor anchorSelectedColor(){ return QColor(60, 200, 255); }
    static QColor handleColor()        { return QColor(180, 140, 60); }
    static QColor handleSelectedColor(){ return QColor(100, 160, 255); }
    static QColor curveColor()         { return QColor(255, 200, 100, 200); }
    static QColor polylineColor()      { return QColor(200, 200, 200, 180); }

private:
    static void drawAnchor(QPainter *painter, const Anchor &anchor, bool selected);
    static void drawHandles(QPainter *painter, const Anchor &anchor, bool selected);
    static void drawCurveSegment(QPainter *painter, const Anchor &a0, const Anchor &a1,
                                 const QString &shape);
};

} // namespace NoteChain