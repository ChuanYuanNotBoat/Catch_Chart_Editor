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

    bool handled = NoteChain::bridgeHandleMousePress(
        editor, event, event->position().x(), event->position().y(), shift, ctrl);

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
    bool handled = NoteChain::bridgeHandleMouseMove(
        editor, event, event->position().x(), event->position().y());

    if (handled)
        canvas->update();
    return handled;
}

bool ChartCanvas_dispatchNoteChainMouseRelease(ChartCanvas *canvas, QMouseEvent *event)
{
    if (!canvas || !canvas->isNoteChainModeActive() || !canvas->noteChainEditor())
        return false;

    auto *editor = canvas->noteChainEditor();
    bool handled = NoteChain::bridgeHandleMouseRelease(
        editor, event, event->position().x(), event->position().y());

    if (handled)
        canvas->update();
    return handled;
}

void ChartCanvas_drawNoteChainOverlay(ChartCanvas *canvas, QPainter *painter)
{
    if (!canvas || !canvas->isNoteChainModeActive() || !canvas->noteChainEditor())
        return;

    NoteChain::bridgeRenderOverlay(
        canvas->noteChainEditor(), painter,
        painter->viewport(), 0, 0);
}