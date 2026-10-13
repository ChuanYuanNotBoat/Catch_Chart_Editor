#include "SpectrumViewport.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

std::array<QRect, 2> spectrumChannelRects(int width, int height)
{
    const int split = width / 2;
    return {QRect(16, 0, qMax(1, split - 20), height), QRect(split + 16, 0, qMax(1, width - split - 20), height)};
}

SpectrumViewport::RenderRequest SpectrumViewport::makeRenderRequest(
    std::shared_ptr<const analysis::StereoSpectrum> spectrum, const analysis::SpectrumRaster &raster,
    quint64 page, QSize logical, double dpr, const QString &transform, const Projection &timeAtY)
{
    RenderRequest request;
    request.spectrum = std::move(spectrum);
    request.raster = raster;
    request.page = page;
    request.logicalSize = logical;
    request.dpr = std::isfinite(dpr) && dpr > 0 ? dpr : 1.0;
    request.transform = transform;

    const int physicalHeight = qMax(1, int(std::ceil(logical.height() * request.dpr)));
    request.frames.fill(-1, physicalHeight);
    if (!request.spectrum || !request.spectrum->valid() || !raster.valid()
        || !std::isfinite(request.spectrum->hopSeconds) || request.spectrum->hopSeconds <= 0)
        return request;

    const auto &data = *request.spectrum;
    const double end = data.startSeconds + data.durationSeconds;
    const int frameCount = qMin(int(data.frames.size()), qMin(raster.left.height(), raster.right.height()));
    for (int y = 0; y < physicalHeight; ++y)
    {
        const double seconds = timeAtY((y + .5) / request.dpr) / 1000.0;
        const double position = (seconds - data.startSeconds) / data.hopSeconds;
        if (std::isfinite(position) && seconds >= data.startSeconds && seconds < end && position >= 0
            && position < frameCount)
            request.frames[y] = int(std::floor(position));
    }
    return request;
}

QImage SpectrumViewport::render(const RenderRequest &request)
{
    const QSize logical = request.logicalSize;
    const double dpr = std::isfinite(request.dpr) && request.dpr > 0 ? request.dpr : 1.0;
    const QSize physical(qMax(1, int(std::ceil(logical.width() * dpr))),
                         qMax(1, int(std::ceil(logical.height() * dpr))));
    QImage image(physical, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);

    if (!request.spectrum || !request.spectrum->valid() || !request.raster.valid()
        || request.frames.isEmpty())
        return image;

    const auto &s = *request.spectrum;
    const auto &r = request.raster;
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
        out.width = qMax(0, qMin(physical.width() - out.left, qRound(rectangles[ch].width() * dpr)));
        out.gutter = ch ? qRound((logical.width() / 2) * dpr) : 0;
        out.bins.resize(out.width);
        for (int x = 0; x < out.bins.size(); ++x)
            out.bins[x] = qMin(r.left.width() - 1, x * r.left.width() / qMax(1, out.width));
    }

    int previous = -2;
    const int rowCount = qMin(physical.height(), request.frames.size());
    const QRgb peak = qPremultiply(qRgba(70, 187, 171, 190));
    const QRgb rms = qPremultiply(qRgba(204, 232, 159, 220));
    for (int y = 0; y < rowCount; ++y)
    {
        const int frame = request.frames[y];
        auto *target = reinterpret_cast<QRgb *>(image.scanLine(y));
        if (frame == previous && y > 0)
        {
            std::memcpy(target, image.constScanLine(y - 1), size_t(image.bytesPerLine()));
            continue;
        }
        previous = frame;
        if (frame < 0 || size_t(frame) >= s.frames.size())
            continue;

        const auto &level = s.frames[size_t(frame)];
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto &mapping = channels[ch];
            const QImage &channelRaster = ch ? r.right : r.left;
            if (frame >= channelRaster.height())
                continue;
            const auto *source = reinterpret_cast<const QRgb *>(channelRaster.constScanLine(frame));
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
    return image;
}

bool SpectrumViewport::matches(const RenderRequest &request) const
{
    return !m_image.isNull() && request.page == m_page && request.logicalSize == m_size
           && request.dpr == m_dpr && request.transform == m_transform && request.frames == m_frames;
}

bool SpectrumViewport::canDisplayFor(const RenderRequest &request) const
{
    return !m_image.isNull() && request.page == m_page && request.logicalSize == m_size
           && request.dpr == m_dpr && request.transform == m_transform;
}

void SpectrumViewport::setRenderedImage(const RenderRequest &request, QImage image)
{
    m_image = std::move(image);
    m_size = request.logicalSize;
    m_transform = request.transform;
    m_page = request.page;
    m_dpr = request.dpr;
    m_frames = request.frames;
    ++m_rebuilds;
}

const QImage &SpectrumViewport::image(const analysis::StereoSpectrum &s, const analysis::SpectrumRaster &r,
                                      quint64 page, QSize logical, double dpr, const QString &transform, double top,
                                      double bottom, const Projection &timeAtY)
{
    Q_UNUSED(top);
    Q_UNUSED(bottom);
    auto borrowedSpectrum = std::shared_ptr<const analysis::StereoSpectrum>(
        &s, [](const analysis::StereoSpectrum *) {});
    const auto request = makeRenderRequest(std::move(borrowedSpectrum), r, page, logical, dpr, transform, timeAtY);
    if (!matches(request))
        setRenderedImage(request, render(request));
    return m_image;
}
