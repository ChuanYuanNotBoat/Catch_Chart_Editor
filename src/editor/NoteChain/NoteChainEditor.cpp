// NoteChainEditor.cpp - main editor implementation (based on Python input_handler.py)
#include "NoteChainEditor.h"
#include "NoteChainCurveSampler.h"
#include "NoteChainPersistence.h"
#include "controller/ChartController.h"
#include "model/Note.h"
#include <QDateTime>
#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <numeric>
namespace NoteChain {

namespace {
QString normalNotePositionKey(const Note &note)
{
    if (note.type != NoteType::NORMAL || note.denominator == 0)
        return QString();
    qint64 num = static_cast<qint64>(note.beatNum) * note.denominator + note.numerator;
    qint64 den = note.denominator;
    if (den < 0) {
        den = -den;
        num = -num;
    }
    const qint64 g = std::gcd(num < 0 ? -num : num, den);
    if (g > 0) {
        num /= g;
        den /= g;
    }
    return QStringLiteral("%1/%2:%3").arg(num).arg(den).arg(note.x);
}

int defaultCommitDenominator(const NoteChainState &state)
{
    const QVariantMap ctx = state.lastContext();
    const int overrideDen = ctx.value(QStringLiteral("plugin_time_division_override"), 0).toInt();
    if (overrideDen > 0)
        return overrideDen;
    return qMax(1, ctx.value(QStringLiteral("time_division"), 4).toInt());
}
}

NoteChainEditor::NoteChainEditor(QObject *parent) : QObject(parent) {}

void NoteChainEditor::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    if (active) {
        m_state.setCurveVisible(true);
    } else {
        // auto-save on deactivate
        if (!m_sidecarPath.isEmpty() && m_state.projectDirty())
            NoteChainPersistence::saveToFile(m_state, m_sidecarPath);
        m_state.drag() = DragState{};
        m_state.linkDrag() = LinkDrag{};
        m_state.boxSelect() = BoxSelect{};
        m_state.clearAnchorSelection();
        m_state.clearLinkSelection();
    }
}

void NoteChainEditor::setHostContext(const QVariantMap &ctx) {
    m_state.setLastContext(ctx);
    syncAnchorPlacementWithHostMode();
    syncAnchorSelectionFromHostNotes();
}

void NoteChainEditor::syncAnchorPlacementWithHostMode() {
    QVariantMap ctx = m_state.lastContext();
    QVariantMap hostSel = ctx.value("host_selection_tool").toMap();
    QString mode = hostSel.value("mode").toString().trimmed().toLower();
    if (mode == "anchor_place") m_state.setAnchorPlacementEnabled(true);
    else if (mode == "place_note" || mode == "place_rain" || mode == "delete" || mode == "select")
        m_state.setAnchorPlacementEnabled(false);
}

void NoteChainEditor::syncAnchorSelectionFromHostNotes() {
    QVariantMap ctx = m_state.lastContext();
    QVariantList hostSel = ctx.value("selected_notes").toList();
    // Build set of (beat_key, x) positions from host-selected notes
    struct PosKey { double beat; int x;
        bool operator<(const PosKey &o) const { return beat < o.beat || (qAbs(beat - o.beat) < 1e-9 && x < o.x); }
        bool operator==(const PosKey &o) const { return qAbs(beat - o.beat) < 1e-9 && x == o.x; }
    };
    QSet<int> ids;
    QVector<PosKey> hostKeys;
    for (auto &v : hostSel) {
        QVariantMap note = v.toMap();
        QVariantList beat = note.value("beat").toList();
        if (beat.size() < 3) continue;
        double b = static_cast<double>(beat[0].toInt()) + static_cast<double>(beat[1].toInt()) / qMax(1, beat[2].toInt());
        int x = qRound(note.value("x", note.value("lane_x", 0)).toDouble());
        hostKeys.append({b, x});
        ids.insert(note.value("id", -1).toInt());
    }
    // Only update if host selection actually changed
    auto &last = m_state.lastHostSelectedNoteIds();
    if (last == ids) return;
    last = ids;
    // Match host notes to anchors: if a host note is close to an anchor, select that anchor
    if (hostKeys.isEmpty()) { m_state.clearAnchorSelection(); m_state.invalidateCurveCache(); return; }
    m_state.clearAnchorSelection();
    const double beatTol = 0.1; const int xTol = 8;
    for (const auto &hk : hostKeys) {
        for (int i = 0; i < m_state.anchors().size(); ++i) {
            const auto &a = m_state.anchorAt(i);
            if (qAbs(a.beat - hk.beat) < beatTol && qAbs(qRound(a.laneX) - hk.x) < xTol) {
                m_state.selectAnchor(a.id);
                break;
            }
        }
    }
    m_state.invalidateCurveCache();
}

void NoteChainEditor::recordHistory() {
    StateSnapshot snap = m_state.captureSnapshot();
    // dedup
    if (m_historyIdx >= 0 && m_historyIdx < m_history.size() && m_history[m_historyIdx] == snap) return;
    if (m_historyIdx < m_history.size() - 1) m_history.resize(m_historyIdx + 1);
    m_history.append(snap);
    if (m_history.size() > Const::kMaxHistory) m_history.removeFirst();
    m_historyIdx = m_history.size() - 1;
}

void NoteChainEditor::markDirty() { m_state.setProjectDirty(true); emit needsRepaint(); }

// ====== Hit testing ======
int NoteChainEditor::findAnchorHit(double cx, double cy) const {
    int best = -1; double bestDist = Const::kAnchorHitRadius;
    for (int i = 0; i < m_state.anchors().size(); ++i) {
        const auto &a = m_state.anchorAt(i);
        double d = ncDist(cx, cy, a.laneX, a.beat);
        if (d < bestDist) { best = i; bestDist = d; }
    }
    return best;
}

QPair<QString,int> NoteChainEditor::findHandleHit(double cx, double cy) const {
    QPair<QString,int> best("", -1); double bestDist = Const::kHandleHitRadius;
    for (int i = 0; i < m_state.anchors().size(); ++i) {
        const auto &a = m_state.anchorAt(i);
        if (a.hasInHandle()) {
            QPointF p = a.inAbs(); double d = ncDist(cx, cy, p.x(), p.y());
            if (d < bestDist) { best = {"in", i}; bestDist = d; }
        }
        if (a.hasOutHandle()) {
            QPointF p = a.outAbs(); double d = ncDist(cx, cy, p.x(), p.y());
            if (d < bestDist) { best = {"out", i}; bestDist = d; }
        }
    }
    return best;
}

QPair<int,int> NoteChainEditor::findSegmentHit(double cx, double cy) const {
    QVector<SegmentInfo> segs = m_state.connectedAnchorSegments();
    QPair<int,int> best(-1, -1); double bestDist = Const::kSegmentHitDist;
    for (const auto &seg : segs) {
        auto pts = sampleSegment(seg.a0, seg.a1, seg.shape, Const::kMaxSamplesPerSeg);
        for (int j = 0; j < pts.size() - 1; ++j) {
            double d = ncPtSegDist(cx, cy, pts[j].laneX, pts[j].beat, pts[j+1].laneX, pts[j+1].beat);
            if (d < bestDist) { best = {seg.id0, seg.id1}; bestDist = d; }
        }
    }
    return best;
}

// ====== Cursor hint (P0-2) ======
QString NoteChainEditor::hoverCursorHint(double cx, double cy) const {
    if (!m_active) return QString();
    // Link drag: always pointing_hand
    if (m_state.linkDrag().active) return QStringLiteral("pointing_hand");
    // Handle hit
    auto hh = const_cast<NoteChainEditor*>(this)->findHandleHit(cx, cy);
    if (hh.second >= 0) return QStringLiteral("pointing_hand");
    // Anchor hit
    if (m_state.selectionEnabled("anchors")) {
        int ah = const_cast<NoteChainEditor*>(this)->findAnchorHit(cx, cy);
        if (ah >= 0) return QStringLiteral("pointing_hand");
    }
    // Drag in progress
    const auto &d = m_state.drag();
    if (d.mode == "anchor") return QStringLiteral("size_all");
    if (d.mode == "in" || d.mode == "out") return QStringLiteral("crosshair");
    return QString();
}

// ====== Mouse events ======
bool NoteChainEditor::handleMousePress(double cx, double cy, int button, bool shift, bool ctrl) {
    if (!m_active || button != Const::kLeftButton) return false;
    // note_curve_snap: passthrough left-button events to host for note selection
    if (m_state.noteCurveSnapEnabled()) return false;

    // P0-3: snap chart point from host context
    auto snapPoint = [this](double lx, double bt) -> QPointF {
        QVariantMap ctx = m_state.lastContext();
        bool gridSnap = ctx.value("grid_snap", false).toBool();
        int gridDiv = qMax(1, ctx.value("grid_division", 8).toInt());
        int timeDiv = qMax(1, ctx.value("time_division", 1).toInt());
        lx = ncClamp(lx, 0.0, Const::kLaneWidth);
        if (gridSnap && gridDiv > 0) lx = qRound((lx / Const::kLaneWidth) * gridDiv) * (Const::kLaneWidth / gridDiv);
        bt = qMax(0.0, bt);
        bt = qRound(bt * timeDiv) / static_cast<double>(timeDiv);
        return {lx, bt};
    };
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    auto hHit = findHandleHit(cx, cy);
    int aHit = m_state.selectionEnabled("anchors") ? findAnchorHit(cx, cy) : -1;
    auto segHit = m_state.selectionEnabled("segments") ? findSegmentHit(cx, cy) : QPair<int,int>(-1,-1);
    if (hHit.second >= 0 && !shift) { m_state.drag() = {hHit.first, hHit.second}; recordHistory(); return true; }
    if (aHit >= 0) { int anchorId = m_state.anchorAt(aHit).id;
        if (shift) { m_state.drag() = DragState{}; m_state.linkDrag() = LinkDrag{true, anchorId, -1, cx, cy}; return true; }
        bool isDouble = (m_state.lastClickAnchor() == aHit && (now - m_state.lastClickMs()) <= Const::kDoubleClickMs);
        m_state.setLastClick(aHit, now);
        if (isDouble) { auto &a = m_state.anchorAt(aHit); a.smooth = !a.smooth; m_state.invalidateCurveCache(); recordHistory(); return true; }
        if (ctrl) m_state.toggleAnchorSelection(anchorId); else { m_state.clearAnchorSelection(); m_state.clearLinkSelection(); m_state.selectAnchor(anchorId); }
        m_state.drag() = {"anchor", aHit}; m_state.setPendingConnectAnchorId(-1); recordHistory(); return true; }
    if (segHit.first >= 0) { LinkKey k = makeLinkKey(segHit.first, segHit.second); if (ctrl) m_state.toggleLinkSelection(k); else { m_state.clearAnchorSelection(); m_state.clearLinkSelection(); m_state.selectLink(k); } return true; }
    bool isSM = m_state.lastContext().value("host_selection_tool").toMap().value("is_select_mode", false).toBool();
    bool ns = m_state.selectionEnabled("notes");
    if ((ctrl || isSM) && !ns) { m_state.boxSelect() = BoxSelect{true, cx, cy, cx, cy, ctrl}; return true; }
    if (ns && !m_state.anchorPlacementEnabled()) return false;
    if (!m_state.anchorPlacementEnabled()) return true;
    // P1-1: bounds check - don't place anchors outside editable lane
    if (cx < 0.0 || cx > Const::kLaneWidth || cy < 0.0) { m_state.clearAnchorSelection(); m_state.clearLinkSelection(); m_state.setPendingConnectAnchorId(-1); return true; }
    QPointF sp = snapPoint(cx, cy);
    int sc = m_state.selectedAnchorIds().size(); int idx = m_state.appendAnchor(sp.x(), sp.y()); int nid = m_state.anchorAt(idx).id;
    if (sc == 1 && nid > 0) { int prev = *m_state.selectedAnchorIds().begin(); m_state.addLink(prev, nid); m_state.setSingleSelectedAnchor(nid); } else { m_state.clearAnchorSelection(); }
    m_state.setPendingConnectAnchorId(-1); m_state.cleanupLinksAndSelection(); m_state.drag() = {"anchor", idx}; markDirty(); return true;
}
bool NoteChainEditor::handleMouseMove(double cx, double cy, int buttons) {
    if (!m_active) return false; auto &ld = m_state.linkDrag();
    if (ld.active) { ld.x = cx; ld.y = cy; return true; }
    if (m_state.boxSelect().active) { m_state.boxSelect().endX = cx; m_state.boxSelect().endY = cy; return true; }
    // P1-4: mid-drag switch from anchor drag to link drag on shift
    auto &drag = m_state.drag();
    if (drag.mode == "anchor" && drag.index >= 0 && drag.index < m_state.anchors().size() && (buttons & Qt::LeftButton) && m_state.shiftDown()) {
        int anchorId = m_state.anchorAt(drag.index).id;
        drag = DragState{};
        m_state.linkDrag() = LinkDrag{true, anchorId, -1, cx, cy};
        return true;
    }
    if (m_lastMoveTimer.isValid() && m_lastMoveTimer.elapsed() < kMoveThrottleMs) return !m_state.drag().mode.isEmpty();
    m_lastMoveTimer.start();
    if (drag.mode.isEmpty() || drag.index < 0 || drag.index >= m_state.anchors().size()) return false;
    auto &a = m_state.anchorAt(drag.index);
    if (drag.mode == "anchor") {
        QVariantMap ctx = m_state.lastContext();
        int timeDiv = qMax(1, ctx.value("time_division", 1).toInt());
        a.laneX = ncClamp(cx, 0.0, Const::kLaneWidth);
        a.beat = qMax(0.0, cy);
        a.beat = qRound(a.beat * timeDiv) / static_cast<double>(timeDiv); // snap beat only
        m_state.enforceAnchorAndConnectedHandleConstraints(drag.index);
    }
    else if (drag.mode == "in") { m_state.setAnchorInAbsChart(drag.index, cx, cy, a.smooth); m_state.enforceHandleTimeConstraints(drag.index); }
    else if (drag.mode == "out") { m_state.setAnchorOutAbsChart(drag.index, cx, cy, a.smooth); m_state.enforceHandleTimeConstraints(drag.index); }
    m_state.invalidateCurveCache(); markDirty(); return true;
}
bool NoteChainEditor::handleMouseRelease(double cx, double cy, int) {
    if (!m_active) return false; auto &bs = m_state.boxSelect(); auto &ld = m_state.linkDrag(); auto &drag = m_state.drag();
    if (bs.active) { bs.endX = cx; bs.endY = cy; double rx0=qMin(bs.startX,bs.endX),ry0=qMin(bs.startY,bs.endY),rw=qAbs(bs.endX-bs.startX),rh=qAbs(bs.endY-bs.startY); double rx1=rx0+rw,ry1=ry0+rh; QSet<int> hIds; for(int i=0;i<m_state.anchors().size();++i){auto&a=m_state.anchorAt(i);if(ncPtInRect(a.laneX,a.beat,rx0,ry0,rx1,ry1))hIds.insert(a.id);} if(bs.append){for(int id:hIds)m_state.selectAnchor(id);}else{m_state.clearAnchorSelection();m_state.clearLinkSelection();for(int id:hIds)m_state.selectAnchor(id);} bs=BoxSelect{}; recordHistory(); markDirty(); return true; }
    if (ld.active) { ld.active=false; int best=-1; double bd=20.0; for(int i=0;i<m_state.anchors().size();++i){auto&a=m_state.anchorAt(i);if(a.id==ld.sourceAnchorId)continue;double d=ncDist(cx,cy,a.laneX,a.beat);if(d<bd){best=i;bd=d;}} if(best>=0){m_state.addLink(ld.sourceAnchorId,m_state.anchorAt(best).id);recordHistory();markDirty();} ld=LinkDrag{}; return true; }
    // P1-3: emit checkpoint when drag ends
    if (drag.mode.isEmpty()) return false; bool hadDrag = !drag.mode.isEmpty(); recordHistory(); emit requestHostUndoCheckpoint(tr("Curve Edit")); drag = DragState{}; return true;
}

// ====== Keyboard ======
bool NoteChainEditor::handleKeyDown(int key, bool, bool ctrl) {
    if (!m_active) return false;
    if (key == Qt::Key_Delete || key == Qt::Key_Backspace) { deleteSelected(); return true; }
    if (key == Qt::Key_Escape) { m_state.drag() = DragState{}; m_state.linkDrag() = LinkDrag{}; m_state.boxSelect() = BoxSelect{}; m_state.clearAnchorSelection(); m_state.clearLinkSelection(); m_state.setPendingConnectAnchorId(-1); markDirty(); return true; }
    if (key == Qt::Key_A && !ctrl) { toggleAnchorPlacement(); return true; }
    return false;
}

// ====== Actions ======
bool NoteChainEditor::commitCurveToNotes() {
    if (!m_chartCtrl) return false;
    QVector<SegmentInfo> segs = m_state.connectedAnchorSegments();
    if (segs.isEmpty()) return false;
    QVector<Note> toAdd; QSet<LinkKey> tKeys;
    if (!m_state.selectedLinkKeys().isEmpty()) tKeys = m_state.selectedLinkKeys();
    QSet<QString> existing;
    if (const Chart *chart = m_chartCtrl->chart()) {
        for (const Note &note : chart->notes()) {
            const QString key = normalNotePositionKey(note);
            if (!key.isEmpty())
                existing.insert(key);
        }
    }
    QSet<QString> seen;
    const int fallbackDen = defaultCommitDenominator(m_state);
    for (const auto &seg : segs) {
        if (!tKeys.isEmpty() && !tKeys.contains(makeLinkKey(seg.id0, seg.id1))) continue;
        int den = (m_state.segmentDensityMode(seg.id0, seg.id1) == 0) ? fallbackDen : seg.denominator;
        double lo = qMin(seg.a0.beat, seg.a1.beat), hi = qMax(seg.a0.beat, seg.a1.beat);
        auto sampled = sampleSegment(seg.a0, seg.a1, seg.shape, 32);
        QMap<double, double> beatLane; for (const auto &pt : sampled) beatLane[pt.beat] = pt.laneX;
        int sTick = qRound(lo * den), eTick = qRound(hi * den);
        for (int tick = sTick; tick <= eTick; ++tick) {
            double beat = static_cast<double>(tick) / den;
            if (beat < lo || beat > hi) continue;
            auto it = beatLane.lowerBound(beat); double lx;
            if (it == beatLane.begin()) lx = it.value();
            else if (it == beatLane.end()) { auto prev = beatLane.constEnd(); --prev; lx = prev.value(); }
            else { auto prev = it; --prev; double t = (beat - prev.key()) / qMax(1e-9, it.key() - prev.key()); lx = prev.value() + (it.value() - prev.value()) * t; }
            lx = ncClamp(lx, 0.0, Const::kLaneWidth);
            QVector<int> tri = floatBeatToTriplet(beat, den);
            Note n; n.beatNum = tri[0]; n.numerator = tri[1]; n.denominator = tri[2]; n.x = qRound(lx); n.type = NoteType::NORMAL;
            const QString key = normalNotePositionKey(n);
            if (key.isEmpty() || existing.contains(key) || seen.contains(key))
                continue;
            seen.insert(key);
            toAdd.append(n);
        }
    }
    if (toAdd.isEmpty()) return false;
    std::sort(toAdd.begin(), toAdd.end(), [](const Note &a, const Note &b) { double ba = a.getStartBeat(), bb = b.getStartBeat(); if (qAbs(ba - bb) < 1e-9) return a.x < b.x; return ba < bb; });
    m_chartCtrl->addNotes(toAdd);
    // auto-save sidecar after commit
    if (!m_sidecarPath.isEmpty()) NoteChainPersistence::saveToFile(m_state, m_sidecarPath);
    emit requestHostUndoCheckpoint(QStringLiteral("Commit Curve -> Notes"));
    return true;
}

void NoteChainEditor::setAnchorPlacementEnabled(bool on) { if (m_state.anchorPlacementEnabled() == on) return; m_state.setAnchorPlacementEnabled(on); emit statusMessage(on ? "Anchor ON" : "Anchor OFF"); emit needsRepaint(); }
void NoteChainEditor::setCurveVisible(bool on) { if (m_state.curveVisible() == on) return; m_state.setCurveVisible(on); markDirty(); }
void NoteChainEditor::setPolylineMode(bool on) {
    const QString shape = on ? QStringLiteral("polyline") : QStringLiteral("curve");
    if (!m_state.selectedLinkKeys().isEmpty()) {
        bool changed = false;
        for (const LinkKey &key : m_state.selectedLinkKeys()) {
            if (m_state.segmentShape(key.first, key.second) != shape) {
                m_state.setSegmentShape(key.first, key.second, shape);
                changed = true;
            }
        }
        if (changed) {
            recordHistory();
            markDirty();
        }
        return;
    }
    if (m_state.activeLinkShape() == shape) return;
    m_state.setActiveLinkShape(shape);
    markDirty();
}
void NoteChainEditor::setNoteCurveSnapEnabled(bool on) { if (m_state.noteCurveSnapEnabled() == on) return; m_state.setNoteCurveSnapEnabled(on); emit needsRepaint(); }
void NoteChainEditor::setSelectAnchorsEnabled(bool on) { if (m_state.selectionEnabled("anchors") == on) return; m_state.setSelectionEnabled("anchors", on); emit needsRepaint(); }
void NoteChainEditor::setSelectSegmentsEnabled(bool on) { if (m_state.selectionEnabled("segments") == on) return; m_state.setSelectionEnabled("segments", on); emit needsRepaint(); }
void NoteChainEditor::toggleAnchorPlacement() { setAnchorPlacementEnabled(!m_state.anchorPlacementEnabled()); }
void NoteChainEditor::toggleCurveVisible() { setCurveVisible(!m_state.curveVisible()); }
void NoteChainEditor::togglePolylineMode() { setPolylineMode(m_state.activeLinkShape() != QStringLiteral("polyline")); }
void NoteChainEditor::toggleNoteCurveSnap() { setNoteCurveSnapEnabled(!m_state.noteCurveSnapEnabled()); }
void NoteChainEditor::toggleSelectAnchors() { setSelectAnchorsEnabled(!m_state.selectionEnabled("anchors")); }
void NoteChainEditor::toggleSelectSegments() { setSelectSegmentsEnabled(!m_state.selectionEnabled("segments")); }
void NoteChainEditor::toggleSelectNotes() { m_state.setSelectionEnabled("notes", !m_state.selectionEnabled("notes")); }

bool NoteChainEditor::selectSegmentAt(double chartX, double chartY, bool append) {
    auto hit = findSegmentHit(chartX, chartY);
    if (hit.first < 0)
        return false;
    LinkKey key = makeLinkKey(hit.first, hit.second);
    if (!append && !m_state.isLinkSelected(key)) {
        m_state.clearAnchorSelection();
        m_state.clearLinkSelection();
    }
    if (append)
        m_state.toggleLinkSelection(key);
    else
        m_state.selectLink(key);
    emit needsRepaint();
    return true;
}

bool NoteChainEditor::hasSelectedSegments() const { return !m_state.selectedLinkKeys().isEmpty(); }

int NoteChainEditor::selectedSegmentDensity() const {
    if (m_state.selectedLinkKeys().isEmpty())
        return -2;
    bool first = true;
    int signature = -2;
    for (const LinkKey &key : m_state.selectedLinkKeys()) {
        const int value = (m_state.segmentDensityMode(key.first, key.second) == 0)
                              ? 0
                              : m_state.segmentDen(key.first, key.second);
        if (first) {
            signature = value;
            first = false;
        } else if (signature != value) {
            return -1;
        }
    }
    return signature;
}

bool NoteChainEditor::setSelectedSegmentDensity(int denominator) {
    if (m_state.selectedLinkKeys().isEmpty())
        return false;
    for (const LinkKey &key : m_state.selectedLinkKeys()) {
        if (denominator <= 0)
            m_state.setDensityMode(key.first, key.second, 0);
        else
            m_state.setSegmentDen(key.first, key.second, denominator);
    }
    recordHistory();
    markDirty();
    return true;
}

bool NoteChainEditor::toggleSelectedSegmentShape() {
    if (m_state.selectedLinkKeys().isEmpty())
        return false;
    bool allPolyline = true;
    for (const LinkKey &key : m_state.selectedLinkKeys()) {
        if (m_state.segmentShape(key.first, key.second) != QStringLiteral("polyline")) {
            allPolyline = false;
            break;
        }
    }
    const QString next = allPolyline ? QStringLiteral("curve") : QStringLiteral("polyline");
    for (const LinkKey &key : m_state.selectedLinkKeys())
        m_state.setSegmentShape(key.first, key.second, next);
    recordHistory();
    markDirty();
    return true;
}

void NoteChainEditor::connectSelectedAnchors() {
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
        recordHistory();
        markDirty();
    }
}
void NoteChainEditor::disconnectSelectedSegments() { for (auto &k : m_state.selectedLinkKeys()) m_state.removeLink(k.first, k.second); m_state.clearLinkSelection(); recordHistory(); markDirty(); }
void NoteChainEditor::deleteSelected() { bool ch = false; for (int id : m_state.selectedAnchorIds()) { m_state.removeAnchorById(id); ch = true; } for (auto &k : m_state.selectedLinkKeys()) { m_state.removeLink(k.first, k.second); ch = true; } m_state.clearAnchorSelection(); m_state.clearLinkSelection(); if (ch) { recordHistory(); markDirty(); } }
void NoteChainEditor::resetCurve() {
    // Reset editable content only.  The sidecar identity/revision belongs to
    // the project and must survive, otherwise the next save is rejected as a
    // false CAS conflict.
    const int revision = m_state.projectRevision();
    const QString fileUuid = m_state.projectFileUuid();
    const QString writer = m_state.lastWriterInstance();
    const QString projectPath = m_state.projectPath();
    m_state = NoteChainState{};
    m_state.setProjectRevision(revision);
    m_state.setProjectFileUuid(fileUuid);
    m_state.setLastWriterInstance(writer);
    m_state.setProjectPath(projectPath);
    m_history.clear();
    m_historyIdx = -1;
    recordHistory();
    markDirty();
}

// ====== Rendering (direct QPainter, zero overlay serialization) ======
void NoteChainEditor::render(QPainter *painter, const QRectF &viewport,
                              double scrollBeat, double visibleBeatRange,
                              const CanvasProjection &proj) {
    if (!m_active || !m_state.curveVisible()) return;
    Q_UNUSED(viewport)
    painter->save(); painter->setRenderHint(QPainter::Antialiasing);
    double visLo = scrollBeat - visibleBeatRange * 0.1;
    double visHi = scrollBeat + visibleBeatRange * 1.1;
    OverlayToggles &tg = m_state.overlayToggles();
    // curves
    QVector<SegmentInfo> segs = m_state.connectedAnchorSegments();
    for (const auto &seg : segs) {
        if (qMax(seg.a0.beat, seg.a1.beat) < visLo || qMin(seg.a0.beat, seg.a1.beat) > visHi) continue;
        LinkKey key = makeLinkKey(seg.id0, seg.id1); bool sel = m_state.isLinkSelected(key);
        QColor col = (seg.shape == QStringLiteral("polyline")) ? QColor(200,200,200,180) : QColor(51,204,255,200);
        if (sel) col = QColor(255,214,107);
        auto pts = sampleSegment(seg.a0, seg.a1, seg.shape, 24);
        QPen pen(col, (seg.shape == QStringLiteral("polyline") && !sel) ? 1.5 : (sel ? 3.0 : 2.0)); painter->setPen(pen);
        for (int j = 0; j < pts.size() - 1; ++j) {
            painter->drawLine(QPointF(proj.lx2x(pts[j].laneX), proj.b2y(pts[j].beat)),
                              QPointF(proj.lx2x(pts[j+1].laneX), proj.b2y(pts[j+1].beat)));
        }
        // P0-3: sample point markers (every 4th)
        if (tg.samplePoints) {
            painter->setPen(QPen(QColor(0x88,0xFF,0xFF,0xFF), 1.0));
            painter->setBrush(QColor(0x88,0xFF,0xFF,0xFF));
            for (int k = 0; k < pts.size(); k += 4) {
                double sx = proj.lx2x(pts[k].laneX), sy = proj.b2y(pts[k].beat);
                painter->drawRect(QRectF(sx - 2, sy - 2, 4, 4));
            }
        }
    }
    // anchors + handles + labels
    for (int i = 0; i < m_state.anchors().size(); ++i) {
        const auto &a = m_state.anchorAt(i); bool sel = m_state.isAnchorSelected(a.id);
        bool isDrag = (m_state.drag().index == i && !m_state.drag().mode.isEmpty());
        double ax = proj.lx2x(a.laneX), ay = proj.b2y(a.beat);
        if (tg.handles) {
            { QPointF inP(proj.lx2x(a.laneX+a.inDx), proj.b2y(a.beat+a.inDy)); painter->setPen(QPen(sel?QColor(100,160,255):QColor(180,140,60),1)); painter->drawLine(QPointF(ax,ay),inP); painter->setBrush(sel?QColor(100,160,255):QColor(180,140,60)); painter->setPen(Qt::NoPen); painter->drawEllipse(inP,Const::kHandleDrawRadius,Const::kHandleDrawRadius); }
            { QPointF outP(proj.lx2x(a.laneX+a.outDx), proj.b2y(a.beat+a.outDy)); painter->setPen(QPen(sel?QColor(100,160,255):QColor(180,140,60),1)); painter->drawLine(QPointF(ax,ay),outP); painter->setBrush(sel?QColor(100,160,255):QColor(180,140,60)); painter->setPen(Qt::NoPen); painter->drawEllipse(outP,Const::kHandleDrawRadius,Const::kHandleDrawRadius); }
        }
        if (tg.controlPoints) {
            // Outer ring (semi-transparent, larger)
            QColor ao = isDrag ? QColor(0,119,255,170) : (sel ? QColor(255,155,47,170) : QColor(0,163,255,170));
            painter->setBrush(ao); painter->setPen(Qt::NoPen);
            painter->drawEllipse(QPointF(ax,ay), Const::kAnchorDrawRadius + 1.5, Const::kAnchorDrawRadius + 1.5);
            // Core
            QColor ac = isDrag ? QColor(0,119,255) : (sel ? QColor(255,155,47) : QColor(0,163,255));
            painter->setBrush(ac); painter->setPen(Qt::NoPen);
            painter->drawEllipse(QPointF(ax,ay), Const::kAnchorDrawRadius, Const::kAnchorDrawRadius);
        }
        if (tg.labels) { painter->setPen(QColor(255,255,255)); QFont f=painter->font(); f.setPixelSize(12); painter->setFont(f); painter->drawText(QPointF(ax+12,ay-4),QString("A%1(%2)").arg(i).arg(a.smooth?"S":"C")); }
    }
    // box select
    if (m_state.boxSelect().active) { double x0=qMin(m_state.boxSelect().startX,m_state.boxSelect().endX),y0=qMin(m_state.boxSelect().startY,m_state.boxSelect().endY),x1=qMax(m_state.boxSelect().startX,m_state.boxSelect().endX),y1=qMax(m_state.boxSelect().startY,m_state.boxSelect().endY); painter->setPen(QPen(QColor(255,204,102),1.5,Qt::DashLine)); painter->setBrush(QColor(255,204,102,40)); painter->drawRect(QRectF(proj.lx2x(x0),proj.b2y(y0),proj.lx2x(x1)-proj.lx2x(x0),proj.b2y(y1)-proj.b2y(y0))); }
    // link drag preview
    if (m_state.linkDrag().active) { const auto &ld=m_state.linkDrag(); int idx=m_state.anchorIndexById(ld.sourceAnchorId); if(idx>=0){const auto&a=m_state.anchorAt(idx);painter->setPen(QPen(QColor(255,224,138),2.0,Qt::DashLine));painter->drawLine(QPointF(proj.lx2x(a.laneX),proj.b2y(a.beat)),QPointF(proj.lx2x(ld.x),proj.b2y(ld.y)));
        // P0-4: hover highlight on target anchor
        if (ld.hoverAnchorId >= 0) { int hidx = m_state.anchorIndexById(ld.hoverAnchorId); if (hidx >= 0) { const auto &ha = m_state.anchorAt(hidx); double hx = proj.lx2x(ha.laneX), hy = proj.b2y(ha.beat); painter->setPen(QPen(QColor(255,224,138),2.0)); painter->setBrush(QColor(0x33,0xFF,0xE0,0x8A)); painter->drawRect(QRectF(hx - 8, hy - 8, 16, 16)); } }
    } }
    // P1-2: summary label (density info + anchor mode)
    if (tg.labels) {
        QFont sf; sf.setPixelSize(12); painter->setFont(sf);
        QStringList dns; for (int d : m_state.style().denominators) dns << QString::number(d);
        int overrideDen = m_state.lastContext().value("plugin_time_division_override", 0).toInt();
        int effDen = overrideDen > 0 ? overrideDen : qMax(1, m_state.lastContext().value("time_division", 4).toInt());
        QString ancMode = m_state.anchorPlacementEnabled() ? QStringLiteral("ON") : QStringLiteral("OFF");
        QString sum = QString("Den:%1  Snap:%2  Anchor:%3").arg(dns.join("/")).arg(effDen).arg(ancMode);
        painter->setPen(QColor(0xDD,0xEE,0xFF));
        painter->drawText(QPointF(16, 18), sum);
    }
    painter->restore();
}

// ====== Undo/Redo ======
bool NoteChainEditor::canUndo() const { return m_historyIdx > 0; }
bool NoteChainEditor::canRedo() const { return m_historyIdx < m_history.size() - 1; }
void NoteChainEditor::undo() { if (!canUndo()) return; m_historyIdx--; m_state.restoreSnapshot(m_history[m_historyIdx]); emit needsRepaint(); }
void NoteChainEditor::redo() { if (!canRedo()) return; m_historyIdx++; m_state.restoreSnapshot(m_history[m_historyIdx]); emit needsRepaint(); }

// ====== Persistence ======
bool NoteChainEditor::loadProject(const QString &path) {
    m_sidecarPath = path;
    if (path.isEmpty())
        return false;

    // A missing sidecar starts a new project.  Crucially, never retain the
    // previous chart's state when switching to a chart without a sidecar.
    if (!QFileInfo::exists(path)) {
        m_state = NoteChainState{};
        m_state.setProjectPath(path);
        m_state.setProjectDirty(true);
        m_history.clear();
        m_historyIdx = -1;
        recordHistory();
        return true;
    }

    const bool ok = NoteChainPersistence::loadFromFile(path, m_state);
    if (ok) {
        m_history.clear();
        m_historyIdx = -1;
        recordHistory();
    }
    return ok;
}
bool NoteChainEditor::saveProject(const QString &path) { QString p = path.isEmpty() ? m_sidecarPath : path; if (p.isEmpty()) return false; m_sidecarPath = p; return NoteChainPersistence::saveToFile(m_state, p); }

} // namespace NoteChain
