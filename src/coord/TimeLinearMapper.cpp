// src/coord/TimeLinearMapper.cpp
#include "coord/TimeLinearMapper.h"
#include "coord/BeatLinearMapper.h"
#include "utils/MathUtils.h"
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <limits>

// === Read-only coordinate conversions ===

double TimeLinearMapper::beatToY(double beat, double scrollBeat, const CoordContext &ctx) const
{
    if (!ctx.fullBpmCache || ctx.fullBpmCache->isEmpty())
        return 0;

    // Convert beat to time, then to Y using pixelsPerMs.
    const double noteTimeMs = MathUtils::beatToMs(beat, *ctx.fullBpmCache);
    return timeToY(noteTimeMs, scrollBeat, ctx);
}

double TimeLinearMapper::yToBeat(double y, double scrollBeat, const CoordContext &ctx) const
{
    if (!ctx.bpmCache || ctx.bpmCache->isEmpty())
        return scrollBeat;

    double ms = yToTime(y, scrollBeat, ctx);
    return MathUtils::msToBeatFloat(ms, *ctx.bpmCache);
}

double TimeLinearMapper::timeToY(double timeMs, double /*scrollBeat*/, const CoordContext &ctx) const
{
    const double pixelsPerMs = (m_visibleTimeRangeMs > 0)
                                   ? (ctx.canvasHeight / m_visibleTimeRangeMs)
                                   : 1.0;

    double y = (timeMs - m_scrollTimeMs) * pixelsPerMs;
    if (ctx.verticalFlip)
        y = ctx.canvasHeight - y;
    return y;
}

double TimeLinearMapper::yToTime(double y, double /*scrollBeat*/, const CoordContext &ctx) const
{
    const double pixelsPerMs = (m_visibleTimeRangeMs > 0)
                                   ? (ctx.canvasHeight / m_visibleTimeRangeMs)
                                   : 1.0;

    double relY = ctx.verticalFlip ? (ctx.canvasHeight - y) : y;
    return m_scrollTimeMs + relY / pixelsPerMs;
}

// === Visible range ===

double TimeLinearMapper::effectiveVisibleBeatRange(const CoordContext &ctx) const
{
    if (!ctx.bpmCache || ctx.bpmCache->isEmpty())
    {
        // Fallback when BPM cache unavailable
        if (m_visibleTimeRangeMs <= 0)
            return qMax(1e-6, m_visibleTimeRangeMs * (120.0 / 60000.0));
        return m_visibleTimeRangeMs * (120.0 / 60000.0);
    }

    if (m_visibleTimeRangeMs <= 0)
        return qMax(1e-6, m_visibleTimeRangeMs * (ctx.baseBpm / 60000.0));

    double startBeat = MathUtils::msToBeatFloat(m_scrollTimeMs, *ctx.bpmCache);
    double endBeat = MathUtils::msToBeatFloat(m_scrollTimeMs + m_visibleTimeRangeMs, *ctx.bpmCache);
    return qMax(1e-6, endBeat - startBeat);
}

double TimeLinearMapper::visibleStartBeat(double /*scrollBeat*/, const CoordContext &ctx) const
{
    if (!ctx.fullBpmCache || ctx.fullBpmCache->isEmpty())
        return 0;
    // Use m_scrollTimeMs directly for precision (avoids round-trip through beat).
    return MathUtils::msToBeatFloat(m_scrollTimeMs, *ctx.fullBpmCache);
}

double TimeLinearMapper::scrollTimeMs(double /*scrollBeat*/, const CoordContext & /*ctx*/) const
{
    return m_scrollTimeMs;
}

// === Mutating operations ===

void TimeLinearMapper::setTimeScale(double newScale, double &scrollBeat, const CoordContext &ctx)
{
    if (!ctx.fullBpmCache || ctx.fullBpmCache->isEmpty())
        return;

    const double baselineRatio = kReferenceLineRatio;
    const auto &cache = *ctx.fullBpmCache;

    // Compute reference beat and its time from current state.
    double refBeat = ctx.verticalFlip
                         ? scrollBeat + (1.0 - baselineRatio) * effectiveVisibleBeatRange(ctx)
                         : scrollBeat + baselineRatio * effectiveVisibleBeatRange(ctx);
    double refTimeMs = MathUtils::beatToMs(refBeat, cache);

    // Scale visibleTimeRangeMs: scale up → range shrinks.
    double factor = (ctx.timeScale > 0) ? (ctx.timeScale / newScale) : 1.0;
    m_visibleTimeRangeMs = qMax(1.0, m_visibleTimeRangeMs * factor);

    // Adjust scrollTimeMs to keep reference position fixed.
    m_scrollTimeMs = refTimeMs - baselineRatio * m_visibleTimeRangeMs;

    // Update scrollBeat from new time state.
    scrollBeat = MathUtils::msToBeatFloat(m_scrollTimeMs, cache);

    // Apply scroll limit.
    clampScrollLimit(scrollBeat, ctx);
}

void TimeLinearMapper::advancePlayback(double currentTimeMs, double baselineRatio,
                                        double &scrollBeat, const CoordContext &ctx)
{
    if (!ctx.fullBpmCache || ctx.fullBpmCache->isEmpty())
        return;

    // Direct time-based scrolling for uniform velocity at BPM boundaries.
    double targetScrollTimeMs = currentTimeMs
        - (ctx.verticalFlip ? (1.0 - baselineRatio) : baselineRatio) * m_visibleTimeRangeMs;

    m_scrollTimeMs = targetScrollTimeMs;
    scrollBeat = MathUtils::msToBeatFloat(m_scrollTimeMs, *ctx.fullBpmCache);

    // Apply scroll limit.
    clampScrollLimit(scrollBeat, ctx);
}

void TimeLinearMapper::syncFromBeat(double scrollBeat, const CoordContext &ctx)
{
    // Update m_scrollTimeMs from scrollBeat.
    if (!ctx.fullBpmCache || ctx.fullBpmCache->isEmpty())
    {
        m_scrollTimeMs = 0;
        return;
    }
    m_scrollTimeMs = MathUtils::beatToMs(scrollBeat, *ctx.fullBpmCache);

    // Also update m_visibleTimeRangeMs from the visible beat range.
    const double endBeat = scrollBeat + effectiveVisibleBeatRange(ctx);
    const double endTime = MathUtils::beatToMs(endBeat, *ctx.fullBpmCache);
    m_visibleTimeRangeMs = endTime - m_scrollTimeMs;
}

void TimeLinearMapper::syncFromTime(double &scrollBeat, const CoordContext &ctx)
{
    // Update scrollBeat from m_scrollTimeMs.
    if (!ctx.fullBpmCache || ctx.fullBpmCache->isEmpty())
    {
        scrollBeat = 0;
        return;
    }
    scrollBeat = MathUtils::msToBeatFloat(m_scrollTimeMs, *ctx.fullBpmCache);

    // Also update m_baseVisibleBeatRange equivalent in BeatLinear if needed.
    // (This is handled by the caller through adoptFrom.)
}

double TimeLinearMapper::negativeBeatScrollLimit(double scrollBeat, const CoordContext &ctx) const
{
    // Use the effective beat range for the limit calculation.
    const double visibleRange = effectiveVisibleBeatRange(ctx);
    return scrollLimitFromRange(visibleRange, ctx);
}

void TimeLinearMapper::clampScrollLimit(double &scrollBeat, const CoordContext &ctx)
{
    const double limit = negativeBeatScrollLimit(scrollBeat, ctx);
    if (scrollBeat < limit)
    {
        scrollBeat = limit;

        // Update m_scrollTimeMs to match the clamped position.
        if (ctx.fullBpmCache && !ctx.fullBpmCache->isEmpty())
            m_scrollTimeMs = MathUtils::beatToMs(scrollBeat, *ctx.fullBpmCache);
    }
}

void TimeLinearMapper::adoptFrom(const CoordinateMapper *other,
                                  double scrollBeat, const CoordContext &ctx)
{
    if (!other || other->mode() == Mode::TimeLinear)
        return;

    // Adopt from BeatLinear: compute m_scrollTimeMs and m_visibleTimeRangeMs.
    if (!ctx.fullBpmCache || ctx.fullBpmCache->isEmpty())
    {
        m_scrollTimeMs = 0;
        m_visibleTimeRangeMs = 1000.0;
        return;
    }

    m_scrollTimeMs = MathUtils::beatToMs(scrollBeat, *ctx.fullBpmCache);

    // Compute m_visibleTimeRangeMs from BeatLinear's effective beat range.
    const double beatRange = other->effectiveVisibleBeatRange(ctx);
    const double endBeat = scrollBeat + beatRange;
    const double endTime = MathUtils::beatToMs(endBeat, *ctx.fullBpmCache);
    m_visibleTimeRangeMs = qMax(1.0, endTime - m_scrollTimeMs);
}