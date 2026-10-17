// Standalone DSP benchmark: compile this file, SpectrumAnalysis.cpp and TransientAnalysis.cpp with
// the same compiler/options for both revisions. No Qt/audio backend noise.
#include "TransientAnalysis.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

int main()
{
    constexpr double pi = 3.14159265358979323846;
    std::cout << "[\n";
    bool comma = false;
    for (const auto &fixture : {std::vector<int>{44100, 1, 12}, {48000, 2, 12}, {48000, 2, 120}})
    {
        const int rate = fixture[0], channels = fixture[1], seconds = fixture[2];
        std::vector<float> pcm(size_t(rate) * seconds * channels);
        for (size_t i = 0; i < pcm.size() / channels; ++i)
            for (int ch = 0; ch < channels; ++ch)
                pcm[i * channels + ch] = float(.5 * std::sin(2 * pi * (ch ? 1760 : 440) * i / rate));
        std::vector<double> times, transientTimes;
        size_t frames = 0, bands = 0;
        for (int run = 0; run < 6; ++run)
        {
            const auto start = std::chrono::steady_clock::now();
            const auto spectrum = analysis::analyzeStereo(pcm, rate, channels);
            if (!spectrum.valid())
                return 1;
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            const auto transientStart = std::chrono::steady_clock::now();
            const auto transients = analysis::analyzeTransients(spectrum);
            if (!transients.valid())
                return 1;
            if (run)
            {
                times.push_back(ms);
                transientTimes.push_back(std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - transientStart).count());
            }
            frames = spectrum.frames.size();
            bands = spectrum.frequencies.size();
        }
        std::sort(times.begin(), times.end());
        std::sort(transientTimes.begin(), transientTimes.end());
        std::cout << (comma ? ",\n" : "") << "  {\"rate\":" << rate << ",\"channels\":" << channels
                  << ",\"seconds\":" << seconds << ",\"frames\":" << frames << ",\"bands\":" << bands
                  << ",\"median_ms\":" << times[times.size() / 2] << ",\"max_ms\":" << times.back()
                  << ",\"transient_median_ms\":" << transientTimes[transientTimes.size() / 2] << "}";
        comma = true;
    }
    std::cout << "\n]\n";
}
