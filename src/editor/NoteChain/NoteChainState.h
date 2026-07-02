#pragma once

// NoteChainState.h — 运行时状态管理（对应 Python STATE dict）
// 管理所有锚点、链接、选中集、曲线项目元数据

#include "NoteChainData.h"

#include <QMap>
#include <QSet>
#include <QVector>

namespace NoteChain {

class NoteChainState
{
public:
    NoteChainState();
    ~NoteChainState();

    // ---- 锚点管理 ----
    int addAnchor(const Anchor &anchor);
    void removeAnchor(int id);
    bool hasAnchor(int id) const;
    Anchor anchor(int id) const;                  // 返回副本，不存在时返回无效 Anchor
    void setAnchor(int id, const Anchor &anchor); // 更新锚点
    QMap<int, Anchor> anchors() const { return m_anchors; }
    int anchorCount() const { return m_anchors.size(); }
    int nextAnchorId() const { return m_nextAnchorId; }

    // ---- 链接管理 ----
    void addLink(int fromId, int toId);
    void removeLink(int fromId, int toId);
    bool hasLink(int fromId, int toId) const;
    void clearLinks();                            // 移除所有链接
    QVector<Link> links() const;
    bool linkExists(int fromId, int toId) const;

    // ---- 段密度（分段分母）----
    void setSegmentDenominator(int fromId, int toId, int denominator);
    int segmentDenominator(int fromId, int toId) const;
    SegmentDenominatorMap segmentDenominators() const { return m_segmentDenominators; }

    // ---- 段形态（curve / polyline）----
    void setSegmentShape(int fromId, int toId, const QString &shape);
    QString segmentShape(int fromId, int toId) const;
    SegmentShapeMap segmentShapes() const { return m_segmentShapes; }

    // ---- 选中管理 ----
    void selectAnchor(int id);
    void deselectAnchor(int id);
    void clearSelection();
    void toggleSelection(int id);                  // Ctrl+点击
    bool isSelected(int id) const;
    QSet<int> selectedAnchorIds() const { return m_selectedAnchorIds; }
    int selectedCount() const { return m_selectedAnchorIds.size(); }
    int singleSelectedAnchorId() const;            // 只选中一个时返回其 ID，否则返回 -1

    // ---- 框选（复合选中）----
    void setCompoundSelection(const QSet<int> &ids);
    QSet<int> compoundSelection() const { return m_compoundSelection; }
    void clearCompoundSelection();

    // ---- 曲线项目元数据 ----
    CurveProjectMeta projectMeta() const { return m_projectMeta; }
    void setProjectMeta(const CurveProjectMeta &meta);

    // ---- 深拷贝（用于撤销快照）----
    NoteChainState clone() const;

    // ---- 状态一致性修复 ----
    void cleanupOrphanedLinksAndSelection();

private:
    QMap<int, Anchor> m_anchors;
    QSet<LinkKey> m_links;                         // 规范化键 (minId, maxId)
    SegmentDenominatorMap m_segmentDenominators;
    SegmentShapeMap m_segmentShapes;
    QSet<int> m_selectedAnchorIds;
    QSet<int> m_compoundSelection;
    int m_nextAnchorId = 0;
    CurveProjectMeta m_projectMeta;

    LinkKey makeLinkKey(int fromId, int toId) const;
};

} // namespace NoteChain