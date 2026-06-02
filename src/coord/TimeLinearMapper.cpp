// src/coord/TimeLinearMapper.cpp
#include "coord/TimeLinearMapper.h"
#include "coord/BeatLinearMapper.h"
#include "utils/MathUtils.h"
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
const QVector<MathUtils::BpmCacheEntry> *renderCacheFor(const CoordContext &ctx)
{
    // 优先完整缓存：确保 beat↔ms 与 note 时间一致（note时间基于完整缓存）
    // 过滤缓存仅由 GridRenderer 独立传入，不经过此函数
    if (ctx.fullBpmCache && !ctx.fullBpmCache->isEmpty())
        return ctx.fullBpmCache;
    if (ctx.bpmCache && !ctx.bpmCache->isEmpty())
        return ctx.bpmCache;
    return nullptr;
}
}

// === Read-only coordinate conversions ===

double TimeLinearMapper::beatToY(double beat, double scrollBeat, const CoordContext &ctx) const
{
    const auto *cache = renderCacheFor(ctx);
    if (!cache)
        return 0;

    const double noteTimeMs = MathUtils::beatToMs(beat, *cache);
    return timeToY(noteTimeMs, scrollBeat, ctx);
}

double TimeLinearMapper::yToBeat(double y, double scrollBeat, const CoordContext &ctx) const
{
    const auto *cache = renderCacheFor(ctx);
    if (!cache)
        return scrollBeat;

    double ms = yToTime(y, scrollBeat, ctx);
    return MathUtils::msToBeatFloat(ms, *cache);
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
        if (m_visibleTimeRangeMs <= 0)
            return qMax(1e-6, std::abs(m_visibleTimeRangeMs) * (ctx.baseBpm / 60000.0));
        return m_visibleTimeRangeMs * (ctx.baseBpm / 60000.0);
    }

    if (m_visibleTimeRangeMs <= 0)
        return qMax(1e-6, std::abs(m_visibleTimeRangeMs) * (ctx.baseBpm / 60000.0));

    double startBeat = MathUtils::msToBeatFloat(m_scrollTimeMs, *ctx.bpmCache);
    double endBeat = MathUtils::msToBeatFloat(m_scrollTimeMs + m_visibleTimeRangeMs, *ctx.bpmCache);
    return qMax(1e-6, endBeat - startBeat);
}

double TimeLinearMapper::visibleStartBeat(double /*scrollBeat*/, const CoordContext &ctx) const
{
    const auto *cache = renderCacheFor(ctx);
    if (!cache)
        return 0;
    return MathUtils::msToBeatFloat(m_scrollTimeMs, *cache);
}

double TimeLinearMapper::scrollTimeMs(double /*scrollBeat*/, const CoordContext & /*ctx*/) const
{
    return m_scrollTimeMs;
}

// === Mutating operations ===

void TimeLinearMapper::setTimeScale(double newScale, double &scrollBeat, const CoordContext &ctx)
{
    const auto *cache = renderCacheFor(ctx);
    if (!cache)
        return;

    const double baselineRatio = kReferenceLineRatio;

    const double baselineOffsetRatio = ctx.verticalFlip ? (1.0 - baselineRatio) : baselineRatio;
    const double refBeat = scrollBeat + baselineOffsetRatio * effectiveVisibleBeatRange(ctx);
    const double refTimeMs = MathUtils::beatToMs(refBeat, *cache);

    // Scale up: the visible time range shrinks.
    const double factor = (ctx.timeScale > 0) ? (ctx.timeScale / newScale) : 1.0;
    m_visibleTimeRangeMs = qMax(1.0, m_visibleTimeRangeMs * factor);

    // Adjust scrollTimeMs to keep reference position fixed.
    m_scrollTimeMs = refTimeMs - baselineOffsetRatio * m_visibleTimeRangeMs;

    // Update scrollBeat from new time state.
    scrollBeat = MathUtils::msToBeatFloat(m_scrollTimeMs, *cache);

    // Apply scroll limit.
    clampScrollLimit(scrollBeat, ctx);
}

void TimeLinearMapper::advancePlayback(double currentTimeMs, double baselineRatio,
                                        double &scrollBeat, const CoordContext &ctx)
{
    const auto *cache = renderCacheFor(ctx);
    if (!cache)
        return;

    // 直接使用 currentTimeMs，不做 full→beat→render 的 round-trip。
    // round-trip 在排除BPM存在时会引入系统性偏移，导致流速随BPM变化。
    const double targetScrollTimeMs = currentTimeMs
        - (ctx.verticalFlip ? (1.0 - baselineRatio) : baselineRatio) * m_visibleTimeRangeMs;

    m_scrollTimeMs = targetScrollTimeMs;
    scrollBeat = MathUtils::msToBeatFloat(m_scrollTimeMs, *cache);

    // Apply scroll limit.
    clampScrollLimit(scrollBeat, ctx);
}

void TimeLinearMapper::syncFromBeat(double scrollBeat, const CoordContext &ctx)
{
    const auto *cache = renderCacheFor(ctx);
    if (!cache)
    {
        m_scrollTimeMs = 0;
        return;
    }
    m_scrollTimeMs = MathUtils::beatToMs(scrollBeat, *cache);

    // m_visibleTimeRangeMs 不修改 — TimeLinear 模式下它是权威状态，
    // 保持恒定的下落速度（pixelsPerMs = canvasHeight / m_visibleTimeRangeMs）。
    // 在可变BPM场景下，beat → ms → beat 的往返是有损的，
    // 从 scrollBeat 重新推导 visibleTimeRangeMs 会导致数值漂移。
}

void TimeLinearMapper::syncFromTime(double &scrollBeat, const CoordContext &ctx)
{
    const auto *cache = renderCacheFor(ctx);
    if (!cache)
    {
        scrollBeat = 0;
        return;
    }
    scrollBeat = MathUtils::msToBeatFloat(m_scrollTimeMs, *cache);

    // Also update m_baseVisibleBeatRange equivalent in BeatLinear if needed.
    // (This is handled by the caller through adoptFrom.)
}

void TimeLinearMapper::setScrollTimeMs(double scrollTimeMs, double &scrollBeat, const CoordContext &ctx)
{
    m_scrollTimeMs = scrollTimeMs;
    const auto *cache = renderCacheFor(ctx);
    if (!cache)
    {
        scrollBeat = 0;
        return;
    }

    scrollBeat = MathUtils::msToBeatFloat(m_scrollTimeMs, *cache);
}

void TimeLinearMapper::setScrollBeatPreservingRange(double scrollBeat, const CoordContext &ctx)
{
    const auto *cache = renderCacheFor(ctx);
    if (!cache)
    {
        m_scrollTimeMs = 0;
        return;
    }

    m_scrollTimeMs = MathUtils::beatToMs(scrollBeat, *cache);
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
        const auto *cache = renderCacheFor(ctx);
        if (cache)
            m_scrollTimeMs = MathUtils::beatToMs(scrollBeat, *cache);
    }
}

void TimeLinearMapper::adoptFrom(const CoordinateMapper *other,
                                  double scrollBeat, const CoordContext &ctx)
{
    if (!other || other->mode() == Mode::TimeLinear)
        return;

    // Adopt from BeatLinear: compute m_scrollTimeMs and m_visibleTimeRangeMs.
    const auto *cache = renderCacheFor(ctx);
    if (!cache)
    {
        m_scrollTimeMs = 0;
        m_visibleTimeRangeMs = 1000.0;
        return;
    }

    m_scrollTimeMs = MathUtils::beatToMs(scrollBeat, *cache);

    // Compute m_visibleTimeRangeMs from BeatLinear's effective beat range.
    const double beatRange = other->effectiveVisibleBeatRange(ctx);
    const double endBeat = scrollBeat + beatRange;
    const double endTime = MathUtils::beatToMs(endBeat, *cache);
    m_visibleTimeRangeMs = qMax(1.0, endTime - m_scrollTimeMs);
}
