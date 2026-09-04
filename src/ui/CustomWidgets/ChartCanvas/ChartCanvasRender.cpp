#include "ChartCanvas.h"
#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "controller/PlaybackController.h"
#include "render/NoteRenderer.h"
#include "render/RainRewardGenerator.h"
#include "render/GridRenderer.h"
#include "render/BackgroundRenderer.h"
#include "render/HyperfruitDetector.h"
#include "app/Application.h"
#include "plugin/PluginManager.h"
#include "utils/MathUtils.h"
#include "utils/NativeWindowTheme.h"
#include "utils/Settings.h"
#include "utils/Logger.h"
#include "utils/PlaybackStutterProbe.h"
#include "model/Chart.h"
#include <QPainter>
#include <QPen>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <QCoreApplication>

namespace
{
constexpr double kGridCacheScrollChunkPx = 96.0;

PluginManager *activePluginManager()
{
    auto *app = qobject_cast<Application *>(QCoreApplication::instance());
    if (!app || !app->pluginSystemReady())
        return nullptr;
    return app->pluginManager();
}

class PlaybackPaintAcknowledgement final
{
public:
    PlaybackPaintAcknowledgement(PlaybackController *controller, qint64 frameSeq)
        : m_controller(controller), m_frameSeq(frameSeq)
    {
    }

    ~PlaybackPaintAcknowledgement()
    {
        if (m_controller)
            m_controller->acknowledgeFramePainted(m_frameSeq);
    }

private:
    PlaybackController *m_controller;
    qint64 m_frameSeq;
};
}

void ChartCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    // Frame scheduling is back-pressured by the main canvas. A scope-bound
    // acknowledgement covers empty charts and any future early return without
    // allowing an unrelated paint to release a newer queued frame.
    PlaybackPaintAcknowledgement frameAcknowledgement(
        m_playbackController,
        m_lastPlaybackFrameSeq);

    const bool probeFrame = m_isPlaying && PlaybackStutterProbe::enabled();
    const qint64 paintStartNs = probeFrame ? m_playbackVisualClock.nsecsElapsed() : 0;
    double paintIntervalMs = -1.0;
    if (probeFrame)
    {
        if (m_lastPaintProbeNs > 0 && paintStartNs > m_lastPaintProbeNs)
        {
            paintIntervalMs = static_cast<double>(paintStartNs - m_lastPaintProbeNs) / 1000000.0;
            const double targetIntervalMs = 1000.0 / qMax(1.0, m_playbackController->effectiveFrameRate());
            PlaybackStutterProbe::recordDuration(
                "canvas.paint_interval", paintIntervalMs, targetIntervalMs * 1.35, true);
            if (m_lastPaintIntervalMs >= 0.0)
            {
                PlaybackStutterProbe::recordDuration(
                    "canvas.paint_interval_jerk",
                    std::abs(paintIntervalMs - m_lastPaintIntervalMs),
                    2.0,
                    true);
            }
            m_lastPaintIntervalMs = paintIntervalMs;
        }
        m_lastPaintProbeNs = paintStartNs;
    }
    QElapsedTimer paintTimer;
    if (probeFrame)
        paintTimer.start();
    const auto paintMarkNs = [&paintTimer, probeFrame]() -> qint64
    {
        return probeFrame ? paintTimer.nsecsElapsed() : 0;
    };

    m_frameCount++;
    qint64 elapsed = m_fpsTimer.elapsed();
    if (elapsed >= 1000)
    {
        m_currentFps = m_frameCount * 1000.0 / elapsed;
        m_frameCount = 0;
        m_fpsTimer.restart();
    }

    QPainter painter(this);
    if (!chart())
    {
        painter.fillRect(rect(), Settings::instance().backgroundColor());
        return;
    }

    if (m_timesDirty || m_noteDataDirty)
        rebuildNoteTimesCache();

    if (m_isPlaying && m_playbackController)
    {
        // Render from the controller's authoritative high-resolution clock.
        // Re-anchoring a second canvas-local clock on every frame tick turns
        // signal-handler latency into a small position jump once per tick.
        // The tick should schedule the paint; it should not be a second clock.
        double visualTimeMs = m_playbackController->visualTime();
        if (m_lastPlaybackTargetTimeMs >= 0.0)
            visualTimeMs = qMax(visualTimeMs, m_lastPlaybackTargetTimeMs);
        m_currentPlayTime = qMax(0.0, visualTimeMs);
        advancePlaybackVisual(false, false);
    }

    // Measure motion at the point actually painted. Tick-to-tick displacement
    // is not a speed metric: a correctly paced 8/9 ms timer cadence naturally
    // produces different per-frame steps at 120 Hz. Normalizing the scroll
    // delta by the real paint interval isolates visible speed changes instead.
    if (probeFrame && m_autoScrollEnabled && paintIntervalMs > 0.0)
    {
        const double visibleRange = qMax(1e-6, effectiveVisibleBeatRange());
        const double pixelsPerBeat = height() > 0
                                         ? static_cast<double>(height()) / visibleRange
                                         : 0.0;
        if (m_paintMotionProbeValid)
        {
            const double velocityPxPerSecond =
                std::abs(m_scrollBeat - m_lastPaintScrollBeat) *
                pixelsPerBeat * 1000.0 / paintIntervalMs;
            PlaybackStutterProbe::recordValue(
                "visual.scroll_velocity_px_s",
                velocityPxPerSecond,
                -1.0,
                true);

            if (m_lastPaintScrollVelocityPxPerSecond >= 0.0)
            {
                const double velocityDeltaPxPerSecond =
                    std::abs(velocityPxPerSecond - m_lastPaintScrollVelocityPxPerSecond);
                const double velocityReferencePxPerSecond =
                    qMax(1.0,
                         0.5 * (velocityPxPerSecond +
                                m_lastPaintScrollVelocityPxPerSecond));
                const double velocityChangePercent =
                    velocityDeltaPxPerSecond * 100.0 / velocityReferencePxPerSecond;
                PlaybackStutterProbe::recordValue(
                    "visual.scroll_velocity_delta_px_s",
                    velocityDeltaPxPerSecond,
                    -1.0,
                    true);
                PlaybackStutterProbe::recordValue(
                    "visual.scroll_velocity_change_pct",
                    velocityChangePercent,
                    1.0,
                    true);
            }
            m_lastPaintScrollVelocityPxPerSecond = velocityPxPerSecond;
        }
        m_lastPaintScrollBeat = m_scrollBeat;
        m_paintMotionProbeValid = true;
    }
    else if (probeFrame)
    {
        m_paintMotionProbeValid = false;
        m_lastPaintScrollVelocityPxPerSecond = -1.0;
    }
    const qint64 preparationEndNs = paintMarkNs();

    const Chart *currentChart = chart();
    const auto &bpmList = currentChart->bpmList();
    const auto &notes = currentChart->notes();

    // 仅在第脏或首次绘制时重建背景缓存 (perf fix A)
    {
        QSize sz = size();
        if (m_backgroundCacheDirty || m_backgroundCache.size() != sz)
            drawBackground(painter);
        else if (!m_backgroundCache.isNull())
            painter.drawPixmap(0, 0, m_backgroundCache);
    }
    const qint64 backgroundEndNs = paintMarkNs();

    drawGrid(painter);
    const qint64 gridEndNs = paintMarkNs();

    double startBeat = m_scrollBeat;
    double visibleRange = effectiveVisibleBeatRange();
    double endBeat = startBeat + visibleRange;

    

    if (m_hyperfruitEnabled && !m_hyperCacheValid)
    {
        if (bpmList.isEmpty())
            m_cachedHyperSet.clear();
        else
            m_cachedHyperSet = m_hyperfruitDetector->detect(notes, bpmList, 0);
        m_noteRenderer->setHyperfruitIndices(m_cachedHyperSet);
        m_hyperCacheValid = true;
    }
    const qint64 cacheEndNs = paintMarkNs();

    const int canvasWidth = width();
    const int canvasHeight = height();
    const int lmargin = leftMargin();
    const int rmargin = rightMargin();
    const int availableWidth = qMax(1, canvasWidth - lmargin - rmargin);
    const double invVisibleRange = 1.0 / visibleRange;
    const double baseY = m_verticalFlip ? canvasHeight : 0;
    const double sign = m_verticalFlip ? -1.0 : 1.0;

    QSet<int> selectedSet;
    if (m_selectionController)
        selectedSet = m_selectionController->selectedIndices();

    painter.setClipRect(rect());

    auto renderNoteAtIndex = [&](int i)
    {
        if (i < 0 || i >= notes.size())
            return;
        NoteType type = m_noteTypes[i];
        if (type == NoteType::SOUND)
            return;

        double beat = m_noteBeatPositions[i];
        double endBeatNote = m_noteEndBeatPositions[i];

        if (type == NoteType::NORMAL)
        {
            if (beat < startBeat - 0.5 || beat > endBeat + 0.5)
                return;
        }
        else if (type == NoteType::RAIN)
        {
            if (endBeatNote <= startBeat || beat >= endBeat)
                return;
        }

        double y = baseY + sign * ((beat - m_scrollBeat) * invVisibleRange * canvasHeight);

        if (type == NoteType::RAIN)
        {
            double visibleStartBeat = qMax(beat, startBeat);
            double visibleEndBeat = qMin(endBeatNote, endBeat);
            double yStart = baseY + sign * ((visibleStartBeat - m_scrollBeat) * invVisibleRange * canvasHeight);
            double yEnd = baseY + sign * ((visibleEndBeat - m_scrollBeat) * invVisibleRange * canvasHeight);
            double rectTop = qMin(yStart, yEnd);
            double rectHeight = qAbs(yEnd - yStart);
            if (rectHeight <= 0)
                return;
            QRectF rainRect(lmargin, rectTop, availableWidth, rectHeight);
            bool selected = selectedSet.contains(i);
            // rain 奖励 note 预览点（同一 paint pass 内绘制，无额外图层；关闭时零开销）。
            QVector<QPointF> rewardPoints;
            if (m_noteRenderer->rainRewardPreviewEnabled())
            {
                auto &generator = RainRewardGenerator::instance();
                generator.ensureChart(notes, bpmList, chart()->meta().offset);
                const QVector<RainDrop> drops = generator.dropsFor(notes[i]);
                if (!drops.isEmpty())
                {
                    rewardPoints.reserve(drops.size());
                    for (const RainDrop &drop : drops)
                    {
                        const double dropBeat = beat + drop.beatOffset;
                        if (dropBeat < startBeat || dropBeat > endBeat)
                            continue;
                        const double dropY = baseY + sign * ((dropBeat - m_scrollBeat) * invVisibleRange * canvasHeight);
                        const double dropX = lmargin + drop.xRatio * availableWidth;
                        rewardPoints.append(QPointF(dropX, dropY));
                    }
                }
            }
            m_noteRenderer->drawRain(painter, notes[i], rainRect, selected,
                                     rewardPoints.isEmpty() ? nullptr : &rewardPoints);
        }
        else
        {
            double x = lmargin + m_noteXPositions[i] * availableWidth;
            QPointF pos(x, y);
            bool selected = selectedSet.contains(i);
            m_noteRenderer->drawNote(painter, notes[i], pos, selected, i);
        }
    };

    if (!m_sortedRainNoteIndicesByBeat.isEmpty())
    {
        const auto rainBegin = std::lower_bound(
            m_sortedRainNoteIndicesByBeat.begin(),
            m_sortedRainNoteIndicesByBeat.end(),
            startBeat,
            [this](int idx, double beatValue) {
                return m_noteBeatPositions[idx] < beatValue;
            });

        // Include rain notes that started earlier but may still overlap current view.
        auto rainStartIt = rainBegin;
        while (rainStartIt != m_sortedRainNoteIndicesByBeat.begin())
        {
            auto prev = rainStartIt - 1;
            const int idx = *prev;
            if (m_noteEndBeatPositions[idx] <= startBeat)
                break;
            rainStartIt = prev;
        }

        for (auto it = rainStartIt; it != m_sortedRainNoteIndicesByBeat.end(); ++it)
        {
            const int idx = *it;
            if (m_noteBeatPositions[idx] >= endBeat)
                break;
            renderNoteAtIndex(idx);
        }
    }

    if (!m_sortedNormalNoteIndicesByBeat.isEmpty())
    {
        const auto normalStart = std::lower_bound(
            m_sortedNormalNoteIndicesByBeat.begin(),
            m_sortedNormalNoteIndicesByBeat.end(),
            startBeat - 0.5,
            [this](int idx, double beatValue) {
                return m_noteBeatPositions[idx] < beatValue;
            });
        for (auto it = normalStart; it != m_sortedNormalNoteIndicesByBeat.end(); ++it)
        {
            const int idx = *it;
            if (m_noteBeatPositions[idx] > endBeat + 0.5)
                break;
            renderNoteAtIndex(idx);
        }
    }
    const qint64 notesEndNs = paintMarkNs();

    if (m_isPasting && !m_pasteNotes.isEmpty())
        drawPastePreview(painter, canvasHeight, lmargin, availableWidth, invVisibleRange, baseY, sign);

    if (m_mirrorPreviewVisible)
        drawMirrorPreview(painter, canvasHeight, lmargin, availableWidth, invVisibleRange, baseY, sign);

    if (m_mirrorGuideVisible)
        drawMirrorGuide(painter, canvasHeight, lmargin, availableWidth);

    // 范围选择覆盖层
    if (m_rangeOverlayValid && m_rangeOverlayVisible)
    {
        drawRangeSelectionHighlight(painter, lmargin, availableWidth, canvasHeight);
        drawRangeOverlay(painter, lmargin, rmargin, canvasHeight);
    }

    double baselineY = canvasHeight * kReferenceLineRatio;
    painter.setPen(QPen(QColor(0, 0, 255), 3));
    painter.drawLine(lmargin, baselineY, canvasWidth - rmargin, baselineY);

    if (m_playbackController && m_currentPlayTime > 0)
    {
        // Text is painted directly over the user-configurable canvas color,
        // so pick the readable contrast color for the current background.
        const QColor overlayTextColor =
            NativeWindowTheme::themeColorsFor(Settings::instance().backgroundColor()).text;
        painter.setPen(overlayTextColor);
        painter.drawText(canvasWidth - rmargin - 50, baselineY - 5,
                         QString::number(m_currentPlayTime, 'f', 0) + "ms");
        QString autoScrollText = m_autoScrollEnabled ? tr("AutoScroll: ON") : tr("AutoScroll: OFF");
        painter.drawText(canvasWidth - rmargin - 200, baselineY - 5, autoScrollText);
    }

    if (m_isSelecting)
    {
        QRectF rect = QRectF(m_selectionStart, m_selectionEnd).normalized();
        painter.setPen(Qt::red);
        painter.setBrush(QColor(255, 255, 0, 80));
        painter.drawRect(rect);
    }

    drawPluginOverlays(painter, lmargin, rmargin);

    // NoteChain native: direct QPainter render (zero overlay serialization)
    ChartCanvas_drawNoteChainOverlay(this, &painter);

    painter.setPen(Qt::white);
    painter.setBrush(QColor(0, 0, 0, 128));
    QString fpsText = QString("FPS: %1").arg(m_currentFps, 0, 'f', 1);
    QRect fpsRect(10, canvasHeight - 30, 80, 20);
    painter.fillRect(fpsRect, QColor(0, 0, 0, 128));
    painter.drawText(fpsRect, Qt::AlignCenter, fpsText);

    const qint64 overlaysEndNs = paintMarkNs();
    if (probeFrame)
    {
        const auto toMs = [](qint64 nanoseconds) -> double
        {
            return static_cast<double>(nanoseconds) / 1000000.0;
        };
        PlaybackStutterProbe::recordDuration(
            "canvas.paint.prepare", toMs(preparationEndNs), 1.0, true);
        PlaybackStutterProbe::recordDuration(
            "canvas.paint.background", toMs(backgroundEndNs - preparationEndNs), 1.5, true);
        PlaybackStutterProbe::recordDuration(
            "canvas.paint.grid", toMs(gridEndNs - backgroundEndNs), 2.0, true);
        PlaybackStutterProbe::recordDuration(
            "canvas.paint.cache", toMs(cacheEndNs - gridEndNs), 1.0, true);
        PlaybackStutterProbe::recordDuration(
            "canvas.paint.notes", toMs(notesEndNs - cacheEndNs), 3.0, true);
        PlaybackStutterProbe::recordDuration(
            "canvas.paint.overlays", toMs(overlaysEndNs - notesEndNs), 2.0, true);
        PlaybackStutterProbe::recordDuration(
            "canvas.paint_total", toMs(overlaysEndNs), 8.0, true);
    }

}

void ChartCanvas::drawPastePreview(QPainter &painter,
                                   int canvasHeight,
                                   int lmargin,
                                   int availableWidth,
                                   double invVisibleRange,
                                   double baseY,
                                   double sign)
{
    if (!chart())
        return;

    painter.setOpacity(0.5);

    const auto &bpmList = chart()->bpmList();
    const int offset = chart()->meta().offset;

    QVector<double> fallbackOriginalTimes;
    bool usingCachedOriginalTimes =
        (m_pasteOriginalTimesMs.size() == m_pasteNotes.size()) &&
        std::isfinite(m_pasteBaseOriginalTimeMs);
    if (!usingCachedOriginalTimes)
    {
        fallbackOriginalTimes.resize(m_pasteNotes.size());
        for (int i = 0; i < fallbackOriginalTimes.size(); ++i)
            fallbackOriginalTimes[i] = std::numeric_limits<double>::quiet_NaN();

        for (int i = 0; i < m_pasteNotes.size(); ++i)
        {
            const Note &note = m_pasteNotes[i];
            if (note.type == NoteType::SOUND)
                continue;
            const double t = MathUtils::beatToMs(note.beatNum, note.numerator, note.denominator, bpmList, offset);
            fallbackOriginalTimes[i] = t;
        }
    }

    const QVector<double> &sourceOriginalTimes = usingCachedOriginalTimes ? m_pasteOriginalTimesMs : fallbackOriginalTimes;
    double baseOriginalTime = m_pasteBaseOriginalTimeMs;
    if (!usingCachedOriginalTimes)
    {
        baseOriginalTime = (std::numeric_limits<double>::max)();
        for (double t : sourceOriginalTimes)
        {
            if (std::isfinite(t) && t < baseOriginalTime)
                baseOriginalTime = t;
        }
    }
    if (baseOriginalTime != (std::numeric_limits<double>::max)())
    {
        const QVector<MathUtils::BpmCacheEntry> &previewBpmCache = bpmTimeCache();
        auto previewBeatFromTimeMs = [&previewBpmCache, &bpmList, offset](double ms) -> double
        {
            if (!previewBpmCache.isEmpty())
            {
                int lo = 0;
                int hi = previewBpmCache.size() - 1;
                while (lo < hi)
                {
                    int mid = (lo + hi + 1) / 2;
                    if (previewBpmCache[mid].accumulatedMs <= ms)
                        lo = mid;
                    else
                        hi = mid - 1;
                }
                const auto &seg = previewBpmCache[lo];
                if (seg.bpm <= 0.0)
                    return seg.beatPos;
                return seg.beatPos + (ms - seg.accumulatedMs) * (seg.bpm / 60000.0);
            }
            int b = 0, n = 0, d = 1;
            MathUtils::msToBeat(ms, bpmList, offset, b, n, d);
            return MathUtils::beatToFloat(b, n, d);
        };
        const double baseOriginalBeat = previewBeatFromTimeMs(baseOriginalTime);
        const double referenceBeat = m_pasteAnchorBeat;
        const double baseBeatShift = referenceBeat - baseOriginalBeat;
        const double totalBeatShift = snapPasteTimeOffset(baseBeatShift + m_pasteTimeOffset);
        for (int i = 0; i < m_pasteNotes.size(); ++i)
        {
            const Note &note = m_pasteNotes[i];
            if (note.type == NoteType::SOUND)
                continue;
            if (i >= sourceOriginalTimes.size())
                continue;
            const double originalTime = sourceOriginalTimes[i];
            if (!std::isfinite(originalTime))
                continue;
            const double originalBeatFloat = previewBeatFromTimeMs(originalTime);
            const double requestedPreviewBeat = originalBeatFloat + totalBeatShift;
            Note previewNote = note;
            const bool use288Division = Settings::instance().pasteUse288Division();
            const bool represented = use288Division
                ? MathUtils::quantizeBeatToDivision(requestedPreviewBeat, 288,
                                                    previewNote.beatNum, previewNote.numerator, previewNote.denominator)
                : MathUtils::representBeatWithDivision(requestedPreviewBeat, qMax(1, note.denominator),
                                                       previewNote.beatNum, previewNote.numerator, previewNote.denominator);
            if (!represented)
            {
                MathUtils::floatToBeat(requestedPreviewBeat, previewNote.beatNum,
                                       previewNote.numerator, previewNote.denominator);
            }
            const double previewBeatFloat = MathUtils::beatToFloat(
                previewNote.beatNum, previewNote.numerator, previewNote.denominator);
            const double y = baseY + sign * ((previewBeatFloat - m_scrollBeat) * invVisibleRange * canvasHeight);
            const int previewShiftedX = qBound(0, note.x + qRound(m_pasteXOffset), kLaneWidth);
            const double x = lmargin + (previewShiftedX / static_cast<double>(kLaneWidth)) * availableWidth;
            m_noteRenderer->drawNote(painter, previewNote, QPointF(x, y), false, -1);
        }
    }

    painter.setOpacity(1.0);
    painter.fillRect(QRect(10, 10, 100, 30), QColor(200, 200, 200));
    painter.drawText(QRect(10, 10, 100, 30), Qt::AlignCenter, tr("Confirm"));
    painter.fillRect(QRect(120, 10, 100, 30), QColor(200, 200, 200));
    painter.drawText(QRect(120, 10, 100, 30), Qt::AlignCenter, tr("Cancel"));
    if (Settings::instance().pasteUse288Division())
    {
        painter.setPen(QColor(255, 225, 120));
        painter.drawText(QRect(230, 10, 180, 30), Qt::AlignVCenter | Qt::AlignLeft,
                         tr("Timing: 1/288"));
    }
}

void ChartCanvas::drawMirrorPreview(QPainter &painter,
                                    int canvasHeight,
                                    int lmargin,
                                    int availableWidth,
                                    double invVisibleRange,
                                    double baseY,
                                    double sign)
{
    if (!chart() || !m_selectionController)
        return;

    const QSet<int> selectedSet = m_selectionController->selectedIndices();
    if (selectedSet.isEmpty())
        return;

    const auto &notes = chart()->notes();
    painter.save();
    painter.setOpacity(0.4);

    for (int idx : selectedSet)
    {
        if (idx < 0 || idx >= notes.size())
            continue;

        const Note &note = notes[idx];
        if (note.type == NoteType::SOUND)
            continue;

        Note mirrored = note;
        mirrored.x = qBound(0, m_mirrorAxisX * 2 - note.x, kLaneWidth);
        const double beat = MathUtils::beatToFloat(mirrored.beatNum, mirrored.numerator, mirrored.denominator);
        const double y = baseY + sign * ((beat - m_scrollBeat) * invVisibleRange * canvasHeight);

        if (mirrored.type == NoteType::RAIN)
        {
            const double endBeat = MathUtils::beatToFloat(mirrored.endBeatNum, mirrored.endNumerator, mirrored.endDenominator);
            const double yEnd = baseY + sign * ((endBeat - m_scrollBeat) * invVisibleRange * canvasHeight);
            const double rectTop = qMin(y, yEnd);
            const double rectHeight = qAbs(yEnd - y);
            if (rectHeight <= 0.0)
                continue;
            QRectF rainRect(lmargin, rectTop, availableWidth, rectHeight);
            // 镜像预览：预览点横向沿镜像轴翻转（复用同一份缓存序列）。
            QVector<QPointF> rewardPoints;
            if (m_noteRenderer->rainRewardPreviewEnabled())
            {
                auto &generator = RainRewardGenerator::instance();
                generator.ensureChart(notes, chart()->bpmList(), chart()->meta().offset);
                const QVector<RainDrop> drops = generator.dropsFor(mirrored);
                if (!drops.isEmpty())
                {
                    rewardPoints.reserve(drops.size());
                    for (const RainDrop &drop : drops)
                    {
                        const double dropBeat =
                            MathUtils::beatToFloat(mirrored.beatNum, mirrored.numerator, mirrored.denominator)
                            + drop.beatOffset;
                        const double dropY = baseY + sign * ((dropBeat - m_scrollBeat) * invVisibleRange * canvasHeight);
                        const double dropX = lmargin + (1.0 - drop.xRatio) * availableWidth;
                        rewardPoints.append(QPointF(dropX, dropY));
                    }
                }
            }
            m_noteRenderer->drawRain(painter, mirrored, rainRect, false,
                                     rewardPoints.isEmpty() ? nullptr : &rewardPoints);
        }
        else
        {
            const double x = lmargin + (mirrored.x / static_cast<double>(kLaneWidth)) * availableWidth;
            m_noteRenderer->drawNote(painter, mirrored, QPointF(x, y), false, -1);
        }
    }

    painter.restore();
}

void ChartCanvas::drawMirrorGuide(QPainter &painter, int canvasHeight, int lmargin, int availableWidth)
{
    Q_UNUSED(lmargin);
    Q_UNUSED(availableWidth);

    const double axisCanvasX = laneXToCanvasX(m_mirrorAxisX);
    constexpr double kHandleRadius = 10.0;

    painter.save();
    QPen guidePen(QColor(220, 40, 40), 4, Qt::DashLine);
    guidePen.setCapStyle(Qt::RoundCap);
    painter.setPen(guidePen);
    painter.drawLine(QPointF(axisCanvasX, 0.0), QPointF(axisCanvasX, canvasHeight));

    painter.setPen(QPen(QColor(220, 40, 40), 2));
    painter.setBrush(QColor(255, 255, 255));
    painter.drawEllipse(QPointF(axisCanvasX, 14.0), kHandleRadius, kHandleRadius);
    painter.drawEllipse(QPointF(axisCanvasX, canvasHeight - 14.0), kHandleRadius, kHandleRadius);
    painter.restore();
}

void ChartCanvas::drawPluginOverlays(QPainter &painter, int lmargin, int rmargin)
{
    Q_UNUSED(lmargin);
    Q_UNUSED(rmargin);

    if (!m_pluginToolModeActive)
        return;
    if (!m_pluginOverlayToggles.value("overlay_enabled", true).toBool())
        return;

    const QList<PluginInterface::CanvasOverlayItem> &drawItems = m_overlayCache;
    if (drawItems.isEmpty())
        return;

    for (const auto &item : drawItems)
    {
        QPen pen(item.color, item.width);
        painter.setPen(pen);
        QPointF from = item.from;
        QPointF to = item.to;
        QRectF rect = item.rect;
        if (item.chartSpace)
        {
            from = QPointF(laneXToCanvasX(item.chartFrom.x()), beatToY(item.chartFrom.y()));
            to = QPointF(laneXToCanvasX(item.chartTo.x()), beatToY(item.chartTo.y()));
            if (item.kind == PluginInterface::CanvasOverlayItem::Rect)
            {
                if (item.rectCenterOnChartPoint)
                {
                    rect = QRectF(from.x() - item.rect.width() * 0.5,
                                  from.y() - item.rect.height() * 0.5,
                                  item.rect.width(),
                                  item.rect.height());
                }
                else
                {
                    rect = QRectF(from.x(), from.y(), item.rect.width(), item.rect.height());
                }
            }
        }
        if (item.kind == PluginInterface::CanvasOverlayItem::Rect)
        {
            painter.fillRect(rect, item.fillColor);
            painter.drawRect(rect);
        }
        else if (item.kind == PluginInterface::CanvasOverlayItem::Text)
        {
            QFont f = painter.font();
            f.setPixelSize(qMax(8, item.fontPx));
            painter.setFont(f);
            painter.drawText(from, item.text);
        }
        else
        {
            painter.drawLine(from, to);
        }
    }
}

void ChartCanvas::drawBackground(QPainter &painter)
{
    QSize sz = size();
    if (m_backgroundCacheDirty || m_backgroundCache.size() != sz)
    {
        if (!chart())
        {
            m_backgroundRenderer->setBackgroundImage("");
            m_backgroundRenderer->setBackgroundColor(Settings::instance().backgroundColor());
            m_backgroundRenderer->setImageEnabled(Settings::instance().backgroundImageEnabled());
            m_backgroundRenderer->setImageBrightness(Settings::instance().backgroundImageBrightness());
            m_backgroundCache = m_backgroundRenderer->generateBackground(sz);
        }
        else
        {
            const MetaData &meta = chart()->meta();
            QString bgPath = meta.backgroundFile;
            if (!bgPath.isEmpty())
            {
                if (!QDir::isAbsolutePath(bgPath))
                {
                    const QString chartDir = QFileInfo(m_chartController->chartFilePath()).absolutePath();
                    bgPath = QDir(chartDir).filePath(bgPath);
                }
                m_backgroundRenderer->setBackgroundImage(bgPath);
            }
            else
            {
                m_backgroundRenderer->setBackgroundImage("");
            }
            m_backgroundRenderer->setBackgroundColor(Settings::instance().backgroundColor());
            m_backgroundRenderer->setImageEnabled(Settings::instance().backgroundImageEnabled());
            m_backgroundRenderer->setImageBrightness(Settings::instance().backgroundImageBrightness());
            m_backgroundCache = m_backgroundRenderer->generateBackground(sz);
        }
        m_backgroundCacheDirty = false;
    }
    painter.drawPixmap(0, 0, m_backgroundCache);
}

void ChartCanvas::drawGrid(QPainter &painter)
{
    try
    {
        QRect rect = this->rect();
        int lmargin = leftMargin();
        int rmargin = rightMargin();
        if (lmargin > 0 || rmargin > 0)
        {
            rect.adjust(lmargin, 0, -rmargin, 0);
        }

        if (!chart())
        {
            invalidateGridCache();
            return;
        }

        const double startBeat = m_scrollBeat;
        const double totalBeats = effectiveVisibleBeatRange();
        const double endBeat = startBeat + totalBeats;

        const int viewportHeight = qMax(1, rect.height());
        const double beatsPerPixel = totalBeats / viewportHeight;
        // Quantize the backing cache in larger vertical chunks and compensate
        // draw position per-frame so playback scrolling can mostly reuse cache.
        const double quantStepBeat = qMax(1e-6, beatsPerPixel * kGridCacheScrollChunkPx);
        const auto quantizeDown = [quantStepBeat](double value) -> double
        {
            return std::floor(value / quantStepBeat) * quantStepBeat;
        };
        const double renderStartBeat = quantizeDown(startBeat);
        const double renderEndBeat = renderStartBeat + totalBeats;

        const int cachePadPx = qMax(8, static_cast<int>(std::ceil((quantStepBeat / totalBeats) * viewportHeight)) + 2);
        const double cachePadBeat = beatsPerPixel * static_cast<double>(cachePadPx);
        const double cacheStartBeat = renderStartBeat - cachePadBeat;
        const double cacheEndBeat = renderEndBeat + cachePadBeat;
        const QSize cacheSize(rect.width(), viewportHeight + cachePadPx * 2);

        // Rendering preferences change only through explicit UI actions. Read
        // QSettings once per invalidation instead of parsing them every frame.
        if (!m_gridCacheValid)
        {
            m_gridCacheColorEnabled = Settings::instance().timelineDivisionColorEnabled();
            m_gridCacheColorPreset = Settings::instance().timelineDivisionColorPreset();
            m_gridCacheColorCustomDivisions = Settings::instance().timelineDivisionColorCustomDivisions();
            m_gridCacheBeatNumberFontSize = Settings::instance().beatNumberFontSize();

            QFont beatNumberFont;
            beatNumberFont.setPointSize(m_gridCacheBeatNumberFontSize);
            const QFontMetrics metrics(beatNumberFont);
            m_gridCacheBeatNumberLeftMargin = m_gridCacheBeatNumberFontSize > 0
                                                  ? metrics.horizontalAdvance("999999") + 4
                                                  : 0;
        }

        const bool colorEnabled = m_gridCacheColorEnabled;
        const QString &colorPreset = m_gridCacheColorPreset;
        const QList<int> &colorCustom = m_gridCacheColorCustomDivisions;
        const int beatNumberFontSize = m_gridCacheBeatNumberFontSize;
        const int beatNumberLeftMargin = m_gridCacheBeatNumberLeftMargin;

        // During playback the grid moves every frame. Blending a full-height
        // transparent pixmap costs more than drawing the small set of visible
        // lines directly, and rebuilding that pixmap at each scroll chunk
        // creates a periodic multi-millisecond spike. Keep the cache for the
        // stationary editor, but use constant-cost direct rendering in motion.
        if (m_isPlaying)
        {
            m_gridRenderer->drawGrid(painter, rect, m_gridDivision,
                                     startBeat, endBeat,
                                     m_timeDivision,
                                     m_verticalFlip,
                                     colorEnabled,
                                     colorPreset,
                                     colorCustom,
                                     beatNumberFontSize,
                                     beatNumberLeftMargin);
            // Preferences were refreshed above. Leaving the old pixmap
            // metadata intact makes the first paused frame rebuild it only if
            // the viewport has actually moved to a different chunk.
            m_gridCacheValid = true;
            return;
        }

        const bool needRebuild =
            !m_gridCacheValid ||
            m_gridCacheRect.size() != rect.size() ||
            m_gridCacheRect.topLeft() != rect.topLeft() ||
            m_gridCacheDivision != m_gridDivision ||
            m_gridCacheTimeDivision != m_timeDivision ||
            m_gridCacheVerticalFlip != m_verticalFlip ||
            m_gridCachePadPx != cachePadPx ||
            std::abs(m_gridCacheStartBeat - cacheStartBeat) > 1e-6 ||
            std::abs(m_gridCacheEndBeat - cacheEndBeat) > 1e-6;

        if (needRebuild)
        {
            if (cacheSize.width() <= 0 || cacheSize.height() <= 0)
            {
                invalidateGridCache();
                return;
            }

            const QSize expandedCacheSize(cacheSize.width() + beatNumberLeftMargin, cacheSize.height());
            m_gridCache = QPixmap(expandedCacheSize);
            m_gridCache.fill(Qt::transparent);
            QPainter cachePainter(&m_gridCache);
            // 缓存矩形在左侧预留 beatNumberLeftMargin 空间给节拍编号
            const QRect cacheRect(beatNumberLeftMargin, 0, cacheSize.width(), cacheSize.height());
            m_gridRenderer->drawGrid(cachePainter, cacheRect, m_gridDivision,
                                     cacheStartBeat, cacheEndBeat,
                                     m_timeDivision,
                                     m_verticalFlip,
                                     colorEnabled,
                                     colorPreset,
                                     colorCustom,
                                     beatNumberFontSize,
                                     beatNumberLeftMargin);
            m_gridCacheRect = rect;
            m_gridCacheStartBeat = cacheStartBeat;
            m_gridCacheEndBeat = cacheEndBeat;
            m_gridCacheDivision = m_gridDivision;
            m_gridCacheTimeDivision = m_timeDivision;
            m_gridCacheVerticalFlip = m_verticalFlip;
            m_gridCacheColorEnabled = colorEnabled;
            m_gridCacheColorPreset = colorPreset;
            m_gridCacheColorCustomDivisions = colorCustom;
            m_gridCachePadPx = cachePadPx;
            m_gridCacheBeatNumberFontSize = beatNumberFontSize;
            m_gridCacheBeatNumberLeftMargin = beatNumberLeftMargin;
            m_gridCacheValid = true;
        }

        if (!m_gridCache.isNull())
        {
            const double shiftPx = (startBeat - renderStartBeat) / totalBeats * viewportHeight;
            const double cacheTop = m_verticalFlip
                                        ? static_cast<double>(rect.top()) - m_gridCachePadPx + shiftPx
                                        : static_cast<double>(rect.top()) - m_gridCachePadPx - shiftPx;
            painter.save();
            painter.setClipRect(QRect(rect.left() - beatNumberLeftMargin, rect.top(),
                                      rect.width() + beatNumberLeftMargin, rect.height()));
            painter.drawPixmap(QPointF(rect.left() - beatNumberLeftMargin, cacheTop), m_gridCache);
            painter.restore();
        }
    }
    catch (const std::exception &e)
    {
        Logger::error(QString("ChartCanvas::drawGrid - Exception: %1").arg(e.what()));
    }
    catch (...)
    {
        Logger::error("ChartCanvas::drawGrid - Unknown exception");
    }
}

double ChartCanvas::getNoteTimeMs(const Note &note) const
{
    return MathUtils::beatToMs(note.beatNum, note.numerator, note.denominator,
                               chart()->bpmList(),
                               chart()->meta().offset);
}

double ChartCanvas::yPosFromTime(double timeMs) const
{
    return beatToY(beatFromTimeMs(timeMs));
}

double ChartCanvas::beatToY(double beat) const
{
    double visibleRange = effectiveVisibleBeatRange();
    if (visibleRange <= 0)
        return 0;
    double y = (beat - m_scrollBeat) / visibleRange * height();
    if (m_verticalFlip)
        y = height() - y;
    return y;
}

double ChartCanvas::yToBeat(double y) const
{
    if (height() <= 0)
        return m_scrollBeat;

    if (m_verticalFlip)
        y = height() - y;

    return m_scrollBeat + (y / height()) * effectiveVisibleBeatRange();
}

double ChartCanvas::yToTime(double y) const
{
    double beat = yToBeat(y);
    int beatNum, num, den;
    MathUtils::floatToBeat(beat, beatNum, num, den);
    return MathUtils::beatToMs(beatNum, num, den,
                               chart()->bpmList(),
                               chart()->meta().offset);
}

QPointF ChartCanvas::noteToPos(const Note &note) const
{
    double beat = MathUtils::beatToFloat(note.beatNum, note.numerator, note.denominator);
    double y = beatToY(beat);
    int lmargin = leftMargin();
    int rmargin = rightMargin();
    int availableWidth = qMax(1, width() - lmargin - rmargin);
    double x = lmargin + (note.x / static_cast<double>(kLaneWidth)) * availableWidth;
    return QPointF(x, y);
}

Note ChartCanvas::posToNote(const QPointF &pos) const
{
    double beat = yToBeat(pos.y());
    int beatNum, num, den;
    MathUtils::floatToBeat(beat, beatNum, num, den);
    int lmargin = leftMargin();
    int rmargin = rightMargin();
    int availableWidth = qMax(1, width() - lmargin - rmargin);
    int x = static_cast<int>((pos.x() - lmargin) / availableWidth * kLaneWidth);

    if (m_gridSnap)
    {
        x = MathUtils::snapXToGrid(x, m_gridDivision);
    }

    x = qBound(0, x, kLaneWidth);

    Note note(beat, num, den, x);

    if (m_timeDivision > 0)
    {
        note = MathUtils::snapNoteToTimeWithBoundary(note, m_timeDivision);
    }
    else
    {
        note = MathUtils::snapNoteToTimeWithBoundary(note, 1);
    }

    return note;
}

QRectF ChartCanvas::getRainNoteRect(const Note &note) const
{
    if (!chart())
        return QRectF();

    const auto &bpmList = chart()->bpmList();
    int offset = chart()->meta().offset;

    double startTime = MathUtils::beatToMs(note.beatNum, note.numerator, note.denominator, bpmList, offset);
    double endTime = MathUtils::beatToMs(note.endBeatNum, note.endNumerator, note.endDenominator, bpmList, offset);

    double yStart = yPosFromTime(startTime);
    double yEnd = yPosFromTime(endTime);

    double rectTop = qMin(yStart, yEnd);
    double rectHeight = qAbs(yEnd - yStart);

    int lmargin = leftMargin();
    int rmargin = rightMargin();
    double rainWidth = qMax(1, width() - lmargin - rmargin);

    return QRectF(lmargin, rectTop, rainWidth, rectHeight);
}

int ChartCanvas::hitTestNote(const QPointF &pos) const
{
    if (!chart())
        return -1;

    const auto &notes = chart()->notes();
    int noteSize = m_noteRenderer->getNoteSize();
    double minDist = noteSize * 0.6;
    int hit = -1;

    for (int i = 0; i < notes.size(); ++i)
    {
        const Note &note = notes[i];
        if (note.type == NoteType::SOUND)
            continue;

        if (note.type == NoteType::RAIN)
        {
            QRectF rainRect = getRainNoteRect(note);
            if (rainRect.contains(pos))
                return i;
        }
        else
        {
            QPointF notePos = noteToPos(note);
            double dist = QLineF(notePos, pos).length();
            if (dist < minDist)
            {
                minDist = dist;
                hit = i;
            }
        }
    }
    return hit;
}



// --- 范围选择覆盖层 ---

void ChartCanvas::setRangeOverlay(double startBeat, double endBeat)
{
    m_rangeStartBeat = startBeat;
    m_rangeEndBeat = endBeat;
    m_rangeOverlayValid = true;
    update();
}

void ChartCanvas::clearRangeOverlay()
{
    m_rangeOverlayValid = false;
    update();
}

void ChartCanvas::setRangeOverlayVisible(bool visible)
{
    m_rangeOverlayVisible = visible;
    update();
}

double ChartCanvas::snapBeatToTimeDivision(double beat) const
{
    if (m_timeDivision <= 0) return beat;
    return std::round(beat * m_timeDivision) / m_timeDivision;
}

int ChartCanvas::hitTestRangeHandle(const QPointF &pos) const
{
    if (!m_rangeOverlayValid || !m_rangeOverlayVisible)
        return 0;

    double startY = beatToY(m_rangeStartBeat);
    double endY = beatToY(m_rangeEndBeat);

    constexpr double kHandleHitRadius = 12.0;
    int lmargin = const_cast<ChartCanvas *>(this)->leftMargin();

    if (std::abs(pos.y() - startY) <= kHandleHitRadius && pos.x() >= lmargin - 4 && pos.x() <= lmargin + 28)
        return 1;

    if (std::abs(pos.y() - endY) <= kHandleHitRadius && pos.x() >= lmargin - 4 && pos.x() <= lmargin + 28)
        return 2;

    return 0;
}

void ChartCanvas::drawRangeOverlay(QPainter &painter, int lmargin, int rmargin, int canvasHeight)
{
    Q_UNUSED(rmargin);
    Q_UNUSED(canvasHeight);

    if (!m_rangeOverlayValid)
        return;

    double startY = beatToY(m_rangeStartBeat);
    double endY = beatToY(m_rangeEndBeat);

    QColor lineColor(68, 136, 170, 204);
    QPen linePen(lineColor, 4);
    linePen.setCapStyle(Qt::RoundCap);
    painter.setPen(linePen);

    int canvasWidth = width();
    int drawnWidth = canvasWidth - lmargin - rmargin;

    painter.drawLine(lmargin, static_cast<int>(startY), lmargin + drawnWidth, static_cast<int>(startY));
    painter.drawLine(lmargin, static_cast<int>(endY), lmargin + drawnWidth, static_cast<int>(endY));

    painter.setBrush(lineColor);
    painter.setPen(Qt::NoPen);

    constexpr double kHandleWidth = 20.0;
    constexpr double kHandleHalfHeight = 6.0;
    QRectF startHandle(lmargin, startY - kHandleHalfHeight, kHandleWidth, kHandleHalfHeight * 2);
    QRectF endHandle(lmargin, endY - kHandleHalfHeight, kHandleWidth, kHandleHalfHeight * 2);
    painter.drawRect(startHandle);
    painter.drawRect(endHandle);
}

void ChartCanvas::drawRangeSelectionHighlight(QPainter &painter, int lmargin, int availableWidth, int canvasHeight)
{
    if (!chart() || !m_selectionController)
        return;

    double visibleStart = m_scrollBeat;
    double visibleEnd = visibleStart + effectiveVisibleBeatRange();

    QSet<int> selectedSet = m_selectionController->selectedIndices();
    const QVector<Note> &notes = chart()->notes();

    QColor highlightColor(68, 136, 170, 153);

    painter.save();
    double invVisibleRange = 1.0 / effectiveVisibleBeatRange();
    double baseY = m_verticalFlip ? canvasHeight : 0;
    double sign = m_verticalFlip ? -1.0 : 1.0;

    for (int i = 0; i < notes.size(); ++i)
    {
        const Note &note = notes[i];
        if (note.type == NoteType::SOUND)
            continue;
        if (selectedSet.contains(i))
            continue;

        double noteStart = note.getStartBeat();

        bool inRange = false;
        if (note.isRain)
        {
            double noteEnd = note.getEndBeat();
            inRange = (noteStart >= m_rangeStartBeat && noteEnd <= m_rangeEndBeat);
        }
        else
        {
            inRange = (noteStart >= m_rangeStartBeat && noteStart <= m_rangeEndBeat);
        }

        if (!inRange)
            continue;

        if (note.isRain)
        {
            double noteEnd = note.getEndBeat();
            if (noteEnd <= visibleStart || noteStart >= visibleEnd)
                continue;
        }
        else
        {
            if (noteStart < visibleStart - 0.5 || noteStart > visibleEnd + 0.5)
                continue;
        }

        double y = baseY + sign * ((noteStart - m_scrollBeat) * invVisibleRange * canvasHeight);

        if (note.isRain)
        {
            double noteEnd = note.getEndBeat();
            double visEnd = qMin(noteEnd, visibleEnd);
            double yEnd = baseY + sign * ((visEnd - m_scrollBeat) * invVisibleRange * canvasHeight);
            double rectTop = qMin(y, yEnd);
            double rectHeight = qAbs(yEnd - y);
            if (rectHeight <= 0)
                continue;
            QRectF rainRect(lmargin, rectTop, availableWidth, rectHeight);
            painter.fillRect(rainRect, highlightColor);
        }
        else
        {
            double x = lmargin + (note.x / 512.0) * availableWidth;
            int noteSize = m_noteRenderer ? m_noteRenderer->getNoteSize() : 24;
            QRectF noteRect(x - noteSize / 2.0, y - noteSize / 2.0, noteSize, noteSize);
            painter.fillRect(noteRect, highlightColor);
        }
    }

    painter.restore();
}
