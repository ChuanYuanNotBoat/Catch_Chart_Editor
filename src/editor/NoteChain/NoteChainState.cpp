// NoteChainState.cpp — 运行时状态管理实现

#include "NoteChainState.h"

namespace NoteChain {

NoteChainState::NoteChainState()  = default;
NoteChainState::~NoteChainState() = default;

LinkKey NoteChainState::makeLinkKey(int fromId, int toId) const
{
    int minId = qMin(fromId, toId);
    int maxId = qMax(fromId, toId);
    return LinkKey(minId, maxId);
}

// ---- 锚点管理 ----

int NoteChainState::addAnchor(const Anchor &anchor)
{
    int id = anchor.id >= 0 ? anchor.id : m_nextAnchorId++;
    Anchor a = anchor;
    a.id = id;
    m_anchors[id] = a;
    if (id >= m_nextAnchorId)
        m_nextAnchorId = id + 1;
    return id;
}

void NoteChainState::removeAnchor(int id)
{
    m_anchors.remove(id);

    // 移除与该锚点相关的所有链接
    QVector<LinkKey> toRemove;
    for (const LinkKey &key : m_links) {
        if (key.first == id || key.second == id)
            toRemove.append(key);
    }
    for (const LinkKey &key : toRemove) {
        m_links.remove(key);
        m_segmentDenominators.remove(key);
        m_segmentShapes.remove(key);
    }

    // 从选中集中移除
    m_selectedAnchorIds.remove(id);
    m_compoundSelection.remove(id);
}

bool NoteChainState::hasAnchor(int id) const
{
    return m_anchors.contains(id);
}

Anchor NoteChainState::anchor(int id) const
{
    return m_anchors.value(id, Anchor());
}

void NoteChainState::setAnchor(int id, const Anchor &anchor)
{
    if (m_anchors.contains(id)) {
        Anchor a = anchor;
        a.id = id;
        m_anchors[id] = a;
    }
}

// ---- 链接管理 ----

void NoteChainState::addLink(int fromId, int toId)
{
    if (fromId == toId || !hasAnchor(fromId) || !hasAnchor(toId))
        return;
    LinkKey key = makeLinkKey(fromId, toId);
    m_links.insert(key);
}

void NoteChainState::removeLink(int fromId, int toId)
{
    LinkKey key = makeLinkKey(fromId, toId);
    m_links.remove(key);
    m_segmentDenominators.remove(key);
    m_segmentShapes.remove(key);
}

bool NoteChainState::hasLink(int fromId, int toId) const
{
    return m_links.contains(makeLinkKey(fromId, toId));
}

void NoteChainState::clearLinks()
{
    m_links.clear();
    m_segmentDenominators.clear();
    m_segmentShapes.clear();
}

QVector<Link> NoteChainState::links() const
{
    QVector<Link> result;
    for (const LinkKey &key : m_links) {
        Link link;
        link.fromAnchorId = key.first;
        link.toAnchorId   = key.second;
        result.append(link);
    }
    return result;
}

bool NoteChainState::linkExists(int fromId, int toId) const
{
    return hasLink(fromId, toId);
}

// ---- 段密度 ----

void NoteChainState::setSegmentDenominator(int fromId, int toId, int denominator)
{
    if (!linkExists(fromId, toId))
        return;
    LinkKey key = makeLinkKey(fromId, toId);
    m_segmentDenominators[key] = denominator;
}

int NoteChainState::segmentDenominator(int fromId, int toId) const
{
    LinkKey key = makeLinkKey(fromId, toId);
    return m_segmentDenominators.value(key, kDefaultSegmentDenominator);
}

// ---- 段形态 ----

void NoteChainState::setSegmentShape(int fromId, int toId, const QString &shape)
{
    if (!linkExists(fromId, toId))
        return;
    LinkKey key = makeLinkKey(fromId, toId);
    m_segmentShapes[key] = shape;
}

QString NoteChainState::segmentShape(int fromId, int toId) const
{
    LinkKey key = makeLinkKey(fromId, toId);
    return m_segmentShapes.value(key, QString::fromLatin1(kShapeCurve));
}

// ---- 选中管理 ----

void NoteChainState::selectAnchor(int id)
{
    if (hasAnchor(id))
        m_selectedAnchorIds.insert(id);
}

void NoteChainState::deselectAnchor(int id)
{
    m_selectedAnchorIds.remove(id);
}

void NoteChainState::clearSelection()
{
    m_selectedAnchorIds.clear();
}

void NoteChainState::toggleSelection(int id)
{
    if (m_selectedAnchorIds.contains(id))
        m_selectedAnchorIds.remove(id);
    else if (hasAnchor(id))
        m_selectedAnchorIds.insert(id);
}

bool NoteChainState::isSelected(int id) const
{
    return m_selectedAnchorIds.contains(id);
}

int NoteChainState::singleSelectedAnchorId() const
{
    if (m_selectedAnchorIds.size() == 1)
        return *m_selectedAnchorIds.begin();
    return -1;
}

// ---- 框选 ----

void NoteChainState::setCompoundSelection(const QSet<int> &ids)
{
    m_compoundSelection = ids;
}

void NoteChainState::clearCompoundSelection()
{
    m_compoundSelection.clear();
}

// ---- 曲线项目元数据 ----

void NoteChainState::setProjectMeta(const CurveProjectMeta &meta)
{
    m_projectMeta = meta;
}

// ---- 深拷贝 ----

NoteChainState NoteChainState::clone() const
{
    NoteChainState copy;
    copy.m_anchors            = m_anchors;
    copy.m_links              = m_links;
    copy.m_segmentDenominators = m_segmentDenominators;
    copy.m_segmentShapes      = m_segmentShapes;
    copy.m_selectedAnchorIds  = m_selectedAnchorIds;
    copy.m_compoundSelection  = m_compoundSelection;
    copy.m_nextAnchorId       = m_nextAnchorId;
    copy.m_projectMeta        = m_projectMeta;
    return copy;
}

// ---- 状态一致性修复 ----
// 对应 Python: _cleanup_links_and_selection()

void NoteChainState::cleanupOrphanedLinksAndSelection()
{
    // 移除引用了不存在锚点的链接
    QVector<LinkKey> orphanedLinks;
    for (const LinkKey &key : m_links) {
        if (!m_anchors.contains(key.first) || !m_anchors.contains(key.second))
            orphanedLinks.append(key);
    }
    for (const LinkKey &key : orphanedLinks) {
        m_links.remove(key);
        m_segmentDenominators.remove(key);
        m_segmentShapes.remove(key);
    }

    // 移除引用不存在锚点的选中项
    QSet<int> validSelection;
    for (int id : m_selectedAnchorIds) {
        if (m_anchors.contains(id))
            validSelection.insert(id);
    }
    m_selectedAnchorIds = validSelection;

    // 移除引用不存在锚点的框选项
    QSet<int> validCompound;
    for (int id : m_compoundSelection) {
        if (m_anchors.contains(id))
            validCompound.insert(id);
    }
    m_compoundSelection = validCompound;
}

} // namespace NoteChain