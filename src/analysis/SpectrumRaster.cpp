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
        QList<QRgb> colors(256);
        for (size_t i = 0; i < colors.size(); ++i)
        {
            double v = double(i) / (colors.size() - 1);
            colors[i] = QColor::fromRgbF(std::clamp(2.4 * v - 1., 0., 1.), std::clamp(1.9 * v - .3, 0., 1.),
                                         std::clamp(3 * v, .04, 1.) * (1 - .65 * v))
                            .rgb();
        }
        return colors;
    }();
    SpectrumRaster out{QImage(int(bins), int(frames), QImage::Format_Indexed8),
                       QImage(int(bins), int(frames), QImage::Format_Indexed8), {}};
    if (!out.valid())
        return {};
    out.left.setColorTable(palette);
    out.right.setColorTable(palette);
    auto overlayPalette = palette;
    for (qsizetype i = 0; i < overlayPalette.size(); ++i)
    {
        const auto rgb = overlayPalette[i];
        const int alpha = qRound(115 * std::clamp((double(i) / 255 - .15) / .85, 0., 1.));
        overlayPalette[i] = qRgba(qRed(rgb), qGreen(rgb), qBlue(rgb), alpha);
    }
    QImage overlay(int(bins), int(frames), QImage::Format_Indexed8);
    if (overlay.isNull())
        return {};
    overlay.setColorTable(overlayPalette);
    for (size_t y = 0; y < frames; ++y)
    {
        if (cancel && cancel->load())
            return {};
        auto *left = out.left.scanLine(int(y)), *right = out.right.scanLine(int(y));
        auto *combined = overlay.scanLine(int(y));
        for (size_t x = 0; x < bins; ++x)
        {
            const float a = s.leftDb[y * bins + x], b = s.rightDb[y * bins + x];
            if (!std::isfinite(a) || !std::isfinite(b))
                return {};
            auto encode = [](float db) {
                return uchar(std::clamp((db + 90.) / 90., 0., 1.) * 255 + .5);
            };
            left[x] = encode(a);
            right[x] = encode(b);
            combined[x] = std::max(left[x], right[x]);
        }
    }
    out.overlay.append(std::move(overlay));
    while (out.overlay.back().height() > 1)
    {
        if (cancel && cancel->load())
            return {};
        const auto &source = out.overlay.back();
        QImage reduced(int(bins), (source.height() + 1) / 2, QImage::Format_Indexed8);
        if (reduced.isNull())
            return {};
        reduced.setColorTable(overlayPalette);
        for (int y = 0; y < reduced.height(); ++y)
        {
            const auto *first = source.constScanLine(y * 2);
            const auto *second = source.constScanLine(std::min(y * 2 + 1, source.height() - 1));
            auto *target = reduced.scanLine(y);
            for (size_t x = 0; x < bins; ++x)
                target[x] = std::max(first[x], second[x]);
        }
        out.overlay.append(std::move(reduced));
    }
    return out;
}
} // namespace analysis
