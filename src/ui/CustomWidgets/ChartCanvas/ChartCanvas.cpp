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
#include "file/BpmAuxFiles.h"
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
#include <algorithm>
#include <cmath>
#include <QShowEvent>
#include <QDebug>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

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
      m_overlayPlaybackIntervalMs(kOverlayQueryIntervalMsToolModePlaying),
      m_mapper(&m_beatLinearMapper),
      m_coordinateMode(CoordinateMode::BeatLinear),
      m_scrollTimeMs(0.0),
      m_visibleTimeRangeMs(1000.0),
      m_gridCacheStartBeat(0.0),
      m_gridCacheEndBeat(0.0),
      m_gridCacheMode(CoordinateMode::BeatLinear),
      m_overlayQueryTimer(new QTimer(this)),
      m_overlayQueryScheduled(false),
      m_overlayQueryIntervalMsIdle(0)
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

    m_excludeRenderingEnabled = Settings::instance().excludeRenderingEnabled();
    m_fpsTimer.start();
    m_playbackVisualClock.start();
    m_pluginOverlayToggles.insert("overlay_enabled", true);
    m_pluginOverlayToggles.insert("preview", true);
    m_pluginOverlayToggles.insert("control_points", true);
    m_pluginOverlayToggles.insert("handles", true);
    m_pluginOverlayToggles.insert("sample_points", true);
    m_pluginOverlayToggles.insert("labels", true);

    connect(m_overlayQueryTimer, &QTimer::timeout, this, &ChartCanvas::onOverlayQueryTimerFire);
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

void ChartCanvas::rebuildBpmTimeCache() const
{
    m_bpmTimeCache.clear();
    m_excludedRenderBpmCache.clear();
    m_excludedBeatRanges.clear();
    m_hasExcludedBpms = false;
    m_cachedExcludes.loaded = false;

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

    // 构建排除项缓存（缓存数据供 computeBaseBpm 复用）
    BpmAuxFiles::BpmExcludesData excludesData;
    bool hasExcludes = false;
    if (m_chartController)
    {
        const QString chartPath = m_chartController->chartFilePath();
        if (!chartPath.isEmpty())
            hasExcludes = BpmAuxFiles::loadBpmExcludes(chartPath, excludesData);
    }

    if (hasExcludes)
    {
        auto isExcluded = [&](const BpmEntry &bpm) -> bool
        {
            for (const auto &range : excludesData.excludes)
            {
                bool geStart = (bpm.beatNum > range.startBeatNum)
                               || (bpm.beatNum == range.startBeatNum
                                   && bpm.numerator * range.startDenominator >= range.startNumerator * bpm.denominator);
                bool leEnd = (bpm.beatNum < range.endBeatNum)
                             || (bpm.beatNum == range.endBeatNum
                                 && bpm.numerator * range.endDenominator <= range.endNumerator * bpm.denominator);
                if (geStart && leEnd)
                    return true;
            }
            return false;
        };

        // 检查特定BPM是否落入特定排除范围（与isExcluded不同：后者检查任意范围）
        auto bpmInRange = [](const BpmEntry &bpm, const BpmAuxFiles::BpmExcludeRange &rng) -> bool
        {
            bool geStart = (bpm.beatNum > rng.startBeatNum)
                           || (bpm.beatNum == rng.startBeatNum
                               && bpm.numerator * rng.startDenominator >= rng.startNumerator * bpm.denominator);
            bool leEnd = (bpm.beatNum < rng.endBeatNum)
                         || (bpm.beatNum == rng.endBeatNum
                             && bpm.numerator * rng.endDenominator <= rng.endNumerator * bpm.denominator);
            return geStart && leEnd;
        };

        for (const auto &range : excludesData.excludes)
        {
            double startBeat = MathUtils::beatToFloat(range.startBeatNum, range.startNumerator, range.startDenominator);
            double endBeat = MathUtils::beatToFloat(range.endBeatNum, range.endNumerator, range.endDenominator);
            if (endBeat < startBeat)
                std::swap(startBeat, endBeat);

            // 仅当此排除范围至少覆盖一个真实 BPM 条目时才加入 m_excludedBeatRanges。
            // 否则空范围会触发橙色背景绘制和网格线跳过，
            // 导致非排除区域出现橙色线。
            bool coversBpm = false;
            for (const auto &bpm : bpmList)
            {
                if (bpmInRange(bpm, range))
                {
                    coversBpm = true;
                    break;
                }
            }
            if (!coversBpm)
                continue;

            if (std::abs(endBeat - startBeat) <= 1e-9)
            {
                // Single-point exclude range: extend to the next BPM
                // position so the orange rectangle and independent
                // beat-number grid have enough height to render.
                double extendedEnd = startBeat;
                for (int i = 0; i < bpmList.size(); ++i)
                {
                    if (bpmInRange(bpmList[i], range))
                    {
                        if (i + 1 < bpmList.size())
                            extendedEnd = MathUtils::beatToFloat(bpmList[i + 1].beatNum,
                                                                 bpmList[i + 1].numerator,
                                                                 bpmList[i + 1].denominator);
                        else
                            extendedEnd = startBeat + 0.5; // minimum visible height
                        break;
                    }
                }
                if (extendedEnd <= startBeat)
                    extendedEnd = startBeat + 0.5;
                m_excludedBeatRanges.append(qMakePair(startBeat, extendedEnd));
            }
            else
            {
                m_excludedBeatRanges.append(qMakePair(startBeat, endBeat));
            }
        }
        m_hasExcludedBpms = !m_excludedBeatRanges.isEmpty();

        if (m_hasExcludedBpms)
        {
            QVector<BpmEntry> filteredBpmList;
            for (int i = 0; i < bpmList.size(); ++i)
            {
                if (isExcluded(bpmList[i]))
                    continue;
                filteredBpmList.append(bpmList[i]);
            }
            if (!filteredBpmList.isEmpty() && filteredBpmList.size() != bpmList.size())
                m_excludedRenderBpmCache = MathUtils::buildBpmTimeCache(filteredBpmList, offset);
        }
    }

    // 缓存排除项数据供 computeBaseBpm 复用
    if (hasExcludes)
    {
        m_cachedExcludes.data = excludesData;
        m_cachedExcludes.loaded = true;
    }

    m_bpmCacheDirty = false;

    // 重新计算基于时间的加权平均 BPM（排除项过滤后）
    m_baseBpm = computeBaseBpm();

    // BPM缓存重建后，如果处于TimeLinear模式，需要重新同步滚动坐标
    // BeatLinear模式下 m_scrollBeat 是权威状态，不应从时间反推
    if (m_coordinateMode == CoordinateMode::TimeLinear && !m_bpmTimeCache.isEmpty())
    {
        syncScrollTimeFromBeat();  // sync canvas legacy members

        // 同步 mapper 内部状态（m_scrollTimeMs），使其与 canvas 一致
        const CoordContext ctx = buildCoordContext();
        m_mapper->syncFromBeat(m_scrollBeat, ctx);
    }
}

const QVector<MathUtils::BpmCacheEntry> &ChartCanvas::bpmTimeCache() const
{
    if (m_bpmCacheDirty)
        rebuildBpmTimeCache();
    // 当排除项不参与渲染且存在排除项时，返回过滤后的缓存（用于网格线和beat高度）
    // 需检查过滤缓存非空，保证 all-excluded 边界情况不丢失网格渲染
    if (m_excludeRenderingEnabled && m_hasExcludedBpms && !m_excludedRenderBpmCache.isEmpty())
        return m_excludedRenderBpmCache;
    return m_bpmTimeCache;
}

const QVector<MathUtils::BpmCacheEntry> &ChartCanvas::fullBpmTimeCache() const
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

    // note 时间必须基于完整 BPM 缓存（含排除项），确保播放时间一致
    // 网格线和 beat 高度使用 bpmTimeCache()（过滤后缓存）
    const QVector<MathUtils::BpmCacheEntry> &fullCache = fullBpmTimeCache();
    if (fullCache.isEmpty())
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
        m_noteTimesMs[i] = MathUtils::beatToMs(note.beatNum, note.numerator, note.denominator, fullCache);
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
    const double baselineRatio = kReferenceLineRatio;
    const double baselineOffsetRatio = m_verticalFlip ? (1.0 - baselineRatio) : baselineRatio;
    const double previousScrollBeat = m_scrollBeat;

    double newScrollBeat = m_scrollBeat;
    {
        const CoordContext ctx = buildCoordContext();
        m_mapper->advancePlayback(clampedTimeMs, baselineRatio, newScrollBeat, ctx);
    }

    const bool scrollChanged = qAbs(newScrollBeat - previousScrollBeat) >= 1e-6;
    const bool timeChanged = qAbs(clampedTimeMs - m_currentPlayTime) >= 0.05;
    if (!scrollChanged && !timeChanged)
        return;

    m_scrollBeat = newScrollBeat;
    m_currentPlayTime = clampedTimeMs;

    // Sync legacy members from current mode's mapper
    m_scrollTimeMs = m_timeLinearMapper.scrollTimeMsRaw();
    m_visibleTimeRangeMs = m_timeLinearMapper.visibleTimeRangeMs();
    m_baseVisibleBeatRange = m_beatLinearMapper.baseVisibleBeatRange();
    update();
    if (scrollChanged)
        emit scrollPositionChanged(m_scrollBeat);
}

void ChartCanvas::syncCurrentPlayTimeToReferenceLine()
{
    if (!chart())
        return;

    const double baselineRatio = kReferenceLineRatio;
    const double baselineOffsetRatio = m_verticalFlip ? (1.0 - baselineRatio) : baselineRatio;

    if (m_coordinateMode == CoordinateMode::TimeLinear)
    {
        const CoordContext ctx = buildCoordContext();
        const double referenceY = m_verticalFlip
                                      ? height() - baselineOffsetRatio * height()
                                      : baselineOffsetRatio * height();
        const double baselineBeat = m_timeLinearMapper.yToBeat(referenceY, m_scrollBeat, ctx);
        m_currentPlayTime = qMax(0.0, MathUtils::beatToMs(baselineBeat, fullBpmTimeCache()));
        return;
    }

    const auto &bpmList = chart()->bpmList();
    const int offset = chart()->meta().offset;
    const double baselineBeat = m_scrollBeat + baselineOffsetRatio * effectiveVisibleBeatRange();
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

void ChartCanvas::setCoordinateMode(CoordinateMode mode)
{
    if (m_coordinateMode == mode)
        return;

    CoordinateMapper *newMapper = (mode == CoordinateMode::TimeLinear)
                                      ? static_cast<CoordinateMapper *>(&m_timeLinearMapper)
                                      : static_cast<CoordinateMapper *>(&m_beatLinearMapper);

    const CoordContext ctx = buildCoordContext();
    newMapper->adoptFrom(m_mapper, m_scrollBeat, ctx);
    // 避免在 adoptFrom 后再次调用 syncFromBeat/syncFromTime；
    // 对 TimeLinear，visibleTimeRangeMs 是权威状态，额外同步易引入漂移。

    m_mapper = newMapper;

    // Sync legacy members for backward compatibility during transition
    m_scrollTimeMs = m_timeLinearMapper.scrollTimeMsRaw();
    m_visibleTimeRangeMs = m_timeLinearMapper.visibleTimeRangeMs();
    m_baseVisibleBeatRange = m_beatLinearMapper.baseVisibleBeatRange();

    m_coordinateMode = mode;
    invalidateGridCache();
    emit coordinateModeChanged(mode);
    update();
}

double ChartCanvas::effectiveVisibleBeatRange() const
{
    const CoordContext ctx = buildCoordContext();
    return m_mapper->effectiveVisibleBeatRange(ctx);
}

void ChartCanvas::syncScrollTimeFromBeat() const
{
    // 使用完整缓存：m_scrollBeat 始终处于完整 beat 空间
    const auto &cache = fullBpmTimeCache();
    if (cache.isEmpty())
    {
        m_scrollTimeMs = 0;
        return;
    }
    m_scrollTimeMs = MathUtils::beatToMs(m_scrollBeat, cache);

    // TimeLinear 模式下不重新计算 visibleTimeRangeMs，它是权威状态
    if (m_coordinateMode != CoordinateMode::TimeLinear)
    {
        const double endBeat = m_scrollBeat + effectiveVisibleBeatRange();
        const double endTime = MathUtils::beatToMs(endBeat, cache);
        m_visibleTimeRangeMs = endTime - m_scrollTimeMs;
    }
}

void ChartCanvas::syncScrollBeatFromTime() const
{
    // 使用完整缓存：m_scrollBeat 始终处于完整 beat 空间，与 m_noteBeatPositions 一致
    const auto &cache = fullBpmTimeCache();
    if (cache.isEmpty())
    {
        m_scrollBeat = 0;
        return;
    }
    m_scrollBeat = MathUtils::msToBeatFloat(m_scrollTimeMs, cache);

    // BeatLinear 模式下从时间范围反推 baseVisibleBeatRange
    const double endTime = m_scrollTimeMs + m_visibleTimeRangeMs;
    const double endBeat = MathUtils::msToBeatFloat(endTime, cache);
    m_baseVisibleBeatRange = (endBeat - m_scrollBeat) * m_timeScale;
}

void ChartCanvas::syncCoordinateState() const
{
    if (m_coordinateMode == CoordinateMode::TimeLinear)
        syncScrollBeatFromTime();   // time 权威 → 推导 beat
    else
        syncScrollTimeFromBeat();   // beat 权威 → 推导 time
}

double ChartCanvas::computeBaseBpm() const
{
    // 基于时间的加权平均 BPM：对每个 BPM 段按其持续时间加权平均
    // 排除在 excludes 列表中的 BPM 段
    // 复用 rebuildBpmTimeCache 缓存的排除项数据（m_cachedExcludes）
    if (!chart())
        return 120.0;

    const auto &bpmList = chart()->bpmList();
    if (bpmList.isEmpty())
        return 120.0;

    // 使用缓存的排除项数据，避免重复加载
    const bool hasCachedExcludes = m_cachedExcludes.loaded;
    auto isExcluded = [&](const BpmEntry &bpm) -> bool
    {
        if (!hasCachedExcludes)
            return false;
        for (const auto &range : m_cachedExcludes.data.excludes)
        {
            bool geStart = (bpm.beatNum > range.startBeatNum)
                           || (bpm.beatNum == range.startBeatNum
                               && bpm.numerator * range.startDenominator >= range.startNumerator * bpm.denominator);
            bool leEnd = (bpm.beatNum < range.endBeatNum)
                         || (bpm.beatNum == range.endBeatNum
                                 && bpm.numerator * range.endDenominator <= range.endNumerator * bpm.denominator);
            if (geStart && leEnd)
                return true;
        }
        return false;
    };

    double totalWeightedBpm = 0.0;
    double totalTime = 0.0;

    for (int i = 0; i < bpmList.size(); ++i)
    {
        const BpmEntry &entry = bpmList[i];
        if (entry.bpm <= 0.0)
            continue;
        if (isExcluded(entry))
            continue;

        double beatPos = entry.beatNum + static_cast<double>(entry.numerator) / entry.denominator;
        double nextBeatPos;
        if (i + 1 < bpmList.size())
        {
            const BpmEntry &next = bpmList[i + 1];
            nextBeatPos = next.beatNum + static_cast<double>(next.numerator) / next.denominator;
        }
        else
        {
            // 最后一段：用第一段的 beat 范围估计
            if (bpmList.size() > 1)
            {
                const BpmEntry &first = bpmList[0];
                nextBeatPos = first.beatNum + 1.0 + (beatPos - first.beatNum);
            }
            else
            {
                nextBeatPos = beatPos + 100.0;
            }
        }

        double beatLen = nextBeatPos - beatPos;
        if (beatLen <= 0)
            continue;

        double durationMs = beatLen * (60000.0 / entry.bpm);
        totalWeightedBpm += entry.bpm * durationMs;
        totalTime += durationMs;
    }

    if (totalTime <= 0)
        return bpmList.first().bpm;

    return totalWeightedBpm / totalTime;
}

double ChartCanvas::negativeBeatScrollLimit() const
{
    if (!chart())
        return 0.0;
    const CoordContext ctx = buildCoordContext();
    return m_mapper->negativeBeatScrollLimit(m_scrollBeat, ctx);
}

void ChartCanvas::clampScrollTimeToLimit() const
{
    const CoordContext ctx = buildCoordContext();
    m_mapper->clampScrollLimit(m_scrollBeat, ctx);

    // 同步 legacy 成员（过渡期）
    if (m_coordinateMode == CoordinateMode::TimeLinear)
    {
        m_scrollTimeMs = m_timeLinearMapper.scrollTimeMsRaw();
        m_visibleTimeRangeMs = m_timeLinearMapper.visibleTimeRangeMs();
    }
    else
    {
        syncScrollTimeFromBeat();
    }
}

void ChartCanvas::setExcludeRenderingEnabled(bool enabled)
{
    if (m_excludeRenderingEnabled == enabled)
        return;
    m_excludeRenderingEnabled = enabled;
    Settings::instance().setExcludeRenderingEnabled(enabled);
    m_bpmCacheDirty = true;
    invalidateGridCache();
    m_timesDirty = true;
    m_noteDataDirty = true;
    update();
}

void ChartCanvas::setBpmCacheDirty()
{
    m_bpmCacheDirty = true;
    invalidateGridCache();
    m_timesDirty = true;
    m_noteDataDirty = true;
}

bool ChartCanvas::isBeatInExcludedRange(double beat) const
{
    if (!m_hasExcludedBpms)
        return false;
    for (const auto &range : m_excludedBeatRanges)
    {
        if (beat >= range.first && beat < range.second)
            return true;
    }
    return false;
}

void ChartCanvas::drawExcludedRangeBackgrounds(QPainter &painter, double startBeat, double endBeat,
                                                double baseY, double sign, double invVisibleRange,
                                                int canvasHeight, int lmargin, int availableWidth,
                                                double scrollTimeMs, double pixelsPerMs,
                                                bool useTimeLinear) const
{
    if (!m_hasExcludedBpms)
        return;

    for (int ri = 0; ri < m_excludedBeatRanges.size(); ++ri)
    {
        const auto &range = m_excludedBeatRanges[ri];
        double rangeStart = range.first;
        double rangeEnd = range.second;

        // 裁剪到可见范围
        if (rangeEnd <= startBeat || rangeStart >= endBeat)
            continue;
        double visStart = qMax(rangeStart, startBeat);
        double visEnd = qMin(rangeEnd, endBeat);

        double yStart, yEnd;
        if (useTimeLinear)
        {
            // 设计意图：橙色矩形位置 = 音符实际时间位置（受排除项BPM影响），
            // 网格线位置 = 真实歌曲BPM决定的节奏位置（不受排除项BPM影响）。
            // 两者刻意不一致，让制谱者直观识别着色调整。
            double startTimeMs = MathUtils::beatToMs(visStart, fullBpmTimeCache());
            double endTimeMs = MathUtils::beatToMs(visEnd, fullBpmTimeCache());
            yStart = baseY + sign * ((startTimeMs - scrollTimeMs) * pixelsPerMs);
            yEnd = baseY + sign * ((endTimeMs - scrollTimeMs) * pixelsPerMs);
        }
        else
        {
            yStart = baseY + sign * ((visStart - m_scrollBeat) * invVisibleRange * canvasHeight);
            yEnd = baseY + sign * ((visEnd - m_scrollBeat) * invVisibleRange * canvasHeight);
        }

        double rectTop = qMin(yStart, yEnd);
        double rectHeight = qAbs(yEnd - yStart);
        if (rectHeight <= 0)
            continue;

        // 绘制橙色半透明背景
        QRectF orangeRect(lmargin, rectTop, availableWidth, rectHeight);
        painter.fillRect(orangeRect, QColor(255, 165, 0, 60));

        QRect canvasRect(lmargin, 0, availableWidth, canvasHeight);
        m_gridRenderer->drawExcludedRangeGrid(painter, canvasRect, m_gridDivision,
                                              rangeStart, rangeEnd,
                                              visStart, visEnd,
                                              yStart, yEnd,
                                              useTimeLinear,
                                              m_timeDivision,
                                              scrollTimeMs, pixelsPerMs,
                                              &fullBpmTimeCache(),
                                              m_verticalFlip);
    }
}

CoordContext ChartCanvas::buildCoordContext() const
{
    CoordContext ctx;
    ctx.canvasHeight = height();
    ctx.timeScale = m_timeScale;
    ctx.verticalFlip = m_verticalFlip;
    ctx.bpmCache = &bpmTimeCache();
    ctx.fullBpmCache = &fullBpmTimeCache();
    ctx.baseBpm = m_baseBpm;
    ctx.offsetMs = chart() ? chart()->meta().offset : 0;
    return ctx;
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
        intervalMs = kOverlayQueryIntervalMsToolMode;

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
    if (m_overlayQueryScheduled)
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


