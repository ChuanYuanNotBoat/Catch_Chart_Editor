// src/coord/BeatLinearMapper.h
#pragma once

#include "coord/CoordinateMapper.h"

// BeatLinear coordinate mapper.
// Y is proportional to beat position. Each beat occupies the same pixel height.
// This is the default mode compatible with legacy charts.
class BeatLinearMapper : public CoordinateMapper
{
public:
    Mode mode() const override { return Mode::BeatLinear; }

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

    // === BeatLinear-specific ===
    double baseVisibleBeatRange() const { return m_baseVisibleBeatRange; }
    void setBaseVisibleBeatRange(double range) { m_baseVisibleBeatRange = range; }

private:
    double m_baseVisibleBeatRange = 10.0;
};