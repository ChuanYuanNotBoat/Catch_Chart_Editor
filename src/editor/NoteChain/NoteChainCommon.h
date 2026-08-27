#pragma once

// NoteChainTypes.h — 完整数据结构定义
// 基于 Python: state.py (STATE dict), curve_model.py, batch_commit.py
// 坐标体系: chart-space (laneX ∈ [0,512], beat = 浮点有理数)

#include <QPointF>
#include <QString>
#include <QVector>
#include <QMap>
#include <QSet>
#include <QPair>
#include <QHash>
#include <QJsonObject>
#include <QVariantMap>

namespace NoteChain {

// ====== 常量 ======
namespace Const {
    constexpr double kLaneWidth          = 512.0;
    // Interaction and drawing sizes are canvas pixels.  Geometry stored in
    // Anchor remains chart-space; never compare these values to lane/beat
    // coordinates directly.
    constexpr double kAnchorHitRadius    = 16.0;
    constexpr double kHandleHitRadius    = 14.0;
    constexpr double kSegmentHitDist     = 14.0;
    constexpr double kAnchorDrawRadius   = 6.0;
    constexpr double kHandleDrawRadius   = 4.0;
    constexpr double kDefaultSegmentDen  = 4;
    constexpr int    kMaxHistory         = 128;
    constexpr int    kSerializeDen       = 288;
    constexpr int    kMaxSamplesPerSeg   = 32;
    constexpr int    kDoubleClickMs      = 280;
    constexpr int    kLeftButton         = 1;
    constexpr int    kRightButton        = 2;

    // V3 sidecar 格式版本号
    constexpr int kSidecarFormatVersion = 3;
    constexpr int kDefaultNodeGroupId   = 1;
    constexpr int kDefaultCurveGroupId  = 1;

    // 默认 style presets
    inline const QVector<QVector<int>> &stylePresets() {
        static const QVector<QVector<int>> presets = {
            {4, 8, 12, 16},
            {8, 8, 12, 16, 24},
            {4, 6, 8, 12, 16, 24},
            {12, 16, 24, 32},
        };
        return presets;
    }

    constexpr const char *kShapeCurve    = "curve";
    constexpr const char *kShapePolyline = "polyline";
    constexpr const char *kCurveCheckpointPrefix = "Plugin Curve Edit";

    // checkpoint prefix for host undo integration
    inline const char *checkpointPrefix() { return kCurveCheckpointPrefix; }
}

// ====== 链接键 ======
using LinkKey = QPair<int, int>; // (minId, maxId)

inline LinkKey makeLinkKey(int a, int b) {
    return LinkKey(qMin(a, b), qMax(a, b));
}

// ====== 锚点 ======
// 对应 Python: anchor dict {"id", "lane_x", "beat", "in": [dx,dy], "out": [dx,dy], "smooth": bool}
struct Anchor {
    int    id      = -1;
    double laneX   = 0.0;    // chart lane 坐标
    double beat    = 0.0;    // chart beat 坐标
    double inDx    = 0.0;    // 入控制柄 lane 偏移
    double inDy    = 0.0;    // 入控制柄 beat 偏移
    double outDx   = 0.0;    // 出控制柄 lane 偏移
    double outDy   = 0.0;    // 出控制柄 beat 偏移
    bool   smooth  = true;   // true=smooth, false=corner

    QPointF pos()          const { return QPointF(laneX, beat); }
    QPointF inAbs()        const { return QPointF(laneX + inDx, beat + inDy); }
    QPointF outAbs()       const { return QPointF(laneX + outDx, beat + outDy); }
    bool    hasInHandle()  const { return inDx != 0.0 || inDy != 0.0; }
    bool    hasOutHandle() const { return outDx != 0.0 || outDy != 0.0; }

    bool operator==(const Anchor &o) const {
        return id == o.id && qAbs(laneX - o.laneX) < 1e-9 && qAbs(beat - o.beat) < 1e-9
            && qAbs(inDx - o.inDx) < 1e-9 && qAbs(inDy - o.inDy) < 1e-9
            && qAbs(outDx - o.outDx) < 1e-9 && qAbs(outDy - o.outDy) < 1e-9
            && smooth == o.smooth;
    }
};

// ====== 链接 ======
// 对应 Python: link = [fromId, toId]
struct Link {
    int from = -1;
    int to   = -1;
    LinkKey key() const { return makeLinkKey(from, to); }
    bool valid() const { return from >= 0 && to >= 0 && from != to; }
    bool operator==(const Link &o) const { return from == o.from && to == o.to; }
};

// ====== 框选 ======
struct BoxSelect {
    bool   active = false;
    double startX = 0.0, startY = 0.0; // canvas 像素坐标
    double endX   = 0.0, endY   = 0.0;
    bool   append = false;              // Ctrl 追加
};

// ====== 拖拽状态 ======
struct DragState {
    QString mode;     // "" | "anchor" | "in" | "out"
    int     index = -1; // anchor 在 m_anchors 数组中的索引
};

// ====== Link 拖拽 ======
struct LinkDrag {
    bool   active        = false;
    int    sourceAnchorId = -1;
    int    hoverAnchorId  = -1;
    double x = 0.0, y = 0.0; // canvas 像素坐标（用于预览线）
};

// ====== 选择目标 ======
struct SelectionTargets {
    bool anchors  = true;
    bool segments = true;
    bool notes    = false;
};

// ====== Style preset ======
struct StylePreset {
    QString      name;
    QVector<int> denominators;
};

// ====== Overlay toggles ======
struct OverlayToggles {
    bool enabled      = true;
    bool preview      = true;
    bool controlPoints = true;
    bool handles      = true;
    bool samplePoints = true;
    bool labels       = true;
};

// V3 sidecar extension fields are intentionally retained even when the
// native editor does not interpret them.  Dropping these values on save made
// opening a Python-authored project in the C++ editor destructive.
struct NodePersistenceMeta {
    QVector<int> groupIds{Const::kDefaultNodeGroupId};
    QJsonObject reserved;

    bool operator==(const NodePersistenceMeta &o) const {
        return groupIds == o.groupIds && reserved == o.reserved;
    }
};

struct CurvePersistenceMeta {
    int curveId = 0;
    int curveNo = 0;
    QVector<int> groupIds{Const::kDefaultCurveGroupId};
    QJsonObject specialJoystickReserved;
    QJsonObject reserved;

    bool operator==(const CurvePersistenceMeta &o) const {
        return curveId == o.curveId && curveNo == o.curveNo
            && groupIds == o.groupIds
            && specialJoystickReserved == o.specialJoystickReserved
            && reserved == o.reserved;
    }
};

struct GroupPersistenceMeta {
    int id = 0;
    QString name;
    QJsonObject reserved;

    bool operator==(const GroupPersistenceMeta &o) const {
        return id == o.id && name == o.name && reserved == o.reserved;
    }
};

// ====== 采样点（用于 commit） ======
struct SampledPoint {
    double laneX = 0.0;
    double beat  = 0.0;
};

// ====== 段信息 ======
struct SegmentInfo {
    int    i0, i1;        // anchor 在数组中的索引
    int    id0, id1;      // anchor id
    Anchor a0, a1;
    QString shape;        // "curve" | "polyline"
    int    denominator = Const::kDefaultSegmentDen;
};

// ====== 历史快照 ======
// 包含所有可撤销的状态字段
struct StateSnapshot {
    QVector<Anchor> anchors;
    QVector<Link>   links;
    QMap<LinkKey, int>     segmentDenominators;
    QMap<LinkKey, QString> segmentShapes;
    QMap<LinkKey, int>     densityModes;       // "follow"=0 或 "fixed"=den
    QMap<int, NodePersistenceMeta> nodeMeta;
    QMap<LinkKey, CurvePersistenceMeta> curveMeta;
    QVector<GroupPersistenceMeta> nodeGroups;
    QVector<GroupPersistenceMeta> curveGroups;
    QSet<int>              selectedAnchorIds;
    QSet<LinkKey>          selectedLinkKeys;
    StylePreset            style;
    int                    nextAnchorId = 1;
    int                    nextCurveId  = 1;
    int                    nextGroupId  = 2;
    SelectionTargets       selectionTargets;
    bool                   curveVisible = true;
    bool                   anchorPlacementEnabled = false;
    bool                   noteCurveSnapEnabled = false;
    QString                activeLinkShape = QStringLiteral("curve");

    bool operator==(const StateSnapshot &o) const {
        return anchors == o.anchors && links == o.links
            && segmentDenominators == o.segmentDenominators
            && segmentShapes == o.segmentShapes
            && densityModes == o.densityModes
            && nodeMeta == o.nodeMeta
            && curveMeta == o.curveMeta
            && nodeGroups == o.nodeGroups
            && curveGroups == o.curveGroups
            && selectedAnchorIds == o.selectedAnchorIds
            && selectedLinkKeys == o.selectedLinkKeys
            && style.name == o.style.name
            && style.denominators == o.style.denominators
            && nextAnchorId == o.nextAnchorId
            && nextCurveId == o.nextCurveId
            && nextGroupId == o.nextGroupId
            && selectionTargets.anchors == o.selectionTargets.anchors
            && selectionTargets.segments == o.selectionTargets.segments
            && selectionTargets.notes == o.selectionTargets.notes
            && curveVisible == o.curveVisible
            && anchorPlacementEnabled == o.anchorPlacementEnabled
            && noteCurveSnapEnabled == o.noteCurveSnapEnabled
            && activeLinkShape == o.activeLinkShape;
    }
};

} // namespace NoteChain
