#pragma once
// NoteChainEditor.h - main editor orchestrator (based on Python input_handler.py + note_chain_assist.py)
#include "NoteChainState.h"
#include <QObject>
#include <QPainter>
#include <QElapsedTimer>

class QMouseEvent;
class ChartController;

namespace NoteChain {

class NoteChainEditor : public QObject {
    Q_OBJECT
public:
    explicit NoteChainEditor(QObject *parent = nullptr);

    void setActive(bool active);
    bool isActive() const { return m_active; }

    void setChartController(ChartController *ctrl) { m_chartCtrl = ctrl; }

    // ---- Mouse events (called from ChartCanvas) ----
    // canvasX/Y are in chart-space (laneX, beat). shiftDown/ctrlDown from Qt modifiers.
    bool handleMousePress(double chartX, double chartY, int button, bool shift, bool ctrl);
    bool handleMouseMove(double chartX, double chartY, int buttons);
    bool handleMouseRelease(double chartX, double chartY, int button);

    // ---- Cursor hint (called after mouseMove to update canvas cursor) ----
    QString hoverCursorHint(double chartX, double chartY) const;

    // ---- Keyboard ----
    bool handleKeyDown(int key, bool shift, bool ctrl);

    // ---- Render (direct QPainter, no overlay serialization) ----
    void render(QPainter *painter, const QRectF &viewport,
                double scrollBeat, double visibleBeatRange,
                const CanvasProjection &proj);

    // ---- Actions (tool_actions.py) ----
    bool commitCurveToNotes();
    void toggleAnchorPlacement();
    void toggleCurveVisible();
    void togglePolylineMode();
    void toggleNoteCurveSnap();
    void toggleSelectAnchors();
    void toggleSelectSegments();
    void toggleSelectNotes();
    void setAnchorPlacementEnabled(bool on);
    void setCurveVisible(bool on);
    void setPolylineMode(bool on);
    void setNoteCurveSnapEnabled(bool on);
    void setSelectAnchorsEnabled(bool on);
    void setSelectSegmentsEnabled(bool on);
    void connectSelectedAnchors();
    void disconnectSelectedSegments();
    void deleteSelected();
    void resetCurve();
    bool selectSegmentAt(double chartX, double chartY, bool append);
    bool hasSelectedSegments() const;
    int selectedSegmentDensity() const; // -2=no target, -1=mixed, 0=follow, >0=fixed
    bool setSelectedSegmentDensity(int denominator);
    bool toggleSelectedSegmentShape();

    // ---- Persistence ----
    bool loadProject(const QString &path);
    bool saveProject(const QString &path = QString());
    QString currentSidecarPath() const { return m_sidecarPath; }

    // ---- Undo/Redo ----
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();

    // ---- State access ----
    NoteChainState &state() { return m_state; }
    const NoteChainState &state() const { return m_state; }

    // ---- Context sync (from host) ----
    void setHostContext(const QVariantMap &ctx);

signals:
    void needsRepaint();
    void statusMessage(const QString &msg);
    void requestHostUndoCheckpoint(const QString &label);

private:
    bool m_active = false;
    NoteChainState m_state;
    ChartController *m_chartCtrl = nullptr;
    QString m_sidecarPath;

    // History
    QVector<StateSnapshot> m_history;
    int m_historyIdx = -1;

    // Drag throttling
    QElapsedTimer m_lastMoveTimer;
    static constexpr int kMoveThrottleMs = 16;

    // Internal helpers
    void recordHistory();
    void markDirty();
    int findAnchorHit(double chartX, double chartY) const;
    QPair<QString,int> findHandleHit(double chartX, double chartY) const;
    QPair<int,int> findSegmentHit(double chartX, double chartY) const; // returns (id0,id1) or (-1,-1)
    void syncAnchorPlacementWithHostMode();
    void syncAnchorSelectionFromHostNotes();
};

} // namespace NoteChain
