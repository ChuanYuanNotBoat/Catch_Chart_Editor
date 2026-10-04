#include "TimingInterpolation.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <functional>

namespace analysis
{
namespace
{
using Int = std::int64_t;
bool positiveBpm(double value) { return std::isfinite(value) && value >= .001 && value <= 10000; }
struct NormalBeat { Int whole, n, d; };
std::optional<NormalBeat> normalized(BeatTriplet t)
{
    if (t.denominator <= 0)
        return {};
    Int w = Int(t.whole) + t.numerator / t.denominator, n = t.numerator % t.denominator, d = t.denominator;
    if (n < 0) { --w; n += d; }
    const auto g = std::gcd(n, d);
    return NormalBeat{w, n / g, d / g};
}
std::optional<BeatSpan> scaled(BeatSpan span, Int index, Int divisions)
{
    if (index == 0)
        return BeatSpan{0, 1};
    Int n = span.numerator, d = span.denominator;
    auto g = std::gcd(n, d); n /= g; d /= g;
    g = std::gcd(n, divisions); n /= g; divisions /= g;
    g = std::gcd(index, d); index /= g; d /= g;
    if (index > std::numeric_limits<Int>::max() / n || divisions > std::numeric_limits<Int>::max() / d)
        return {};
    return BeatSpan{n * index, d * divisions};
}
long double localDuration(long double bpm, long double delta, long double beats)
{
    const long double ratio = delta / bpm;
    return 60000.L * beats / bpm * (ratio == 0 ? 1.L : std::log1p(ratio) / ratio);
}
} // namespace
std::optional<BeatTriplet> addBeatSpan(BeatTriplet start, BeatSpan span)
{
    const auto s = normalized(start);
    if (!s || span.numerator < 0 || span.denominator <= 0 || span.denominator > INT32_MAX)
        return {};
    if (span.numerator == 0)
        return start;
    const Int addedWhole = span.numerator / span.denominator;
    if (addedWhole > Int(INT32_MAX) - s->whole)
        return {};
    const Int whole = s->whole + addedWhole;
    const Int remainder = span.numerator % span.denominator;
    const Int g = std::gcd(s->d, span.denominator), factor = span.denominator / g;
    Int d = s->d * factor, n = s->n * factor + remainder * (s->d / g);
    const Int w = whole + n / d;
    n %= d;
    const Int divisor = std::gcd(n, d); n /= divisor; d /= divisor;
    if (w < 0 || w > INT32_MAX || d > INT32_MAX)
        return {};
    return BeatTriplet{int(w), int(n), int(d)};
}
std::optional<BeatSpan> subtractBeats(BeatTriplet end, BeatTriplet start)
{
    const auto a = normalized(start), b = normalized(end);
    if (!a || !b || end.position() <= start.position())
        return {};
    const Int g = std::gcd(a->d, b->d), d = a->d * (b->d / g);
    const Int fractional = b->n * (a->d / g) - a->n * (b->d / g);
    const Int whole = b->whole - a->whole;
    if (whole > 1000001 || whole < 0 || whole > (std::numeric_limits<Int>::max() - std::abs(fractional)) / d)
        return {};
    Int n = whole * d + fractional, den = d;
    if (n <= 0)
        return {};
    const Int divisor = std::gcd(n, den); n /= divisor; den /= divisor;
    if (den > INT32_MAX || double(n) / den > 1000000)
        return {};
    return BeatSpan{n, den};
}
double interpolatedTime(double start, double end, double length, double beat)
{
    if (!positiveBpm(start) || !positiveBpm(end) || !std::isfinite(length) || length <= 0
        || !std::isfinite(beat) || beat < 0 || beat > length)
        return std::numeric_limits<double>::quiet_NaN();
    return double(localDuration(start, (static_cast<long double>(end) - start) * beat / length, beat));
}
std::optional<BeatSpan> spanForDuration(double start, double end, double duration)
{
    if (!positiveBpm(start) || !positiveBpm(end) || !std::isfinite(duration) || duration <= 0)
        return {};
    const double length = duration / interpolatedTime(start, end, 1, 1);
    // Duration is a measurement. Quantize only the derived length explicitly;
    // Start remains the original beat triplet. Re-evaluate the resulting duration.
    if (!std::isfinite(length) || length < .000001 || length > 1000000)
        return {};
    Int n = std::llround(length * 1000000), d = 1000000;
    const Int g = std::gcd(n, d);
    return BeatSpan{n / g, d / g};
}
TimingInterpolation interpolateTiming(const InterpolationOptions &o)
{
    TimingInterpolation result;
    auto fail = [&](const char *message) { result.nodes.clear(); result.error = message; };
    if (!o.start.position().isValid() || o.start.position() < BeatPosition(0, 0, 1)
        || !positiveBpm(o.startBpm) || !positiveBpm(o.endBpm) || o.length.numerator <= 0
        || o.length.denominator <= 0 || o.maximumGap.numerator <= 0 || o.maximumGap.denominator <= 0
        || o.length.beats() > 1000000 || !std::isfinite(o.maximumErrorMilliseconds)
        || o.maximumErrorMilliseconds <= 0 || o.maximumErrorMilliseconds > 10
        || o.maximumSegments < 1 || o.maximumSegments > 8192)
    {
        fail("Invalid beat range, BPM, density or error limit"); return result;
    }
    const double length = o.length.beats(), gap = o.maximumGap.beats();
    const auto endpoint = addBeatSpan(o.start, o.length);
    const double count = std::ceil(length / gap);
    if (!endpoint || !std::isfinite(count) || count < 1 || count > double(o.maximumSegments))
    {
        fail("Range or density exceeds Malody beat limits or the segment limit"); return result;
    }
    const Int base = Int(count);
    const long double slope = (static_cast<long double>(o.endBpm) - o.startBpm) / length;
    std::function<bool(Int, Int, int)> segment = [&](Int index, Int divisions, int depth) {
        const auto a = scaled(o.length, index, divisions), b = scaled(o.length, index + 1, divisions);
        if (!a || !b)
            return false;
        const auto beat = addBeatSpan(o.start, *a), endBeat = addBeatSpan(o.start, *b);
        if (!beat || !endBeat || beat->position() >= endBeat->position())
            return false;
        const long double x = static_cast<long double>(a->numerator) / a->denominator;
        const long double y = static_cast<long double>(b->numerator) / b->denominator;
        const long double h = y - x, bpmA = o.startBpm + slope * x, delta = slope * h;
        const long double duration = localDuration(bpmA, delta, h), effective = 60000.L * h / duration;
        if (h <= 0 || !std::isfinite(effective) || !std::isfinite(duration) || duration <= 0)
            return false;
        const long double critical = delta == 0 ? 0 : std::clamp((effective - bpmA) / delta, 0.L, 1.L) * h;
        const double error = double(std::abs(60000.L * critical / effective - localDuration(bpmA, slope * critical, critical)));
        // Leave a numerical margin for the actual chart cache's double arithmetic.
        if (error >= o.maximumErrorMilliseconds * .99)
        {
            if (depth >= 30 || divisions > INT32_MAX / 2)
                return false;
            return segment(index * 2, divisions * 2, depth + 1)
                   && segment(index * 2 + 1, divisions * 2, depth + 1);
        }
        if (result.nodes.size() >= o.maximumSegments)
            return false;
        result.nodes.push_back({*beat, double(x), interpolatedTime(o.startBpm, o.endBpm, length, double(x)),
                                double(effective), error});
        result.maximumErrorMilliseconds = std::max(result.maximumErrorMilliseconds, error);
        return true;
    };
    for (Int i = 0; i < base; ++i)
        if (!segment(i, base, 0))
        {
            fail("Cannot satisfy the error bound within exact Malody beat or segment limits"); return result;
        }
    result.durationMilliseconds = interpolatedTime(o.startBpm, o.endBpm, length, length);
    result.nodes.push_back({*endpoint, length, result.durationMilliseconds, o.endBpm, 0});
    return result;
}
} // namespace analysis
