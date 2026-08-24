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
    // Pointer positions are canvas pixels; projection performs the only
    // canvas/chart conversion used by interaction and hit testing.
    bool handleMousePress(const QPointF &canvasPos, const CanvasProjection &projection,
                          int button, bool shift, bool ctrl);
    bool handleMouseMove(const QPointF &canvasPos, const CanvasProjection &projection,
                         int buttons, bool shift);
    bool handleMouseRelease(const QPointF &canvasPos, const CanvasProjection &projection, int button);

    // ---- Cursor hint (called after mouseMove to update canvas cursor) ----
    QString hoverCursorHint(const QPointF &canvasPos, const CanvasProjection &projection) const;

    // ---- Keyboard ----
    bool handleKeyDown(int key, bool shift, bool ctrl);

    // ---- Render (direct QPainter, no overlay serialization) ----
    void render(QPainter *painter, const QRectF &viewport, const CanvasProjection &projection);

    // ---- Actions (tool_actions.py) ----
    bool commitCurveToNotes();
    bool commitContextSegmentsToNotes();
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
    void setSelectNotesEnabled(bool on);
    void connectSelectedAnchors();
    void disconnectSelectedSegments();
    void deleteSelected();
    void resetCurve();
    void prepareContextMenuAt(const QPointF &canvasPos, const CanvasProjection &projection);
    bool hasSelectedItems() const;
    bool hasSelectedSegments() const;
    bool hasContextSegments() const { return !m_contextLinkKeys.isEmpty(); }
    int selectedSegmentDensity() const; // -2=no target, -1=mixed, 0=follow, >0=fixed
    int contextSegmentDensity() const;
    bool setSelectedSegmentDensity(int denominator);
    bool setContextSegmentDensity(int denominator);
    bool toggleSelectedSegmentShape();
    bool toggleContextSegmentShape();
    bool snapLaneXAtBeat(double beat, double preferredLaneX, double *outLaneX) const;
    bool exportStylePreset(const QString &path, QString *errorMessage = nullptr) const;
    bool importStylePreset(const QString &path, QString *errorMessage = nullptr);

    // ---- Persistence ----
    bool loadProject(const QString &path);
    bool saveProject(const QString &path = QString());
    QString currentSidecarPath() const { return m_sidecarPath; }

    // ---- Undo/Redo ----
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();
    void onHostUndo(const QString &actionText);
    void onHostRedo(const QString &actionText);

    // ---- State access ----
    NoteChainState &state() { return m_state; }
    const NoteChainState &state() const { return m_state; }

    // ---- Context sync (from host) ----
    void setHostContext(const QVariantMap &ctx);

signals:
    void needsRepaint();
    void statusMessage(const QString &msg);
    void requestHostUndoCheckpoint(const QString &label);
    void controlsChanged();

private:
    bool m_active = false;
    NoteChainState m_state;
    ChartController *m_chartCtrl = nullptr;
    QString m_sidecarPath;

    // History
    QVector<StateSnapshot> m_history;
    int m_historyIdx = -1;
    QSet<LinkKey> m_contextLinkKeys;
    QString m_lastHostMode;
    bool m_dragChanged = false;

    // Segment polyline cache shared by render and hit testing.
    mutable quint64 m_cachedCurveRevision = 0;
    mutable QHash<LinkKey, QVector<SampledPoint>> m_segmentSampleCache;

    // Drag throttling
    QElapsedTimer m_lastMoveTimer;
    static constexpr int kMoveThrottleMs = 16;

    // Internal helpers
    bool recordHistory();
    bool finishMutation(const QString &label);
    void markDirty();
    int findAnchorHit(const QPointF &canvasPos, const CanvasProjection &projection) const;
    QPair<QString,int> findHandleHit(const QPointF &canvasPos, const CanvasProjection &projection) const;
    QPair<int,int> findSegmentHit(const QPointF &canvasPos, const CanvasProjection &projection) const;
    QVector<SampledPoint> segmentSamples(const SegmentInfo &segment, int count = 24) const;
    bool commitLinksToNotes(const QSet<LinkKey> *targetLinks);
    int densityForLinks(const QSet<LinkKey> &links) const;
    bool setDensityForLinks(const QSet<LinkKey> &links, int denominator, const QString &label);
    bool toggleShapeForLinks(const QSet<LinkKey> &links, const QString &label);
    void syncAnchorPlacementWithHostMode();
    void syncAnchorSelectionFromHostNotes();
};

} // namespace NoteChain
