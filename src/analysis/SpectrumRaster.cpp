#include "SpectrumRaster.h"
#include <QColor>
#include <array>
#include <algorithm>
#include <cmath>
namespace analysis
{
SpectrumRaster prepareSpectrumRaster(const StereoSpectrum &s, const std::atomic<bool> *cancel)
{
    const auto bins = s.frequencies.size(), frames = s.frames.size();
    if (!s.valid() || !bins || bins > 4096 || frames > 12000 || s.leftDb.size() != frames * bins
        || s.rightDb.size() != frames * bins || !std::isfinite(s.startSeconds) || s.startSeconds < 0
        || !std::isfinite(s.durationSeconds) || s.durationSeconds <= 0 || !std::isfinite(s.hopSeconds)
        || s.hopSeconds <= 0)
        return {};
    static const auto palette = [] {
        std::array<QRgb, 1024> colors{};
        for (size_t i = 0; i < colors.size(); ++i)
        {
            double v = double(i) / (colors.size() - 1);
            colors[i] = QColor::fromRgbF(std::clamp(2.4 * v - 1., 0., 1.), std::clamp(1.9 * v - .3, 0., 1.),
                                         std::clamp(3 * v, .04, 1.) * (1 - .65 * v))
                            .rgb();
        }
        return colors;
    }();
    SpectrumRaster out{QImage(int(bins), int(frames), QImage::Format_RGB32),
                       QImage(int(bins), int(frames), QImage::Format_RGB32)};
    if (!out.valid())
        return {};
    for (size_t y = 0; y < frames; ++y)
    {
        if (cancel && cancel->load())
            return {};
        auto *left = reinterpret_cast<QRgb *>(out.left.scanLine(int(y))),
             *right = reinterpret_cast<QRgb *>(out.right.scanLine(int(y)));
        for (size_t x = 0; x < bins; ++x)
        {
            const float a = s.leftDb[y * bins + x], b = s.rightDb[y * bins + x];
            if (!std::isfinite(a) || !std::isfinite(b))
                return {};
            auto color = [&](float db) {
                return palette[size_t(std::clamp((db + 90.) / 90., 0., 1.) * (palette.size() - 1) + .5)];
            };
            left[x] = color(a);
            right[x] = color(b);
        }
    }
    return out;
}
} // namespace analysis
