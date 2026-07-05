// ChartCanvasNoteChain.cpp — NoteChain 原生编辑器集成
// 编译链接到 ChartCanvas 体系的独立实现文件

#include "ChartCanvas.h"
#include "editor/NoteChain/NoteChainCanvasBridge.h"
#include <QMouseEvent>
#include <QPainter>

void ChartCanvas::setNoteChainModeActive(bool active)
{
    if (m_noteChainModeActive == active)
        return;

    // 与插件工具模式互斥
    if (active && m_pluginToolModeActive)
        setPluginToolMode(false);

    m_noteChainModeActive = active;

    if (active) {
        if (!m_noteChainEditor)
            m_noteChainEditor = new NoteChain::NoteChainEditor(this);
        m_noteChainEditor->setActive(true);
        setMode(AnchorPlace);
    } else {
        if (m_noteChainEditor)
            m_noteChainEditor->setActive(false);
    }

    update();
}

// NoteChain 事件桥接：在 mousePressEvent 开头调用
bool ChartCanvas_dispatchNoteChainMousePress(ChartCanvas *canvas, QMouseEvent *event)
{
    if (!canvas || !canvas->isNoteChainModeActive() || !canvas->noteChainEditor())
        return false;

    auto *editor = canvas->noteChainEditor();
    bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    bool ctrl  = event->modifiers().testFlag(Qt::ControlModifier);

    // 将 canvas 像素坐标转换为 chart 坐标
    double laneX = canvas->chartCanvasXToLaneX(event->position().x());
    double beat  = canvas->chartYToBeat(event->position().y());

    bool handled = NoteChain::bridgeHandleMousePress(
        editor, event, laneX, beat, shift, ctrl);

    if (handled) {
        event->accept();
        canvas->update();
    }
    return handled;
}

bool ChartCanvas_dispatchNoteChainMouseMove(ChartCanvas *canvas, QMouseEvent *event)
{
    if (!canvas || !canvas->isNoteChainModeActive() || !canvas->noteChainEditor())
        return false;

    auto *editor = canvas->noteChainEditor();
    double laneX = canvas->chartCanvasXToLaneX(event->position().x());
    double beat  = canvas->chartYToBeat(event->position().y());

    bool handled = NoteChain::bridgeHandleMouseMove(
        editor, event, laneX, beat);

    if (handled)
        canvas->update();
    return handled;
}

bool ChartCanvas_dispatchNoteChainMouseRelease(ChartCanvas *canvas, QMouseEvent *event)
{
    if (!canvas || !canvas->isNoteChainModeActive() || !canvas->noteChainEditor())
        return false;

    auto *editor = canvas->noteChainEditor();
    double laneX = canvas->chartCanvasXToLaneX(event->position().x());
    double beat  = canvas->chartYToBeat(event->position().y());

    bool handled = NoteChain::bridgeHandleMouseRelease(
        editor, event, laneX, beat);

    if (handled)
        canvas->update();
    return handled;
}

void ChartCanvas_drawNoteChainOverlay(ChartCanvas *canvas, QPainter *painter)
{
    if (!canvas || !canvas->isNoteChainModeActive() || !canvas->noteChainEditor())
        return;

    // 构建 chart→canvas 投影函数
    auto projX = [canvas](double laneX) -> double {
        return canvas->chartLaneXToCanvasX(static_cast<int>(laneX));
    };
    auto projY = [canvas](double beat) -> double {
        return canvas->chartBeatToY(beat);
    };

    NoteChain::bridgeRenderOverlay(
        canvas->noteChainEditor(), painter,
        painter->viewport(),
        canvas->scrollBeat(),
        canvas->visibleBeatRange(),
        projX, projY);
}
