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
                                                     bool allowHardResync,
                                                     double observationGranularityMs = 0.0)
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
    // A processed-frame timestamp is not an instantaneous device clock.
    // At low playback rates a single PCM frame can span almost a second of
    // wall time. Correct only error outside its measured media-time bounds.
    const double uncertainty = std::isfinite(observationGranularityMs)
                                   ? std::max(0.0, observationGranularityMs)
                                   : 0.0;
    const double boundedObservation = std::clamp(
        predicted, std::max(0.0, observed - uncertainty), observed + uncertainty);
    const double errorMs = boundedObservation - predicted;
    const double absoluteErrorMs = std::abs(errorMs);
    const double hardResyncMs = std::max(50.0, 120.0 * rate);
    if (allowHardResync && absoluteErrorMs >= hardResyncMs)
        return {boundedObservation, 0.0, true};

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
