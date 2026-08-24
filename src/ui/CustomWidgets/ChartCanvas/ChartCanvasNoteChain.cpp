// ChartCanvasNoteChain.cpp - native NoteChain integration.
#include "ChartCanvas.h"

#include "controller/ChartController.h"
#include "editor/NoteChain/NoteChainEditor.h"
#include "editor/NoteChain/NoteChainPersistence.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMouseEvent>
#include <QPainter>

namespace {

NoteChain::CanvasProjection projectionFor(const ChartCanvas *canvas)
{
    NoteChain::CanvasProjection projection;
    if (!canvas)
        return projection;
    projection.lmargin = canvas->chartLaneXToCanvasX(0);
    const double laneRight = canvas->chartLaneXToCanvasX(512);
    projection.available = qMax(1.0, laneRight - projection.lmargin);
    projection.rmargin = qMax(0.0, canvas->width() - laneRight);
    projection.laneW = 512.0;
    projection.ch = qMax(1, canvas->height());
    projection.scrollB = canvas->scrollBeat();
    projection.visRange = canvas->visibleBeatRange();
    projection.flip = canvas->isVerticalFlip();
    return projection;
}

QString workingCurvePath(ChartCanvas *canvas)
{
    if (!canvas)
        return QString();
    const QVariantMap context = canvas->pluginCanvasActionContext();
    QString path = context.value(QStringLiteral("curve_project_path")).toString();
    if (!path.isEmpty()) {
        const QString sourceChart = context.value(QStringLiteral("chart_path_source")).toString();
        const QString sourceCurve = NoteChain::NoteChainPersistence::sidecarPathForChart(sourceChart);
        if (!QFileInfo::exists(path) && QFileInfo::exists(sourceCurve)) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile::copy(sourceCurve, path);
        }
    }
    return path;
}

} // namespace

void ChartCanvas::setNoteChainModeActive(bool active)
{
    if (m_noteChainModeActive == active)
        return;
    if (active && m_pluginToolModeActive)
        setPluginToolMode(false);
    m_noteChainModeActive = active;
    if (active) {
        if (!m_noteChainEditor) {
            m_noteChainEditor = new NoteChain::NoteChainEditor(this);
            connect(m_noteChainEditor, &NoteChain::NoteChainEditor::needsRepaint,
                    this, qOverload<>(&ChartCanvas::update));
            connect(m_noteChainEditor, &NoteChain::NoteChainEditor::statusMessage,
                    this, &ChartCanvas::statusMessage);
            connect(m_noteChainEditor, &NoteChain::NoteChainEditor::controlsChanged,
                    this, &ChartCanvas::noteChainControlsChanged);
            connect(m_noteChainEditor, &NoteChain::NoteChainEditor::requestHostUndoCheckpoint,
                    this, [this](const QString &label) {
                        if (m_chartController) m_chartController->pushUndoMarker(label);
                    });
        }
        m_noteChainEditor->setChartController(m_chartController);
        setMode(AnchorPlace);
        const QVariantMap context = buildPluginCanvasContext();
        const QString sidecarPath = workingCurvePath(this);
        if (!sidecarPath.isEmpty() && sidecarPath != m_noteChainEditor->currentSidecarPath())
            m_noteChainEditor->loadProject(sidecarPath);
        m_noteChainEditor->setHostContext(context);
        m_noteChainEditor->setActive(true);
        stopOverlayQueryTimer();
        m_overlayCache.clear();
        m_eventOverlayCache.clear();
    } else if (m_noteChainEditor) {
        m_noteChainEditor->setActive(false);
        unsetCursor();
    }
    emit noteChainControlsChanged();
    update();
}

bool ChartCanvas_dispatchNoteChainMousePress(ChartCanvas *canvas, QMouseEvent *event)
{
    if (!canvas || !event || !canvas->isNoteChainModeActive() || !canvas->noteChainEditor())
        return false;
    NoteChain::NoteChainEditor *editor = canvas->noteChainEditor();
    editor->setHostContext(canvas->pluginCanvasActionContext());
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    const bool control = event->modifiers().testFlag(Qt::ControlModifier);
    const int button = event->button() == Qt::LeftButton ? NoteChain::Const::kLeftButton
                     : event->button() == Qt::RightButton ? NoteChain::Const::kRightButton : 0;
    const bool handled = editor->handleMousePress(event->position(), projectionFor(canvas),
                                                   button, shift, control);
    if (handled) {
        event->accept();
        canvas->update();
    }
    return handled;
}

bool ChartCanvas_dispatchNoteChainMouseMove(ChartCanvas *canvas, QMouseEvent *event)
{
    if (!canvas || !event || !canvas->isNoteChainModeActive() || !canvas->noteChainEditor())
        return false;
    NoteChain::NoteChainEditor *editor = canvas->noteChainEditor();
    // Host context serializes the chart selection and all Normal-note positions.
    // None of those values changes merely because the pointer moved, so rebuilding
    // it here made idle mouse motion O(chart note count). Press/release and explicit
    // host state changes keep the editor context synchronized.
    const NoteChain::CanvasProjection projection = projectionFor(canvas);
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    const bool handled = editor->handleMouseMove(event->position(), projection,
                                                  static_cast<int>(event->buttons()), shift);
    const QString cursor = editor->hoverCursorHint(event->position(), projection);
    Qt::CursorShape cursorShape = Qt::ArrowCursor;
    if (cursor == QLatin1String("pointing_hand")) cursorShape = Qt::PointingHandCursor;
    else if (cursor == QLatin1String("size_all")) cursorShape = Qt::SizeAllCursor;
    else if (cursor == QLatin1String("crosshair")) cursorShape = Qt::CrossCursor;
    if (cursorShape == Qt::ArrowCursor) {
        if (canvas->testAttribute(Qt::WA_SetCursor))
            canvas->unsetCursor();
    } else if (!canvas->testAttribute(Qt::WA_SetCursor) || canvas->cursor().shape() != cursorShape) {
        canvas->setCursor(cursorShape);
    }
    if (handled) canvas->update();
    return handled;
}

bool ChartCanvas_dispatchNoteChainMouseRelease(ChartCanvas *canvas, QMouseEvent *event)
{
    if (!canvas || !event || !canvas->isNoteChainModeActive() || !canvas->noteChainEditor())
        return false;
    NoteChain::NoteChainEditor *editor = canvas->noteChainEditor();
    editor->setHostContext(canvas->pluginCanvasActionContext());
    const int button = event->button() == Qt::LeftButton ? NoteChain::Const::kLeftButton
                     : event->button() == Qt::RightButton ? NoteChain::Const::kRightButton : 0;
    const bool handled = editor->handleMouseRelease(event->position(), projectionFor(canvas), button);
    if (handled) canvas->update();
    return handled;
}

void ChartCanvas_drawNoteChainOverlay(ChartCanvas *canvas, QPainter *painter)
{
    if (!canvas || !painter || !canvas->isNoteChainModeActive() || !canvas->noteChainEditor())
        return;
    canvas->noteChainEditor()->render(painter, painter->viewport(), projectionFor(canvas));
}
