// src/coord/TimeLinearMapper.h
#pragma once

#include "coord/CoordinateMapper.h"

// TimeLinear coordinate mapper.
// Y is proportional to milliseconds. Supports variable BPM charts where
// each beat may have a different duration. Beat height varies with BPM.
class TimeLinearMapper : public CoordinateMapper
{
public:
    Mode mode() const override { return Mode::TimeLinear; }

    // === Read-only coordinate conversions ===
    double beatToY(double beat, double scrollBeat, const CoordContext &ctx) const override;
    double yToBeat(double y, double scrollBeat, const CoordContext &ctx) const override;
    double timeToY(double timeMs, double scrollBeat, const CoordContext &ctx) const override;
    double yToTime(double y, double scrollBeat, const CoordContext &ctx) const override;

    // === Visible range ===
    double effectiveVisibleBeatRange(const CoordContext &ctx) const override;
    double visibleStartBeat(double scrollBeat, const CoordContext &ctx) const override;
    double scrollTimeMs(double scrollBeat, const CoordContext &ctx) const override;

    // === Mutating operations ===
    void setTimeScale(double newScale, double &scrollBeat, const CoordContext &ctx) override;
    void advancePlayback(double currentTimeMs, double baselineRatio,
                         double &scrollBeat, const CoordContext &ctx) override;
    void syncFromBeat(double scrollBeat, const CoordContext &ctx) override;
    void syncFromTime(double &scrollBeat, const CoordContext &ctx) override;
    double negativeBeatScrollLimit(double scrollBeat, const CoordContext &ctx) const override;
    void clampScrollLimit(double &scrollBeat, const CoordContext &ctx) override;
    void adoptFrom(const CoordinateMapper *other,
                   double scrollBeat, const CoordContext &ctx) override;

    // === TimeLinear-specific ===
    double visibleTimeRangeMs() const { return m_visibleTimeRangeMs; }
    double scrollTimeMsRaw() const { return m_scrollTimeMs; }

private:
    double m_scrollTimeMs = 0.0;
    double m_visibleTimeRangeMs = 1000.0;
};