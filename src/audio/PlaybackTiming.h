#pragma once

#include <cmath>

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
}
