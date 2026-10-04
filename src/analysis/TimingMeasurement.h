#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace analysis
{
struct BeatSpan
{
    std::int64_t numerator = 1, denominator = 1;
    double beats() const { return double(numerator) / double(denominator); }
};
// Exact decimal/fraction input. No locale, milliseconds or chart conversion.
// Up to nine decimal places; positive spans no larger than one million beats.
std::optional<BeatSpan> parseBeatSpan(std::string_view text);
struct TimingMeasurement
{
    double durationMilliseconds = 0, bpm = 0;
    bool valid = false;
};
TimingMeasurement measureTiming(BeatSpan span, double startMilliseconds, double endMilliseconds);
} // namespace analysis
