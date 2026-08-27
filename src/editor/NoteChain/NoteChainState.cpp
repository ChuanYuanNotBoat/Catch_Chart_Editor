// NoteChainState.cpp - native runtime state with Python-compatible semantics.
#include "NoteChainState.h"

#include <algorithm>
#include <limits>

namespace NoteChain {

namespace {
GroupPersistenceMeta defaultGroup(int id, const QString &name)
{
    GroupPersistenceMeta group;
    group.id = id;
    group.name = name;
    return group;
}
}

NoteChainState::NoteChainState()
{
    m_activeShape = QStringLiteral("curve");
    m_style.name = QStringLiteral("balanced");
    m_style.denominators = {4, 8, 12, 16};
    m_nodeGroups = {defaultGroup(Const::kDefaultNodeGroupId, QStringLiteral("base"))};
    m_curveGroups = {defaultGroup(Const::kDefaultCurveGroupId, QStringLiteral("base"))};
}

int NoteChainState::appendAnchor(double laneX, double beat)
{
    Anchor anchor;
    anchor.id = m_nextAnchorId++;
    anchor.laneX = ncClamp(laneX, 0.0, Const::kLaneWidth);
    anchor.beat = qMax(0.0, beat);

    int insertAt = m_anchors.size();
    for (int i = 0; i < m_anchors.size(); ++i) {
        if (anchor.beat < m_anchors[i].beat) {
            insertAt = i;
            break;
        }
    }

    if (m_anchors.isEmpty()) {
        anchor.inDx = -16.0;
        anchor.outDx = 16.0;
    } else {
        const Anchor *reference = nullptr;
        if (insertAt > 0 && insertAt < m_anchors.size()) {
            const Anchor &previous = m_anchors[insertAt - 1];
            const Anchor &next = m_anchors[insertAt];
            reference = (qAbs(anchor.beat - previous.beat) <= qAbs(next.beat - anchor.beat))
                            ? &previous : &next;
        } else if (insertAt > 0) {
            reference = &m_anchors[insertAt - 1];
        } else {
            reference = &m_anchors[insertAt];
        }

        double laneDelta = 16.0;
        double beatDelta = 0.0;
        if (reference) {
            if (anchor.beat <= reference->beat) {
                laneDelta = reference->laneX - anchor.laneX;
                beatDelta = reference->beat - anchor.beat;
            } else {
                laneDelta = anchor.laneX - reference->laneX;
                beatDelta = anchor.beat - reference->beat;
            }
            laneDelta = ncClamp(laneDelta * 0.25, -96.0, 96.0);
            beatDelta = ncClamp(beatDelta * 0.25, -2.0, 2.0);
        }
        anchor.inDx = -laneDelta;
        anchor.inDy = -beatDelta;
        anchor.outDx = laneDelta;
        anchor.outDy = beatDelta;
    }
    return insertAnchor(anchor);
}

int NoteChainState::insertAnchor(const Anchor &value)
{
    Anchor anchor = value;
    anchor.laneX = ncClamp(anchor.laneX, 0.0, Const::kLaneWidth);
    anchor.beat = qMax(0.0, anchor.beat);
    if (anchor.id <= 0 || anchorIndexById(anchor.id) >= 0)
        anchor.id = m_nextAnchorId++;
    else
        m_nextAnchorId = qMax(m_nextAnchorId, anchor.id + 1);

    int index = m_anchors.size();
    for (int i = 0; i < m_anchors.size(); ++i) {
        if (anchor.beat < m_anchors[i].beat) {
            index = i;
            break;
        }
    }
    m_anchors.insert(index, anchor);
    if (!m_nodeMeta.contains(anchor.id))
        m_nodeMeta.insert(anchor.id, NodePersistenceMeta{});
    invalidateCurveCache();
    return index;
}

void NoteChainState::removeAnchorById(int id)
{
    const int index = anchorIndexById(id);
    if (index < 0)
        return;
    QVector<LinkKey> deadLinks;
    for (const Link &link : m_links) {
        if (link.from == id || link.to == id)
            deadLinks.append(link.key());
    }
    for (const LinkKey &key : deadLinks) {
        m_segDen.remove(key);
        m_segShape.remove(key);
        m_densMode.remove(key);
        m_curveMeta.remove(key);
        m_selLinkKeys.remove(key);
    }
    m_links.erase(std::remove_if(m_links.begin(), m_links.end(), [id](const Link &link) {
        return link.from == id || link.to == id;
    }), m_links.end());
    m_anchors.removeAt(index);
    m_nodeMeta.remove(id);
    m_selAnchorIds.remove(id);
    invalidateCurveCache();
}

int NoteChainState::anchorIndexById(int id) const
{
    for (int i = 0; i < m_anchors.size(); ++i) {
        if (m_anchors[i].id == id)
            return i;
    }
    return -1;
}

Anchor &NoteChainState::anchorAt(int index) { return m_anchors[index]; }
const Anchor &NoteChainState::anchorAt(int index) const { return m_anchors[index]; }

void NoteChainState::setAnchorInAbsChart(int index, double laneX, double beat, bool mirror)
{
    Anchor &anchor = m_anchors[index];
    anchor.inDx = laneX - anchor.laneX;
    anchor.inDy = beat - anchor.beat;
    if (mirror && anchor.smooth) {
        anchor.outDx = -anchor.inDx;
        anchor.outDy = -anchor.inDy;
    }
    invalidateCurveCache();
}

void NoteChainState::setAnchorOutAbsChart(int index, double laneX, double beat, bool mirror)
{
    Anchor &anchor = m_anchors[index];
    anchor.outDx = laneX - anchor.laneX;
    anchor.outDy = beat - anchor.beat;
    if (mirror && anchor.smooth) {
        anchor.inDx = -anchor.outDx;
        anchor.inDy = -anchor.outDy;
    }
    invalidateCurveCache();
}

QMap<int, int> NoteChainState::anchorIndexMap() const
{
    QMap<int, int> result;
    for (int i = 0; i < m_anchors.size(); ++i)
        result.insert(m_anchors[i].id, i);
    return result;
}

Link NoteChainState::normalizedLink(int fromId, int toId) const
{
    const int fromIndex = anchorIndexById(fromId);
    const int toIndex = anchorIndexById(toId);
    if (fromIndex < 0 || toIndex < 0 || fromIndex == toIndex)
        return {};
    return (fromIndex < toIndex) ? Link{fromId, toId} : Link{toId, fromId};
}

void NoteChainState::addLink(int fromId, int toId)
{
    const Link link = normalizedLink(fromId, toId);
    if (!link.valid() || hasLink(link.from, link.to))
        return;
    m_links.append(link);
    setSegmentDen(link.from, link.to, Const::kDefaultSegmentDen);
    setSegmentShape(link.from, link.to, m_activeShape);

    CurvePersistenceMeta meta;
    meta.curveId = m_nextCurveId++;
    QSet<int> usedNumbers;
    for (const CurvePersistenceMeta &existing : m_curveMeta)
        if (existing.curveNo > 0) usedNumbers.insert(existing.curveNo);
    meta.curveNo = 1;
    while (usedNumbers.contains(meta.curveNo)) ++meta.curveNo;
    m_curveMeta.insert(link.key(), meta);
    invalidateCurveCache();
}

void NoteChainState::removeLink(int fromId, int toId) { removeLinkInternal(fromId, toId); }

bool NoteChainState::hasLink(int fromId, int toId) const
{
    const LinkKey key = makeLinkKey(fromId, toId);
    return std::any_of(m_links.cbegin(), m_links.cend(), [key](const Link &link) {
        return link.key() == key;
    });
}

void NoteChainState::clearLinks()
{
    m_links.clear();
    m_segDen.clear();
    m_segShape.clear();
    m_densMode.clear();
    m_curveMeta.clear();
    m_selLinkKeys.clear();
    invalidateCurveCache();
}

void NoteChainState::removeLinkInternal(int fromId, int toId)
{
    const LinkKey key = makeLinkKey(fromId, toId);
    const int oldSize = m_links.size();
    m_links.erase(std::remove_if(m_links.begin(), m_links.end(), [key](const Link &link) {
        return link.key() == key;
    }), m_links.end());
    if (m_links.size() == oldSize)
        return;
    m_segDen.remove(key);
    m_segShape.remove(key);
    m_densMode.remove(key);
    m_curveMeta.remove(key);
    m_selLinkKeys.remove(key);
    invalidateCurveCache();
}

void NoteChainState::setSegmentDen(int fromId, int toId, int denominator)
{
    const LinkKey key = makeLinkKey(fromId, toId);
    const int value = qMax(1, denominator);
    m_segDen.insert(key, value);
    m_densMode.insert(key, value);
}

int NoteChainState::segmentDen(int fromId, int toId) const
{
    return m_segDen.value(makeLinkKey(fromId, toId), Const::kDefaultSegmentDen);
}

int NoteChainState::segmentDensityMode(int fromId, int toId) const
{
    const LinkKey key = makeLinkKey(fromId, toId);
    if (m_densMode.contains(key))
        return m_densMode.value(key);
    return m_segDen.contains(key) ? m_segDen.value(key) : 0;
}

void NoteChainState::setSegmentShape(int fromId, int toId, const QString &shape)
{
    const LinkKey key = makeLinkKey(fromId, toId);
    const QString normalized = ncNormalizeShape(shape);
    if (normalized == QLatin1String("curve"))
        m_segShape.remove(key);
    else
        m_segShape.insert(key, normalized);
    invalidateCurveCache();
}

QString NoteChainState::segmentShape(int fromId, int toId) const
{
    return ncNormalizeShape(m_segShape.value(makeLinkKey(fromId, toId)));
}

void NoteChainState::setDensityMode(int fromId, int toId, int mode)
{
    const LinkKey key = makeLinkKey(fromId, toId);
    if (mode <= 0) {
        m_densMode.insert(key, 0);
        m_segDen.remove(key);
    } else {
        m_densMode.insert(key, mode);
        m_segDen.insert(key, mode);
    }
}

void NoteChainState::seedMissingSegmentDenominators()
{
    for (const Link &link : m_links) {
        const LinkKey key = link.key();
        if (m_densMode.value(key, -1) == 0)
            continue;
        if (!m_segDen.contains(key)) {
            m_segDen.insert(key, Const::kDefaultSegmentDen);
            m_densMode.insert(key, Const::kDefaultSegmentDen);
        }
    }
}

QVector<SegmentInfo> NoteChainState::connectedAnchorSegments() const
{
    QVector<SegmentInfo> segments;
    segments.reserve(m_links.size());
    for (const Link &rawLink : m_links) {
        const Link link = normalizedLink(rawLink.from, rawLink.to);
        const int i0 = anchorIndexById(link.from);
        const int i1 = anchorIndexById(link.to);
        if (!link.valid() || i0 < 0 || i1 < 0)
            continue;
        SegmentInfo segment;
        segment.i0 = i0;
        segment.i1 = i1;
        segment.id0 = link.from;
        segment.id1 = link.to;
        segment.a0 = m_anchors[i0];
        segment.a1 = m_anchors[i1];
        segment.shape = segmentShape(link.from, link.to);
        segment.denominator = segmentDen(link.from, link.to);
        segments.append(segment);
    }
    std::sort(segments.begin(), segments.end(), [](const SegmentInfo &a, const SegmentInfo &b) {
        return qMakePair(qMin(a.i0, a.i1), qMax(a.i0, a.i1))
             < qMakePair(qMin(b.i0, b.i1), qMax(b.i0, b.i1));
    });
    return segments;
}

NodePersistenceMeta NoteChainState::nodeMeta(int anchorId) const
{
    return m_nodeMeta.value(anchorId, NodePersistenceMeta{});
}

void NoteChainState::setNodeMeta(int anchorId, const NodePersistenceMeta &meta)
{
    if (anchorIndexById(anchorId) >= 0)
        m_nodeMeta.insert(anchorId, meta);
}

CurvePersistenceMeta NoteChainState::curveMeta(int fromId, int toId) const
{
    return m_curveMeta.value(makeLinkKey(fromId, toId), CurvePersistenceMeta{});
}

void NoteChainState::setCurveMeta(int fromId, int toId, const CurvePersistenceMeta &meta)
{
    if (!hasLink(fromId, toId))
        return;
    m_curveMeta.insert(makeLinkKey(fromId, toId), meta);
    m_nextCurveId = qMax(m_nextCurveId, meta.curveId + 1);
}

void NoteChainState::selectAnchor(int id) { if (anchorIndexById(id) >= 0) m_selAnchorIds.insert(id); }
void NoteChainState::deselectAnchor(int id) { m_selAnchorIds.remove(id); }
void NoteChainState::clearAnchorSelection() { m_selAnchorIds.clear(); }
void NoteChainState::toggleAnchorSelection(int id) {
    if (m_selAnchorIds.contains(id)) m_selAnchorIds.remove(id); else selectAnchor(id);
}
void NoteChainState::setSingleSelectedAnchor(int id) { m_selAnchorIds.clear(); selectAnchor(id); }
bool NoteChainState::isAnchorSelected(int id) const { return m_selAnchorIds.contains(id); }
void NoteChainState::selectLink(LinkKey key) { if (hasLink(key.first, key.second)) m_selLinkKeys.insert(key); }
void NoteChainState::deselectLink(LinkKey key) { m_selLinkKeys.remove(key); }
void NoteChainState::clearLinkSelection() { m_selLinkKeys.clear(); }
void NoteChainState::toggleLinkSelection(LinkKey key) {
    if (m_selLinkKeys.contains(key)) m_selLinkKeys.remove(key); else selectLink(key);
}
bool NoteChainState::isLinkSelected(LinkKey key) const { return m_selLinkKeys.contains(key); }

bool NoteChainState::selectionEnabled(const QString &kind) const
{
    if (m_noteSnap && (kind == QLatin1String("anchors") || kind == QLatin1String("segments")))
        return false;
    if (kind == QLatin1String("anchors")) return m_selTargets.anchors;
    if (kind == QLatin1String("segments")) return m_selTargets.segments;
    if (kind == QLatin1String("notes")) return m_selTargets.notes;
    return false;
}

bool NoteChainState::selectionTargetEnabled(const QString &kind) const
{
    if (kind == QLatin1String("anchors")) return m_selTargets.anchors;
    if (kind == QLatin1String("segments")) return m_selTargets.segments;
    if (kind == QLatin1String("notes")) return m_selTargets.notes;
    return false;
}

void NoteChainState::setSelectionEnabled(const QString &kind, bool enabled)
{
    if (kind == QLatin1String("anchors")) {
        m_selTargets.anchors = enabled;
        if (!enabled) m_selAnchorIds.clear();
    } else if (kind == QLatin1String("segments")) {
        m_selTargets.segments = enabled;
        if (!enabled) m_selLinkKeys.clear();
    } else if (kind == QLatin1String("notes")) {
        m_selTargets.notes = enabled;
    }
}

void NoteChainState::enforceHandleTimeConstraints(int index, int timeDivision)
{
    if (index < 0 || index >= m_anchors.size())
        return;
    const double epsilon = 1.0 / (qMax(1, timeDivision) * 4.0);
    Anchor &anchor = m_anchors[index];
    double outUpper = std::numeric_limits<double>::infinity();
    double inLower = -std::numeric_limits<double>::infinity();
    bool hasOutUpper = false;
    bool hasInLower = false;
    for (const SegmentInfo &segment : connectedAnchorSegments()) {
        const Anchor *other = nullptr;
        if (segment.id0 == anchor.id) other = &segment.a1;
        else if (segment.id1 == anchor.id) other = &segment.a0;
        if (!other) continue;
        if (other->beat > anchor.beat) {
            outUpper = qMin(outUpper, qMax(0.0, other->beat - anchor.beat - epsilon));
            hasOutUpper = true;
        } else if (other->beat < anchor.beat) {
            inLower = qMax(inLower, qMin(0.0, other->beat - anchor.beat + epsilon));
            hasInLower = true;
        }
    }
    if (hasOutUpper) anchor.outDy = ncClamp(anchor.outDy, 0.0, outUpper);
    if (hasInLower) anchor.inDy = ncClamp(anchor.inDy, inLower, 0.0);
    invalidateCurveCache();
}

void NoteChainState::enforceAnchorAndConnectedHandleConstraints(int index, int timeDivision)
{
    if (index < 0 || index >= m_anchors.size())
        return;
    const int anchorId = m_anchors[index].id;
    QVector<int> neighbors;
    double lower = -std::numeric_limits<double>::infinity();
    double upper = std::numeric_limits<double>::infinity();
    bool hasLower = false;
    bool hasUpper = false;
    const double epsilon = 1.0 / (qMax(1, timeDivision) * 4.0);
    for (const SegmentInfo &segment : connectedAnchorSegments()) {
        int otherIndex = -1;
        if (segment.id0 == anchorId) otherIndex = segment.i1;
        else if (segment.id1 == anchorId) otherIndex = segment.i0;
        if (otherIndex < 0) continue;
        neighbors.append(otherIndex);
        const double otherBeat = m_anchors[otherIndex].beat;
        if (otherBeat < m_anchors[index].beat) {
            lower = qMax(lower, otherBeat + epsilon);
            hasLower = true;
        } else if (otherBeat > m_anchors[index].beat) {
            upper = qMin(upper, otherBeat - epsilon);
            hasUpper = true;
        }
    }
    if (neighbors.isEmpty())
        return;
    if (hasLower) m_anchors[index].beat = qMax(m_anchors[index].beat, lower);
    if (hasUpper) m_anchors[index].beat = qMin(m_anchors[index].beat, upper);
    enforceHandleTimeConstraints(index, timeDivision);
    for (int neighbor : neighbors)
        enforceHandleTimeConstraints(neighbor, timeDivision);
    invalidateCurveCache();
}

void NoteChainState::cleanupLinksAndSelection()
{
    QSet<int> validIds;
    for (const Anchor &anchor : m_anchors)
        validIds.insert(anchor.id);

    QSet<LinkKey> seen;
    QVector<Link> cleaned;
    for (const Link &rawLink : m_links) {
        const Link link = normalizedLink(rawLink.from, rawLink.to);
        if (!link.valid() || seen.contains(link.key()))
            continue;
        seen.insert(link.key());
        cleaned.append(link);
    }
    m_links = cleaned;

    const auto removeInvalidLinkKeys = [&seen](auto &map) {
        for (auto it = map.begin(); it != map.end();) {
            if (!seen.contains(it.key())) it = map.erase(it);
            else ++it;
        }
    };
    removeInvalidLinkKeys(m_segDen);
    removeInvalidLinkKeys(m_segShape);
    removeInvalidLinkKeys(m_densMode);
    removeInvalidLinkKeys(m_curveMeta);

    for (auto it = m_nodeMeta.begin(); it != m_nodeMeta.end();) {
        if (!validIds.contains(it.key())) it = m_nodeMeta.erase(it);
        else ++it;
    }
    for (auto it = m_selAnchorIds.begin(); it != m_selAnchorIds.end();) {
        if (!validIds.contains(*it)) it = m_selAnchorIds.erase(it);
        else ++it;
    }
    for (auto it = m_selLinkKeys.begin(); it != m_selLinkKeys.end();) {
        if (!seen.contains(*it)) it = m_selLinkKeys.erase(it);
        else ++it;
    }
    invalidateCurveCache();
}

StateSnapshot NoteChainState::captureSnapshot() const
{
    StateSnapshot snapshot;
    snapshot.anchors = m_anchors;
    snapshot.links = m_links;
    snapshot.segmentDenominators = m_segDen;
    snapshot.segmentShapes = m_segShape;
    snapshot.densityModes = m_densMode;
    snapshot.nodeMeta = m_nodeMeta;
    snapshot.curveMeta = m_curveMeta;
    snapshot.nodeGroups = m_nodeGroups;
    snapshot.curveGroups = m_curveGroups;
    snapshot.selectedAnchorIds = m_selAnchorIds;
    snapshot.selectedLinkKeys = m_selLinkKeys;
    snapshot.style = m_style;
    snapshot.nextAnchorId = m_nextAnchorId;
    snapshot.nextCurveId = m_nextCurveId;
    snapshot.selectionTargets = m_selTargets;
    snapshot.curveVisible = m_curveVisible;
    snapshot.anchorPlacementEnabled = m_anchorPlaceEnabled;
    snapshot.noteCurveSnapEnabled = m_noteSnap;
    snapshot.activeLinkShape = m_activeShape;
    return snapshot;
}

void NoteChainState::restoreSnapshot(const StateSnapshot &snapshot)
{
    m_anchors = snapshot.anchors;
    m_links = snapshot.links;
    m_segDen = snapshot.segmentDenominators;
    m_segShape = snapshot.segmentShapes;
    m_densMode = snapshot.densityModes;
    m_nodeMeta = snapshot.nodeMeta;
    m_curveMeta = snapshot.curveMeta;
    m_nodeGroups = snapshot.nodeGroups;
    m_curveGroups = snapshot.curveGroups;
    m_selAnchorIds = snapshot.selectedAnchorIds;
    m_selLinkKeys = snapshot.selectedLinkKeys;
    m_style = snapshot.style;
    m_nextAnchorId = snapshot.nextAnchorId;
    m_nextCurveId = snapshot.nextCurveId;
    m_selTargets = snapshot.selectionTargets;
    m_curveVisible = snapshot.curveVisible;
    m_anchorPlaceEnabled = snapshot.anchorPlacementEnabled;
    m_noteSnap = snapshot.noteCurveSnapEnabled;
    m_activeShape = ncNormalizeShape(snapshot.activeLinkShape);
    m_drag = DragState{};
    m_linkDrag = LinkDrag{};
    m_boxSelect = BoxSelect{};
    m_pendingConnect = -1;
    m_shiftDown = false;
    invalidateCurveCache();
}

} // namespace NoteChain
