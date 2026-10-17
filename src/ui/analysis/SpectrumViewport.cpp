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
    quint64 page, QSize logical, double dpr, const QString &transform, const Projection &timeAtY,
    QVector<Source> tiles)
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
    // Shared fine tiles take precedence over a reduced analysis page. Missing
    // tiles may still use the previous page while new blocks are arriving.
    if (request.spectrum && request.spectrum->valid() && raster.valid())
        tiles.append({request.spectrum, raster});
    for (auto &source : tiles)
        if (source.spectrum && source.spectrum->valid() && source.raster.valid()
            && source.spectrum->frames.size() < 65536 && source.spectrum->hopSeconds > 0)
            request.sources.append(std::move(source));
    for (int y = 0; y < physicalHeight; ++y)
    {
        const double seconds = timeAtY((y + .5) / request.dpr) / 1000.0;
        if (!std::isfinite(seconds))
            continue;
        for (int index = 0; index < request.sources.size(); ++index)
        {
            const auto &source = request.sources[index];
            const auto &data = *source.spectrum;
            const double position = (seconds - data.startSeconds) / data.hopSeconds;
            const int count = qMin(int(data.frames.size()), qMin(source.raster.left.height(), source.raster.right.height()));
            if (seconds >= data.startSeconds && seconds < data.startSeconds + data.durationSeconds
                && position >= 0 && position < count)
            {
                request.frames[y] = (index << 16) | int(std::floor(position));
                break;
            }
        }
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

    if (request.sources.isEmpty() || request.frames.isEmpty())
        return image;
    const auto rectangles = spectrumChannelRects(logical.width(), logical.height());
    struct Channel
    {
        int left = 0, width = 0, gutter = 0;
        QVector<int> bins;
    };
    struct PreparedSource
    {
        std::array<Channel, 2> channels;
        std::array<QList<QRgb>, 2> palettes;
    };
    QVector<PreparedSource> prepared(request.sources.size());
    for (int index = 0; index < request.sources.size(); ++index)
    {
        const auto &r = request.sources[index].raster;
        auto &channels = prepared[index].channels;
        prepared[index].palettes = {r.left.colorTable(), r.right.colorTable()};
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
    }

    int previous = -2;
    const int rowCount = qMin(physical.height(), request.frames.size());
    const QRgb peak = qPremultiply(qRgba(70, 187, 171, 190));
    const QRgb rms = qPremultiply(qRgba(204, 232, 159, 220));
    for (int y = 0; y < rowCount; ++y)
    {
        const int encoded = request.frames[y];
        auto *target = reinterpret_cast<QRgb *>(image.scanLine(y));
        if (encoded == previous && y > 0)
        {
            std::memcpy(target, image.constScanLine(y - 1), size_t(image.bytesPerLine()));
            continue;
        }
        previous = encoded;
        if (encoded < 0 || (encoded >> 16) >= request.sources.size())
            continue;
        const int index = encoded >> 16, frame = encoded & 65535;
        const auto &s = *request.sources[index].spectrum;
        const auto &r = request.sources[index].raster;
        const auto &channels = prepared[index].channels;
        const auto &palettes = prepared[index].palettes;
        if (size_t(frame) >= s.frames.size())
            continue;

        const auto &level = s.frames[size_t(frame)];
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto &mapping = channels[ch];
            const QImage &channelRaster = ch ? r.right : r.left;
            if (frame >= channelRaster.height())
                continue;
            if (channelRaster.format() == QImage::Format_Indexed8)
            {
                const auto *source = channelRaster.constScanLine(frame);
                const auto &palette = palettes[ch];
                for (int x = 0; x < mapping.width; ++x)
                    target[mapping.left + x] = palette[source[mapping.bins[x]]];
            }
            else
            {
                const auto *source = reinterpret_cast<const QRgb *>(channelRaster.constScanLine(frame));
                for (int x = 0; x < mapping.width; ++x)
                    target[mapping.left + x] = source[mapping.bins[x]];
            }
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
    // Keep the last coherent viewport while another shared block arrives.
    // Source replacement explicitly clears the viewport in AnalysisCanvas.
    return !m_image.isNull() && request.logicalSize == m_size
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
