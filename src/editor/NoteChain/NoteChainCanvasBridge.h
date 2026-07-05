#pragma once

// NoteChainCanvasBridge.h — ChartCanvas 集成桥接
// 包含此文件即可在 ChartCanvas 体系中使用 NoteChain 原生编辑器，
// 无需修改 ChartCanvas.h 头文件。

#include "editor/NoteChain/NoteChainEditor.h"

#include <QMouseEvent>
#include <QPainter>

namespace NoteChain {

/// 处理鼠标按下事件
/// 返回 true 表示事件已被消费
inline bool bridgeHandleMousePress(NoteChainEditor *editor, QMouseEvent *event,
                                   double canvasX, double canvasY,
                                   bool shiftDown, bool ctrlDown)
{
    if (!editor || !editor->isActive())
        return false;

    auto result = editor->handleMousePress(event, canvasX, canvasY, shiftDown, ctrlDown);
    return result != NoteChainEditor::MouseResult::NotHandled;
}

/// 处理鼠标移动事件
inline bool bridgeHandleMouseMove(NoteChainEditor *editor, QMouseEvent *event,
                                  double canvasX, double canvasY)
{
    if (!editor || !editor->isActive())
        return false;

    auto result = editor->handleMouseMove(event, canvasX, canvasY);
    return result != NoteChainEditor::MouseResult::NotHandled;
}

/// 处理鼠标释放事件
inline bool bridgeHandleMouseRelease(NoteChainEditor *editor, QMouseEvent *event,
                                     double canvasX, double canvasY)
{
    if (!editor || !editor->isActive())
        return false;

    auto result = editor->handleMouseRelease(event, canvasX, canvasY);
    return result != NoteChainEditor::MouseResult::NotHandled;
}

/// 绘制叠加层
inline void bridgeRenderOverlay(NoteChainEditor *editor, QPainter *painter,
                                const QRectF &rect, double scrollBeat,
                                double visibleBeatRange,
                                const ProjectX &projX, const ProjectY &projY)
{
    if (!editor || !editor->isActive())
        return;
    editor->renderOverlay(painter, rect, scrollBeat, visibleBeatRange, projX, projY);
}

} // namespace NoteChain