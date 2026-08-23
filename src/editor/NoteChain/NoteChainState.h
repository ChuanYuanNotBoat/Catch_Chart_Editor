#pragma once
// NoteChainState.h — 运行时状态树 (基于 Python state.py)
#include "NoteChainCommon.h"
#include "NoteChainCurveSampler.h"
#include <QObject>

namespace NoteChain {

class NoteChainState {
public:
    NoteChainState();

    // 锚点 (Python: STATE["anchors"])
    int  appendAnchor(double laneX, double beat);
    int  insertAnchor(const Anchor &anchor);
    void removeAnchorById(int id);
    int  anchorIndexById(int id) const;
    const QVector<Anchor>& anchors() const { return m_anchors; }
    Anchor& anchorAt(int idx);
    const Anchor& anchorAt(int idx) const;
    void setAnchorInAbsChart(int idx, double laneX, double beat, bool mirror);
    void setAnchorOutAbsChart(int idx, double laneX, double beat, bool mirror);
    int  nextAnchorId() const { return m_nextAnchorId; }
    QMap<int,int> anchorIndexMap() const;
    Link normalizedLink(int fromId, int toId) const;

    // 链接 (Python: STATE["links"])
    void addLink(int fromId, int toId);
    void removeLink(int fromId, int toId);
    bool hasLink(int fromId, int toId) const;
    void clearLinks();
    const QVector<Link>& linksAll() const { return m_links; }

    // 段密度/形态 (curve_model.py)
    void setSegmentDen(int fromId, int toId, int den);
    int  segmentDen(int fromId, int toId) const;
    int  segmentDensityMode(int fromId, int toId) const;
    void setSegmentShape(int fromId, int toId, const QString &shape);
    QString segmentShape(int fromId, int toId) const;
    void setDensityMode(int fromId, int toId, int mode);
    void seedMissingSegmentDenominators();
    QVector<SegmentInfo> connectedAnchorSegments() const;

    // V3 extension metadata. Unknown/reserved fields survive native edits.
    NodePersistenceMeta nodeMeta(int anchorId) const;
    void setNodeMeta(int anchorId, const NodePersistenceMeta &meta);
    CurvePersistenceMeta curveMeta(int fromId, int toId) const;
    void setCurveMeta(int fromId, int toId, const CurvePersistenceMeta &meta);
    const QVector<GroupPersistenceMeta>& nodeGroups() const { return m_nodeGroups; }
    const QVector<GroupPersistenceMeta>& curveGroups() const { return m_curveGroups; }
    void setNodeGroups(const QVector<GroupPersistenceMeta> &groups) { m_nodeGroups = groups; }
    void setCurveGroups(const QVector<GroupPersistenceMeta> &groups) { m_curveGroups = groups; }
    int nextCurveId() const { return m_nextCurveId; }
    void setNextCurveId(int id) { m_nextCurveId = qMax(1, id); }

    // 选中 (selected_anchor_ids / selected_links)
    void selectAnchor(int id), deselectAnchor(int id), clearAnchorSelection();
    void toggleAnchorSelection(int id), setSingleSelectedAnchor(int id);
    bool isAnchorSelected(int id) const;
    const QSet<int>& selectedAnchorIds() const { return m_selAnchorIds; }
    void selectLink(LinkKey k), deselectLink(LinkKey k), clearLinkSelection();
    void toggleLinkSelection(LinkKey k);
    bool isLinkSelected(LinkKey k) const;
    const QSet<LinkKey>& selectedLinkKeys() const { return m_selLinkKeys; }

    // 框选/拖拽/link drag
    BoxSelect& boxSelect() { return m_boxSelect; }
    const BoxSelect& boxSelect() const { return m_boxSelect; }
    DragState& drag() { return m_drag; }
    const DragState& drag() const { return m_drag; }
    LinkDrag& linkDrag() { return m_linkDrag; }
    const LinkDrag& linkDrag() const { return m_linkDrag; }

    // 选择目标 (selection_targets)
    bool selectionEnabled(const QString &kind) const;
    bool selectionTargetEnabled(const QString &kind) const;
    void setSelectionEnabled(const QString &kind, bool v);

    // 可见性 / overlay toggle / 锚点放置 / snap
    bool curveVisible() const { return m_curveVisible; }
    void setCurveVisible(bool v) { m_curveVisible = v; }
    OverlayToggles& overlayToggles() { return m_overlayToggles; }
    bool anchorPlacementEnabled() const { return m_anchorPlaceEnabled; }
    void setAnchorPlacementEnabled(bool v) { m_anchorPlaceEnabled = v; }
    bool noteCurveSnapEnabled() const { return m_noteSnap; }
    void setNoteCurveSnapEnabled(bool v) { m_noteSnap = v; }
    QString activeLinkShape() const { return m_activeShape; }
    void setActiveLinkShape(const QString &s) { m_activeShape = ncNormalizeShape(s); }

    // Style / pending connect / 双击 / shift
    StylePreset& style() { return m_style; }
    const StylePreset& style() const { return m_style; }
    void setStyle(const StylePreset &style) { m_style = style; }
    int pendingConnectAnchorId() const { return m_pendingConnect; }
    void setPendingConnectAnchorId(int id) { m_pendingConnect = id; }
    int lastClickAnchor() const { return m_lastClickIdx; }
    qint64 lastClickMs() const { return m_lastClickMs; }
    void setLastClick(int idx, qint64 ts) { m_lastClickIdx = idx; m_lastClickMs = ts; }
    bool shiftDown() const { return m_shiftDown; }
    void setShiftDown(bool v) { m_shiftDown = v; }

    // 曲线缓存 / 项目元数据
    void invalidateCurveCache() { m_cacheValid = false; ++m_curveRevision; }
    bool isCurveCacheValid() const { return m_cacheValid; }
    quint64 curveRevision() const { return m_curveRevision; }
    int projectRevision() const { return m_projRev; }
    void setProjectRevision(int r) { m_projRev = r; }
    QString projectFileUuid() const { return m_projUuid; }
    void setProjectFileUuid(const QString &s) { m_projUuid = s; }
    QString lastWriterInstance() const { return m_lastWriter; }
    void setLastWriterInstance(const QString &s) { m_lastWriter = s; }
    QString projectPath() const { return m_projPath; }
    void setProjectPath(const QString &s) { m_projPath = s; }
    bool projectDirty() const { return m_projDirty; }
    void setProjectDirty(bool v) { m_projDirty = v; }
    bool suppressPersistOnce() const { return m_suppressPersist; }
    void setSuppressPersistOnce(bool v) { m_suppressPersist = v; }
    void setNextAnchorId(int id) { m_nextAnchorId = qMax(1, id); }

    // 约束 & 清理
    void enforceHandleTimeConstraints(int idx, int timeDivision = 4);
    void enforceAnchorAndConnectedHandleConstraints(int idx, int timeDivision = 4);
    void cleanupLinksAndSelection();

    // 快照
    StateSnapshot captureSnapshot() const;
    void restoreSnapshot(const StateSnapshot &snap);

    // 宿主联动
    void setLastContext(const QVariantMap &ctx) { m_lastCtx = ctx; }
    QVariantMap lastContext() const { return m_lastCtx; }
    QSet<int>& lastHostSelectedNoteIds() { return m_lastHostSel; }

private:
    QVector<Anchor> m_anchors; int m_nextAnchorId = 1;
    QVector<Link> m_links;
    QMap<LinkKey,int> m_segDen, m_densMode;
    QMap<LinkKey,QString> m_segShape;
    QMap<int,NodePersistenceMeta> m_nodeMeta;
    QMap<LinkKey,CurvePersistenceMeta> m_curveMeta;
    QVector<GroupPersistenceMeta> m_nodeGroups, m_curveGroups;
    int m_nextCurveId = 1;
    QSet<int> m_selAnchorIds; QSet<LinkKey> m_selLinkKeys;
    BoxSelect m_boxSelect; DragState m_drag; LinkDrag m_linkDrag;
    SelectionTargets m_selTargets;
    bool m_curveVisible=true; OverlayToggles m_overlayToggles;
    bool m_anchorPlaceEnabled=false, m_noteSnap=false;
    QString m_activeShape; StylePreset m_style;
    int m_pendingConnect=-1, m_lastClickIdx=-1; qint64 m_lastClickMs=0;
    bool m_shiftDown=false, m_cacheValid=false;
    quint64 m_curveRevision = 1;
    int m_projRev=0; QString m_projUuid, m_lastWriter, m_projPath;
    bool m_projDirty=false, m_suppressPersist=false;
    QVariantMap m_lastCtx; QSet<int> m_lastHostSel;
    void removeLinkInternal(int fromId, int toId);
};
} // namespace NoteChain
