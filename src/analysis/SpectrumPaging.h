#pragma once
#include <algorithm>
#include <cmath>
namespace analysis
{
struct SpectrumPageRange
{
    double startMs = 0, endMs = 0, requiredStartMs = 0, requiredEndMs = 0;
    bool valid = false;
    double durationMs() const
    {
        return endMs - startMs;
    }
};
// A smaller rolling page with space on both sides. The desired page always
// covers the exact visible interval; alignment cannot clip its far edge.
inline SpectrumPageRange spectrumPageRange(double a, double b, double eofMs = -1)
{
    SpectrumPageRange r;
    if (!std::isfinite(a) || !std::isfinite(b))
        return r;
    r.requiredStartMs = std::max(0., std::min(a, b));
    r.requiredEndMs = std::max(r.requiredStartMs, std::max(a, b));
    if (eofMs >= 0)
    {
        r.requiredEndMs = std::min(r.requiredEndMs, eofMs);
        r.requiredStartMs = std::min(r.requiredStartMs, eofMs);
    }
    const double span = r.requiredEndMs - r.requiredStartMs;
    if (span <= 0 || span > 100000)
        return r;
    const double margin = std::min(std::max(2000., span * .6), (120000 - span) / 2);
    r.startMs = std::max(0., std::floor((r.requiredStartMs - margin) / 2000) * 2000);
    r.endMs = std::max(r.startMs + 12000, std::ceil((r.requiredEndMs + margin) / 2000) * 2000);
    if (r.endMs - r.startMs > 120000)
        r.startMs = std::max(0., r.endMs - 120000);
    if (eofMs >= 0)
        r.endMs = std::min(r.endMs, eofMs);
    r.valid = r.endMs > r.startMs && r.startMs <= r.requiredStartMs && r.endMs >= r.requiredEndMs;
    return r;
}
} // namespace analysis
