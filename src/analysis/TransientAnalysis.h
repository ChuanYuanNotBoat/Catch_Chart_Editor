#pragma once
#include "SpectrumAnalysis.h"
#include <array>
namespace analysis
{
struct TransientOptions
{
    double relativeThreshold = 0.12; // fraction of the strongest flux in this audio page
    double minimumIntervalSeconds = 0.06;
};
struct OnsetFrame
{
    double timeSeconds = 0;
    float strength = 0, threshold = 0;
    std::array<float, 3> bands{}; // 30–250 Hz, 250–2000 Hz, 2000 Hz–Nyquist
};
enum class PeakDisposition
{
    Detected,
    BelowThreshold,
    TooClose,
    PageBoundary
};
struct TransientPeak
{
    size_t frame = 0;
    double timeSeconds = 0, widthSeconds = 0;
    float strength = 0;
    std::array<float, 3> bands{};
    PeakDisposition disposition = PeakDisposition::BelowThreshold;
};
struct TransientResult
{
    std::vector<OnsetFrame> frames;
    std::vector<TransientPeak> peaks;
    float maximumStrength = 0;
    std::string error;
    bool valid() const
    {
        return error.empty() && !frames.empty();
    }
};
// Deterministic visual baseline over display-band STFT data, without Qt or chart state.
// Times are absolute audio seconds; peak width is a flux-lobe width, not sound duration.
TransientResult analyzeTransients(const StereoSpectrum &, const TransientOptions & = {},
                                  const std::atomic<bool> *cancel = nullptr);
} // namespace analysis
