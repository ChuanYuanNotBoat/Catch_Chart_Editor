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
#include "app/Application.h"
#include "plugin/PluginManager.h"
#include <QShowEvent>
#include <QScreen>
#include <QWindow>
#include <QDebug>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QComboBox>


namespace
{
    PluginManager *activePluginManager()
    {
        auto *app = qobject_cast<Application *>(QCoreApplication::instance());
        if (!app || !app->pluginSystemReady())
            return nullptr;
        return app->pluginManager();
    }
}

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
    m_pasteBaseOriginalTimeMs((std::numeric_limits<double>::max)()),
      m_lastScrollSignalTimeMs(0),
      m_noteSoundPlayer(nullptr),
      m_nextPlayableNoteIndex(0),
      m_lastNoteSoundTimeMs(0.0),
      m_gridCacheStartBeat(0.0),
      m_gridCacheEndBeat(0.0),
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
      m_lastPaintProbeNs(0),
      m_lastPaintIntervalMs(-1.0),
      m_overlayPlaybackIntervalMs(kOverlayQueryIntervalMsToolModePlaying),
      m_overlayQueryTimer(new QTimer(this)),
      m_overlayQueryScheduled(false),
      m_overlayQueryInCanvasInput(false),
      m_lastPluginMouseMoveDispatchMs(0),
      m_overlayQueryIntervalMsIdle(0),
      m_selectionAnchorIndex(-1),
      m_selectionExtentIndex(-1)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(800, 400);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

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


    // Install application-wide event filter to redirect arrow keys from GUI widgets to canvas
    QCoreApplication::instance()->installEventFilter(this);

    connect(m_overlayQueryTimer, &QTimer::timeout, this, &ChartCanvas::onOverlayQueryTimerFire);
}

ChartCanvas::~ChartCanvas()
{
    delete m_noteRenderer;
    delete m_gridRenderer;
    delete m_hyperfruitDetector;
    delete m_backgroundRenderer;
}

bool ChartCanvas::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_displayFrameWindow.data() &&
        event->type() == QEvent::UpdateRequest &&
        m_displayFrameRequestPending)
    {
        m_displayFrameRequestPending = false;
        if (m_displayFrameLoopActive && m_isPlaying && m_playbackController)
        {
            m_playbackController->advanceExternalFramePulse();
            QTimer::singleShot(0, this, &ChartCanvas::requestDisplayFrame);
        }
    }
    else if (watched == m_displayFrameWindow.data() && event->type() == QEvent::Expose)
    {
        QTimer::singleShot(0, this, [this]()
        {
            if (m_isPlaying && m_displayFrameWindow && m_displayFrameWindow->isExposed())
                startDisplayFrameLoop();
            else
                stopDisplayFrameLoop();
        });
    }

    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        const int key = ke->key();
        if ((key == Qt::Key_Left || key == Qt::Key_Right ||
             key == Qt::Key_Up   || key == Qt::Key_Down) &&
            !ke->isAutoRepeat())
        {
            // Only redirect when focus is on a non-input GUI widget
            // (buttons, radio buttons, etc.) that shouldn't consume arrow keys.
            QWidget *focusWidget = QApplication::focusWidget();
            if (focusWidget && focusWidget != this && !isAncestorOf(focusWidget))
            {
                // Don't steal arrow keys when a popup is active (e.g. combo box dropdown)
                if (QApplication::activePopupWidget())
                    return QWidget::eventFilter(watched, event);

                // Don't steal arrow keys from text input widgets that need them
                if (!qobject_cast<QLineEdit *>(focusWidget) &&
                    !qobject_cast<QTextEdit *>(focusWidget) &&
                    !qobject_cast<QPlainTextEdit *>(focusWidget) &&
                    !qobject_cast<QAbstractSpinBox *>(focusWidget) &&
                    !qobject_cast<QComboBox *>(focusWidget))
                {
                    setFocus();
                    keyPressEvent(ke);
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
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

void ChartCanvas::rebuildBpmTimeCache() const
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

const QVector<MathUtils::BpmCacheEntry> &ChartCanvas::bpmTimeCache() const
{
    if (m_bpmCacheDirty)
        rebuildBpmTimeCache();
    return m_bpmTimeCache;
}

double ChartCanvas::beatFromTimeMs(double timeMs) const
{
    const QVector<MathUtils::BpmCacheEntry> &cache = bpmTimeCache();
    if (cache.isEmpty())
        return 0.0;

    int lo = 0;
    int hi = cache.size() - 1;
    while (lo < hi)
    {
        const int mid = (lo + hi + 1) / 2;
        if (cache[mid].accumulatedMs <= timeMs)
            lo = mid;
        else
            hi = mid - 1;
    }

    const MathUtils::BpmCacheEntry &segment = cache[lo];
    if (segment.bpm <= 0.0)
        return segment.beatPos;
    return segment.beatPos + (timeMs - segment.accumulatedMs) * (segment.bpm / 60000.0);
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
              [this](int a, int b)
              {
                  return m_noteBeatPositions[a] < m_noteBeatPositions[b];
              });
    std::sort(m_sortedRainNoteIndicesByBeat.begin(),
              m_sortedRainNoteIndicesByBeat.end(),
              [this](int a, int b)
              {
                  return m_noteBeatPositions[a] < m_noteBeatPositions[b];
              });

    std::sort(m_playableNoteTimesMs.begin(), m_playableNoteTimesMs.end());
    m_nextPlayableNoteIndex = static_cast<int>(std::lower_bound(
                                                   m_playableNoteTimesMs.begin(),
                                                   m_playableNoteTimesMs.end(),
                                                   m_lastNoteSoundTimeMs) -
                                               m_playableNoteTimesMs.begin());

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
        disconnect(m_chartController, &ChartController::notesChanged, this, nullptr);
        disconnect(m_chartController, &ChartController::metaDataChanged, this, nullptr);
    }
    m_chartController = controller;
    if (m_noteChainEditor)
        m_noteChainEditor->setChartController(controller);
    if (controller)
    {
        connect(controller, &ChartController::chartChanged, this, [this]()
                {
            // Only dirty background cache when the background file path actually changed.
            // Note/BPM edits should not trigger expensive background regeneration.
            bool bgChanged = false;
            if (m_chartController && m_chartController->chart()) {
                const QString &currentBg = m_chartController->chart()->meta().backgroundFile;
                if (currentBg != m_lastKnownBackgroundFile) {
                    bgChanged = true;
                    m_lastKnownBackgroundFile = currentBg;
                }
            }
            invalidateChartCaches(bgChanged);
            update(); });
        // Note changes: invalidate caches but skip background check (handled separately via metaDataChanged)
        connect(controller, &ChartController::notesChanged, this, [this]() {
            invalidateChartCaches(false);
            update();
        });

        // Meta changes: check for background file changes only
        connect(controller, &ChartController::metaDataChanged, this, [this]() {
            bool bgChanged = false;
            if (m_chartController && m_chartController->chart()) {
                const QString &currentBg = m_chartController->chart()->meta().backgroundFile;
                if (currentBg != m_lastKnownBackgroundFile) {
                    bgChanged = true;
                    m_lastKnownBackgroundFile = currentBg;
                }
            }
            if (bgChanged)
                invalidateChartCaches(true);
            update();
        });
        m_hyperfruitDetector->setCS(3.2);
        m_noteRenderer->setHyperfruitDetector(m_hyperfruitDetector);

        // Initialize background file tracking so note edits don't trigger unnecessary bg regen
        if (controller->chart())
            m_lastKnownBackgroundFile = controller->chart()->meta().backgroundFile;
    }
    invalidateChartCaches(true);
    update();
}

void ChartCanvas::setPlaybackController(PlaybackController *controller)
{
    if (m_playbackController == controller)
        return;

    stopDisplayFrameLoop();

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
        attachDisplayFrameWindow();
        if (m_displayFrameScreen)
            m_playbackController->setDisplayRefreshRate(m_displayFrameScreen->refreshRate());
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
                m_lastPaintProbeNs = 0;
                m_lastPaintIntervalMs = -1.0;
                const double startMs = m_playbackController ? m_playbackController->currentTime() : m_currentPlayTime;
                m_currentPlayTime = qMax(0.0, startMs);
                m_lastNoteSoundTimeMs = m_currentPlayTime;
                m_nextPlayableNoteIndex = static_cast<int>(std::lower_bound(
                    m_playableNoteTimesMs.begin(),
                    m_playableNoteTimesMs.end(),
                    m_lastNoteSoundTimeMs) - m_playableNoteTimesMs.begin());
                startDisplayFrameLoop();
                if (!m_displayFrameLoopActive)
                    requestNextFrame();
            } else {
                stopDisplayFrameLoop();
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
                m_lastPaintProbeNs = 0;
                m_lastPaintIntervalMs = -1.0;
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

void ChartCanvas::refreshRenderSettings()
{
    m_noteRenderer->refreshSettings();
    invalidateGridCache();
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

void ChartCanvas::startOverlayQueryTimer()
{
    if (m_overlayQueryTimer->isActive())
        return;

    const bool isPlaying = m_isPlaying;
    int intervalMs;
    if (isPlaying)
        intervalMs = m_overlayPlaybackIntervalMs;
    else
        intervalMs = kOverlayQueryIntervalMsIdle; // perf fix D: idle 800ms rather than 33ms

    m_overlayQueryTimer->start(intervalMs);
    m_overlayQueryScheduled = false;
}

void ChartCanvas::stopOverlayQueryTimer()
{
    m_overlayQueryTimer->stop();
    m_overlayQueryScheduled = false;
}

void ChartCanvas::onOverlayQueryTimerFire()
{
    if (m_overlayQueryScheduled || m_overlayQueryInCanvasInput)
        return;

    PluginManager *pm = activePluginManager();
    if (!pm || !m_pluginToolModeActive)
        return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool canQuery = nowMs >= m_overlayQueryBlockedUntilMs;

    if (!canQuery)
        return;

    m_overlayQueryScheduled = true;

    QVariantMap overlayContext = buildPluginCanvasContext();
    overlayContext.insert("overlay_snapshot_requested_at_ms", nowMs);

    QElapsedTimer requestTimer;
    requestTimer.start();

    QList<PluginInterface::CanvasOverlayItem> newItems = pm->canvasOverlays(overlayContext);
    m_overlayCache = newItems;
    m_lastOverlayQueryMs = nowMs;

    const qint64 elapsedMs = requestTimer.elapsed();
    const bool isPlaying = m_isPlaying;

    if (isPlaying)
    {
        if (elapsedMs > kOverlayPlaybackQueryBudgetMs)
        {
            if (m_overlayPlaybackIntervalMs < kOverlayQueryIntervalMsToolModePlayingMedium)
                m_overlayPlaybackIntervalMs = kOverlayQueryIntervalMsToolModePlayingMedium;
            else if (m_overlayPlaybackIntervalMs < kOverlayQueryIntervalMsToolModePlayingSlow)
                m_overlayPlaybackIntervalMs = kOverlayQueryIntervalMsToolModePlayingSlow;
        }
        else if (m_overlayPlaybackIntervalMs > kOverlayQueryIntervalMsToolModePlaying)
        {
            m_overlayPlaybackIntervalMs = (m_overlayPlaybackIntervalMs > kOverlayQueryIntervalMsToolModePlayingMedium)
                                              ? kOverlayQueryIntervalMsToolModePlayingMedium
                                              : kOverlayQueryIntervalMsToolModePlaying;
        }
        m_overlayQueryBlockedUntilMs = 0;
    }
    else
    {
        m_overlayPlaybackIntervalMs = kOverlayQueryIntervalMsToolModePlaying;
        if (elapsedMs > kOverlaySlowCallThresholdMs)
        {
            m_overlayQueryBlockedUntilMs = nowMs + kOverlaySlowCallBackoffMs;
            Logger::warn(QString("Plugin overlay query is slow (%1 ms); temporarily throttling for %2 ms.")
                             .arg(elapsedMs)
                             .arg(kOverlaySlowCallBackoffMs));
        }
        else
        {
            m_overlayQueryBlockedUntilMs = 0;
        }
    }

    // Adaptive timer interval
    int nextIntervalMs;
    if (isPlaying)
        nextIntervalMs = m_overlayPlaybackIntervalMs;
    else
        nextIntervalMs = kOverlayQueryIntervalMsToolMode;

    m_overlayQueryTimer->start(nextIntervalMs);
    m_overlayQueryScheduled = false;

    update();
}
