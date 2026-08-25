// src/ui/CustomWidgets/ChartCanvas/ChartCanvas.h
#pragma once

#include <QWidget>
#include <QPointF>
#include <QSet>
#include <QDateTime>
#include <QVector>
#include <QElapsedTimer>
#include <QTimer>
#include "model/Note.h"
#include "plugin/PluginInterface.h"
#include "utils/MathUtils.h"
#include "editor/NoteChain/NoteChainEditor.h"

class ChartController;
class SelectionController;
class NoteRenderer;
class GridRenderer;
class HyperfruitDetector;
class BackgroundRenderer;
class Skin;
class PlaybackController;
class NoteSoundPlayer;
class Chart;
class QMenu;

namespace NoteChain { class NoteChainEditor; }

class ChartCanvas : public QWidget
{
    Q_OBJECT
public:
    enum Mode
    {
        PlaceNote,
        PlaceRain,
        Delete,
        Select,
        AnchorPlace
    };
    explicit ChartCanvas(QWidget *parent = nullptr);
    ~ChartCanvas();

    void setChartController(ChartController *controller);
    void setSelectionController(SelectionController *controller);
    void setSkin(Skin *skin);
    void setPlaybackController(PlaybackController *controller);
    void setColorMode(bool enabled);
    void setHyperfruitEnabled(bool enabled);
    void setTimeDivision(int division);
    void setGridDivision(int division);
    void setGridSnap(bool snap);
    void setScrollPos(double timeMs);
    void setNoteSize(int size);
    void setMode(Mode mode);
    void setNoteSoundFile(const QString &filePath);
    void setNoteSoundEnabled(bool enabled);
    void setNoteSoundVolume(int volumePercent);

    // Copy/paste
    void handleCopy();
    void paste();
    void pasteAtCursor(const QPoint &pos);
    void cancelOperation();
    void beginPastePreview(const QVector<Note> &notes, const QPoint &cursorPos = QPoint());

    bool isVerticalFlip() const;
    void setVerticalFlip(bool flip);
    double currentPlayTime() const;
    int timeDivision() const { return m_timeDivision; }

    void setTimeScale(double scale);
    double timeScale() const { return m_timeScale; }
    void refreshBackground();
    int mirrorAxisX() const { return m_mirrorAxisX; }
    bool isMirrorGuideVisible() const { return m_mirrorGuideVisible; }
    bool isMirrorPreviewVisible() const { return m_mirrorPreviewVisible; }
    void setMirrorAxisX(int axisX);
    void setMirrorGuideVisible(bool visible);
    void setMirrorPreviewVisible(bool visible);
    bool flipSelectedNotes();
    bool flipSelectedNotesAroundCenter();
    void setPluginToolMode(bool enabled, const QString &pluginId = QString());
    void setSourceChartPath(const QString &sourceChartPath);
    bool isPluginToolModeActive() const { return m_pluginToolModeActive; }
    QString pluginToolPluginId() const { return m_pluginToolPluginId; }
    void setPluginOverlayToggles(const QVariantMap &toggles);
    QVariantMap pluginOverlayToggles() const { return m_pluginOverlayToggles; }
    QVariantMap pluginCanvasActionContext() const;
    bool triggerPluginDeleteSelection();
    void recordManualJerkMark();

    // NoteChain native integration
    void setNoteChainModeActive(bool active);
    bool isNoteChainModeActive() const { return m_noteChainModeActive; }
    NoteChain::NoteChainEditor *noteChainEditor() const { return m_noteChainEditor; }

    // Coordinate conversion for NoteChain (made public for overlay/editor use)
    double scrollBeat() const { return m_scrollBeat; }
    double visibleBeatRange() const { return effectiveVisibleBeatRange(); }
    double chartBeatToY(double beat) const { return beatToY(beat); }
    double chartYToBeat(double y) const { return yToBeat(y); }
    int chartCanvasXToLaneX(double canvasX) const { return canvasXToLaneX(canvasX); }
    double chartLaneXToCanvasX(int laneX) const { return laneXToCanvasX(laneX); }

    // Range overlay control (from LongRangeSelector)
    void setRangeOverlay(double startBeat, double endBeat);
    void clearRangeOverlay();
    void setRangeOverlayVisible(bool visible);

public slots:
    void showGridSettings();
    void playbackPositionChanged(double timeMs);
    void playFromReferenceLine();

signals:
    void verticalFlipChanged(bool flipped);
    void scrollPositionChanged(double beat);
    void timeScaleChanged(double scale);
    void mirrorAxisChanged(int axisX);
    void statusMessage(const QString &msg); // Status bar message hook.
    void noteChainControlsChanged();
    void rangeStartChanged(double beat);
    void rangeEndChanged(double beat);
    void rangeDragFinished(double startBeat, double endBeat);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void timerEvent(QTimerEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;


private:
    static constexpr int kLaneWidth = 512;
    static constexpr double kReferenceLineRatio = 0.8;
    static constexpr int kScrollSignalIntervalMs = 33;
    static constexpr double kWheelScrollBeatStepRatio = 0.1;
    static constexpr int kSideMarginDivisor = 20;
    static constexpr int kOverlayQueryIntervalMsToolMode = 33;
    static constexpr int kOverlayQueryIntervalMsToolModePlaying = 16;
    static constexpr int kPluginMouseMoveMinIntervalMs = 16;
    static constexpr int kOverlayQueryIntervalMsToolModePlayingMedium = 33;
    static constexpr int kOverlayQueryIntervalMsToolModePlayingSlow = 50;
    static constexpr int kOverlayQueryIntervalMsIdle = 800;
    static constexpr int kOverlayPlaybackQueryBudgetMs = 3;
    static constexpr int kOverlaySlowCallThresholdMs = 40;
    static constexpr int kOverlaySlowCallBackoffMs = 1000;

    void drawBackground(QPainter &painter);
    void drawGrid(QPainter &painter);
    void invalidateGridCache();
    void drawPastePreview(QPainter &painter,
                          int canvasHeight,
                          int lmargin,
                          int availableWidth,
                          double invVisibleRange,
                          double baseY,
                          double sign);
    void drawMirrorPreview(QPainter &painter,
                           int canvasHeight,
                           int lmargin,
                           int availableWidth,
                           double invVisibleRange,
                           double baseY,
                           double sign);
    void drawMirrorGuide(QPainter &painter, int canvasHeight, int lmargin, int availableWidth);
    void drawPluginOverlays(QPainter &painter, int lmargin, int rmargin);
    QPointF noteToPos(const Note &note) const;
    Note posToNote(const QPointF &pos) const;
    double yPosFromTime(double timeMs) const;
    double beatToY(double beat) const;
    double yToBeat(double y) const;
    int hitTestNote(const QPointF &pos) const;
    QRectF getRainNoteRect(const Note &note) const;
    void updateBackgroundCache();
    int hitTestRangeHandle(const QPointF &pos) const;
    double snapBeatToTimeDivision(double beat) const;
    void drawRangeOverlay(QPainter &painter, int lmargin, int rmargin, int canvasHeight);
    void drawRangeSelectionHighlight(QPainter &painter, int lmargin, int availableWidth, int canvasHeight);

    void beginMoveSelection(const QPointF &startPos, int referenceIndex = -1);
    void updateMoveSelection(const QPointF &currentPos);
    void endMoveSelection();
    void prepareMoveChanges();
    void showRightClickMenu(QMouseEvent *event);
    QVector<int> collectColorTargetIndices(const QPoint &pos) const;
    QVector<int> collectMirrorTargetIndices(const QPoint &pos) const;
    void populateColorMenu(QMenu *colorMenu, const QVector<int> &targetIndices);
    bool performMirrorFlip(const QVector<int> &targetIndices, int axisX, const QString &actionName);
    bool hasNoteSnapReferenceOverlays() const;
    void refreshPluginOverlayCacheForSnap();
    bool curveSnapXForBeat(double beat, int currentX, int *outX) const;
    void applyCurveSnapToMovedNote(Note *note, double beat) const;
    void handleLeftMousePress(QMouseEvent *event);
    bool handlePastePreviewLeftClick(const QPoint &pos);
    bool handleRainPlacementLeftClick(const QPointF &pos);
    bool handleHitNoteLeftClick(int hitIndex, Qt::KeyboardModifiers modifiers, const QPointF &pos);
    bool handleMirrorGuidePress(const QPointF &pos);
    bool handleSelectionRelease();
    bool handleMoveSelectionRelease();
    bool handlePasteDragRelease();
    bool handleMirrorGuideRelease();
    bool handleRangeHandleRelease();

    bool handleGenericDragRelease();
    int clampMirrorAxisX(int axisX) const;
    int canvasXToLaneX(double canvasX) const;
    double laneXToCanvasX(int laneX) const;
    bool isMirrorGuideHandleHit(const QPointF &pos) const;

    void snapPlayheadToGrid();
    void syncCurrentPlayTimeToReferenceLine();
    void startSnapTimer();
    void stopSnapTimer();
    bool dispatchPluginCanvasInput(const PluginInterface::CanvasInputEvent &event, bool *outConsumed = nullptr);
    QVariantMap buildPluginCanvasContext() const;
    QString resolvePluginCanvasToolId() const;
    void applyPluginCursor(const QString &cursorName);
    bool triggerPluginBatchAction(const QString &actionId, const QString &actionTitle);
    bool triggerPluginToolAction(const QString &actionId, const QString &actionTitle);

    double getNoteTimeMs(const Note &note) const;
    void confirmPaste();

    void rebuildBpmTimeCache();
    const QVector<MathUtils::BpmCacheEntry> &bpmTimeCache();

    void rebuildNoteTimesCache();
    const Chart *chart() const;
    Chart *chart();
    QVector<Note> *mutableNotes();

    double effectiveVisibleBeatRange() const
    {
        return m_baseVisibleBeatRange / m_timeScale;
    }

    // Paste preview helpers
    double calculatePasteReferenceTime() const;
    double yToTime(double y) const;
    // Interval copy selection state.
    enum IntervalState
    {
        IntervalNone,
        IntervalWaitingEnd // Start point recorded, waiting for second copy click.
    };
    IntervalState m_intervalState;
    double m_intervalStartTime;
    QVector<Note> m_intervalNotes;

    void startIntervalSelection();
    void completeIntervalSelection();

    // Auto-scroll helper for arrow-key note navigation
    void autoScrollToNote(const Note &note);

    void cancelIntervalSelection();

    // Paste preview drag state
    bool m_isDraggingPaste;
    QPointF m_pasteDragStartPos;
    double m_pasteTimeOffset;
    double m_pasteXOffset;
    double m_pasteTimeOffsetRaw; // Raw paste-preview time offset (unsnapped).
    double m_pasteXOffsetRaw;    // Raw paste-preview X offset.
    double m_pasteAnchorBeat;    // Reference beat locked when preview starts.
    double m_pasteRefBeat;
    int m_pasteDragReferenceIndex;
    bool m_pasteSnapReferenceActive = false;
    void cancelPaste();
    void beginDragPaste(const QPointF &startPos);
    void updateDragPaste(const QPointF &currentPos);
    void endDragPaste();
    void updatePastePreviewPosition();
    double snapPasteTimeOffset(double offsetBeat) const;

    // Precomputed cache data
    QVector<double> m_noteBeatPositions;
    QVector<double> m_noteEndBeatPositions;
    QVector<double> m_noteXPositions;
    QVector<double> m_noteTimesMs;
    QVector<NoteType> m_noteTypes;
    QVector<int> m_sortedNormalNoteIndicesByBeat;
    QVector<int> m_sortedRainNoteIndicesByBeat;
    bool m_noteDataDirty;
    bool m_timesDirty;
    QVector<MathUtils::BpmCacheEntry> m_bpmTimeCache;
    bool m_bpmCacheDirty;

    ChartController *m_chartController;
    SelectionController *m_selectionController;
    PlaybackController *m_playbackController;
    NoteRenderer *m_noteRenderer;
    GridRenderer *m_gridRenderer;
    HyperfruitDetector *m_hyperfruitDetector;
    BackgroundRenderer *m_backgroundRenderer;

    Mode m_currentMode;
    bool m_colorMode;
    bool m_hyperfruitEnabled;
    bool m_verticalFlip;
    int m_timeDivision;
    int m_gridDivision;
    bool m_gridSnap;
    double m_scrollBeat;
    double m_baseVisibleBeatRange;
    double m_timeScale;
    double m_currentPlayTime;
    bool m_autoScrollEnabled;

    bool m_isSelecting;
    QPointF m_selectionStart;
    QPointF m_selectionEnd;
    int m_selectionAnchorIndex = -1; // Range selection anchor (text-editor style shift+arrow)
    int m_selectionExtentIndex = -1; // Moveable end of the range selection

    bool m_isDragging;
    QPointF m_dragStart;
    QSet<int> m_draggedNotes;
    int m_mirrorAxisX;
    bool m_mirrorGuideVisible;
    bool m_mirrorPreviewVisible;
    bool m_isDraggingMirrorGuide;

    bool m_isPasting;
    bool m_useCursorPaste;   // Paste anchored to cursor position (right-click action).
    QPoint m_pasteCursorPos; // Cursor position used for right-click paste.
    QVector<Note> m_pasteNotes;
    QVector<double> m_pasteOriginalTimesMs;
    double m_pasteBaseOriginalTimeMs;
    QPointF m_pasteOffset;

    // Range overlay state
    bool m_rangeOverlayVisible = true;
    double m_rangeStartBeat = -1;
    double m_rangeEndBeat = -1;
    bool m_rangeOverlayValid = false;

    // Range handle dragging
    bool m_isDraggingRangeStart = false;
    bool m_isDraggingRangeEnd = false;

    bool m_pendingMove = false;

    bool m_isMovingSelection;
    QPointF m_moveStartPos;
    double m_moveDeltaBeatRaw;
    double m_moveDeltaXRaw;
    QHash<int, QPair<Note, Note>> m_moveChanges; // Original->updated note snapshot by index.
    QSet<int> m_originalSelectedIndices;
    int m_dragReferenceIndex;
    bool m_noteSnapReferenceActiveForMove;

    bool m_gridSnapBackup;
    bool m_wasGridSnapEnabled;
    bool m_hyperfruitEnabledBackup;

    bool m_rainFirst;
    QPointF m_rainStartPos;

    bool m_snapToGrid;
    int m_snapTimerId;
    bool m_isScrolling;

    QList<PluginInterface::CanvasOverlayItem> m_overlayCache;
    QList<PluginInterface::CanvasOverlayItem> m_eventOverlayCache;
    qint64 m_lastOverlayQueryMs;
    qint64 m_overlayQueryBlockedUntilMs;
    bool m_pluginToolModeActive;
    QString m_pluginToolPluginId;
    QString m_sourceChartPath;
    QVariantMap m_pluginOverlayToggles;
    int m_pluginPlacementDensityOverride;

    // NoteChain native integration
    bool m_noteChainModeActive = false;
    NoteChain::NoteChainEditor *m_noteChainEditor = nullptr;

    // Asynchronous overlay query (moved out of paintEvent for performance)
    QTimer *m_overlayQueryTimer;
    bool m_overlayQueryScheduled;
    bool m_overlayQueryInCanvasInput;
    qint64 m_lastPluginMouseMoveDispatchMs;
    int m_overlayQueryIntervalMsIdle;
    void startOverlayQueryTimer();
    void stopOverlayQueryTimer();

    QSet<int> m_cachedHyperSet;
    bool m_hyperCacheValid;

    QPixmap m_backgroundCache;
    bool m_backgroundCacheDirty;
    QString m_lastKnownBackgroundFile; // Track bg file to avoid useless background regen on note edits


    QElapsedTimer m_fpsTimer;
    QElapsedTimer m_playbackVisualClock;
    int m_frameCount;
    double m_currentFps;

    bool m_isPlaying;
    qint64 m_lastScrollSignalTimeMs;
    NoteSoundPlayer *m_noteSoundPlayer;
    QVector<double> m_playableNoteTimesMs;
    int m_nextPlayableNoteIndex;
    double m_lastNoteSoundTimeMs;

    int leftMargin() const;
    int rightMargin() const;
    void invalidateChartCaches(bool includeBackground);
    void resetOverlayQueryState();
    void advanceNoteSoundClock(double playbackTimeMs);
    void advancePlaybackVisual(bool scheduleRepaint, bool recordProbe = true);

private slots:
    void onSelectionChanged();
    void requestNextFrame();
    void onPlaybackFrameTick(double predictedTimeMs, qint64 frameSeq);
    void onOverlayQueryTimerFire();

private:
    QPixmap m_gridCache;
    QRect m_gridCacheRect;
    double m_gridCacheStartBeat;
    double m_gridCacheEndBeat;
    int m_gridCacheDivision;
    int m_gridCacheTimeDivision;
    bool m_gridCacheVerticalFlip;
    bool m_gridCacheColorEnabled;
    QString m_gridCacheColorPreset;
    QList<int> m_gridCacheColorCustomDivisions;
    bool m_gridCacheValid;
    int m_gridCachePadPx;
    int m_gridCacheBeatNumberFontSize;
    int m_gridCacheBeatNumberLeftMargin;
    qint64 m_lastPlaybackFrameSeq;
    double m_lastPlaybackPredictedTimeMs;
    double m_lastPlaybackTargetTimeMs;
    double m_lastPlaybackStepMs;
    double m_lastPlaybackScrollStepPx;
    double m_lastPlaybackPlayheadYPx;
    double m_lastPlaybackPlayheadStepPx;
    bool m_playbackVisualFramePending;
    qint64 m_lastPlaybackTickNs;
    qint64 m_lastPlaybackVisualAdvanceNs;
    int m_overlayPlaybackIntervalMs;
};

// NoteChain native integration free functions (declared in ChartCanvasNoteChain.cpp)
class QMouseEvent;
bool ChartCanvas_dispatchNoteChainMousePress(ChartCanvas *c, QMouseEvent *event);
bool ChartCanvas_dispatchNoteChainMouseMove(ChartCanvas *c, QMouseEvent *event);
bool ChartCanvas_dispatchNoteChainMouseRelease(ChartCanvas *c, QMouseEvent *event);
void ChartCanvas_drawNoteChainOverlay(ChartCanvas *c, QPainter *painter);
