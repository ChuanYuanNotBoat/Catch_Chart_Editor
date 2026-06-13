// src/coord/BeatLinearMapper.cpp
#include "coord/BeatLinearMapper.h"
#include "utils/MathUtils.h"
#include <QtGlobal>
#include <algorithm>
#include <cmath>

// === Read-only coordinate conversions ===

double BeatLinearMapper::beatToY(double beat, double scrollBeat, const CoordContext &ctx) const
{
    double visibleRange = effectiveVisibleBeatRange(ctx);
    if (visibleRange <= 0)
        return 0;
    double y = (beat - scrollBeat) / visibleRange * ctx.canvasHeight;
    if (ctx.verticalFlip)
        y = ctx.canvasHeight - y;
    return y;
}

double BeatLinearMapper::yToBeat(double y, double scrollBeat, const CoordContext &ctx) const
{
    if (ctx.canvasHeight <= 0)
        return scrollBeat;
    if (ctx.verticalFlip)
        y = ctx.canvasHeight - y;
    return scrollBeat + (y / ctx.canvasHeight) * effectiveVisibleBeatRange(ctx);
}

double BeatLinearMapper::timeToY(double timeMs, double scrollBeat, const CoordContext &ctx) const
{
    if (!ctx.fullBpmCache || ctx.fullBpmCache->isEmpty())
        return 0;

    int scrollBN, scrollN, scrollD;
    MathUtils::floatToBeat(scrollBeat, scrollBN, scrollN, scrollD);
    double scrollMs = MathUtils::beatToMs(scrollBN, scrollN, scrollD, *ctx.fullBpmCache);

    double visRange = effectiveVisibleBeatRange(ctx);
    double scrollBpm = MathUtils::lookupBpmAtBeat(scrollBeat, *ctx.fullBpmCache);
    double pixelsPerBeat = (visRange > 0) ? (ctx.canvasHeight / visRange) : 1.0;
    double pixelsPerMs = (scrollBpm > 0) ? (pixelsPerBeat * scrollBpm / 60000.0) : (pixelsPerBeat / 1000.0);

    double y = (timeMs - scrollMs) * pixelsPerMs;
    if (ctx.verticalFlip)
        y = ctx.canvasHeight - y;
    return y;
}

double BeatLinearMapper::yToTime(double y, double scrollBeat, const CoordContext &ctx) const
{
    if (!ctx.fullBpmCache || ctx.fullBpmCache->isEmpty())
        return 0;

    double beat = yToBeat(y, scrollBeat, ctx);
    int beatNum, num, den;
    MathUtils::floatToBeat(beat, beatNum, num, den);
    return MathUtils::beatToMs(beatNum, num, den, *ctx.fullBpmCache);
}

// === Visible range ===

double BeatLinearMapper::effectiveVisibleBeatRange(const CoordContext &ctx) const
{
    return (ctx.timeScale > 0) ? (m_baseVisibleBeatRange / ctx.timeScale) : m_baseVisibleBeatRange;
}

double BeatLinearMapper::visibleStartBeat(double scrollBeat, const CoordContext & /*ctx*/) const
{
    return scrollBeat;
}

double BeatLinearMapper::scrollTimeMs(double scrollBeat, const CoordContext &ctx) const
{
    if (!ctx.fullBpmCache || ctx.fullBpmCache->isEmpty())
        return 0;
    return MathUtils::beatToMs(scrollBeat, *ctx.fullBpmCache);
}

// === Mutating operations ===

void BeatLinearMapper::setTimeScale(double newScale, double &scrollBeat, const CoordContext &ctx)
{
    const double baselineRatio = kReferenceLineRatio;
    const double oldEffectiveRange = effectiveVisibleBeatRange(ctx);
    const double refOffset = ctx.verticalFlip
                                 ? (1.0 - baselineRatio) * oldEffectiveRange
                                 : baselineRatio * oldEffectiveRange;
    const double baselineBeat = scrollBeat + refOffset;

    // Compute new scrollBeat using the new scale (not yet applied to ctx.timeScale).
    const double newEffectiveRange = (newScale > 0) ? (m_baseVisibleBeatRange / newScale) : m_baseVisibleBeatRange;
    const double newRefOffset = ctx.verticalFlip
                                    ? (1.0 - baselineRatio) * newEffectiveRange
                                    : baselineRatio * newEffectiveRange;
    scrollBeat = baselineBeat - newRefOffset;

    const double limit = scrollLimitFromRange(newEffectiveRange, ctx);
    if (scrollBeat < limit)
        scrollBeat = limit;
}

void BeatLinearMapper::advancePlayback(double currentTimeMs, double baselineRatio,
                                        double &scrollBeat, const CoordContext &ctx)
{
    if (!ctx.fullBpmCache || ctx.fullBpmCache->isEmpty())
        return;

    const double beat = MathUtils::msToBeatFloat(currentTimeMs, *ctx.fullBpmCache);
    const double visRange = effectiveVisibleBeatRange(ctx);

    if (ctx.verticalFlip)
        scrollBeat = beat - (1.0 - baselineRatio) * visRange;
    else
        scrollBeat = beat - baselineRatio * visRange;

    clampScrollLimit(scrollBeat, ctx);
}

void BeatLinearMapper::syncFromBeat(double /*scrollBeat*/, const CoordContext & /*ctx*/)
{
    // BeatLinear: scrollBeat is the primary state, no derived state to update.
}

void BeatLinearMapper::syncFromTime(double & /*scrollBeat*/, const CoordContext & /*ctx*/)
{
    // BeatLinear: not applicable. scrollBeat is primary, time is derived.
    // This is only meaningful for TimeLinearMapper.
}

double BeatLinearMapper::negativeBeatScrollLimit(double /*scrollBeat*/, const CoordContext &ctx) const
{
    const double visibleRange = effectiveVisibleBeatRange(ctx);
    return scrollLimitFromRange(visibleRange, ctx);
}

void BeatLinearMapper::clampScrollLimit(double &scrollBeat, const CoordContext &ctx)
{
    const double limit = negativeBeatScrollLimit(scrollBeat, ctx);
    if (scrollBeat < limit)
        scrollBeat = limit;
}

void BeatLinearMapper::adoptFrom(const CoordinateMapper *other,
                                  double scrollBeat, const CoordContext &ctx)
{
    if (!other || other->mode() == Mode::BeatLinear)
        return;

    // Adopt from TimeLinear: compute m_baseVisibleBeatRange from the other mapper's visible beat range.
    const double beatRange = other->effectiveVisibleBeatRange(ctx);
    m_baseVisibleBeatRange = beatRange * ctx.timeScale;
}