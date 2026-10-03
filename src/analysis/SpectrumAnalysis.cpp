#include "SpectrumAnalysis.h"
#include <algorithm>
#include <cmath>
#include <complex>
namespace analysis
{
namespace
{
constexpr double pi = 3.14159265358979323846;
void fft(std::vector<std::complex<double>> &a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const auto step = std::polar(1.0, -2 * pi / len);
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w(1, 0);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= step;
            }
        }
    }
}
} // namespace
StereoSpectrum analyzeStereo(const std::vector<float> &pcm, int rate, int channels, double start,
                             const std::atomic<bool> *cancel)
{
    StereoSpectrum out;
    out.sampleRate = rate;
    out.sourceChannels = channels;
    out.startSeconds = start;
    if (rate < 1000 || rate > 384000 || channels < 1 || channels > 32 || pcm.empty() ||
        pcm.size() % channels || !std::isfinite(start) || start < 0)
    {
        out.error = "Invalid PCM input";
        return out;
    }
    const size_t samples = pcm.size() / channels;
    constexpr int n = 2048, bands = 128;
    const size_t hop = std::max<size_t>(rate / 100, (samples + 11999) / 12000);
    out.durationSeconds = double(samples) / rate;
    out.hopSeconds = double(hop) / rate;
    const double high = std::min(20000.0, rate / 2.0), low = 30;
    for (int b = 0; b < bands; ++b)
        out.frequencies.push_back(float(low * std::pow(high / low, double(b) / (bands - 1))));
    const size_t count = (samples + hop - 1) / hop;
    out.frames.reserve(count);
    out.leftDb.reserve(count * bands);
    out.rightDb.reserve(count * bands);
    std::vector<std::complex<double>> bins(n);
    for (size_t center = 0; center < samples; center += hop)
    {
        if (cancel && cancel->load())
        {
            out.error = "Cancelled";
            return out;
        }
        SpectrumFrame frame;
        for (int ch = 0; ch < 2; ++ch)
        {
            double sum = 0, peak = 0;
            for (size_t i = center; i < std::min(samples, center + hop); ++i)
            {
                const float v = pcm[i * channels + std::min(ch, channels - 1)];
                if (!std::isfinite(v))
                {
                    out.error = "Non-finite PCM sample";
                    return out;
                }
                sum += double(v) * v;
                peak = std::max(peak, std::abs(double(v)));
            }
            const float rms = float(std::sqrt(sum / std::min(hop, samples - center)));
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
            for (int k = 0; k < n; ++k)
            {
                const auto index = static_cast<long long>(center) + k - n / 2;
                const double v = index >= 0 && size_t(index) < samples
                                     ? pcm[size_t(index) * channels + std::min(ch, channels - 1)]
                                     : 0;
                if (!std::isfinite(v))
                {
                    out.error = "Non-finite PCM sample";
                    return out;
                }
                bins[k] = v * (0.5 - 0.5 * std::cos(2 * pi * k / (n - 1)));
            }
            fft(bins);
            auto &db = ch == 0 ? out.leftDb : out.rightDb;
            for (int b = 0; b < bands; ++b)
            {
                const double f0 = b ? std::sqrt(out.frequencies[b - 1] * out.frequencies[b]) : low;
                const double f1 =
                    b + 1 < bands ? std::sqrt(out.frequencies[b] * out.frequencies[b + 1]) : high;
                const int k0 = std::clamp(int(std::floor(f0 * n / rate)), 1, n / 2);
                const int k1 = std::clamp(int(std::ceil(f1 * n / rate)), k0, n / 2);
                double mag = 0;
                for (int k = k0; k <= k1; ++k)
                    mag = std::max(mag, std::abs(bins[k]) * 4 / n);
                db.push_back(float(std::max(-90.0, 20 * std::log10(std::max(1e-9, mag)))));
            }
        }
        out.frames.push_back(frame);
    }
    return out;
}
} // namespace analysis
