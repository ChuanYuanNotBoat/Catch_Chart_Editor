#pragma once

// NoteChainData.h — 纯数据结构（对应 Python modular/core/anchor.py 和 link.py）

#include "NoteChainCommon.h"

#include <QPointF>
#include <QString>

namespace NoteChain {

// ---- 锚点数据结构 ----
// 对应 Python: anchor["x"], anchor["y"], anchor["hx_i"], anchor["hy_i"], anchor["hx_o"], anchor["hy_o"]
// 坐标体系：laneX/beat 为 chart 坐标系（谱面坐标），handleIn*/handleOut* 为相对偏移
struct Anchor
{
    int id = -1;              // 锚点唯一 ID
    double laneX = 0.0;       // chart lane 坐标
    double beat = 0.0;        // chart beat 坐标
    double handleInDx = 0.0;  // 入控制柄 lane 偏移（相对于锚点）
    double handleInDy = 0.0;  // 入控制柄 beat 偏移
    double handleOutDx = 0.0; // 出控制柄 lane 偏移（相对于锚点）
    double handleOutDy = 0.0; // 出控制柄 beat 偏移

    QPointF position() const { return QPointF(laneX, beat); }
    QPointF handleInAbs() const  { return QPointF(laneX + handleInDx, beat + handleInDy); }
    QPointF handleOutAbs() const { return QPointF(laneX + handleOutDx, beat + handleOutDy); }

    bool hasHandleIn() const  { return handleInDx != 0.0 || handleInDy != 0.0; }
    bool hasHandleOut() const { return handleOutDx != 0.0 || handleOutDy != 0.0; }
};

// ---- 链接数据结构 ----
// 对应 Python: link["from"], link["to"]
struct Link
{
    int fromAnchorId = -1;
    int toAnchorId   = -1;

    LinkKey key() const
    {
        int minId = qMin(fromAnchorId, toAnchorId);
        int maxId = qMax(fromAnchorId, toAnchorId);
        return LinkKey(minId, maxId);
    }

    bool isValid() const { return fromAnchorId >= 0 && toAnchorId >= 0 && fromAnchorId != toAnchorId; }
};

// ---- 曲线项目 V3 元数据 ----
struct CurveProjectMeta
{
    QString filename;      // 侧车文件名
    int revision = 0;      // CAS 版本号
    int anchorIdCounter = 0;
};

} // namespace NoteChain