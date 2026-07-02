#pragma once

// NoteChainData.h — 纯数据结构（对应 Python modular/core/anchor.py 和 link.py）

#include "NoteChainCommon.h"

#include <QPointF>
#include <QString>

namespace NoteChain {

// ---- 锚点数据结构 ----
// 对应 Python: anchor["x"], anchor["y"], anchor["hx_i"], anchor["hy_i"], anchor["hx_o"], anchor["hy_o"]
struct Anchor
{
    int id = -1;        // 锚点唯一 ID
    double x = 0.0;     // 画布 X 坐标（节拍 beat）
    double y = 0.0;     // 画布 Y 坐标
    double hx_i = 0.0;  // 入控制柄 X 偏移（相对于锚点）
    double hy_i = 0.0;  // 入控制柄 Y 偏移
    double hx_o = 0.0;  // 出控制柄 X 偏移（相对于锚点）
    double hy_o = 0.0;  // 出控制柄 Y 偏移

    QPointF position() const { return QPointF(x, y); }
    QPointF handleInAbs() const  { return QPointF(x + hx_i, y + hy_i); }
    QPointF handleOutAbs() const { return QPointF(x + hx_o, y + hy_o); }

    bool hasHandleIn() const  { return hx_i != 0.0 || hy_i != 0.0; }
    bool hasHandleOut() const { return hx_o != 0.0 || hy_o != 0.0; }
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