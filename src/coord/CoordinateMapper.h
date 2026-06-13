// src/coord/CoordinateMapper.h
#pragma once

#include <QVector>
#include <cmath>
#include <algorithm>
#include "utils/MathUtils.h"

// Lightweight context passed to mapper methods.
// Carries shared read-only state from ChartCanvas.
struct CoordContext
{
    int canvasHeight;
    double timeScale;
    bool verticalFlip;
    const QVector<MathUtils::BpmCacheEntry> *bpmCache;     // filtered cache (excludes applied)
    const QVector<MathUtils::BpmCacheEntry> *fullBpmCache;  // full cache (all BPMs)
    double baseBpm;
    int offsetMs; // chart audio offset
};

class CoordinateMapper
{
public:
    enum class Mode
    {
        BeatLinear, // Y proportional to beat (default, legacy charts)
        TimeLinear  // Y proportional to milliseconds (variable BPM)
    };

    static constexpr double kReferenceLineRatio = 0.8;

    virtual ~CoordinateMapper() = default;
    virtual Mode mode() const = 0;

    // === Coordinate conversions (read-only) ===

    // Map a beat position to screen Y coordinate.
    virtual double beatToY(double beat, double scrollBeat, const CoordContext &ctx) const = 0;

    // Map a screen Y coordinate to beat position.
    virtual double yToBeat(double y, double scrollBeat, const CoordContext &ctx) const = 0;

    // Map a time (ms) to screen Y coordinate.
    virtual double timeToY(double timeMs, double scrollBeat, const CoordContext &ctx) const = 0;

    // Map a screen Y coordinate to time (ms).
    virtual double yToTime(double y, double scrollBeat, const CoordContext &ctx) const = 0;

    // === Visible range ===

    // Return the visible beat range for the current state.
    virtual double effectiveVisibleBeatRange(const CoordContext &ctx) const = 0;

    // Return the beat at the top of the visible viewport.
    virtual double visibleStartBeat(double scrollBeat, const CoordContext &ctx) const = 0;

    // Return the scroll time in milliseconds.
    virtual double scrollTimeMs(double scrollBeat, const CoordContext &ctx) const = 0;

    // === Mutating operations ===

    // Zoom: adjust timeScale while keeping the reference line beat position fixed.
    // Updates scrollBeat in place. Caller must update ctx.timeScale afterwards.
    virtual void setTimeScale(double newScale, double &scrollBeat, const CoordContext &ctx) = 0;

    // Playback auto-scroll: update scroll position to follow playback time.
    // Updates scrollBeat in place. Caller handles scroll signal emission.
    virtual void advancePlayback(double currentTimeMs, double baselineRatio,
                                 double &scrollBeat, const CoordContext &ctx) = 0;

    // Sync mapper's time state from the current scrollBeat.
    virtual void syncFromBeat(double scrollBeat, const CoordContext &ctx) = 0;

    // Sync scrollBeat from the mapper's time state.
    virtual void syncFromTime(double &scrollBeat, const CoordContext &ctx) = 0;

    // Compute the minimum allowed scrollBeat (negative scroll limit).
    virtual double negativeBeatScrollLimit(double scrollBeat, const CoordContext &ctx) const = 0;

    // Clamp scrollBeat to the negative scroll limit and sync if needed.
    virtual void clampScrollLimit(double &scrollBeat, const CoordContext &ctx) = 0;

    // Adopt state from another mapper (used during mode switch).
    virtual void adoptFrom(const CoordinateMapper *other,
                           double scrollBeat, const CoordContext &ctx) = 0;

protected:
    // Shared helper: compute scroll limit from a given visible range.
    double scrollLimitFromRange(double visibleRange, const CoordContext &ctx) const
    {
        const double absOffsetBeat = std::abs(ctx.offsetMs) * ctx.baseBpm / 60000.0;
        const int reservedBeats = std::max(1, static_cast<int>(std::ceil(absOffsetBeat)) + 1);
        const double refOffset = ctx.verticalFlip
                                     ? (1.0 - kReferenceLineRatio) * visibleRange
                                     : kReferenceLineRatio * visibleRange;
        return -static_cast<double>(reservedBeats) - refOffset;
    }
};