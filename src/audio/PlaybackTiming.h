#pragma once

#include <cmath>
#include <algorithm>

namespace PlaybackTiming
{
// Convert a real output/scheduling duration to the equivalent duration on the
// media timeline. Playback positions are media-time values, not wall time.
inline double wallDurationToMediaMs(double wallDurationMs, double playbackRate)
{
    if (!std::isfinite(wallDurationMs))
        return 0.0;
    if (!std::isfinite(playbackRate) || playbackRate <= 0.0)
        playbackRate = 1.0;
    return wallDurationMs * playbackRate;
}

struct ClockAdjustment
{
    double anchorTimeMs = 0.0;
    double rateCorrection = 0.0;
    bool hardResync = false;
};

// Re-anchor without changing the time visible in the current frame. Small
// backend-clock errors are paid back through a tightly bounded rate slew. A
// hard resync is reserved for established playback; the first sample after
// play/seek may be coarse and must never teleport the presentation clock.
inline ClockAdjustment adjustClockTowardObservation(double predictedMs,
                                                     double observedMs,
                                                     double playbackRate,
                                                     double currentRateCorrection,
                                                     bool allowHardResync)
{
    constexpr double kSlewConvergenceWallMs = 1500.0;
    constexpr double kSlewMaxRateFraction = 0.005;
    constexpr double kSlewFilterGain = 0.15;

    const double rate = std::isfinite(playbackRate) && playbackRate > 0.0
                            ? playbackRate
                            : 1.0;
    const double predicted = std::isfinite(predictedMs) ? std::max(0.0, predictedMs) : 0.0;
    if (!std::isfinite(observedMs))
        return {predicted, 0.0, false};

    const double observed = std::max(0.0, observedMs);
    const double errorMs = observed - predicted;
    const double absoluteErrorMs = std::abs(errorMs);
    const double hardResyncMs = std::max(50.0, 120.0 * rate);
    if (allowHardResync && absoluteErrorMs >= hardResyncMs)
        return {observed, 0.0, true};

    const double deadZoneMs = std::max(0.15, 0.75 * rate);
    double targetRateCorrection = 0.0;
    if (absoluteErrorMs > deadZoneMs)
    {
        const double maxRateCorrection = std::max(1e-6, rate * kSlewMaxRateFraction);
        targetRateCorrection = std::clamp(
            errorMs / kSlewConvergenceWallMs,
            -maxRateCorrection,
            maxRateCorrection);
    }

    const double previousCorrection = std::isfinite(currentRateCorrection)
                                          ? currentRateCorrection
                                          : 0.0;
    double nextCorrection = previousCorrection +
                            (targetRateCorrection - previousCorrection) * kSlewFilterGain;
    if (std::abs(nextCorrection) < 1e-7)
        nextCorrection = 0.0;
    return {predicted, nextCorrection, false};
}
}
