#pragma once
#include "TimingMeasurement.h"
#include "model/BeatPosition.h"
#include <vector>
#include <string>

namespace analysis
{
struct BeatTriplet
{
    int whole = 0, numerator = 0, denominator = 1;
    BeatPosition position() const { return {whole, numerator, denominator}; }
};
std::optional<BeatTriplet> addBeatSpan(BeatTriplet start, BeatSpan span);
std::optional<BeatSpan> subtractBeats(BeatTriplet end, BeatTriplet start);
// BPM is linear in beat coordinates. Audio time is its exact integral.
double interpolatedTime(double startBpm, double endBpm, double lengthBeats, double relativeBeat);
std::optional<BeatSpan> spanForDuration(double startBpm, double endBpm, double durationMilliseconds);
struct InterpolationOptions
{
    BeatTriplet start;
    BeatSpan length{8, 1}, maximumGap{1, 1};
    double startBpm = 120, endBpm = 180, maximumErrorMilliseconds = 10;
    std::size_t maximumSegments = 8192;
};
struct InterpolationNode
{
    BeatTriplet beat;
    double relativeBeat = 0, idealMilliseconds = 0, bpm = 0, errorMilliseconds = 0;
};
struct TimingInterpolation
{
    std::vector<InterpolationNode> nodes; // includes the exact End anchor
    double durationMilliseconds = 0, maximumErrorMilliseconds = 0;
    std::string error;
    bool valid() const { return error.empty() && nodes.size() >= 2; }
};
TimingInterpolation interpolateTiming(const InterpolationOptions &);
} // namespace analysis
