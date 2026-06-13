#include "ChartCanvas.h"
#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "controller/PlaybackController.h"
#include "render/NoteRenderer.h"
#include "render/GridRenderer.h"
#include "render/BackgroundRenderer.h"
#include "render/HyperfruitDetector.h"
#include "app/Application.h"
#include "plugin/PluginManager.h"
#include "utils/MathUtils.h"
#include "utils/Settings.h"
#include "utils/DiagnosticCollector.h"
#include "utils/Logger.h"
#include "model/Chart.h"
#include <QPainter>
#include <QPen>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <QCoreApplication>

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

void ChartCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

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

    if (m_isPlaying && m_playbackController && m_lastPlaybackTargetTimeMs >= 0.0)
    {
        const qint64 nowNs = m_playbackVisualClock.nsecsElapsed();
        double visualTimeMs = m_lastPlaybackTargetTimeMs;
        if (m_lastPlaybackTickNs > 0 && nowNs > m_lastPlaybackTickNs)
        {
            const double dtMs = static_cast<double>(nowNs - m_lastPlaybackTickNs) / 1000000.0;
            visualTimeMs += dtMs * m_playbackController->speed();
        }
        m_currentPlayTime = qMax(0.0, visualTimeMs);
        advancePlaybackVisual(false, false);
    }

    const Chart *currentChart = chart();
    const auto &bpmList = currentChart->bpmList();
    const auto &notes = currentChart->notes();

    drawBackground(painter);
    drawGrid(painter);

    // Build mapper context once for the entire paint pass
    const CoordContext mapperCtx = buildCoordContext();
    const bool useTimeLinear = (m_coordinateMode == CoordinateMode::TimeLinear);
    double visibleRange = effectiveVisibleBeatRange();
    double startBeat;
    if (useTimeLinear)
        startBeat = MathUtils::msToBeatFloat(m_scrollTimeMs, fullBpmTimeCache());
    else
        startBeat = m_scrollBeat;
    double endBeat = startBeat + visibleRange;

    if (m_hyperfruitEnabled && !bpmList.isEmpty())
    {
        if (!m_hyperCacheValid)
        {
            m_cachedHyperSet = m_hyperfruitDetector->detect(notes, bpmList, 0);
            m_hyperCacheValid = true;
        }
        m_noteRenderer->setHyperfruitIndices(m_cachedHyperSet);
    }

    const int canvasWidth = width();
    const int canvasHeight = height();
    const int lmargin = leftMargin();
    const int rmargin = rightMargin();
    const int availableWidth = qMax(1, canvasWidth - lmargin - rmargin);
    const double invVisibleRange = 1.0 / visibleRange;
    const double baseY = m_verticalFlip ? canvasHeight : 0;
    const double sign = m_verticalFlip ? -1.0 : 1.0;

    double scrollTimeMs;
    if (useTimeLinear)
    {
        // TimeLinear: 直接使用权威时间 m_scrollTimeMs，避免经过 m_scrollBeat（过滤缓存空间）
        // 的 round-trip 转换误差
        scrollTimeMs = m_scrollTimeMs;
    }
    else
    {
        int scrollBeatNum, scrollNum, scrollDen;
        MathUtils::floatToBeat(m_scrollBeat, scrollBeatNum, scrollNum, scrollDen);
        scrollTimeMs = MathUtils::beatToMs(scrollBeatNum, scrollNum, scrollDen, bpmList, currentChart->meta().offset);
    }

    // TimeLinear: 使用固定的 pixelsPerMs = canvasHeight / visibleTimeRangeMs
    const double pixelsPerMs = (m_visibleTimeRangeMs > 0) ? (canvasHeight / m_visibleTimeRangeMs) : 1.0;

    QSet<int> selectedSet;
    if (m_selectionController)
        selectedSet = m_selectionController->selectedIndices();

    // 绘制排除BPM范围的橙色矩形背景（仅当排除项不参与渲染时）
    if (m_excludeRenderingEnabled && m_hasExcludedBpms)
    {
        drawExcludedRangeBackgrounds(painter, startBeat, endBeat,
                                     baseY, sign, invVisibleRange,
                                     canvasHeight, lmargin, availableWidth,
                                     scrollTimeMs, pixelsPerMs,
                                     useTimeLinear);
    }

    painter.setClipRect(rect());

    int renderedNotesCount = 0;

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

        double y = m_mapper->beatToY(beat, m_scrollBeat, mapperCtx);

        if (type == NoteType::RAIN)
        {
            double visibleStartBeat = qMax(beat, startBeat);
            double visibleEndBeat = qMin(endBeatNote, endBeat);
            double yStart = m_mapper->beatToY(visibleStartBeat, m_scrollBeat, mapperCtx);
            double yEnd = m_mapper->beatToY(visibleEndBeat, m_scrollBeat, mapperCtx);
            double rectTop = qMin(yStart, yEnd);
            double rectHeight = qAbs(yEnd - yStart);
            if (rectHeight <= 0)
                return;
            QRectF rainRect(lmargin, rectTop, availableWidth, rectHeight);
            bool selected = selectedSet.contains(i);
            m_noteRenderer->drawRain(painter, notes[i], rainRect, selected);
            renderedNotesCount++;
        }
        else
        {
            double x = lmargin + m_noteXPositions[i] * availableWidth;
            QPointF pos(x, y);
            bool selected = selectedSet.contains(i);
            m_noteRenderer->drawNote(painter, notes[i], pos, selected, i);
            renderedNotesCount++;
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

    if (m_isPasting && !m_pasteNotes.isEmpty())
        drawPastePreview(painter, canvasHeight, lmargin, availableWidth, invVisibleRange, baseY, sign);

    if (m_mirrorPreviewVisible)
        drawMirrorPreview(painter, canvasHeight, lmargin, availableWidth, invVisibleRange, baseY, sign);

    if (m_mirrorGuideVisible)
        drawMirrorGuide(painter, canvasHeight, lmargin, availableWidth);

    double baselineY = canvasHeight * kReferenceLineRatio;
    painter.setPen(QPen(QColor(0, 0, 255), 3));
    painter.drawLine(lmargin, baselineY, canvasWidth - rmargin, baselineY);

    if (m_playbackController && m_currentPlayTime > 0)
    {
        painter.setPen(Qt::black);
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

    painter.setPen(Qt::white);
    painter.setBrush(QColor(0, 0, 0, 128));
    QString fpsText = QString("FPS: %1").arg(m_currentFps, 0, 'f', 1);
    QRect fpsRect(10, canvasHeight - 30, 80, 20);
    painter.fillRect(fpsRect, QColor(0, 0, 0, 128));
    painter.drawText(fpsRect, Qt::AlignCenter, fpsText);

    auto now = std::chrono::high_resolution_clock::now();
    static auto lastRecord = now;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRecord).count();
    if (elapsedMs > 500)
    {
        DiagnosticCollector::instance().recordRenderMetrics(elapsedMs, renderedNotesCount);
        lastRecord = now;
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
        baseOriginalTime = std::numeric_limits<double>::max();
        for (double t : sourceOriginalTimes)
        {
            if (std::isfinite(t) && t < baseOriginalTime)
                baseOriginalTime = t;
        }
    }
    if (baseOriginalTime != std::numeric_limits<double>::max())
    {
        const QVector<MathUtils::BpmCacheEntry> &previewBpmCache = fullBpmTimeCache();
        auto previewBeatFromTimeMs = [&previewBpmCache](double ms) -> double
        {
            return MathUtils::msToBeatFloat(ms, previewBpmCache);
        };
        const double baseOriginalBeat = previewBeatFromTimeMs(baseOriginalTime);
        const double referenceBeat = m_pasteAnchorBeat;
        const double baseBeatShift = referenceBeat - baseOriginalBeat;
        const double totalBeatShift = snapPasteTimeOffset(baseBeatShift + m_pasteTimeOffset);
        auto previewAssignBeatWithDen = [](double beatFloat, int targetDen, int &outBeatNum, int &outNum, int &outDen)
        {
            if (targetDen <= 0)
                targetDen = 1;
            const qint64 ticks = qRound64(beatFloat * targetDen);
            outBeatNum = static_cast<int>(ticks / targetDen);
            outNum = static_cast<int>(ticks % targetDen);
            if (outNum < 0)
            {
                outNum += targetDen;
                outBeatNum -= 1;
            }
            outDen = targetDen;
        };
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
            int previewBeatNum = 0, previewNum = 0, previewDen = 1;
            const int targetPreviewDen = Settings::instance().pasteUse288Division() ? 288 : qMax(1, note.denominator);
            const double originalBeatFloat = previewBeatFromTimeMs(originalTime);
            const double previewBeatFloat = originalBeatFloat + totalBeatShift;
            previewAssignBeatWithDen(previewBeatFloat, targetPreviewDen, previewBeatNum, previewNum, previewDen);
            const double beat = MathUtils::beatToFloat(previewBeatNum, previewNum, previewDen);
            const double y = baseY + sign * ((beat - m_scrollBeat) * invVisibleRange * canvasHeight);
            const int previewShiftedX = qBound(0, note.x + qRound(m_pasteXOffset), kLaneWidth);
            const double x = lmargin + (previewShiftedX / static_cast<double>(kLaneWidth)) * availableWidth;
            m_noteRenderer->drawNote(painter, note, QPointF(x, y), false, -1);
        }
    }

    painter.setOpacity(1.0);
    painter.fillRect(QRect(10, 10, 100, 30), QColor(200, 200, 200));
    painter.drawText(QRect(10, 10, 100, 30), Qt::AlignCenter, tr("Confirm"));
    painter.fillRect(QRect(120, 10, 100, 30), QColor(200, 200, 200));
    painter.drawText(QRect(120, 10, 100, 30), Qt::AlignCenter, tr("Cancel"));
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
            m_noteRenderer->drawRain(painter, mirrored, rainRect, false);
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
    PluginManager *pm = activePluginManager();
    if (!pm)
        return;
    if (!m_pluginToolModeActive)
        return;
    if (!m_pluginOverlayToggles.value("overlay_enabled", true).toBool())
        return;

    const bool isPlaying = m_playbackController &&
                           m_playbackController->state() == PlaybackController::Playing;
    const bool allowQueryInCurrentState = true;
    const int queryIntervalMs = m_pluginToolModeActive
                                    ? (isPlaying ? m_overlayPlaybackIntervalMs
                                                 : kOverlayQueryIntervalMsToolMode)
                                    : kOverlayQueryIntervalMsIdle;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool canQuery = nowMs >= m_overlayQueryBlockedUntilMs;
    const bool dueForQuery =
        (m_lastOverlayQueryMs == 0) ||
        (nowMs - m_lastOverlayQueryMs >= queryIntervalMs);

    if (allowQueryInCurrentState && canQuery && dueForQuery)
    {
        QVariantMap overlayContext = buildPluginCanvasContext();
        overlayContext.insert("left_margin", lmargin);
        overlayContext.insert("right_margin", rmargin);
        overlayContext.insert("overlay_snapshot_requested_at_ms", nowMs);

        QElapsedTimer requestTimer;
        requestTimer.start();
        m_overlayCache = pm->canvasOverlays(overlayContext);
        m_lastOverlayQueryMs = nowMs;

        const qint64 elapsedMs = requestTimer.elapsed();
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
    }

    QList<PluginInterface::CanvasOverlayItem> drawItems = m_overlayCache;

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


        const QVector<MathUtils::BpmCacheEntry> &bpmCache = bpmTimeCache();
        if (bpmCache.isEmpty())
        {
            invalidateGridCache();
            return;
        }

        const int viewportHeight = qMax(1, rect.height());

        const bool colorEnabled = Settings::instance().timelineDivisionColorEnabled();
        const QString colorPreset = Settings::instance().timelineDivisionColorPreset();
        const QList<int> colorCustom = Settings::instance().timelineDivisionColorCustomDivisions();

        if (m_coordinateMode == CoordinateMode::BeatLinear)
        {
            // ===== BeatLinear 模式：每个 beat 等高，基于 beat 空间缓存 =====
            const double visibleBeatRange = effectiveVisibleBeatRange();
            const double startBeat = m_scrollBeat;
            const double endBeat = startBeat + visibleBeatRange;

            const double beatsPerPixel = visibleBeatRange / viewportHeight;
            const double quantStepBeat = qMax(0.01, beatsPerPixel * 24.0);
            const auto quantizeBeatDown = [quantStepBeat](double value) -> double
            {
                return std::floor(value / quantStepBeat) * quantStepBeat;
            };
            const double renderStartBeat = quantizeBeatDown(startBeat);
            const double renderEndBeat = renderStartBeat + visibleBeatRange;

            const int cachePadPx = qMax(8, static_cast<int>(std::ceil((quantStepBeat / visibleBeatRange) * viewportHeight)) + 2);
            const double cachePadBeat = beatsPerPixel * static_cast<double>(cachePadPx);
            const double cacheStartBeat = renderStartBeat - cachePadBeat;
            const double cacheEndBeat = renderEndBeat + cachePadBeat;
            const QSize cacheSize(rect.width(), viewportHeight + cachePadPx * 2);

            const bool needRebuild =
                !m_gridCacheValid ||
                m_gridCacheRect.size() != rect.size() ||
                m_gridCacheRect.topLeft() != rect.topLeft() ||
                m_gridCacheDivision != m_gridDivision ||
                m_gridCacheTimeDivision != m_timeDivision ||
                m_gridCacheVerticalFlip != m_verticalFlip ||
                m_gridCacheColorEnabled != colorEnabled ||
                m_gridCacheColorPreset != colorPreset ||
                m_gridCacheColorCustomDivisions != colorCustom ||
                m_gridCachePadPx != cachePadPx ||
                m_gridCacheMode != m_coordinateMode ||
                std::abs(m_gridCacheStartBeat - cacheStartBeat) > 1e-6 ||
                std::abs(m_gridCacheEndBeat - cacheEndBeat) > 1e-6;

            if (needRebuild)
            {
                if (cacheSize.width() <= 0 || cacheSize.height() <= 0)
                {
                    invalidateGridCache();
                    return;
                }

                m_gridCache = QPixmap(cacheSize);
                m_gridCache.fill(Qt::transparent);
                QPainter cachePainter(&m_gridCache);
                const QRect cacheRect(0, 0, cacheSize.width(), cacheSize.height());
                m_gridRenderer->drawGridBeatLinear(cachePainter, cacheRect, m_gridDivision,
                                                   cacheStartBeat, cacheEndBeat,
                                                   m_timeDivision, bpmCache,
                                                   m_verticalFlip,
                                                   colorEnabled,
                                                   colorPreset,
                                                   colorCustom);
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
                m_gridCacheMode = m_coordinateMode;
                m_gridCacheValid = true;
            }

            if (!m_gridCache.isNull())
            {
                const double shiftPx = (startBeat - renderStartBeat) / visibleBeatRange * viewportHeight;
                const double cacheTop = m_verticalFlip
                                            ? static_cast<double>(rect.top()) - m_gridCachePadPx + shiftPx
                                            : static_cast<double>(rect.top()) - m_gridCachePadPx - shiftPx;
                painter.save();
                painter.setClipRect(rect);
                painter.drawPixmap(QPointF(rect.left(), cacheTop), m_gridCache);
                painter.restore();
            }
        }
        else
        {
            // ===== TimeLinear 模式：基于时间的网格，BPM 感知 =====
            // TimeLinear 模式始终使用权威时间轴（基于完整缓存），
            // 排除项通过 isInExcludedRangeByTime 跳过，不改变时间基准
            double startTime = m_scrollTimeMs;
            double endTime = m_scrollTimeMs + m_visibleTimeRangeMs;

            const double rawSpanMs = qMax(1.0, endTime - startTime);
            const double msPerPixel = rawSpanMs / viewportHeight;
            // Quantize the backing cache in larger vertical chunks and compensate
            // draw position per-frame so playback scrolling can mostly reuse cache.
            const double quantStepMs = qMax(1.0, msPerPixel * 24.0);
            const auto quantizeDown = [quantStepMs](double value) -> double
            {
                return std::floor(value / quantStepMs) * quantStepMs;
            };
            const double renderStartTime = quantizeDown(startTime);
            const double renderEndTime = renderStartTime + rawSpanMs;

            const int cachePadPx = qMax(8, static_cast<int>(std::ceil((quantStepMs / rawSpanMs) * viewportHeight)) + 2);
            const double cachePadMs = msPerPixel * static_cast<double>(cachePadPx);
            const double cacheStartTime = renderStartTime - cachePadMs;
            const double cacheEndTime = renderEndTime + cachePadMs;
            const QSize cacheSize(rect.width(), viewportHeight + cachePadPx * 2);

            const bool needRebuild =
                !m_gridCacheValid ||
                m_gridCacheRect.size() != rect.size() ||
                m_gridCacheRect.topLeft() != rect.topLeft() ||
                m_gridCacheDivision != m_gridDivision ||
                m_gridCacheTimeDivision != m_timeDivision ||
                m_gridCacheVerticalFlip != m_verticalFlip ||
                m_gridCacheColorEnabled != colorEnabled ||
                m_gridCacheColorPreset != colorPreset ||
                m_gridCacheColorCustomDivisions != colorCustom ||
                m_gridCachePadPx != cachePadPx ||
                m_gridCacheMode != m_coordinateMode ||
                std::abs(m_gridCacheStartTime - cacheStartTime) > 0.05 ||
                std::abs(m_gridCacheEndTime - cacheEndTime) > 0.05;

            if (needRebuild)
            {
                if (cacheSize.width() <= 0 || cacheSize.height() <= 0)
                {
                    invalidateGridCache();
                    return;
                }

                m_gridCache = QPixmap(cacheSize);
                m_gridCache.fill(Qt::transparent);
                QPainter cachePainter(&m_gridCache);
                const QRect cacheRect(0, 0, cacheSize.width(), cacheSize.height());
                m_gridRenderer->drawGrid(cachePainter, cacheRect, m_gridDivision,
                                         cacheStartTime, cacheEndTime,
                                         m_timeDivision, bpmTimeCache(),
                                         m_verticalFlip,
                                         colorEnabled,
                                         colorPreset,
                                         colorCustom,
                                         &fullBpmTimeCache(),
                                         (m_excludeRenderingEnabled && m_hasExcludedBpms) ? &m_excludedBeatRanges : nullptr);
                m_gridCacheRect = rect;
                m_gridCacheStartTime = cacheStartTime;
                m_gridCacheEndTime = cacheEndTime;
                m_gridCacheDivision = m_gridDivision;
                m_gridCacheTimeDivision = m_timeDivision;
                m_gridCacheVerticalFlip = m_verticalFlip;
                m_gridCacheColorEnabled = colorEnabled;
                m_gridCacheColorPreset = colorPreset;
                m_gridCacheColorCustomDivisions = colorCustom;
                m_gridCachePadPx = cachePadPx;
                m_gridCacheMode = m_coordinateMode;
                m_gridCacheValid = true;
            }

            if (!m_gridCache.isNull())
            {
                const double shiftPx = (startTime - renderStartTime) / rawSpanMs * viewportHeight;
                const double cacheTop = m_verticalFlip
                                            ? static_cast<double>(rect.top()) - m_gridCachePadPx + shiftPx
                                            : static_cast<double>(rect.top()) - m_gridCachePadPx - shiftPx;
                painter.save();
                painter.setClipRect(rect);
                painter.drawPixmap(QPointF(rect.left(), cacheTop), m_gridCache);
                painter.restore();
            }
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
    const CoordContext ctx = buildCoordContext();
    return m_mapper->timeToY(timeMs, m_scrollBeat, ctx);
}

double ChartCanvas::beatToY(double beat) const
{
    const CoordContext ctx = buildCoordContext();
    return m_mapper->beatToY(beat, m_scrollBeat, ctx);
}

double ChartCanvas::yToBeat(double y) const
{
    const CoordContext ctx = buildCoordContext();
    return m_mapper->yToBeat(y, m_scrollBeat, ctx);
}

double ChartCanvas::yToTime(double y) const
{
    const CoordContext ctx = buildCoordContext();
    return m_mapper->yToTime(y, m_scrollBeat, ctx);
}

double ChartCanvas::timeToY(double timeMs) const
{
    const CoordContext ctx = buildCoordContext();
    return m_mapper->timeToY(timeMs, m_scrollBeat, ctx);
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



