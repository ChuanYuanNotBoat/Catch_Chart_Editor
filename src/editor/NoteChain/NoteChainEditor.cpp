// NoteChainEditor.cpp — 顶层协调器实现

#include "NoteChainEditor.h"
#include "NoteChainOverlay.h"

#include <QMouseEvent>
#include <QPainter>

namespace NoteChain {

NoteChainEditor::NoteChainEditor(QObject *parent)
    : QObject(parent)
{
}

NoteChainEditor::~NoteChainEditor()
{
}

void NoteChainEditor::setActive(bool active)
{
    if (m_active == active)
        return;

    m_active = active;

    if (active) {
        m_dragMode = DragMode::None;
        m_dragAnchorId = -1;
        m_linkDragFromId = -1;
        m_state.clearSelection();
        // 不清空历史栈：用户切换工具不应丢失撤销记录（P0-4 fix）
    }
}

NoteChainEditor::MouseResult NoteChainEditor::handleMousePress(
    QMouseEvent *event, double canvasX, double canvasY,
    bool shiftDown, bool ctrlDown)
{
    if (!m_active)
        return MouseResult::NotHandled;

    if (event->button() == Qt::LeftButton) {
        QMap<int, Anchor> anchorMap = m_state.anchors();
        double bestDist = kHitTolerance;
        int hitId = -1;
        bool isInHandle = false;

        for (auto it = anchorMap.begin(); it != anchorMap.end(); ++it) {
            const Anchor &a = it.value();

            QPointF hi = a.handleInAbs();
            double di = NoteChainCurveSampler::distance(canvasX, canvasY, hi.x(), hi.y());
            if (di < bestDist) {
                bestDist = di;
                hitId = a.id;
                isInHandle = true;
            }

            QPointF ho = a.handleOutAbs();
            double dout = NoteChainCurveSampler::distance(canvasX, canvasY, ho.x(), ho.y());
            if (dout < bestDist) {
                bestDist = dout;
                hitId = a.id;
                isInHandle = false;
            }

            double da = NoteChainCurveSampler::distance(canvasX, canvasY, a.laneX, a.beat);
            if (da < bestDist) {
                bestDist = da;
                hitId = a.id;
            }
        }

        if (hitId >= 0 && bestDist < kHitTolerance) {
            double da = NoteChainCurveSampler::distance(canvasX, canvasY,
                          anchorMap[hitId].laneX, anchorMap[hitId].beat);
            if (da < kHitTolerance * 0.8) {
                if (shiftDown) {
                    m_dragMode = DragMode::LinkDrag;
                    m_linkDragFromId = hitId;
                    m_linkDragCurrentX = canvasX;
                    m_linkDragCurrentY = canvasY;
                    return MouseResult::Handled;
                } else {
                    m_dragMode = DragMode::Anchor;
                    m_dragAnchorId = hitId;
                    if (!ctrlDown) {
                        m_state.clearSelection();
                        m_state.selectAnchor(hitId);
                    } else {
                        m_state.toggleSelection(hitId);
                    }
                    recordHistory();
                    return MouseResult::NeedsRepaint;
                }
            } else {
                m_dragMode = isInHandle ? DragMode::HandleIn : DragMode::HandleOut;
                m_dragAnchorId = hitId;
                recordHistory();
                return MouseResult::Handled;
            }
        }

        // 空白区域：创建新锚点
        Anchor newAnchor;
        newAnchor.id = m_state.nextAnchorId();
        newAnchor.laneX = canvasX;
        newAnchor.beat = canvasY;
        m_state.addAnchor(newAnchor);

        if (!ctrlDown)
            m_state.clearSelection();
        m_state.selectAnchor(newAnchor.id);

        recordHistory();
        return MouseResult::NeedsRepaint;
    }

    return MouseResult::NotHandled;
}

NoteChainEditor::MouseResult NoteChainEditor::handleMouseMove(
    QMouseEvent *event, double canvasX, double canvasY)
{
    Q_UNUSED(event)

    if (!m_active || m_dragMode == DragMode::None)
        return MouseResult::NotHandled;

    // P1-3 fix: 拖拽节流，每 16ms 最多触发一次状态变更 + repaint
    if (m_dragMode != DragMode::LinkDrag &&
        m_lastMoveTimer.isValid() && m_lastMoveTimer.elapsed() < kMoveThrottleMs) {
        return MouseResult::Handled;
    }
    m_lastMoveTimer.restart();

    if (m_dragMode == DragMode::LinkDrag) {
        m_linkDragCurrentX = canvasX;
        m_linkDragCurrentY = canvasY;
        return MouseResult::NeedsRepaint;
    }

    if (m_dragMode == DragMode::Anchor && m_dragAnchorId >= 0) {
        Anchor a = m_state.anchor(m_dragAnchorId);
        if (a.id >= 0) {
            a.laneX = canvasX;
            a.beat = canvasY;
            m_state.setAnchor(m_dragAnchorId, a);
            return MouseResult::NeedsRepaint;
        }
    }

    if (m_dragMode == DragMode::HandleIn && m_dragAnchorId >= 0) {
        Anchor a = m_state.anchor(m_dragAnchorId);
        if (a.id >= 0) {
            a.handleInDx = canvasX - a.laneX;
            a.handleInDy = canvasY - a.beat;
            m_state.setAnchor(m_dragAnchorId, a);
            return MouseResult::NeedsRepaint;
        }
    }

    if (m_dragMode == DragMode::HandleOut && m_dragAnchorId >= 0) {
        Anchor a = m_state.anchor(m_dragAnchorId);
        if (a.id >= 0) {
            a.handleOutDx = canvasX - a.laneX;
            a.handleOutDy = canvasY - a.beat;
            m_state.setAnchor(m_dragAnchorId, a);
            return MouseResult::NeedsRepaint;
        }
    }

    return MouseResult::Handled;
}

NoteChainEditor::MouseResult NoteChainEditor::handleMouseRelease(
    QMouseEvent *event, double canvasX, double canvasY)
{
    Q_UNUSED(event)

    if (!m_active)
        return MouseResult::NotHandled;

    if (m_dragMode == DragMode::LinkDrag && m_linkDragFromId >= 0) {
        const QMap<int, Anchor> &anchorMap = m_state.anchors();
        double bestDist = kHitTolerance;
        int bestTargetId = -1;
        for (auto it = anchorMap.begin(); it != anchorMap.end(); ++it) {
            const Anchor &a = it.value();
            double d = NoteChainCurveSampler::distance(canvasX, canvasY, a.laneX, a.beat);
            if (d < bestDist && a.id != m_linkDragFromId) {
                bestDist = d;
                bestTargetId = a.id;
            }
        }
        if (bestTargetId >= 0) {
            m_state.addLink(m_linkDragFromId, bestTargetId);
            recordHistory();
        }
    }

    DragMode prevMode = m_dragMode;
    MouseResult result = (prevMode != DragMode::None)
                         ? MouseResult::NeedsRepaint
                         : MouseResult::NotHandled;

    m_dragMode = DragMode::None;
    m_dragAnchorId = -1;
    m_linkDragFromId = -1;

    return result;
}

NoteChainEditor::MouseResult NoteChainEditor::handleMouseDoubleClick(
    QMouseEvent *event, double /*canvasX*/, double /*canvasY*/)
{
    Q_UNUSED(event)
    return MouseResult::NotHandled;
}

void NoteChainEditor::handleKeyDelete()
{
    if (!m_active)
        return;

    QSet<int> selected = m_state.selectedAnchorIds();
    if (selected.isEmpty())
        return;

    for (int id : selected)
        m_state.removeAnchor(id);

    m_state.clearSelection();
    m_state.cleanupOrphanedLinksAndSelection();
    recordHistory();
}

void NoteChainEditor::handleKeyEscape()
{
    if (!m_active)
        return;

    m_dragMode = DragMode::None;
    m_dragAnchorId = -1;
    m_linkDragFromId = -1;
    m_state.clearSelection();
}

void NoteChainEditor::renderOverlay(QPainter *painter, const QRectF &rect,
                                     double scrollBeat, double visibleBeatRange,
                                     const ProjectX &projX, const ProjectY &projY)
{
    if (!m_active)
        return;

    Q_UNUSED(rect)
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    NoteChainOverlay::render(painter, m_state, scrollBeat, visibleBeatRange, projX, projY);

    // 绘制 LinkDrag 预览线（当前鼠标位置已是 canvas 坐标）
    if (m_dragMode == DragMode::LinkDrag && m_linkDragFromId >= 0) {
        Anchor fromAnchor = m_state.anchor(m_linkDragFromId);
        if (fromAnchor.id >= 0) {
            NoteChainOverlay::drawLinkDragPreview(
                painter, fromAnchor, m_linkDragCurrentX, m_linkDragCurrentY, projX, projY);
        }
    }

    painter->restore();
}

bool NoteChainEditor::canUndo() const
{
    return m_history.canUndo();
}

bool NoteChainEditor::canRedo() const
{
    return m_history.canRedo();
}

void NoteChainEditor::undo()
{
    if (m_history.undo(m_state))
        m_state.cleanupOrphanedLinksAndSelection();
}

void NoteChainEditor::redo()
{
    if (m_history.redo(m_state))
        m_state.cleanupOrphanedLinksAndSelection();
}

void NoteChainEditor::recordHistory()
{
    m_history.push(m_state);
}

bool NoteChainEditor::loadProject(const QString &sidecarPath)
{
    m_currentSidecarPath = sidecarPath;
    bool ok = NoteChainPersistence::loadFromFile(sidecarPath, m_state);
    if (ok) {
        m_history.clear();
        recordHistory();
    }
    return ok;
}

bool NoteChainEditor::saveProject(const QString &sidecarPath)
{
    QString path = sidecarPath.isEmpty() ? m_currentSidecarPath : sidecarPath;
    if (path.isEmpty())
        return false;
    m_currentSidecarPath = path;
    return NoteChainPersistence::saveToFile(m_state, path);
}

QVector<SampledPoint> NoteChainEditor::generateNotes() const
{
    return NoteChainCurveSampler::generateNotesFromCurve(m_state);
}

int NoteChainEditor::selectedAnchorId() const
{
    return m_state.singleSelectedAnchorId();
}

QVector<int> NoteChainEditor::allAnchorIds() const
{
    QVector<int> result;
    QMap<int, Anchor> anchorMap = m_state.anchors();
    for (auto it = anchorMap.begin(); it != anchorMap.end(); ++it)
        result.append(it.key());
    return result;
}

} // namespace NoteChain