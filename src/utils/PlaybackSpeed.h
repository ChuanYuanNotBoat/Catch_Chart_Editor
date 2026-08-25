#pragma once

#include <algorithm>
#include <cmath>

namespace PlaybackSpeed
{
inline constexpr double Minimum = 0.1;
inline constexpr double Maximum = 10.0;
inline constexpr double Default = 1.0;

inline double sanitize(double speed)
{
    if (!std::isfinite(speed))
        return Default;
    return std::clamp(speed, Minimum, Maximum);
}
}
