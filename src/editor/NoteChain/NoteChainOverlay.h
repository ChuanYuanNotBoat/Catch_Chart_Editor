#pragma once

// NoteChainOverlay.h — Canvas 叠加层渲染
// 对应 Python modular/rendering/overlay.py

#include "NoteChainState.h"
#include "NoteChainCurveSampler.h"

#include <QPainter>
#include <QColor>
#include <functional>

namespace NoteChain {

// chart→canvas 投影函数类型
using ProjectX = std::function<double(double laneX)>; // laneX → canvasX
using ProjectY = std::function<double(double beat)>;  // beat  → canvasY

class NoteChainOverlay
{
public:
    static void render(QPainter *painter, const NoteChainState &state,
                       double scrollBeat, double visibleBeatRange,
                       const ProjectX &projX, const ProjectY &projY);

    static QColor anchorColor()        { return QColor(255, 180, 60); }
    static QColor anchorSelectedColor(){ return QColor(60, 200, 255); }
    static QColor handleColor()        { return QColor(180, 140, 60); }
    static QColor handleSelectedColor(){ return QColor(100, 160, 255); }
    static QColor curveColor()         { return QColor(255, 200, 100, 200); }
    static QColor polylineColor()      { return QColor(200, 200, 200, 180); }
    static QColor linkPreviewColor()   { return QColor(255, 255, 100, 160); }

    /// 绘制 Shift+拖拽创建链接时的预览线
    /// toLaneX/toBeat 为鼠标当前位置的 chart 坐标
    static void drawLinkDragPreview(QPainter *painter,
                                    const Anchor &fromAnchor,
                                    double toLaneX, double toBeat,
                                    const ProjectX &projX, const ProjectY &projY);

private:
    static void drawAnchor(QPainter *painter, const Anchor &anchor, bool selected,
                           const ProjectX &projX, const ProjectY &projY);
    static void drawHandles(QPainter *painter, const Anchor &anchor, bool selected,
                            const ProjectX &projX, const ProjectY &projY);
    static void drawCurveSegment(QPainter *painter, const Anchor &a0, const Anchor &a1,
                                 const QString &shape,
                                 const ProjectX &projX, const ProjectY &projY);
};

} // namespace NoteChain
