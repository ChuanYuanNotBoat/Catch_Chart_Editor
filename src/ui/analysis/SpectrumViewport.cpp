#include "SpectrumViewport.h"
#include <algorithm>
#include <cmath>
#include <cstring>
std::array<QRect, 2> spectrumChannelRects(int width, int height)
{
    const int split = width / 2;
    return {QRect(16, 0, qMax(1, split - 20), height), QRect(split + 16, 0, qMax(1, width - split - 20), height)};
}
const QImage &SpectrumViewport::image(const analysis::StereoSpectrum &s, const analysis::SpectrumRaster &r,
                                      quint64 page, QSize logical, double dpr, const QString &transform, double top,
                                      double bottom, const Projection &timeAtY)
{
    if (!m_image.isNull() && page == m_page && logical == m_size && dpr == m_dpr && transform == m_transform
        && top == m_top && bottom == m_bottom)
        return m_image;
    m_page = page;
    m_size = logical;
    m_dpr = dpr;
    m_transform = transform;
    m_top = top;
    m_bottom = bottom;
    const QSize physical(qMax(1, int(std::ceil(logical.width() * dpr))),
                         qMax(1, int(std::ceil(logical.height() * dpr))));
    m_image = QImage(physical, QImage::Format_ARGB32_Premultiplied);
    m_image.setDevicePixelRatio(dpr);
    m_image.fill(Qt::transparent);
    ++m_rebuilds;
    const auto rectangles = spectrumChannelRects(logical.width(), logical.height());
    struct Channel
    {
        int left = 0, width = 0, gutter = 0;
        QVector<int> bins;
    };
    std::array<Channel, 2> channels;
    for (int ch = 0; ch < 2; ++ch)
    {
        auto &out = channels[ch];
        out.left = qRound(rectangles[ch].left() * dpr);
        out.width = qMin(physical.width() - out.left, qRound(rectangles[ch].width() * dpr));
        out.gutter = ch ? qRound((logical.width() / 2) * dpr) : 0;
        out.bins.resize(qMax(0, out.width));
        for (int x = 0; x < out.bins.size(); ++x)
            out.bins[x] = qMin(r.left.width() - 1, x * r.left.width() / qMax(1, out.width));
    }
    int previous = -2;
    const double end = s.startSeconds + s.durationSeconds;
    const QRgb peak = qPremultiply(qRgba(70, 187, 171, 190)), rms = qPremultiply(qRgba(204, 232, 159, 220));
    for (int y = 0; y < physical.height(); ++y)
    {
        const double seconds = timeAtY((y + .5) / dpr) / 1000;
        const double position = (seconds - s.startSeconds) / s.hopSeconds;
        int frame = -1;
        if (std::isfinite(position) && seconds >= s.startSeconds && seconds < end && position >= 0
            && position < r.left.height())
            frame = int(std::floor(position));
        auto *target = reinterpret_cast<QRgb *>(m_image.scanLine(y));
        if (frame == previous && y > 0)
        {
            std::memcpy(target, m_image.constScanLine(y - 1), size_t(m_image.bytesPerLine()));
            continue;
        }
        previous = frame;
        if (frame < 0)
            continue;
        const auto &level = s.frames[size_t(frame)];
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto &mapping = channels[ch];
            const auto *source = reinterpret_cast<const QRgb *>((ch ? r.right : r.left).constScanLine(frame));
            for (int x = 0; x < mapping.width; ++x)
                target[mapping.left + x] = source[mapping.bins[x]];
            const int span = qMax(1, qRound(14 * dpr));
            const double v = std::clamp(double(ch ? level.rightPeak : level.leftPeak), 0., 1.);
            const int start = mapping.gutter + qRound(dpr);
            if (std::isfinite(v))
            {
                const int last = qMin(physical.width() - 1, start + qRound(v * span));
                for (int x = start; x <= last; ++x)
                    target[x] = peak;
            }
            const double value = double(ch ? level.rightRms : level.leftRms);
            if (std::isfinite(value))
            {
                const int x = qBound(0, start + qRound(std::clamp(value, 0., 1.) * span), physical.width() - 1);
                target[x] = rms;
            }
        }
    }
    return m_image;
}
