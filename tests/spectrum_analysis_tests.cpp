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
        require(s.hopSeconds <= .005 && s.frames.size() == size_t((rate + rate / 200 - 1) / (rate / 200)),
                "fine temporal resolution missing");
        require(s.frequencies.size() == 192, "fine frequency bands missing");
        require(s.startSeconds == 17.5 && s.durationSeconds == 1, "audio origin/duration changed");
        require(std::abs(peakFrequency(s, s.leftDb) - 440) < 55, "left tone missing");
        require(std::abs(peakFrequency(s, s.rightDb) - 1760) < 140, "right tone missing");
        require(std::abs(s.frames[100].leftRms - .5 / std::sqrt(2.0)) < .03, "left RMS incorrect");
        require(std::abs(s.frames[100].rightRms - .2 / std::sqrt(2.0)) < .03, "right RMS incorrect");
        // The packed transform must retain amplitude and phase independence.
        for (int i = 0; i < rate; ++i)
        {
            pcm[2 * i] = float(.5 * std::sin(2 * pi * 64 * i / 2048));
            pcm[2 * i + 1] = -pcm[2 * i];
        }
        s = analysis::analyzeStereo(pcm, rate, 2);
        const auto offset = size_t(100) * s.frequencies.size();
        const auto peak = *std::max_element(s.leftDb.begin() + offset, s.leftDb.begin() + offset + s.frequencies.size());
        require(std::abs(peak + 6.0206) < .03, "packed FFT amplitude calibration changed");
        for (size_t i = 0; i < s.leftDb.size(); ++i)
            require(std::abs(s.leftDb[i] - s.rightDb[i]) < .001, "opposite-phase channels differ");
        const size_t first = size_t(rate / 200) * 40, length = size_t(rate / 200) * 100;
        const auto crop = analysis::analyzeStereo(pcm, rate, 2, double(first) / rate, nullptr, first, length);
        require(crop.valid() && crop.frames.size() == 100, "context crop failed");
        require(crop.startSeconds == double(first) / rate && crop.durationSeconds == double(length) / rate,
                "context leaked into audio crop coordinates");
        for (size_t i = 0; i < crop.leftDb.size(); ++i)
            require(std::abs(crop.leftDb[i] - s.leftDb[40 * s.frequencies.size() + i]) < .001,
                    "crop boundary FFT differs from continuous audio");
        const auto grid = analysis::analyzeStereo(pcm, rate, 2, 0, nullptr, 0, rate, .005);
        const auto gridCrop = analysis::analyzeStereo(pcm, rate, 2, .5, nullptr, rate / 2, rate / 2, .005);
        require(grid.valid() && grid.hopSeconds == .005 && grid.frames.size() == 200,
                "44.1 kHz shared block time grid drifted");
        require(gridCrop.valid() && gridCrop.frames.size() == 100, "shared grid crop failed");
        for (size_t i = 0; i < gridCrop.leftDb.size(); ++i)
            require(std::abs(gridCrop.leftDb[i] - grid.leftDb[100 * grid.frequencies.size() + i]) < .001,
                    "shared block boundary differs from continuous grid");
        for (int i = 0; i < rate; ++i)
            pcm[2 * i + 1] = 0;
        s = analysis::analyzeStereo(pcm, rate, 2);
        require(std::all_of(s.rightDb.begin(), s.rightDb.end(), [](float db) { return db == -90; }),
                "packed FFT leaked into the silent channel");
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
