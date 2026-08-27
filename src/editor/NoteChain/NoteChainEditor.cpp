// NoteChainEditor.cpp - native curve editor and host integration.
#include "NoteChainEditor.h"

#include "NoteChainPersistence.h"
#include "controller/ChartController.h"
#include "model/Note.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <limits>
#include <numeric>
#include <utility>

namespace NoteChain {

namespace {

QString normalNotePositionKey(const Note &note)
{
    if (note.type != NoteType::NORMAL || note.denominator == 0)
        return QString();
    qint64 numerator = static_cast<qint64>(note.beatNum) * note.denominator + note.numerator;
    qint64 denominator = note.denominator;
    if (denominator < 0) {
        denominator = -denominator;
        numerator = -numerator;
    }
    const qint64 divisor = std::gcd(numerator < 0 ? -numerator : numerator, denominator);
    if (divisor > 0) {
        numerator /= divisor;
        denominator /= divisor;
    }
    return QStringLiteral("%1/%2:%3").arg(numerator).arg(denominator).arg(note.x);
}

int defaultCommitDenominator(const NoteChainState &state)
{
    const QVariantMap context = state.lastContext();
    const int overrideDenominator = context.value(QStringLiteral("plugin_time_division_override"), 0).toInt();
    if (overrideDenominator > 0)
        return overrideDenominator;
    const int timeDivision = context.value(QStringLiteral("time_division"), 0).toInt();
    if (timeDivision > 0)
        return timeDivision;
    if (!state.style().denominators.isEmpty())
        return qMax(1, state.style().denominators.first());
    return Const::kDefaultSegmentDen;
}

bool isCurveCheckpoint(const QString &actionText)
{
    return actionText.trimmed().startsWith(QString::fromLatin1(Const::checkpointPrefix()), Qt::CaseInsensitive);
}

QPointF snappedChartPoint(const NoteChainState &state, const QPointF &chartPoint,
                          bool snapBeat, bool snapLane)
{
    const QVariantMap context = state.lastContext();
    const int gridDivision = qMax(1, context.value(QStringLiteral("grid_division"), 8).toInt());
    const int timeDivision = qMax(1, context.value(QStringLiteral("time_division"), 1).toInt());
    double laneX = ncClamp(chartPoint.x(), 0.0, Const::kLaneWidth);
    double beat = qMax(0.0, chartPoint.y());
    if (snapLane && context.value(QStringLiteral("grid_snap"), false).toBool())
        laneX = qRound((laneX / Const::kLaneWidth) * gridDivision) * (Const::kLaneWidth / gridDivision);
    if (snapBeat)
        beat = qRound(beat * timeDivision) / static_cast<double>(timeDivision);
    return {laneX, beat};
}

QString checkpointLabel(const QString &detail)
{
    return QStringLiteral("%1: %2").arg(QString::fromLatin1(Const::checkpointPrefix()), detail);
}

} // namespace

NoteChainEditor::NoteChainEditor(QObject *parent)
    : QObject(parent)
{
    recordHistory();
}

void NoteChainEditor::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (!active) {
        if (!m_sidecarPath.isEmpty() && m_state.projectDirty()) {
            QString error;
            if (!NoteChainPersistence::saveToFile(m_state, m_sidecarPath, &error))
                emit statusMessage(tr("Failed to save curve project: %1").arg(error));
        }
        m_state.drag() = DragState{};
        m_state.linkDrag() = LinkDrag{};
        m_state.boxSelect() = BoxSelect{};
        m_state.clearAnchorSelection();
        m_state.clearLinkSelection();
        m_contextLinkKeys.clear();
        m_dragChanged = false;
    }
    emit controlsChanged();
    emit needsRepaint();
}

void NoteChainEditor::setHostContext(const QVariantMap &context)
{
    m_state.setLastContext(context);
    const QVariantMap toggles = context.value(QStringLiteral("overlay_toggles")).toMap();
    OverlayToggles &overlay = m_state.overlayToggles();
    if (toggles.contains(QStringLiteral("overlay_enabled"))) overlay.enabled = toggles.value(QStringLiteral("overlay_enabled")).toBool();
    if (toggles.contains(QStringLiteral("preview"))) overlay.preview = toggles.value(QStringLiteral("preview")).toBool();
    if (toggles.contains(QStringLiteral("control_points"))) overlay.controlPoints = toggles.value(QStringLiteral("control_points")).toBool();
    if (toggles.contains(QStringLiteral("handles"))) overlay.handles = toggles.value(QStringLiteral("handles")).toBool();
    if (toggles.contains(QStringLiteral("sample_points"))) overlay.samplePoints = toggles.value(QStringLiteral("sample_points")).toBool();
    if (toggles.contains(QStringLiteral("labels"))) overlay.labels = toggles.value(QStringLiteral("labels")).toBool();
    syncAnchorPlacementWithHostMode();
    syncAnchorSelectionFromHostNotes();
}

void NoteChainEditor::syncAnchorPlacementWithHostMode()
{
    const QString mode = m_state.lastContext().value(QStringLiteral("host_selection_tool")).toMap()
                             .value(QStringLiteral("mode")).toString().trimmed().toLower();
    if (mode.isEmpty() || mode == m_lastHostMode)
        return;
    m_lastHostMode = mode;
    bool changed = false;
    if (mode == QLatin1String("anchor_place")) {
        changed = !m_state.anchorPlacementEnabled();
        m_state.setAnchorPlacementEnabled(true);
    } else if (mode == QLatin1String("place_note") || mode == QLatin1String("place_rain")
               || mode == QLatin1String("delete") || mode == QLatin1String("select")) {
        changed = m_state.anchorPlacementEnabled();
        m_state.setAnchorPlacementEnabled(false);
    }
    if (changed)
        emit controlsChanged();
}

void NoteChainEditor::syncAnchorSelectionFromHostNotes()
{
    QSet<int> &lastSignature = m_state.lastHostSelectedNoteIds();
    if (!m_state.selectionTargetEnabled(QStringLiteral("notes"))
        || !m_state.selectionTargetEnabled(QStringLiteral("anchors"))) {
        lastSignature.clear();
        return;
    }
    if (!m_state.drag().mode.isEmpty() || m_state.boxSelect().active)
        return;

    QSet<int> signature;
    for (const QVariant &value : m_state.lastContext().value(QStringLiteral("selected_note_ids")).toList()) {
        const QString id = value.toString();
        if (!id.isEmpty())
            signature.insert(static_cast<int>(qHash(id)));
    }
    if (lastSignature == signature)
        return;
    lastSignature = signature;

    const QVariantList hostSelection = m_state.lastContext().value(QStringLiteral("selected_notes")).toList();
    struct Position { double beat = 0.0; int laneX = 0; };
    QVector<Position> positions;
    for (const QVariant &value : hostSelection) {
        const QVariantMap note = value.toMap();
        const QVariant beatRaw = note.value(QStringLiteral("beat"));
        bool beatOk = false;
        double beat = beatRaw.toDouble(&beatOk);
        if (!beatOk) {
            const QVariantList beatValue = beatRaw.toList();
            if (beatValue.size() >= 3 && beatValue[2].toInt() != 0) {
                beat = beatValue[0].toInt()
                     + static_cast<double>(beatValue[1].toInt()) / beatValue[2].toInt();
                beatOk = true;
            }
        }
        if (!beatOk)
            continue;
        const int laneX = qRound(note.value(QStringLiteral("x"), note.value(QStringLiteral("lane_x"))).toDouble());
        positions.append({beat, laneX});
    }
    if (positions.isEmpty() || m_state.anchors().isEmpty())
        return;

    QSet<int> usedAnchorIds;
    QVector<int> pickedAnchorIds;
    for (const Position &position : positions) {
        int bestId = -1;
        double bestDistanceSquared = std::numeric_limits<double>::infinity();
        for (const Anchor &anchor : m_state.anchors()) {
            if (usedAnchorIds.contains(anchor.id))
                continue;
            const double dx = anchor.laneX - position.laneX;
            const double dy = anchor.beat - position.beat;
            const double distanceSquared = dx * dx + dy * dy;
            if (distanceSquared < bestDistanceSquared) {
                bestDistanceSquared = distanceSquared;
                bestId = anchor.id;
            }
        }
        if (bestId > 0) {
            usedAnchorIds.insert(bestId);
            pickedAnchorIds.append(bestId);
        }
    }
    if (pickedAnchorIds.isEmpty())
        return;

    std::sort(pickedAnchorIds.begin(), pickedAnchorIds.end(), [this](int left, int right) {
        return m_state.anchorIndexById(left) < m_state.anchorIndexById(right);
    });
    m_state.clearAnchorSelection();
    for (int id : pickedAnchorIds)
        m_state.selectAnchor(id);
    emit needsRepaint();
}

bool NoteChainEditor::recordHistory()
{
    const StateSnapshot snapshot = m_state.captureSnapshot();
    if (m_historyIdx >= 0 && m_historyIdx < m_history.size() && m_history[m_historyIdx] == snapshot)
        return false;
    if (m_historyIdx < m_history.size() - 1)
        m_history.resize(m_historyIdx + 1);
    m_history.append(snapshot);
    if (m_history.size() > Const::kMaxHistory)
        m_history.removeFirst();
    m_historyIdx = m_history.size() - 1;
    return true;
}

bool NoteChainEditor::finishMutation(const QString &label)
{
    if (!recordHistory())
        return false;
    markDirty();
    emit requestHostUndoCheckpoint(checkpointLabel(label));
    return true;
}

void NoteChainEditor::markDirty()
{
    m_state.setProjectDirty(true);
    emit needsRepaint();
}

QVector<SampledPoint> NoteChainEditor::segmentSamples(const SegmentInfo &segment, int count) const
{
    if (count != 24)
        return sampleSegment(segment.a0, segment.a1, segment.shape, count);
    if (m_cachedCurveRevision != m_state.curveRevision()) {
        m_segmentSampleCache.clear();
        m_cachedCurveRevision = m_state.curveRevision();
    }
    const LinkKey key = makeLinkKey(segment.id0, segment.id1);
    auto found = m_segmentSampleCache.constFind(key);
    if (found != m_segmentSampleCache.cend())
        return found.value();
    const QVector<SampledPoint> samples = sampleSegment(segment.a0, segment.a1, segment.shape, count);
    m_segmentSampleCache.insert(key, samples);
    return samples;
}

int NoteChainEditor::findAnchorHit(const QPointF &canvasPos, const CanvasProjection &projection) const
{
    int bestIndex = -1;
    double bestDistance = Const::kAnchorHitRadius;
    for (int i = 0; i < m_state.anchors().size(); ++i) {
        const QPointF point = projection.chartToCanvas(m_state.anchorAt(i).pos());
        const double distance = ncDist(canvasPos.x(), canvasPos.y(), point.x(), point.y());
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }
    return bestIndex;
}

QPair<QString, int> NoteChainEditor::findHandleHit(const QPointF &canvasPos, const CanvasProjection &projection) const
{
    QPair<QString, int> best(QString(), -1);
    double bestDistance = Const::kHandleHitRadius;
    for (int i = 0; i < m_state.anchors().size(); ++i) {
        const Anchor &anchor = m_state.anchorAt(i);
        const QPointF inPoint = projection.chartToCanvas(anchor.inAbs());
        const QPointF outPoint = projection.chartToCanvas(anchor.outAbs());
        const double inDistance = ncDist(canvasPos.x(), canvasPos.y(), inPoint.x(), inPoint.y());
        if (inDistance < bestDistance) {
            best = {QStringLiteral("in"), i};
            bestDistance = inDistance;
        }
        const double outDistance = ncDist(canvasPos.x(), canvasPos.y(), outPoint.x(), outPoint.y());
        if (outDistance < bestDistance) {
            best = {QStringLiteral("out"), i};
            bestDistance = outDistance;
        }
    }
    return best;
}

QPair<int, int> NoteChainEditor::findSegmentHit(const QPointF &canvasPos, const CanvasProjection &projection) const
{
    QPair<int, int> best(-1, -1);
    double bestDistance = Const::kSegmentHitDist;
    for (const SegmentInfo &segment : m_state.connectedAnchorSegments()) {
        const QVector<SampledPoint> samples = segmentSamples(segment);
        if (samples.size() < 2)
            continue;
        QPointF previous = projection.chartToCanvas({samples.first().laneX, samples.first().beat});
        for (int i = 1; i < samples.size(); ++i) {
            const QPointF current = projection.chartToCanvas({samples[i].laneX, samples[i].beat});
            const double distance = ncPtSegDist(canvasPos.x(), canvasPos.y(),
                                                previous.x(), previous.y(), current.x(), current.y());
            if (distance < bestDistance) {
                bestDistance = distance;
                best = {segment.id0, segment.id1};
            }
            previous = current;
        }
    }
    return best;
}

QString NoteChainEditor::hoverCursorHint(const QPointF &canvasPos, const CanvasProjection &projection) const
{
    if (!m_active)
        return QString();
    if (m_state.linkDrag().active)
        return QStringLiteral("pointing_hand");
    if (!m_state.drag().mode.isEmpty()) {
        if (m_state.drag().mode == QLatin1String("anchor")) return QStringLiteral("size_all");
        return QStringLiteral("crosshair");
    }
    if (findHandleHit(canvasPos, projection).second >= 0)
        return QStringLiteral("pointing_hand");
    if (m_state.selectionEnabled(QStringLiteral("anchors")) && findAnchorHit(canvasPos, projection) >= 0)
        return QStringLiteral("pointing_hand");
    return QString();
}

bool NoteChainEditor::handleMousePress(const QPointF &canvasPos, const CanvasProjection &projection,
                                       int button, bool shift, bool ctrl)
{
    if (!m_active)
        return false;
    if (button == Const::kRightButton) {
        prepareContextMenuAt(canvasPos, projection);
        return false;
    }
    if (button != Const::kLeftButton || m_state.noteCurveSnapEnabled())
        return false;

    const QPair<QString, int> handleHit = findHandleHit(canvasPos, projection);
    const int anchorHit = m_state.selectionEnabled(QStringLiteral("anchors"))
                              ? findAnchorHit(canvasPos, projection) : -1;
    const QPair<int, int> segmentHit = m_state.selectionEnabled(QStringLiteral("segments"))
                                           ? findSegmentHit(canvasPos, projection) : QPair<int, int>(-1, -1);
    const bool blankHit = handleHit.second < 0 && anchorHit < 0 && segmentHit.first < 0;
    const bool hadSelection = !m_state.selectedAnchorIds().isEmpty() || !m_state.selectedLinkKeys().isEmpty();
    const bool singleAnchorFastChain = m_state.anchorPlacementEnabled()
                                    && m_state.selectedAnchorIds().size() == 1
                                    && m_state.selectedLinkKeys().isEmpty();
    if (blankHit && hadSelection && !singleAnchorFastChain) {
        m_state.clearAnchorSelection();
        m_state.clearLinkSelection();
        m_state.setPendingConnectAnchorId(-1);
        emit needsRepaint();
        return true;
    }

    const QVariantMap hostSelectionTool = m_state.lastContext().value(QStringLiteral("host_selection_tool")).toMap();
    const bool hostSelectMode = hostSelectionTool.value(QStringLiteral("is_select_mode"), false).toBool();
    const bool notesSelectable = m_state.selectionEnabled(QStringLiteral("notes"));
    if (blankHit && hostSelectMode && notesSelectable)
        return false;

    if (handleHit.second >= 0 && !shift) {
        m_state.drag() = {handleHit.first, handleHit.second};
        m_dragChanged = false;
        return true;
    }

    if (anchorHit >= 0) {
        const int anchorId = m_state.anchorAt(anchorHit).id;
        if (shift) {
            m_state.drag() = DragState{};
            m_state.linkDrag() = LinkDrag{true, anchorId, -1, canvasPos.x(), canvasPos.y()};
            return true;
        }
        if (ctrl)
            m_state.toggleAnchorSelection(anchorId);
        else
            m_state.selectAnchor(anchorId);
        m_state.clearLinkSelection();

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const bool doubleClick = m_state.lastClickAnchor() == anchorHit
                              && now - m_state.lastClickMs() <= Const::kDoubleClickMs;
        m_state.setLastClick(anchorHit, now);
        if (doubleClick) {
            Anchor &anchor = m_state.anchorAt(anchorHit);
            anchor.smooth = !anchor.smooth;
            m_state.invalidateCurveCache();
            finishMutation(tr("Toggle Anchor Smoothness"));
            return true;
        }
        m_state.drag() = {QStringLiteral("anchor"), anchorHit};
        m_state.setPendingConnectAnchorId(-1);
        m_dragChanged = false;
        emit needsRepaint();
        return true;
    }

    if (segmentHit.first >= 0) {
        const LinkKey key = makeLinkKey(segmentHit.first, segmentHit.second);
        if (ctrl)
            m_state.toggleLinkSelection(key);
        else {
            m_state.clearLinkSelection();
            m_state.selectLink(key);
        }
        if (m_state.selectionEnabled(QStringLiteral("anchors"))) {
            if (!ctrl) m_state.clearAnchorSelection();
            m_state.selectAnchor(segmentHit.first);
            m_state.selectAnchor(segmentHit.second);
        } else {
            m_state.clearAnchorSelection();
        }
        emit needsRepaint();
        return true;
    }

    if ((ctrl || hostSelectMode) && !notesSelectable) {
        m_state.boxSelect() = BoxSelect{true, canvasPos.x(), canvasPos.y(),
                                       canvasPos.x(), canvasPos.y(), ctrl};
        emit needsRepaint();
        return true;
    }
    if (notesSelectable && !m_state.anchorPlacementEnabled())
        return false;
    if (!m_state.anchorPlacementEnabled())
        return true;
    if (!projection.containsEditableCanvasPoint(canvasPos)) {
        m_state.clearAnchorSelection();
        m_state.clearLinkSelection();
        m_state.setPendingConnectAnchorId(-1);
        emit needsRepaint();
        return true;
    }

    const QPointF chartPoint = snappedChartPoint(m_state, projection.canvasToChart(canvasPos), true, true);
    const QSet<int> selectedBefore = m_state.selectedAnchorIds();
    const int newIndex = m_state.appendAnchor(chartPoint.x(), chartPoint.y());
    const int newId = m_state.anchorAt(newIndex).id;
    if (selectedBefore.size() == 1) {
        m_state.addLink(*selectedBefore.cbegin(), newId);
        m_state.setSingleSelectedAnchor(newId);
    } else {
        m_state.clearAnchorSelection();
    }
    m_state.setPendingConnectAnchorId(-1);
    m_state.cleanupLinksAndSelection();
    m_state.drag() = {QStringLiteral("anchor"), newIndex};
    m_dragChanged = true;
    markDirty();
    return true;
}

bool NoteChainEditor::handleMouseMove(const QPointF &canvasPos, const CanvasProjection &projection,
                                      int buttons, bool shift)
{
    if (!m_active)
        return false;
    LinkDrag &linkDrag = m_state.linkDrag();
    if (linkDrag.active) {
        linkDrag.x = canvasPos.x();
        linkDrag.y = canvasPos.y();
        linkDrag.hoverAnchorId = -1;
        if (m_state.selectionEnabled(QStringLiteral("anchors"))) {
            const int hit = findAnchorHit(canvasPos, projection);
            if (hit >= 0 && m_state.anchorAt(hit).id != linkDrag.sourceAnchorId)
                linkDrag.hoverAnchorId = m_state.anchorAt(hit).id;
        }
        return true;
    }

    DragState &drag = m_state.drag();
    if (shift && drag.mode == QLatin1String("anchor") && drag.index >= 0
        && drag.index < m_state.anchors().size() && (buttons & Qt::LeftButton)) {
        const int sourceId = m_state.anchorAt(drag.index).id;
        drag = DragState{};
        m_state.linkDrag() = LinkDrag{true, sourceId, -1, canvasPos.x(), canvasPos.y()};
        return true;
    }
    if (m_state.boxSelect().active) {
        m_state.boxSelect().endX = canvasPos.x();
        m_state.boxSelect().endY = canvasPos.y();
        return true;
    }
    if (drag.mode.isEmpty() || drag.index < 0 || drag.index >= m_state.anchors().size())
        return false;
    if (m_lastMoveTimer.isValid() && m_lastMoveTimer.elapsed() < kMoveThrottleMs)
        return true;
    m_lastMoveTimer.start();

    const QPointF chartPoint = projection.canvasToChart(canvasPos);
    const int timeDivision = qMax(1, m_state.lastContext().value(QStringLiteral("time_division"), 1).toInt());
    Anchor &anchor = m_state.anchorAt(drag.index);
    if (drag.mode == QLatin1String("anchor")) {
        const QPointF snapped = snappedChartPoint(m_state, chartPoint, true, false);
        anchor.laneX = snapped.x();
        anchor.beat = snapped.y();
        m_state.enforceAnchorAndConnectedHandleConstraints(drag.index, timeDivision);
    } else if (drag.mode == QLatin1String("in")) {
        m_state.setAnchorInAbsChart(drag.index, chartPoint.x(), chartPoint.y(), true);
        m_state.enforceHandleTimeConstraints(drag.index, timeDivision);
    } else if (drag.mode == QLatin1String("out")) {
        m_state.setAnchorOutAbsChart(drag.index, chartPoint.x(), chartPoint.y(), true);
        m_state.enforceHandleTimeConstraints(drag.index, timeDivision);
    }
    m_state.invalidateCurveCache();
    m_dragChanged = true;
    markDirty();
    return true;
}

bool NoteChainEditor::handleMouseRelease(const QPointF &canvasPos, const CanvasProjection &projection, int button)
{
    Q_UNUSED(button)
    if (!m_active)
        return false;
    LinkDrag &linkDrag = m_state.linkDrag();
    if (linkDrag.active) {
        const int sourceId = linkDrag.sourceAnchorId;
        const int targetId = linkDrag.hoverAnchorId;
        linkDrag = LinkDrag{};
        if (sourceId > 0 && targetId > 0 && sourceId != targetId && !m_state.hasLink(sourceId, targetId)) {
            m_state.addLink(sourceId, targetId);
            m_state.cleanupLinksAndSelection();
            finishMutation(tr("Connect Curve Segment"));
        }
        return true;
    }

    BoxSelect &box = m_state.boxSelect();
    if (box.active) {
        box.endX = canvasPos.x();
        box.endY = canvasPos.y();
        const QRectF selection = QRectF(QPointF(box.startX, box.startY),
                                        QPointF(box.endX, box.endY)).normalized();
        QSet<int> selectedAnchors = box.append ? m_state.selectedAnchorIds() : QSet<int>{};
        QSet<LinkKey> selectedLinks = box.append ? m_state.selectedLinkKeys() : QSet<LinkKey>{};
        for (const Anchor &anchor : m_state.anchors()) {
            if (selection.contains(projection.chartToCanvas(anchor.pos())))
                selectedAnchors.insert(anchor.id);
        }
        for (const SegmentInfo &segment : m_state.connectedAnchorSegments()) {
            const QVector<SampledPoint> samples = segmentSamples(segment);
            const bool intersects = std::any_of(samples.cbegin(), samples.cend(), [&](const SampledPoint &point) {
                return selection.contains(projection.chartToCanvas({point.laneX, point.beat}));
            });
            if (intersects)
                selectedLinks.insert(makeLinkKey(segment.id0, segment.id1));
        }
        m_state.clearAnchorSelection();
        m_state.clearLinkSelection();
        if (m_state.selectionEnabled(QStringLiteral("anchors")))
            for (int id : selectedAnchors) m_state.selectAnchor(id);
        if (m_state.selectionEnabled(QStringLiteral("segments")))
            for (const LinkKey &key : selectedLinks) m_state.selectLink(key);
        box = BoxSelect{};
        emit needsRepaint();
        return true;
    }

    DragState &drag = m_state.drag();
    if (drag.mode.isEmpty())
        return false;
    drag = DragState{};
    if (m_dragChanged)
        finishMutation(tr("Edit Curve"));
    m_dragChanged = false;
    return true;
}

bool NoteChainEditor::handleKeyDown(int key, bool shift, bool ctrl)
{
    Q_UNUSED(shift)
    if (!m_active)
        return false;
    if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
        deleteSelected();
        return true;
    }
    if (key == Qt::Key_Escape) {
        const bool hadInteraction = !m_state.drag().mode.isEmpty() || m_state.linkDrag().active || m_state.boxSelect().active;
        const bool hadSelection = !m_state.selectedAnchorIds().isEmpty() || !m_state.selectedLinkKeys().isEmpty();
        m_state.drag() = DragState{};
        m_state.linkDrag() = LinkDrag{};
        m_state.boxSelect() = BoxSelect{};
        m_state.clearAnchorSelection();
        m_state.clearLinkSelection();
        m_state.setPendingConnectAnchorId(-1);
        m_dragChanged = false;
        if (hadInteraction || hadSelection) emit needsRepaint();
        return hadInteraction || hadSelection;
    }
    if (key == Qt::Key_A && !ctrl) {
        toggleAnchorPlacement();
        return true;
    }
    return false;
}

bool NoteChainEditor::commitLinksToNotes(const QSet<LinkKey> *targetLinks)
{
    if (!m_chartCtrl)
        return false;
    const QVector<SegmentInfo> segments = m_state.connectedAnchorSegments();
    if (segments.isEmpty() || (targetLinks && targetLinks->isEmpty()))
        return false;

    if (!m_sidecarPath.isEmpty() && m_state.projectDirty()) {
        QString error;
        if (!NoteChainPersistence::saveToFile(m_state, m_sidecarPath, &error)) {
            emit statusMessage(tr("Failed to save curve project: %1").arg(error));
            return false;
        }
    }

    QSet<QString> existing;
    if (const Chart *chart = m_chartCtrl->chart()) {
        for (const Note &note : chart->notes()) {
            const QString key = normalNotePositionKey(note);
            if (!key.isEmpty()) existing.insert(key);
        }
    }
    QSet<QString> seen;
    QVector<Note> notes;
    const int fallbackDenominator = defaultCommitDenominator(m_state);
    for (const SegmentInfo &segment : segments) {
        const LinkKey key = makeLinkKey(segment.id0, segment.id1);
        if (targetLinks && !targetLinks->contains(key))
            continue;
        const int denominator = m_state.segmentDensityMode(segment.id0, segment.id1) == 0
                                    ? fallbackDenominator : qMax(1, segment.denominator);
        const QVector<SampledPoint> samplesByBeat = normalizeSamplesByBeat(segmentSamples(segment, 32));
        if (samplesByBeat.size() < 2)
            continue;
        const double lowBeat = samplesByBeat.first().beat;
        const double highBeat = samplesByBeat.last().beat;
        int startTick = qRound(lowBeat * denominator);
        int endTick = qRound(highBeat * denominator);
        if (endTick < startTick) qSwap(startTick, endTick);
        for (int tick = startTick; tick <= endTick; ++tick) {
            const double beat = static_cast<double>(tick) / denominator;
            if (beat < lowBeat || beat > highBeat)
                continue;
            const QVector<int> triplet = floatBeatToTriplet(beat, denominator);
            Note note;
            note.beatNum = triplet[0];
            note.numerator = triplet[1];
            note.denominator = triplet[2];
            note.x = qRound(ncClamp(laneXAtBeat(samplesByBeat, beat), 0.0, Const::kLaneWidth));
            note.type = NoteType::NORMAL;
            const QString positionKey = normalNotePositionKey(note);
            if (positionKey.isEmpty() || existing.contains(positionKey) || seen.contains(positionKey))
                continue;
            seen.insert(positionKey);
            notes.append(note);
        }
    }
    if (notes.isEmpty())
        return false;
    std::sort(notes.begin(), notes.end(), [](const Note &left, const Note &right) {
        const double leftBeat = left.getStartBeat();
        const double rightBeat = right.getStartBeat();
        if (qAbs(leftBeat - rightBeat) <= 1e-9) return left.x < right.x;
        return leftBeat < rightBeat;
    });
    return m_chartCtrl->applyBatchEdit(tr("Commit Curve -> Notes"), notes, {}, {});
}

bool NoteChainEditor::commitCurveToNotes() { return commitLinksToNotes(nullptr); }
bool NoteChainEditor::commitContextSegmentsToNotes() { return commitLinksToNotes(&m_contextLinkKeys); }

void NoteChainEditor::setAnchorPlacementEnabled(bool enabled)
{
    if (m_state.anchorPlacementEnabled() == enabled)
        return;
    m_state.setAnchorPlacementEnabled(enabled);
    emit controlsChanged();
    emit needsRepaint();
}

void NoteChainEditor::setCurveVisible(bool visible)
{
    if (m_state.curveVisible() == visible)
        return;
    m_state.setCurveVisible(visible);
    emit controlsChanged();
    emit needsRepaint();
}

void NoteChainEditor::setPolylineMode(bool polyline)
{
    const QString shape = polyline ? QStringLiteral("polyline") : QStringLiteral("curve");
    if (!m_state.selectedLinkKeys().isEmpty()) {
        bool changed = false;
        for (const LinkKey &key : m_state.selectedLinkKeys()) {
            if (m_state.segmentShape(key.first, key.second) != shape) {
                m_state.setSegmentShape(key.first, key.second, shape);
                changed = true;
            }
        }
        if (changed) finishMutation(tr("Change Curve Shape"));
        emit controlsChanged();
        return;
    }
    if (m_state.activeLinkShape() == shape)
        return;
    m_state.setActiveLinkShape(shape);
    markDirty();
    emit controlsChanged();
}

void NoteChainEditor::setNoteCurveSnapEnabled(bool enabled)
{
    if (m_state.noteCurveSnapEnabled() == enabled)
        return;
    m_state.setNoteCurveSnapEnabled(enabled);
    markDirty();
    emit controlsChanged();
}

void NoteChainEditor::setSelectAnchorsEnabled(bool enabled)
{
    if (m_state.selectionTargetEnabled(QStringLiteral("anchors")) == enabled)
        return;
    m_state.setSelectionEnabled(QStringLiteral("anchors"), enabled);
    emit controlsChanged();
    emit needsRepaint();
}

void NoteChainEditor::setSelectSegmentsEnabled(bool enabled)
{
    if (m_state.selectionTargetEnabled(QStringLiteral("segments")) == enabled)
        return;
    m_state.setSelectionEnabled(QStringLiteral("segments"), enabled);
    emit controlsChanged();
    emit needsRepaint();
}

void NoteChainEditor::setSelectNotesEnabled(bool enabled)
{
    m_state.setSelectionEnabled(QStringLiteral("notes"), enabled);
    emit controlsChanged();
}

void NoteChainEditor::toggleAnchorPlacement() { setAnchorPlacementEnabled(!m_state.anchorPlacementEnabled()); }
void NoteChainEditor::toggleCurveVisible() { setCurveVisible(!m_state.curveVisible()); }
void NoteChainEditor::togglePolylineMode() { setPolylineMode(m_state.activeLinkShape() != QLatin1String("polyline")); }
void NoteChainEditor::toggleNoteCurveSnap() { setNoteCurveSnapEnabled(!m_state.noteCurveSnapEnabled()); }
void NoteChainEditor::toggleSelectAnchors() { setSelectAnchorsEnabled(!m_state.selectionTargetEnabled(QStringLiteral("anchors"))); }
void NoteChainEditor::toggleSelectSegments() { setSelectSegmentsEnabled(!m_state.selectionTargetEnabled(QStringLiteral("segments"))); }
void NoteChainEditor::toggleSelectNotes() { setSelectNotesEnabled(!m_state.selectionTargetEnabled(QStringLiteral("notes"))); }

void NoteChainEditor::prepareContextMenuAt(const QPointF &canvasPos, const CanvasProjection &projection)
{
    const QPair<int, int> hit = findSegmentHit(canvasPos, projection);
    const QSet<LinkKey> selected = m_state.selectedLinkKeys();
    m_contextLinkKeys.clear();
    if (hit.first >= 0) {
        const LinkKey hitKey = makeLinkKey(hit.first, hit.second);
        if (selected.contains(hitKey)) m_contextLinkKeys = selected;
        else m_contextLinkKeys.insert(hitKey);
    } else {
        m_contextLinkKeys = selected;
    }
}

bool NoteChainEditor::hasSelectedItems() const
{
    return !m_state.selectedAnchorIds().isEmpty() || !m_state.selectedLinkKeys().isEmpty();
}

bool NoteChainEditor::hasSelectedSegments() const { return !m_state.selectedLinkKeys().isEmpty(); }

int NoteChainEditor::densityForLinks(const QSet<LinkKey> &links) const
{
    if (links.isEmpty())
        return -2;
    bool first = true;
    int signature = -2;
    for (const LinkKey &key : links) {
        const int value = m_state.segmentDensityMode(key.first, key.second) == 0
                              ? 0 : m_state.segmentDen(key.first, key.second);
        if (first) {
            signature = value;
            first = false;
        } else if (signature != value) {
            return -1;
        }
    }
    return signature;
}

int NoteChainEditor::selectedSegmentDensity() const { return densityForLinks(m_state.selectedLinkKeys()); }
int NoteChainEditor::contextSegmentDensity() const { return densityForLinks(m_contextLinkKeys); }

bool NoteChainEditor::setDensityForLinks(const QSet<LinkKey> &links, int denominator, const QString &label)
{
    if (links.isEmpty())
        return false;
    bool changed = false;
    for (const LinkKey &key : links) {
        const int before = m_state.segmentDensityMode(key.first, key.second) == 0
                               ? 0 : m_state.segmentDen(key.first, key.second);
        if (before == qMax(0, denominator))
            continue;
        if (denominator <= 0) m_state.setDensityMode(key.first, key.second, 0);
        else m_state.setSegmentDen(key.first, key.second, denominator);
        changed = true;
    }
    return changed && finishMutation(label);
}

bool NoteChainEditor::setSelectedSegmentDensity(int denominator)
{
    return setDensityForLinks(m_state.selectedLinkKeys(), denominator, tr("Change Curve Density"));
}

bool NoteChainEditor::setContextSegmentDensity(int denominator)
{
    return setDensityForLinks(m_contextLinkKeys, denominator, tr("Change Curve Density"));
}

bool NoteChainEditor::toggleShapeForLinks(const QSet<LinkKey> &links, const QString &label)
{
    if (links.isEmpty())
        return false;
    bool allPolyline = true;
    for (const LinkKey &key : links) {
        if (m_state.segmentShape(key.first, key.second) != QLatin1String("polyline")) {
            allPolyline = false;
            break;
        }
    }
    const QString nextShape = allPolyline ? QStringLiteral("curve") : QStringLiteral("polyline");
    for (const LinkKey &key : links)
        m_state.setSegmentShape(key.first, key.second, nextShape);
    return finishMutation(label);
}

bool NoteChainEditor::toggleSelectedSegmentShape()
{
    return toggleShapeForLinks(m_state.selectedLinkKeys(), tr("Change Curve Shape"));
}

bool NoteChainEditor::toggleContextSegmentShape()
{
    return toggleShapeForLinks(m_contextLinkKeys, tr("Change Curve Shape"));
}

void NoteChainEditor::connectSelectedAnchors()
{
    QList<int> ids = m_state.selectedAnchorIds().values();
    std::sort(ids.begin(), ids.end(), [this](int left, int right) {
        return m_state.anchorIndexById(left) < m_state.anchorIndexById(right);
    });
    bool changed = false;
    for (int i = 0; i + 1 < ids.size(); ++i) {
        if (!m_state.hasLink(ids[i], ids[i + 1])) {
            m_state.addLink(ids[i], ids[i + 1]);
            changed = true;
        }
    }
    if (changed) {
        m_state.seedMissingSegmentDenominators();
        finishMutation(tr("Connect Curve Segments"));
    }
}

void NoteChainEditor::disconnectSelectedSegments()
{
    const QSet<LinkKey> selected = m_state.selectedLinkKeys();
    if (selected.isEmpty())
        return;
    for (const LinkKey &key : selected)
        m_state.removeLink(key.first, key.second);
    m_state.clearLinkSelection();
    finishMutation(tr("Disconnect Curve Segments"));
}

void NoteChainEditor::deleteSelected()
{
    const QSet<int> selectedAnchors = m_state.selectedAnchorIds();
    const QSet<LinkKey> selectedLinks = m_state.selectedLinkKeys();
    if (selectedAnchors.isEmpty() && selectedLinks.isEmpty())
        return;
    for (const LinkKey &key : selectedLinks)
        m_state.removeLink(key.first, key.second);
    for (int id : selectedAnchors)
        m_state.removeAnchorById(id);
    m_state.clearAnchorSelection();
    m_state.clearLinkSelection();
    finishMutation(tr("Delete Curve Selection"));
}

void NoteChainEditor::resetCurve()
{
    if (m_state.anchors().isEmpty() && m_state.linksAll().isEmpty())
        return;
    QVector<int> anchorIds;
    for (const Anchor &anchor : m_state.anchors()) anchorIds.append(anchor.id);
    for (int id : anchorIds) m_state.removeAnchorById(id);
    m_state.clearLinks();
    const NoteChainState defaults;
    m_state.setNodeGroups(defaults.nodeGroups());
    m_state.setCurveGroups(defaults.curveGroups());
    m_state.setNextCurveId(1);
    m_contextLinkKeys.clear();
    finishMutation(tr("Reset Curve"));
}

bool NoteChainEditor::snapLaneXAtBeat(double beat, double preferredLaneX, double *outLaneX) const
{
    if (!outLaneX || !m_state.noteCurveSnapEnabled())
        return false;
    bool found = false;
    double bestDistance = std::numeric_limits<double>::infinity();
    double bestLaneX = preferredLaneX;
    for (const SegmentInfo &segment : m_state.connectedAnchorSegments()) {
        const QVector<SampledPoint> samples = normalizeSamplesByBeat(segmentSamples(segment));
        if (samples.size() < 2 || beat < samples.first().beat - 1e-7 || beat > samples.last().beat + 1e-7)
            continue;
        const double laneX = laneXAtBeat(samples, beat);
        const double distance = qAbs(laneX - preferredLaneX);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestLaneX = laneX;
            found = true;
        }
    }
    if (found) *outLaneX = bestLaneX;
    return found;
}

bool NoteChainEditor::exportStylePreset(const QString &path, QString *errorMessage) const
{
    if (path.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = tr("Empty style path");
        return false;
    }
    QJsonArray denominators;
    for (int denominator : m_state.style().denominators)
        if (denominator > 0) denominators.append(denominator);
    QJsonObject payload;
    payload["style_name"] = m_state.style().name;
    payload["denominators"] = denominators;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    const QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit()) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    return true;
}

bool NoteChainEditor::importStylePreset(const QString &path, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) *errorMessage = parseError.errorString();
        return false;
    }
    StylePreset style;
    style.name = document.object().value("style_name").toString(QStringLiteral("imported"));
    QSet<int> seen;
    for (const QJsonValue &value : document.object().value("denominators").toArray()) {
        const int denominator = value.toInt();
        if (denominator > 0 && denominator <= 288 && !seen.contains(denominator)) {
            seen.insert(denominator);
            style.denominators.append(denominator);
        }
    }
    if (style.denominators.isEmpty()) {
        if (errorMessage) *errorMessage = tr("Style has no valid denominators");
        return false;
    }
    m_state.setStyle(style);
    markDirty();
    return true;
}

void NoteChainEditor::render(QPainter *painter, const QRectF &viewport, const CanvasProjection &projection)
{
    if (!painter || !m_active || !m_state.curveVisible() || !m_state.overlayToggles().enabled)
        return;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const OverlayToggles &toggles = m_state.overlayToggles();
    const QRectF paddedViewport = viewport.adjusted(-32.0, -32.0, 32.0, 32.0);

    if (toggles.preview) {
        for (const SegmentInfo &segment : m_state.connectedAnchorSegments()) {
            const QVector<SampledPoint> samples = segmentSamples(segment);
            if (samples.size() < 2)
                continue;
            QRectF bounds;
            QVector<QPointF> canvasSamples;
            canvasSamples.reserve(samples.size());
            for (const SampledPoint &sample : samples) {
                const QPointF point = projection.chartToCanvas({sample.laneX, sample.beat});
                canvasSamples.append(point);
                bounds = bounds.isNull() ? QRectF(point, QSizeF(1.0, 1.0)) : bounds.united(QRectF(point, QSizeF(1.0, 1.0)));
            }
            if (!bounds.intersects(paddedViewport))
                continue;
            const bool selected = m_state.isLinkSelected(makeLinkKey(segment.id0, segment.id1));
            painter->setPen(QPen(selected ? QColor(255, 214, 107) : QColor(51, 204, 255, 220),
                                 selected ? 3.0 : 2.0));
            for (int i = 0; i + 1 < canvasSamples.size(); ++i)
                painter->drawLine(canvasSamples[i], canvasSamples[i + 1]);
            if (toggles.samplePoints) {
                painter->setPen(QPen(QColor(255, 255, 255, 136), 1.0));
                painter->setBrush(QColor(255, 255, 255, 136));
                for (int i = 0; i < canvasSamples.size(); i += 4)
                    painter->drawRect(QRectF(canvasSamples[i].x() - 2.0, canvasSamples[i].y() - 2.0, 4.0, 4.0));
            }
        }
    }

    for (int i = 0; i < m_state.anchors().size(); ++i) {
        const Anchor &anchor = m_state.anchorAt(i);
        const QPointF anchorPoint = projection.chartToCanvas(anchor.pos());
        if (!paddedViewport.contains(anchorPoint))
            continue;
        const bool selected = m_state.isAnchorSelected(anchor.id);
        const bool dragging = m_state.drag().mode == QLatin1String("anchor") && m_state.drag().index == i;
        if (toggles.handles) {
            const QPointF inPoint = projection.chartToCanvas(anchor.inAbs());
            const QPointF outPoint = projection.chartToCanvas(anchor.outAbs());
            painter->setPen(QPen(QColor(160, 160, 160, 102), 1.0));
            painter->drawLine(anchorPoint, inPoint);
            painter->drawLine(anchorPoint, outPoint);
            painter->setPen(QPen(Qt::white, 1.0));
            painter->setBrush(QColor(238, 170, 85, 170));
            painter->drawRect(QRectF(inPoint.x() - 4.0, inPoint.y() - 4.0, 8.0, 8.0));
            painter->drawRect(QRectF(outPoint.x() - 4.0, outPoint.y() - 4.0, 8.0, 8.0));
        }
        if (toggles.controlPoints) {
            const QColor fill = selected ? QColor(255, 155, 47, 190)
                              : dragging ? QColor(0, 119, 255, 190)
                                         : QColor(0, 163, 255, 190);
            painter->setPen(QPen(Qt::white, selected ? 2.5 : 1.5));
            painter->setBrush(fill);
            painter->drawRect(QRectF(anchorPoint.x() - 6.0, anchorPoint.y() - 6.0, 12.0, 12.0));
        }
        if (toggles.labels) {
            painter->setPen(Qt::white);
            QFont font = painter->font();
            font.setPixelSize(12);
            painter->setFont(font);
            painter->drawText(anchorPoint + QPointF(8.0, -4.0),
                              QStringLiteral("A%1(%2)").arg(i).arg(anchor.smooth ? tr("Smooth") : tr("Corner")));
        }
    }

    if (m_state.boxSelect().active) {
        const BoxSelect &box = m_state.boxSelect();
        const QRectF rect = QRectF(QPointF(box.startX, box.startY), QPointF(box.endX, box.endY)).normalized();
        painter->setPen(QPen(QColor(255, 204, 102), 1.5, Qt::DashLine));
        painter->setBrush(QColor(255, 204, 102, 50));
        painter->drawRect(rect);
    }

    if (m_state.linkDrag().active) {
        const LinkDrag &drag = m_state.linkDrag();
        const int sourceIndex = m_state.anchorIndexById(drag.sourceAnchorId);
        if (sourceIndex >= 0) {
            const QPointF source = projection.chartToCanvas(m_state.anchorAt(sourceIndex).pos());
            QPointF target(drag.x, drag.y);
            const int hoverIndex = m_state.anchorIndexById(drag.hoverAnchorId);
            if (hoverIndex >= 0) {
                target = projection.chartToCanvas(m_state.anchorAt(hoverIndex).pos());
                painter->setPen(QPen(QColor(255, 224, 138), 2.0));
                painter->setBrush(QColor(255, 224, 138, 50));
                painter->drawRect(QRectF(target.x() - 8.0, target.y() - 8.0, 16.0, 16.0));
            }
            painter->setPen(QPen(QColor(255, 224, 138), 2.0, Qt::DashLine));
            painter->drawLine(source, target);
        }
    }

    if (toggles.labels) {
        const int editorDivision = defaultCommitDenominator(m_state);
        const int selectedDensity = selectedSegmentDensity();
        QString spacingText;
        if (selectedDensity == -1)
            spacingText = tr("Selected segments: mixed spacing");
        else if (selectedDensity == 0)
            spacingText = tr("Selected segments: follow Time Division (1/%1)").arg(editorDivision);
        else if (selectedDensity > 0)
            spacingText = tr("Selected segments: fixed 1/%1").arg(selectedDensity);
        else
            spacingText = tr("Generated notes follow Time Division (1/%1)").arg(editorDivision);
        painter->setPen(QColor(221, 238, 255));
        QFont font = painter->font();
        font.setPixelSize(12);
        painter->setFont(font);
        painter->drawText(QPointF(16.0, 18.0),
                          tr("%1  |  Anchor placement: %2")
                              .arg(spacingText,
                                   m_state.anchorPlacementEnabled() ? tr("ON") : tr("OFF")));
    }
    painter->restore();
}

bool NoteChainEditor::canUndo() const { return m_historyIdx > 0; }
bool NoteChainEditor::canRedo() const { return m_historyIdx >= 0 && m_historyIdx < m_history.size() - 1; }

void NoteChainEditor::undo()
{
    if (!canUndo())
        return;
    --m_historyIdx;
    m_state.restoreSnapshot(m_history[m_historyIdx]);
    m_state.setProjectDirty(true);
    emit controlsChanged();
    emit needsRepaint();
}

void NoteChainEditor::redo()
{
    if (!canRedo())
        return;
    ++m_historyIdx;
    m_state.restoreSnapshot(m_history[m_historyIdx]);
    m_state.setProjectDirty(true);
    emit controlsChanged();
    emit needsRepaint();
}

void NoteChainEditor::onHostUndo(const QString &actionText) { if (isCurveCheckpoint(actionText)) undo(); }
void NoteChainEditor::onHostRedo(const QString &actionText) { if (isCurveCheckpoint(actionText)) redo(); }

bool NoteChainEditor::loadProject(const QString &path)
{
    const QString effectivePath = path.trimmed();
    if (effectivePath.isEmpty())
        return false;
    if (effectivePath == m_sidecarPath)
        return true;

    if (!m_sidecarPath.isEmpty() && m_state.projectDirty()) {
        QString saveError;
        if (!NoteChainPersistence::saveToFile(m_state, m_sidecarPath, &saveError)) {
            emit statusMessage(tr("Failed to save previous curve project: %1").arg(saveError));
            return false;
        }
    }

    // Project switching is transactional: a malformed sidecar must not
    // replace the live curve or redirect later saves to the bad file.
    NoteChainState loadedState;
    if (!QFileInfo::exists(effectivePath)) {
        loadedState.setProjectPath(effectivePath);
        loadedState.setProjectDirty(false);
    } else {
        QString error;
        if (!NoteChainPersistence::loadFromFile(effectivePath, loadedState, &error)) {
            emit statusMessage(tr("Failed to load curve project: %1").arg(error));
            return false;
        }
    }

    m_state = std::move(loadedState);
    m_sidecarPath = effectivePath;
    m_history.clear();
    m_historyIdx = -1;
    recordHistory();
    m_contextLinkKeys.clear();
    m_lastHostMode.clear();
    m_cachedCurveRevision = 0;
    m_segmentSampleCache.clear();
    emit controlsChanged();
    emit needsRepaint();
    return true;
}

bool NoteChainEditor::saveProject(const QString &path)
{
    const QString effectivePath = path.isEmpty() ? m_sidecarPath : path;
    if (effectivePath.isEmpty())
        return false;
    QString error;
    if (!NoteChainPersistence::saveToFile(m_state, effectivePath, &error)) {
        emit statusMessage(tr("Failed to save curve project: %1").arg(error));
        return false;
    }
    m_sidecarPath = effectivePath;
    return true;
}

} // namespace NoteChain
