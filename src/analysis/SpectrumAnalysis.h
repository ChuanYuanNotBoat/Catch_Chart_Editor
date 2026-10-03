#pragma once
#include <atomic>
#include <string>
#include <vector>
namespace analysis
{
struct SpectrumFrame
{
    float leftPeak = 0, rightPeak = 0, leftRms = 0, rightRms = 0;
};
// Audio seconds; chart offsets and UI state never enter this data boundary.
struct StereoSpectrum
{
    double startSeconds = 0, durationSeconds = 0, hopSeconds = 0;
    int sampleRate = 0, sourceChannels = 0, fftSize = 2048;
    std::vector<float> frequencies;
    std::vector<SpectrumFrame> frames;
    std::vector<float> leftDb, rightDb; // frame-major log-band magnitudes, dBFS
    std::string error;
    bool valid() const { return error.empty() && !frames.empty(); }
};
StereoSpectrum analyzeStereo(const std::vector<float> &interleaved, int sampleRate, int channels,
                             double startSeconds = 0, const std::atomic<bool> *cancel = nullptr);
} // namespace analysis
