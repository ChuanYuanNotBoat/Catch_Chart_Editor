#include "analysis/TransientAnalysis.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace
{
void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
bool detectedNear(const analysis::TransientResult &r, double time, int band)
{
    return std::any_of(r.peaks.begin(), r.peaks.end(), [&](const auto &p) {
        return p.disposition == analysis::PeakDisposition::Detected && std::abs(p.timeSeconds - time) < .03
               && p.bands[band] > p.bands[(band + 1) % 3] && p.bands[band] > p.bands[(band + 2) % 3];
    });
}
} // namespace
int main()
{
    try
    {
        constexpr int rate = 44100;
        constexpr double pi = 3.14159265358979323846;
        std::vector<float> pcm(rate * 4 * 2);
        const double times[]{.5, 1.5, 2.5}, frequencies[]{110, 880, 7040};
        for (int i = 0; i < rate * 4; ++i)
            for (int b = 0; b < 3; ++b)
            {
                const double t = double(i) / rate - times[b];
                if (t >= 0 && t < .2)
                    pcm[2 * i + b % 2] += float(.8 * std::exp(-30 * t) * std::sin(2 * pi * frequencies[b] * t));
            }
        auto s = analysis::analyzeStereo(pcm, rate, 2, 17.5);
        auto r = analysis::analyzeTransients(s);
        require(r.valid(), "transient analysis failed");
        require(detectedNear(r, 18., 0), "low attack missing or time origin shifted");
        require(detectedNear(r, 19., 1), "mid attack missing");
        require(std::any_of(r.peaks.begin(), r.peaks.end(),
                            [](const auto &p) {
                                return std::abs(p.timeSeconds - 20.) < .03
                                       && p.disposition == analysis::PeakDisposition::BelowThreshold;
                            }),
                "weak high attack rejection missing");
        const auto sensitive = analysis::analyzeTransients(s, {.05, .06});
        require(detectedNear(sensitive, 20., 2), "lower threshold did not recover high attack");
        require(r.frames.front().strength == 0, "page start emitted artificial flux");
        for (const auto &f : r.frames)
            require(std::isfinite(f.strength) && std::isfinite(f.threshold), "non-finite onset result");
        // Handcrafted channel exchange must still have positive flux. It also
        // creates two attacks 20 ms apart, so the stronger one wins suppression.
        s = {};
        s.sampleRate = rate;
        s.startSeconds = 10;
        s.durationSeconds = 1;
        s.hopSeconds = .01;
        s.frequencies = {110, 880, 7040};
        s.frames.resize(100);
        s.leftDb.assign(300, -90);
        s.rightDb.assign(300, -90);
        s.leftDb[20 * 3] = -20;
        s.rightDb[21 * 3] = -30;
        s.rightDb[22 * 3] = 0;
        r = analysis::analyzeTransients(s, {.01, .06});
        require(r.valid() && r.frames[21].strength > 0, "opposite-channel attack cancelled");
        require(std::any_of(r.peaks.begin(), r.peaks.end(),
                            [](const auto &p) {
                                return p.frame == 22 && p.disposition == analysis::PeakDisposition::Detected;
                            }),
                "stronger neighboring peak did not survive");
        require(std::any_of(r.peaks.begin(), r.peaks.end(),
                            [](const auto &p) {
                                return p.disposition == analysis::PeakDisposition::TooClose;
                            }),
                "minimum interval rejection absent");
        s.leftDb[2 * 3] = 0;
        r = analysis::analyzeTransients(s);
        require(std::any_of(r.peaks.begin(), r.peaks.end(),
                            [](const auto &p) {
                                return p.frame == 2 && p.disposition == analysis::PeakDisposition::PageBoundary;
                            }),
                "padding boundary accepted as real event");
        std::fill(s.leftDb.begin(), s.leftDb.end(), -90);
        std::fill(s.rightDb.begin(), s.rightDb.end(), -90);
        r = analysis::analyzeTransients(s);
        require(r.valid() && r.peaks.empty() && r.maximumStrength == 0, "silence produced events");
        std::atomic<bool> cancel{true};
        require(!analysis::analyzeTransients(s, {}, &cancel).valid(), "cancellation ignored");
        require(!analysis::analyzeTransients(s, {2, .06}).valid(), "invalid options accepted");
        s.rightDb.pop_back();
        require(!analysis::analyzeTransients(s).valid(), "truncated spectrum accepted");
        s.rightDb.push_back(std::numeric_limits<float>::quiet_NaN());
        require(!analysis::analyzeTransients(s).valid(), "non-finite spectrum accepted");
        std::cout << "Band attacks, absolute time, stereo rectification, peak suppression, boundaries, silence and "
                     "cancellation passed\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
