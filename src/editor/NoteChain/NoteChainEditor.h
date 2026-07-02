#pragma once

// NoteChainEditor.h — 顶层协调器
// 管理 NoteChain 全部子系统的生命周期，对外提供简洁接口

#include "NoteChainState.h"
#include "NoteChainHistory.h"
#include "NoteChainPersistence.h"
#include "NoteChainCurveSampler.h"

#include <QObject>
#include <QPainter>
#include <QRectF>

class QMouseEvent;

namespace NoteChain {

class NoteChainOverlay;

class NoteChainEditor : public QObject
{
    Q_OBJECT

public:
    explicit NoteChainEditor(QObject *parent = nullptr);
    ~NoteChainEditor();

    // ---- 模式控制 ----
    void setActive(bool active);
    bool isActive() const { return m_active; }

    // ---- 鼠标交互（从 ChartCanvas 调用）----
    enum class MouseResult {
        NotHandled,
        Handled,
        NeedsRepaint
    };
    MouseResult handleMousePress(QMouseEvent *event, double canvasX, double canvasY,
                                 bool shiftDown, bool ctrlDown);
    MouseResult handleMouseMove(QMouseEvent *event, double canvasX, double canvasY);
    MouseResult handleMouseRelease(QMouseEvent *event, double canvasX, double canvasY);
    MouseResult handleMouseDoubleClick(QMouseEvent *event, double canvasX, double canvasY);

    // ---- 键盘 ----
    void handleKeyDelete();
    void handleKeyEscape();

    // ---- 渲染（从 ChartCanvas drawForeground 调用）----
    void renderOverlay(QPainter *painter, const QRectF &rect,
                       double scrollBeat, double visibleBeatRange);

    // ---- 撤销/重做 ----
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();

    // ---- 持久化 ----
    bool loadProject(const QString &sidecarPath);
    bool saveProject(const QString &sidecarPath = QString());

    // ---- 批量提交（生成 Note）----
    QVector<SampledPoint> generateNotes() const;

    // ---- 状态访问 ----
    NoteChainState &state() { return m_state; }
    const NoteChainState &state() const { return m_state; }
    NoteChainHistory &history() { return m_history; }

    // ---- 面板数据 ----
    int selectedAnchorId() const;
    QVector<int> allAnchorIds() const;

private:
    void recordHistory();
    void ensureOverlay();

    bool m_active = false;

    NoteChainState m_state;
    NoteChainHistory m_history;

    QString m_currentSidecarPath;

    // UI 组件（延迟初始化）
    NoteChainOverlay *m_overlay = nullptr;

    // 拖拽状态
    enum class DragMode {
        None,
        Anchor,       // 拖拽锚点
        HandleIn,     // 拖拽入控制柄
        HandleOut,    // 拖拽出控制柄
        BoxSelect,    // 框选
        LinkDrag,     // Shift+拖拽创建链接
    };
    DragMode m_dragMode = DragMode::None;
    int m_dragAnchorId = -1;
    int m_linkDragFromId = -1;
    double m_boxStartX = 0, m_boxStartY = 0;

    // 锚点放置模式
    bool m_anchorPlaceMode = false;
};

} // namespace NoteChain