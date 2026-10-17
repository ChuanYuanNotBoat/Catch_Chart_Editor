#include "SpectrumAnalysis.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <array>
#include <utility>
namespace analysis
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr int fftSize = 2048;
constexpr int bands = 192;
struct FftPlan
{
    std::array<double, fftSize> window;
    std::array<size_t, fftSize> reversed;
    std::array<std::complex<double>, fftSize / 2> twiddles;
    FftPlan()
    {
        for (size_t i = 0; i < fftSize; ++i)
        {
            window[i] = .5 - .5 * std::cos(2 * pi * i / (fftSize - 1));
            size_t value = i, reverse = 0;
            for (size_t bit = fftSize; bit > 1; bit >>= 1, value >>= 1)
                reverse = (reverse << 1) | (value & 1);
            reversed[i] = reverse;
        }
        for (size_t i = 0; i < twiddles.size(); ++i)
            twiddles[i] = std::polar(1., -2 * pi * i / fftSize);
    }
};
void fft(std::array<std::complex<double>, fftSize> &a, const FftPlan &plan)
{
    // Input is already in bit-reversed order. The immutable plan is shared;
    // buffers belong to this analysis call, including concurrent callers.
    for (size_t len = 2; len <= fftSize; len <<= 1)
    {
        const size_t half = len / 2, step = fftSize / len;
        for (size_t i = 0; i < fftSize; i += len)
        {
            for (size_t j = 0; j < half; ++j)
            {
                const auto u = a[i + j], v = a[i + j + half] * plan.twiddles[j * step];
                a[i + j] = u + v;
                a[i + j + half] = u - v;
            }
        }
    }
}
} // namespace
StereoSpectrum analyzeStereo(const std::vector<float> &pcm, int rate, int channels, double start,
                             const std::atomic<bool> *cancel, size_t firstFrame, size_t frameCount, double frameStepSeconds)
{
    StereoSpectrum out;
    out.sampleRate = rate;
    out.sourceChannels = channels;
    out.startSeconds = start;
    if (rate < 1000 || rate > 384000 || channels < 1 || channels > 32 || pcm.empty() ||
        pcm.size() % channels || !std::isfinite(start) || start < 0 || !std::isfinite(frameStepSeconds)
        || frameStepSeconds < 0)
    {
        out.error = "Invalid PCM input";
        return out;
    }
    const size_t available = pcm.size() / channels;
    if (firstFrame >= available || frameCount == 0)
    {
        out.error = "Invalid PCM crop";
        return out;
    }
    const size_t samples = std::min(frameCount, available - firstFrame);
    if (cancel && cancel->load())
    {
        out.error = "Cancelled";
        return out;
    }
    constexpr int n = fftSize;
    // Twice the former temporal density for local pages; retain the existing
    // 12,000-frame contract used by Transient and the analysis tracks.
    const size_t hop = std::max<size_t>(std::max(1, rate / 200), (samples + 11999) / 12000);
    out.durationSeconds = double(samples) / rate;
    out.hopSeconds = double(hop) / rate;
    if (frameStepSeconds > 0)
        out.hopSeconds = std::max({frameStepSeconds, 1. / rate, out.durationSeconds / 12000});
    const double high = std::min(20000.0, rate / 2.0), low = 30;
    out.frequencies.reserve(bands);
    std::array<std::pair<int, int>, bands> bandBins;
    for (int b = 0; b < bands; ++b)
        out.frequencies.push_back(float(low * std::pow(high / low, double(b) / (bands - 1))));
    for (int b = 0; b < bands; ++b)
    {
        const double f0 = b ? std::sqrt(double(out.frequencies[b - 1]) * out.frequencies[b]) : low;
        const double f1 = b + 1 < bands ? std::sqrt(double(out.frequencies[b]) * out.frequencies[b + 1]) : high;
        const int k0 = std::clamp(int(std::floor(f0 * n / rate)), 1, n / 2);
        bandBins[b] = {k0, std::clamp(int(std::ceil(f1 * n / rate)), k0, n / 2)};
    }
    const size_t count = frameStepSeconds > 0
                             ? std::min<size_t>(12000, size_t(std::ceil(out.durationSeconds / out.hopSeconds - 1e-9)))
                             : (samples + hop - 1) / hop;
    out.frames.reserve(count);
    out.leftDb.resize(count * bands);
    out.rightDb.resize(count * bands);
    static const FftPlan plan;
    std::array<std::complex<double>, n> bins;
    std::array<double, n / 2 + 1> leftPower{}, rightPower{};
    size_t row = 0;
    const double sampleStep = out.hopSeconds * rate;
    for (size_t index = 0; index < count; ++index)
    {
        // A fractional sample stride keeps independently cached blocks on the
        // same 5 ms audio grid, including 44.1 kHz sources (220.5 samples).
        const size_t center = frameStepSeconds > 0 ? size_t(std::llround(index * sampleStep)) : index * hop;
        const size_t nextCenter = std::min(samples, frameStepSeconds > 0
            ? size_t(std::llround((index + 1) * sampleStep)) : center + hop);
        if (cancel && cancel->load())
        {
            out.error = "Cancelled";
            return out;
        }
        SpectrumFrame frame;
        for (int ch = 0; ch < 2; ++ch)
        {
            double sum = 0, peak = 0;
            for (size_t i = center; i < nextCenter; ++i)
            {
                const float v = pcm[(firstFrame + i) * channels + std::min(ch, channels - 1)];
                if (!std::isfinite(v))
                {
                    out.error = "Non-finite PCM sample";
                    return out;
                }
                sum += double(v) * v;
                peak = std::max(peak, std::abs(double(v)));
            }
            const float rms = float(std::sqrt(sum / std::max<size_t>(1, nextCenter - center)));
            if (ch == 0)
            {
                frame.leftRms = rms;
                frame.leftPeak = float(peak);
            }
            else
            {
                frame.rightRms = rms;
                frame.rightPeak = float(peak);
            }
        }
        for (int k = 0; k < n; ++k)
        {
            const auto index = static_cast<long long>(firstFrame + center) + k - n / 2;
            double left = 0, right = 0;
            if (index >= 0 && size_t(index) < available)
            {
                left = pcm[size_t(index) * channels];
                right = pcm[size_t(index) * channels + std::min(1, channels - 1)];
            }
            if (!std::isfinite(left) || !std::isfinite(right))
            {
                out.error = "Non-finite PCM sample";
                return out;
            }
            // Two real channels share one complex FFT. Recover each channel
            // before computing power, so opposite phases never cancel.
            bins[plan.reversed[k]] = {left * plan.window[k], channels > 1 ? right * plan.window[k] : 0};
        }
        fft(bins, plan);
        constexpr double normalization = 16. / (n * n);
        for (int k = 1; k <= n / 2; ++k)
        {
            if (channels == 1)
                leftPower[k] = rightPower[k] = std::norm(bins[k]) * normalization;
            else
            {
                const auto opposite = std::conj(bins[n - k]);
                leftPower[k] = std::norm((bins[k] + opposite) * .5) * normalization;
                rightPower[k] = std::norm((bins[k] - opposite) * std::complex<double>(0, -.5)) * normalization;
            }
        }
        for (int b = 0; b < bands; ++b)
        {
            if (b && bandBins[b] == bandBins[b - 1])
            {
                out.leftDb[row * bands + b] = out.leftDb[row * bands + b - 1];
                out.rightDb[row * bands + b] = out.rightDb[row * bands + b - 1];
                continue;
            }
            const auto [first, last] = bandBins[b];
            const double left = *std::max_element(leftPower.begin() + first, leftPower.begin() + last + 1);
            const double right = *std::max_element(rightPower.begin() + first, rightPower.begin() + last + 1);
            out.leftDb[row * bands + b] = float(10 * std::log10(std::max(1e-9, left)));
            out.rightDb[row * bands + b] = channels == 1 ? out.leftDb[row * bands + b]
                                                       : float(10 * std::log10(std::max(1e-9, right)));
        }
        out.frames.push_back(frame);
        ++row;
    }
    return out;
}
} // namespace analysis
