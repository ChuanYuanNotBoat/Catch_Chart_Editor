#include "analysis/SpectrumAnalysis.h"
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
double peakFrequency(const analysis::StereoSpectrum &s, const std::vector<float> &db)
{
    const auto bins = s.frequencies.size();
    const auto from = db.begin() + (s.frames.size() / 2) * bins;
    return s.frequencies[size_t(std::max_element(from, from + bins) - from)];
}
} // namespace
int main()
{
    try
    {
        constexpr int rate = 44100;
        constexpr double pi = 3.14159265358979323846;
        std::vector<float> pcm(rate * 2);
        for (int i = 0; i < rate; ++i)
        {
            pcm[2 * i] = float(.5 * std::sin(2 * pi * 440 * i / rate));
            pcm[2 * i + 1] = float(.2 * std::sin(2 * pi * 1760 * i / rate));
        }
        auto s = analysis::analyzeStereo(pcm, rate, 2, 17.5);
        require(s.valid(), "stereo analysis failed");
        require(s.frames.size() == 100, "10 ms hop changed");
        require(s.startSeconds == 17.5 && s.durationSeconds == 1, "audio origin/duration changed");
        require(std::abs(peakFrequency(s, s.leftDb) - 440) < 55, "left tone missing");
        require(std::abs(peakFrequency(s, s.rightDb) - 1760) < 140, "right tone missing");
        require(std::abs(s.frames[50].leftRms - .5 / std::sqrt(2.0)) < .02, "left RMS incorrect");
        require(std::abs(s.frames[50].rightRms - .2 / std::sqrt(2.0)) < .02, "right RMS incorrect");
        std::vector<float> mono(rate, .25f);
        s = analysis::analyzeStereo(mono, rate, 1);
        require(s.valid() && s.leftDb == s.rightDb, "mono channels differ");
        require(s.frames[30].leftRms == .25f && s.frames[30].leftPeak == .25f,
                "constant PCM loudness incorrect");
        std::fill(mono.begin(), mono.end(), 0);
        s = analysis::analyzeStereo(mono, rate, 1);
        require(s.valid() && std::all_of(s.leftDb.begin(), s.leftDb.end(), [](float x) { return x == -90; }),
                "silence not finite/floored");
        mono[15000] = std::numeric_limits<float>::quiet_NaN();
        require(!analysis::analyzeStereo(mono, rate, 1).valid(), "non-finite PCM accepted");
        std::atomic<bool> cancelled{true};
        require(!analysis::analyzeStereo(pcm, rate, 2, 0, &cancelled).valid(), "cancel ignored");
        require(!analysis::analyzeStereo(pcm, 0, 2).valid(), "invalid rate accepted");
        require(!analysis::analyzeStereo({1, 2, 3}, rate, 2).valid(), "partial frame accepted");
        std::cout
            << "Spectrum separation, time origin, loudness, silence, validation and cancellation passed\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
