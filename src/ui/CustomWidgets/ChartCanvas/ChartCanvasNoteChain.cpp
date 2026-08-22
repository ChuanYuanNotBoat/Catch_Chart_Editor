// ChartCanvasNoteChain.cpp - NoteChain native integration (zero overlay, direct render)
#include "ChartCanvas.h"
#include "editor/NoteChain/NoteChainEditor.h"
#include "editor/NoteChain/NoteChainPersistence.h"
#include <QMouseEvent>
#include <QPainter>

void ChartCanvas::setNoteChainModeActive(bool active) {
    if (m_noteChainModeActive == active) return;
    // mute with plugin tool mode
    if (active && m_pluginToolModeActive) setPluginToolMode(false);
    m_noteChainModeActive = active;
    if (active) {
        if (!m_noteChainEditor) m_noteChainEditor = new NoteChain::NoteChainEditor(this);
        m_noteChainEditor->setActive(true);
        m_noteChainEditor->setChartController(m_chartController);
        // stop plugin overlay timer; clear overlay cache
        stopOverlayQueryTimer();
        m_overlayCache.clear(); m_eventOverlayCache.clear();
        setMode(AnchorPlace);
        // try load sidecar
        if (!m_sourceChartPath.isEmpty()) {
            QString scPath = NoteChain::NoteChainPersistence::sidecarPathForChart(m_sourceChartPath);
            m_noteChainEditor->loadProject(scPath);
        }
        // Loading replaces the state object, so seed host context afterwards.
        m_noteChainEditor->setHostContext(buildPluginCanvasContext());
    } else {
        if (m_noteChainEditor) m_noteChainEditor->setActive(false);
    }
    update();
}

// ---- mouse dispatch ----
bool ChartCanvas_dispatchNoteChainMousePress(ChartCanvas *c, QMouseEvent *event) {
    if (!c || !c->isNoteChainModeActive() || !c->noteChainEditor()) return false;
    auto *ed = c->noteChainEditor();
    double lx = c->chartCanvasXToLaneX(event->position().x());
    double bt = c->chartYToBeat(event->position().y());
    bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    bool ctrl  = event->modifiers().testFlag(Qt::ControlModifier);
    ed->state().setShiftDown(shift); // P1-4: track shift for mid-drag switch
    int btn = (event->button() == Qt::LeftButton) ? NoteChain::Const::kLeftButton
            : (event->button() == Qt::RightButton) ? NoteChain::Const::kRightButton : 0;
    bool handled = ed->handleMousePress(lx, bt, btn, shift, ctrl);
    if (handled) { event->accept(); c->update(); }
    return handled;
}

bool ChartCanvas_dispatchNoteChainMouseMove(ChartCanvas *c, QMouseEvent *event) {
    if (!c || !c->isNoteChainModeActive() || !c->noteChainEditor()) return false;
    auto *ed = c->noteChainEditor();
    double lx = c->chartCanvasXToLaneX(event->position().x());
    double bt = c->chartYToBeat(event->position().y());
    ed->state().setShiftDown(event->modifiers().testFlag(Qt::ShiftModifier)); // P1-4
    bool handled = ed->handleMouseMove(lx, bt, static_cast<int>(event->buttons()));
    // P0-2: apply hover cursor directly (avoid private applyPluginCursor)
    QString cur = ed->hoverCursorHint(lx, bt);
    if (cur == "pointing_hand") c->setCursor(Qt::PointingHandCursor);
    else if (cur == "size_all") c->setCursor(Qt::SizeAllCursor);
    else if (cur == "crosshair") c->setCursor(Qt::CrossCursor);
    else if (cur.isEmpty() || cur == "arrow") c->unsetCursor();
    if (handled) c->update();
    return handled;
}

bool ChartCanvas_dispatchNoteChainMouseRelease(ChartCanvas *c, QMouseEvent *event) {
    if (!c || !c->isNoteChainModeActive() || !c->noteChainEditor()) return false;
    auto *ed = c->noteChainEditor();
    double lx = c->chartCanvasXToLaneX(event->position().x());
    double bt = c->chartYToBeat(event->position().y());
    int btn = (event->button() == Qt::LeftButton) ? NoteChain::Const::kLeftButton
            : (event->button() == Qt::RightButton) ? NoteChain::Const::kRightButton : 0;
    bool handled = ed->handleMouseRelease(lx, bt, btn);
    if (handled) c->update();
    return handled;
}

// ---- render (direct QPainter, no overlay) ----
void ChartCanvas_drawNoteChainOverlay(ChartCanvas *c, QPainter *painter) {
    if (!c || !c->isNoteChainModeActive() || !c->noteChainEditor()) return;
    NoteChain::CanvasProjection proj;
    proj.lmargin = 64; proj.rmargin = 64;
    proj.available = qMax(1.0, static_cast<double>(c->width()) - proj.lmargin - proj.rmargin);
    proj.laneW = 512.0; proj.ch = static_cast<double>(c->height());
    proj.scrollB = c->scrollBeat(); proj.visRange = c->visibleBeatRange();
    proj.flip = c->isVerticalFlip();
    c->noteChainEditor()->render(painter, painter->viewport(),
                                  proj.scrollB, proj.visRange, proj);
}
