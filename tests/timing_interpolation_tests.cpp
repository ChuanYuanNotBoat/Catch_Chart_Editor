#include "analysis/TimingInterpolation.h"
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <limits>

void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void verify(const analysis::InterpolationOptions &o)
{
    const auto r = analysis::interpolateTiming(o);
    require(r.valid(), r.error.c_str());
    require(r.nodes.front().beat.whole == o.start.whole && r.nodes.front().beat.numerator == o.start.numerator
            && r.nodes.front().beat.denominator == o.start.denominator, "Start triplet changed");
    double accumulated = 0;
    for (std::size_t i = 0; i + 1 < r.nodes.size(); ++i)
    {
        const auto &a = r.nodes[i], &b = r.nodes[i + 1];
        const double h = b.relativeBeat - a.relativeBeat;
        require(a.beat.position() < b.beat.position(), "unordered exact beat nodes");
        require(h <= o.maximumGap.beats() + 1e-9, "density bound exceeded");
        require(a.errorMilliseconds < o.maximumErrorMilliseconds, "analytic error bound exceeded");
        require(std::isfinite(a.bpm) && a.bpm > 0, "nonfinite effective BPM");
        require(std::abs(accumulated - a.idealMilliseconds) < 1e-6, "cumulative drift at node");
        for (int k = 0; k <= 64; ++k)
        {
            const double x = a.relativeBeat + h * k / 64;
            const double ideal = analysis::interpolatedTime(o.startBpm, o.endBpm, o.length.beats(), x);
            const double stored = accumulated + 60000 * (x - a.relativeBeat) / a.bpm;
            require(std::abs(stored - ideal) < o.maximumErrorMilliseconds + 1e-7, "interior time error exceeded");
        }
        accumulated += 60000 * h / a.bpm;
    }
    require(std::abs(accumulated - r.durationMilliseconds) < 1e-6, "End duration drift");
    require(r.nodes.back().bpm == o.endBpm, "End BPM anchor missing");
}
int main()
{
    using namespace analysis;
    try
    {
        InterpolationOptions o;
        o.start = {2, 2, 6}; o.startBpm = o.endBpm = 120;
        const auto flat = interpolateTiming(o);
        require(flat.valid() && flat.nodes.size() == 9 && flat.durationMilliseconds == 4000, "constant BPM duration/density");
        verify(o);
        o.endBpm = 240;
        const auto ramp = interpolateTiming(o);
        require(ramp.valid() && std::abs(ramp.durationMilliseconds - 4000 * std::log(2.)) < 1e-9, "beat-linear integral");
        verify(o);
        o.startBpm = 12; o.endBpm = 480; o.length = {16, 1};
        const auto adaptive = interpolateTiming(o);
        require(adaptive.valid() && adaptive.nodes.size() > 17, "steep ramp did not subdivide");
        double min = 1, max = 0;
        for (std::size_t i = 0; i + 1 < adaptive.nodes.size(); ++i)
        {
            const double gap = adaptive.nodes[i + 1].relativeBeat - adaptive.nodes[i].relativeBeat;
            min = std::min(min, gap); max = std::max(max, gap);
        }
        require(max > min * 2, "adaptive subdivision should be local");
        verify(o);
        const auto end = addBeatSpan({2, 2, 6}, {8, 1});
        require(end && end->whole == 10 && end->numerator == 1 && end->denominator == 3, "exact rational addition");
        const auto span = subtractBeats({10, 1, 3}, {2, 2, 6});
        require(span && span->numerator == 8 && span->denominator == 1, "exact rational subtraction");
        require(!addBeatSpan({INT32_MAX, 0, 1}, {1, 1}), "beat integer overflow accepted");
        require(!addBeatSpan({INT32_MAX, 0, 1}, {std::numeric_limits<std::int64_t>::max(), 1}), "span integer overflow accepted");
        require(!addBeatSpan({2, 1, INT32_MAX}, {1, 2}), "unrepresentable denominator accepted");
        require(!subtractBeats({1, 0, 1}, {2, 0, 1}), "reversed beat range accepted");
        const auto durationSpan = spanForDuration(120, 240, 4000 * std::log(2.));
        require(durationSpan && durationSpan->numerator == 8 && durationSpan->denominator == 1, "duration inverse");
        std::mt19937 rng(170);
        for (int i = 0; i < 150; ++i)
        {
            o.start = {27, 13, 37}; o.startBpm = 10 + rng() % 800; o.endBpm = 10 + rng() % 800;
            o.length = {std::int64_t(1 + rng() % 128), 3}; o.maximumGap = {1, 1};
            o.maximumErrorMilliseconds = .5 + (rng() % 95) / 10.;
            verify(o);
        }
        o.maximumSegments = 1;
        require(!interpolateTiming(o).valid(), "segment budget ignored");
        o.maximumSegments = 8192; o.endBpm = std::numeric_limits<double>::quiet_NaN();
        require(!interpolateTiming(o).valid(), "NaN accepted");
        o.endBpm = 180; o.maximumErrorMilliseconds = 11;
        require(!interpolateTiming(o).valid(), "error bound above 10 ms accepted");
        o.maximumErrorMilliseconds = 10; o.maximumGap = {0, 1};
        require(!interpolateTiming(o).valid(), "zero density accepted");
        std::cout << "Timing interpolation tests passed\n";
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
