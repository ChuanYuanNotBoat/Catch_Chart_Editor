#include "TransientAnalysis.h"
#include <algorithm>
#include <cmath>
#include <numeric>
namespace analysis
{
TransientResult analyzeTransients(const StereoSpectrum &s, const TransientOptions &options,
                                  const std::atomic<bool> *cancel)
{
    TransientResult out;
    const size_t n = s.frames.size(), bins = s.frequencies.size();
    auto cancelled = [&] {
        return cancel && cancel->load();
    };
    auto fail = [&](const char *message) {
        out = {};
        out.error = message;
        return out;
    };
    if (!s.valid() || n > 12000 || !bins || bins > 4096 || s.sampleRate < 1000 || s.fftSize < 2
        || !std::isfinite(s.startSeconds) || s.startSeconds < 0 || !std::isfinite(s.durationSeconds)
        || s.durationSeconds <= 0 || !std::isfinite(s.hopSeconds) || s.hopSeconds <= 0 || s.leftDb.size() != n * bins
        || s.rightDb.size() != n * bins || !std::isfinite(options.relativeThreshold) || options.relativeThreshold < 0
        || options.relativeThreshold > 1 || !std::isfinite(options.minimumIntervalSeconds)
        || options.minimumIntervalSeconds < 0 || options.minimumIntervalSeconds > 2)
        return fail("Invalid transient input or options");
    if (cancelled())
        return fail("Cancelled");
    std::vector<int> band(bins);
    std::array<size_t, 3> counts{};
    for (size_t b = 0; b < bins; ++b)
    {
        if (!std::isfinite(s.frequencies[b]) || s.frequencies[b] <= 0
            || (b && s.frequencies[b] <= s.frequencies[b - 1]))
            return fail("Invalid spectrum frequencies");
        band[b] = s.frequencies[b] < 250 ? 0 : s.frequencies[b] < 2000 ? 1 : 2;
        ++counts[band[b]];
    }
    out.frames.resize(n);
    std::vector<double> previousLeft(bins), previousRight(bins);
    for (size_t i = 0; i < n; ++i)
    {
        if (cancelled())
            return fail("Cancelled");
        auto &frame = out.frames[i];
        frame.timeSeconds = s.startSeconds + i * s.hopSeconds;
        double total = 0;
        for (size_t b = 0; b < bins; ++b)
        {
            const float l = s.leftDb[i * bins + b], r = s.rightDb[i * bins + b];
            if (!std::isfinite(l) || !std::isfinite(r))
                return fail("Non-finite spectrum magnitude");
            // Log compression of linear magnitude; rectify channels separately
            // so an attack in one channel cannot cancel a decay in the other.
            const double left = std::log1p(10 * std::pow(10., std::clamp(double(l), -90., 24.) / 20));
            const double right = std::log1p(10 * std::pow(10., std::clamp(double(r), -90., 24.) / 20));
            const double flux =
                i ? .5 * (std::max(0., left - previousLeft[b]) + std::max(0., right - previousRight[b])) : 0;
            previousLeft[b] = left;
            previousRight[b] = right;
            frame.bands[band[b]] += float(flux);
            total += flux;
        }
        frame.strength = float(total / bins);
        for (size_t b = 0; b < 3; ++b)
            frame.bands[b] /= float(std::max<size_t>(1, counts[b]));
        out.maximumStrength = std::max(out.maximumStrength, frame.strength);
    }
    // A local median floor suppresses a sustained noisy region without erasing
    // isolated attacks. This is a diagnostic score, not calibrated confidence.
    const size_t radius = size_t(std::min(60., std::ceil(.15 / s.hopSeconds)));
    std::vector<float> neighborhood;
    neighborhood.reserve(radius * 2 + 1);
    for (size_t i = 0; i < n; ++i)
    {
        if (cancelled())
            return fail("Cancelled");
        neighborhood.clear();
        for (size_t j = i > radius ? i - radius : 0; j < std::min(n, i + radius + 1); ++j)
            neighborhood.push_back(out.frames[j].strength);
        const auto middle = neighborhood.begin() + neighborhood.size() / 2;
        std::nth_element(neighborhood.begin(), middle, neighborhood.end());
        out.frames[i].threshold =
            float(std::max(options.relativeThreshold * out.maximumStrength, double(*middle) * 1.5));
    }
    const double edge = s.fftSize / (2. * s.sampleRate);
    for (size_t i = 1; i + 1 < n; ++i)
    {
        const auto &frame = out.frames[i];
        if (frame.strength <= 1e-7f || frame.strength < out.frames[i - 1].strength
            || frame.strength <= out.frames[i + 1].strength)
            continue;
        size_t left = i, right = i;
        // Half-height width is bounded to 1 s even for a long flux plateau.
        const size_t limit = size_t(std::min(double(n), std::ceil(.5 / s.hopSeconds)));
        while (left > 0 && i - left < limit && out.frames[left - 1].strength >= frame.strength * .5f)
            --left;
        while (right + 1 < n && right - i < limit && out.frames[right + 1].strength >= frame.strength * .5f)
            ++right;
        auto disposition =
            frame.strength >= frame.threshold ? PeakDisposition::Detected : PeakDisposition::BelowThreshold;
        if (i * s.hopSeconds < edge || i * s.hopSeconds > s.durationSeconds - edge)
            disposition = PeakDisposition::PageBoundary;
        out.peaks.push_back(
            {i, frame.timeSeconds, (right - left + 1) * s.hopSeconds, frame.strength, frame.bands, disposition});
    }
    std::vector<size_t> order(out.peaks.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return out.peaks[a].strength > out.peaks[b].strength;
    });
    std::vector<double> accepted;
    for (size_t index : order)
    {
        if (cancelled())
            return fail("Cancelled");
        auto &peak = out.peaks[index];
        if (peak.disposition != PeakDisposition::Detected)
            continue;
        const auto next = std::lower_bound(accepted.begin(), accepted.end(), peak.timeSeconds);
        if ((next != accepted.end() && *next - peak.timeSeconds < options.minimumIntervalSeconds)
            || (next != accepted.begin() && peak.timeSeconds - *std::prev(next) < options.minimumIntervalSeconds))
            peak.disposition = PeakDisposition::TooClose;
        else
            accepted.insert(next, peak.timeSeconds);
    }
    return out;
}
} // namespace analysis
