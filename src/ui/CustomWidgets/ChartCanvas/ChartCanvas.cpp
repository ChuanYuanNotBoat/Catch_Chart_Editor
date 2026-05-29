#include "ChartCanvas.h"
#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "controller/PlaybackController.h"
#include "audio/NoteSoundPlayer.h"
#include "render/NoteRenderer.h"
#include "render/GridRenderer.h"
#include "render/BackgroundRenderer.h"
#include "render/HyperfruitDetector.h"
#include "utils/MathUtils.h"
#include "utils/Settings.h"
#include "utils/Logger.h"
#include "utils/DiagnosticCollector.h"
#include "model/Chart.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFileInfo>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QCheckBox>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QShowEvent>
#include <QDebug>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

ChartCanvas::ChartCanvas(QWidget *parent)
    : QWidget(parent),
      m_chartController(nullptr),
      m_selectionController(nullptr),
      m_playbackController(nullptr),
      m_noteRenderer(new NoteRenderer),
      m_gridRenderer(new GridRenderer),
      m_hyperfruitDetector(new HyperfruitDetector),
      m_backgroundRenderer(new BackgroundRenderer),
      m_currentMode(PlaceNote),
      m_colorMode(true),
      m_hyperfruitEnabled(true),
      m_verticalFlip(true),
      m_timeDivision(4),
      m_gridDivision(20),
      m_gridSnap(true),
      m_scrollBeat(0),
      m_baseVisibleBeatRange(10),
      m_timeScale(2.25),
      m_currentPlayTime(0),
      m_autoScrollEnabled(true),
      m_isSelecting(false),
      m_isDragging(false),
      m_mirrorAxisX(kLaneWidth / 2),
      m_mirrorGuideVisible(false),
      m_mirrorPreviewVisible(false),
      m_isDraggingMirrorGuide(false),
      m_isPasting(false),
      m_useCursorPaste(false),
      m_pasteCursorPos(0, 0),
      m_intervalState(IntervalNone),
      m_intervalStartTime(0.0),
      m_isMovingSelection(false),
      m_moveDeltaBeatRaw(0.0),
      m_moveDeltaXRaw(0.0),
      m_gridSnapBackup(false),
      m_wasGridSnapEnabled(false),
      m_dragReferenceIndex(-1),
      m_noteSnapReferenceActiveForMove(false),
      m_rainFirst(true),
      m_snapToGrid(true),
      m_snapTimerId(0),
      m_isScrolling(false),
      m_lastOverlayQueryMs(0),
      m_overlayQueryBlockedUntilMs(0),
      m_pluginToolModeActive(false),
      m_pluginToolPluginId(QString()),
      m_pluginPlacementDensityOverride(0),
      m_showUnreachableDivisions(false),
      m_hyperCacheValid(false),
      m_backgroundCacheDirty(true),
      m_noteDataDirty(true),
      m_timesDirty(true),
      m_bpmCacheDirty(true),
      m_frameCount(0),
      m_currentFps(0.0),
      m_isPlaying(false),
      m_isDraggingPaste(false),
      m_pasteTimeOffset(0.0),
      m_pasteXOffset(0.0),
      m_pasteTimeOffsetRaw(0.0),
      m_pasteXOffsetRaw(0.0),
      m_pasteAnchorBeat(0.0),
      m_pasteRefBeat(0.0),
      m_pasteDragReferenceIndex(-1),
      m_pasteBaseOriginalTimeMs(std::numeric_limits<double>::max()),
      m_lastScrollSignalTimeMs(0),
      m_noteSoundPlayer(nullptr),
      m_nextPlayableNoteIndex(0),
      m_lastNoteSoundTimeMs(0.0),
      m_gridCacheStartTime(0.0),
      m_gridCacheEndTime(0.0),
      m_gridCacheDivision(0),
      m_gridCacheTimeDivision(0),
      m_gridCacheVerticalFlip(false),
      m_gridCacheColorEnabled(false),
      m_gridCacheValid(false),
      m_gridCachePadPx(0),
      m_lastPlaybackFrameSeq(-1),
      m_lastPlaybackPredictedTimeMs(-1.0),
      m_lastPlaybackTargetTimeMs(-1.0),
      m_lastPlaybackStepMs(-1.0),
      m_lastPlaybackScrollStepPx(-1.0),
      m_lastPlaybackPlayheadYPx(-1.0),
      m_lastPlaybackPlayheadStepPx(-1.0),
      m_playbackVisualFramePending(false),
      m_lastPlaybackTickNs(0),
      m_lastPlaybackVisualAdvanceNs(0),
      m_overlayPlaybackIntervalMs(kOverlayQueryIntervalMsToolModePlaying)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(800, 400);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NativeWindow);

    m_noteRenderer->setNoteSize(Settings::instance().noteSize());
    m_noteSoundPlayer = new NoteSoundPlayer(this);
    m_noteSoundPlayer->setVolumePercent(Settings::instance().noteSoundVolume());
    const QString noteSoundPath = Settings::instance().noteSoundPath();
    m_noteSoundPlayer->setSoundFile(noteSoundPath);
    m_noteSoundPlayer->setEnabled(!noteSoundPath.isEmpty());

    m_fpsTimer.start();
    m_playbackVisualClock.start();
    m_pluginOverlayToggles.insert("overlay_enabled", true);
    m_pluginOverlayToggles.insert("preview", true);
    m_pluginOverlayToggles.insert("control_points", true);
    m_pluginOverlayToggles.insert("handles", true);
    m_pluginOverlayToggles.insert("sample_points", true);
    m_pluginOverlayToggles.insert("labels", true);
}

ChartCanvas::~ChartCanvas()
{
    delete m_noteRenderer;
    delete m_gridRenderer;
    delete m_hyperfruitDetector;
    delete m_backgroundRenderer;
}

const Chart *ChartCanvas::chart() const
{
    return m_chartController ? m_chartController->chart() : nullptr;
}

Chart *ChartCanvas::chart()
{
    return m_chartController ? m_chartController->mutableChart() : nullptr;
}

QVector<Note> *ChartCanvas::mutableNotes()
{
    Chart *c = chart();
    return c ? &c->notes() : nullptr;
}

void ChartCanvas::rebuildBpmTimeCache()
{
    m_bpmTimeCache.clear();
    if (!chart())
    {
        m_bpmCacheDirty = false;
        return;
    }

    const auto &bpmList = chart()->bpmList();
    if (bpmList.isEmpty())
    {
        m_bpmCacheDirty = false;
        return;
    }

    const int offset = chart()->meta().offset;
    m_bpmTimeCache = MathUtils::buildBpmTimeCache(bpmList, offset);
    m_bpmCacheDirty = false;
}

const QVector<MathUtils::BpmCacheEntry> &ChartCanvas::bpmTimeCache()
{
    if (m_bpmCacheDirty)
        rebuildBpmTimeCache();
    return m_bpmTimeCache;
}

void ChartCanvas::rebuildNoteTimesCache()
{
    if (!chart())
    {
        m_noteBeatPositions.clear();
        m_noteEndBeatPositions.clear();
        m_noteXPositions.clear();
        m_noteTimesMs.clear();
        m_noteTypes.clear();
        m_sortedNormalNoteIndicesByBeat.clear();
        m_sortedRainNoteIndicesByBeat.clear();
        m_playableNoteTimesMs.clear();
        m_nextPlayableNoteIndex = 0;
        m_timesDirty = false;
        m_noteDataDirty = false;
        return;
    }
    const auto &notes = chart()->notes();
    const auto &bpmList = chart()->bpmList();

    if (bpmList.isEmpty())
    {
        qWarning() << "ChartCanvas::rebuildNoteTimesCache: BPM list is empty, cannot compute times.";
        m_noteBeatPositions.clear();
        m_noteEndBeatPositions.clear();
        m_noteXPositions.clear();
        m_noteTimesMs.clear();
        m_noteTypes.clear();
        m_sortedNormalNoteIndicesByBeat.clear();
        m_sortedRainNoteIndicesByBeat.clear();
        m_playableNoteTimesMs.clear();
        m_nextPlayableNoteIndex = 0;
        m_timesDirty = false;
        m_noteDataDirty = false;
        return;
    }

    const QVector<MathUtils::BpmCacheEntry> &bpmCache = bpmTimeCache();
    if (bpmCache.isEmpty())
    {
        m_noteBeatPositions.clear();
        m_noteEndBeatPositions.clear();
        m_noteXPositions.clear();
        m_noteTimesMs.clear();
        m_noteTypes.clear();
        m_sortedNormalNoteIndicesByBeat.clear();
        m_sortedRainNoteIndicesByBeat.clear();
        m_playableNoteTimesMs.clear();
        m_nextPlayableNoteIndex = 0;
        m_timesDirty = false;
        m_noteDataDirty = false;
        return;
    }

    const int N = notes.size();
    m_noteBeatPositions.resize(N);
    m_noteEndBeatPositions.resize(N);
    m_noteXPositions.resize(N);
    m_noteTimesMs.resize(N);
    m_noteTypes.resize(N);
    m_sortedNormalNoteIndicesByBeat.clear();
    m_sortedRainNoteIndicesByBeat.clear();
    m_sortedNormalNoteIndicesByBeat.reserve(N);
    m_sortedRainNoteIndicesByBeat.reserve(N);
    m_playableNoteTimesMs.clear();
    m_playableNoteTimesMs.reserve(N);

    for (int i = 0; i < N; ++i)
    {
        const Note &note = notes[i];
        m_noteTypes[i] = note.type;
        if (note.type == NoteType::SOUND)
        {
            m_noteBeatPositions[i] = 0.0;
            m_noteEndBeatPositions[i] = 0.0;
            m_noteXPositions[i] = 0.0;
            m_noteTimesMs[i] = 0.0;
            continue;
        }
        double beat = MathUtils::beatToFloat(note.beatNum, note.numerator, note.denominator);
        m_noteBeatPositions[i] = beat;
        m_noteTimesMs[i] = MathUtils::beatToMs(note.beatNum, note.numerator, note.denominator, bpmCache);
        m_playableNoteTimesMs.append(m_noteTimesMs[i]);
        if (note.type == NoteType::RAIN)
        {
            double endBeat = MathUtils::beatToFloat(note.endBeatNum, note.endNumerator, note.endDenominator);
            m_noteEndBeatPositions[i] = endBeat;
            m_sortedRainNoteIndicesByBeat.append(i);
        }
        else
        {
            m_noteEndBeatPositions[i] = beat;
            m_sortedNormalNoteIndicesByBeat.append(i);
        }
        m_noteXPositions[i] = static_cast<double>(note.x) / static_cast<double>(kLaneWidth);
    }

    std::sort(m_sortedNormalNoteIndicesByBeat.begin(),
              m_sortedNormalNoteIndicesByBeat.end(),
              [this](int a, int b) {
                  return m_noteBeatPositions[a] < m_noteBeatPositions[b];
              });
    std::sort(m_sortedRainNoteIndicesByBeat.begin(),
              m_sortedRainNoteIndicesByBeat.end(),
              [this](int a, int b) {
                  return m_noteBeatPositions[a] < m_noteBeatPositions[b];
              });

    std::sort(m_playableNoteTimesMs.begin(), m_playableNoteTimesMs.end());
    m_nextPlayableNoteIndex = static_cast<int>(std::lower_bound(
        m_playableNoteTimesMs.begin(),
        m_playableNoteTimesMs.end(),
        m_lastNoteSoundTimeMs) - m_playableNoteTimesMs.begin());

    m_timesDirty = false;
    m_noteDataDirty = false;
}

void ChartCanvas::setChartController(ChartController *controller)
{
    if (m_chartController == controller)
        return;

    if (m_chartController)
    {
        disconnect(m_chartController, &ChartController::chartChanged, this, nullptr);
    }
    m_chartController = controller;
    if (controller)
    {
        connect(controller, &ChartController::chartChanged, this, [this]()
                {
            invalidateChartCaches(true);
            update(); });
        m_hyperfruitDetector->setCS(3.2);
        m_noteRenderer->setHyperfruitDetector(m_hyperfruitDetector);
    }
    invalidateChartCaches(true);
    update();
}

void ChartCanvas::setPlaybackController(PlaybackController *controller)
{
    if (m_playbackController == controller)
        return;

    if (m_playbackController)
    {
        disconnect(m_playbackController, &PlaybackController::positionChanged, this, &ChartCanvas::playbackPositionChanged);
        disconnect(m_playbackController, &PlaybackController::playbackFrameTick, this, &ChartCanvas::onPlaybackFrameTick);
        disconnect(m_playbackController, &PlaybackController::stateChanged, this, nullptr);
    }

    m_playbackController = controller;
    m_currentPlayTime = 0;

    if (m_playbackController)
    {
        connect(m_playbackController, &PlaybackController::positionChanged, this, &ChartCanvas::playbackPositionChanged);
        connect(m_playbackController, &PlaybackController::playbackFrameTick, this, &ChartCanvas::onPlaybackFrameTick);
        connect(m_playbackController, &PlaybackController::stateChanged,
                this, [this](PlaybackController::State state)
                {
            if (state == PlaybackController::Playing) {
                m_autoScrollEnabled = true;
                m_isPlaying = true;
                m_lastScrollSignalTimeMs = 0;
                m_lastPlaybackFrameSeq = -1;
                m_lastPlaybackPredictedTimeMs = -1.0;
                m_lastPlaybackTargetTimeMs = -1.0;
                m_lastPlaybackStepMs = -1.0;
                m_lastPlaybackScrollStepPx = -1.0;
                m_lastPlaybackPlayheadYPx = -1.0;
                m_lastPlaybackPlayheadStepPx = -1.0;
                m_playbackVisualFramePending = false;
                m_lastPlaybackTickNs = 0;
                m_lastPlaybackVisualAdvanceNs = 0;
                const double startMs = m_playbackController ? m_playbackController->currentTime() : m_currentPlayTime;
                m_currentPlayTime = qMax(0.0, startMs);
                m_lastNoteSoundTimeMs = m_currentPlayTime;
                m_nextPlayableNoteIndex = static_cast<int>(std::lower_bound(
                    m_playableNoteTimesMs.begin(),
                    m_playableNoteTimesMs.end(),
                    m_lastNoteSoundTimeMs) - m_playableNoteTimesMs.begin());
                requestNextFrame();
            } else {
                m_isPlaying = false;
                m_lastPlaybackFrameSeq = -1;
                m_lastPlaybackPredictedTimeMs = -1.0;
                m_lastPlaybackTargetTimeMs = -1.0;
                m_lastPlaybackStepMs = -1.0;
                m_lastPlaybackScrollStepPx = -1.0;
                m_lastPlaybackPlayheadYPx = -1.0;
                m_lastPlaybackPlayheadStepPx = -1.0;
                m_playbackVisualFramePending = false;
                m_lastPlaybackTickNs = 0;
                m_lastPlaybackVisualAdvanceNs = 0;
                m_lastNoteSoundTimeMs = m_currentPlayTime;
                snapPlayheadToGrid();
                update();
            } });
        m_currentPlayTime = m_playbackController->currentTime();
    }

    update();
}

void ChartCanvas::setSelectionController(SelectionController *controller)
{
    if (m_selectionController == controller)
        return;
    if (m_selectionController)
        disconnect(m_selectionController, nullptr, this, nullptr);

    m_selectionController = controller;
    if (m_selectionController)
    {
        connect(m_selectionController, &SelectionController::selectionChanged, this, QOverload<>::of(&ChartCanvas::update));
    }
    update();
}

void ChartCanvas::setSkin(Skin *skin)
{
    m_noteRenderer->setSkin(skin);
    update();
}

void ChartCanvas::setColorMode(bool enabled)
{
    if (m_colorMode == enabled)
        return;
    m_colorMode = enabled;
    m_noteRenderer->setShowColors(enabled);
    update();
}

void ChartCanvas::setHyperfruitEnabled(bool enabled)
{
    if (m_hyperfruitEnabled == enabled)
        return;
    m_hyperfruitEnabled = enabled;
    m_noteRenderer->setHyperfruitEnabled(enabled);
    m_hyperCacheValid = false;
    update();
}

bool ChartCanvas::isVerticalFlip() const
{
    return m_verticalFlip;
}

void ChartCanvas::setVerticalFlip(bool flip)
{
    if (m_verticalFlip == flip)
        return;
    m_verticalFlip = flip;
    invalidateGridCache();
    emit verticalFlipChanged(flip);
    update();
}

void ChartCanvas::setTimeDivision(int division)
{
    if (division != m_timeDivision)
    {
        m_timeDivision = division;
        invalidateGridCache();
        snapPlayheadToGrid();
        update();
    }
}

void ChartCanvas::setGridDivision(int division)
{
    if (m_gridDivision != division)
    {
        m_gridDivision = division;
        invalidateGridCache();
        update();
    }
}

void ChartCanvas::setGridSnap(bool snap)
{
    if (m_gridSnap == snap)
        return;
    m_gridSnap = snap;
}

void ChartCanvas::setScrollPos(double timeMs)
{
    if (!chart())
        return;

    const double clampedTimeMs = qMax(0.0, timeMs);

    int beatNum, numerator, denominator;
    MathUtils::msToBeat(clampedTimeMs, chart()->bpmList(),
                        chart()->meta().offset,
                        beatNum, numerator, denominator);
    const double targetBeat = beatNum + static_cast<double>(numerator) / denominator;

    // Keep requested time aligned on the visual reference line, not on viewport start.
    const double baselineRatio = kReferenceLineRatio;
    const double baselineBeatOffset = m_verticalFlip
                                          ? (1.0 - baselineRatio) * effectiveVisibleBeatRange()
                                          : baselineRatio * effectiveVisibleBeatRange();
    double newScrollBeat = targetBeat - baselineBeatOffset;
    if (newScrollBeat < 0.0)
        newScrollBeat = 0.0;

    const bool scrollChanged = qAbs(newScrollBeat - m_scrollBeat) >= 1e-6;
    const bool timeChanged = qAbs(clampedTimeMs - m_currentPlayTime) >= 0.05;
    if (!scrollChanged && !timeChanged)
        return;

    m_scrollBeat = newScrollBeat;
    m_currentPlayTime = clampedTimeMs;
    update();
    if (scrollChanged)
        emit scrollPositionChanged(m_scrollBeat);
}

void ChartCanvas::syncCurrentPlayTimeToReferenceLine()
{
    if (!chart())
        return;

    const auto &bpmList = chart()->bpmList();
    const int offset = chart()->meta().offset;
    const double baselineRatio = kReferenceLineRatio;
    const double baselineBeat = m_verticalFlip
                                    ? m_scrollBeat + (1.0 - baselineRatio) * effectiveVisibleBeatRange()
                                    : m_scrollBeat + baselineRatio * effectiveVisibleBeatRange();

    int beatNum = 0;
    int numerator = 0;
    int denominator = 1;
    MathUtils::floatToBeat(baselineBeat, beatNum, numerator, denominator);
    m_currentPlayTime = MathUtils::beatToMs(beatNum, numerator, denominator, bpmList, offset);
}

void ChartCanvas::setNoteSize(int size)
{
    if (m_noteRenderer->getNoteSize() == size)
        return;
    m_noteRenderer->setNoteSize(size);
    update();
}

void ChartCanvas::setMode(Mode mode)
{
    if (m_currentMode == mode)
        return;
    if (mode != PlaceRain)
    {
        m_rainFirst = true;
    }
    m_currentMode = mode;
    update();
}

void ChartCanvas::setNoteSoundFile(const QString &filePath)
{
    if (!m_noteSoundPlayer)
        return;
    if (m_noteSoundPlayer->soundFile() == filePath)
        return;
    m_noteSoundPlayer->setSoundFile(filePath);
}

void ChartCanvas::setNoteSoundEnabled(bool enabled)
{
    if (!m_noteSoundPlayer)
        return;
    if (m_noteSoundPlayer->isEnabled() == enabled)
        return;
    m_noteSoundPlayer->setEnabled(enabled);
}

void ChartCanvas::setNoteSoundVolume(int volumePercent)
{
    if (!m_noteSoundPlayer)
        return;
    if (m_noteSoundPlayer->volumePercent() == volumePercent)
        return;
    m_noteSoundPlayer->setVolumePercent(volumePercent);
}

void ChartCanvas::invalidateChartCaches(bool includeBackground)
{
    m_hyperCacheValid = false;
    m_noteDataDirty = true;
    m_timesDirty = true;
    m_bpmCacheDirty = true;
    invalidateGridCache();
    if (includeBackground)
        m_backgroundCacheDirty = true;
    resetOverlayQueryState();
}

void ChartCanvas::resetOverlayQueryState()
{
    m_overlayCache.clear();
    m_lastOverlayQueryMs = 0;
    m_overlayQueryBlockedUntilMs = 0;
    m_overlayPlaybackIntervalMs = kOverlayQueryIntervalMsToolModePlaying;
}

void ChartCanvas::setShowUnreachableDivisions(bool enabled)
{
    if (m_showUnreachableDivisions == enabled)
        return;
    m_showUnreachableDivisions = enabled;
    emit showUnreachableDivisionsChanged(enabled);
}




