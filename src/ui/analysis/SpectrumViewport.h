#pragma once
#include "analysis/SpectrumRaster.h"
#include <QString>
#include <QRect>
#include <QSize>
#include <QVector>
#include <array>
#include <functional>
#include <memory>
std::array<QRect, 2> spectrumChannelRects(int width, int height);
class SpectrumViewport
{
public:
    using Projection = std::function<double(double)>; // logical y -> audio milliseconds
    struct Source
    {
        std::shared_ptr<const analysis::StereoSpectrum> spectrum;
        analysis::SpectrumRaster raster;
    };
    struct RenderRequest
    {
        std::shared_ptr<const analysis::StereoSpectrum> spectrum;
        analysis::SpectrumRaster raster;
        quint64 page = 0;
        QSize logicalSize;
        double dpr = 1.0;
        QString transform;
        QVector<int> frames;
        QVector<Source> sources;
    };

    static RenderRequest makeRenderRequest(std::shared_ptr<const analysis::StereoSpectrum>,
                                          const analysis::SpectrumRaster &, quint64 page,
                                          QSize logicalSize, double dpr, const QString &transform,
                                          const Projection &, QVector<Source> tiles = {});
    static QImage render(const RenderRequest &);
    bool matches(const RenderRequest &) const;
    bool canDisplayFor(const RenderRequest &) const;
    void setRenderedImage(const RenderRequest &, QImage image);
    const QImage &cachedImage() const
    {
        return m_image;
    }
    void clear()
    {
        m_image = {};
        m_page = 0;
        m_frames.clear();
    }
    const QImage &image(const analysis::StereoSpectrum &, const analysis::SpectrumRaster &, quint64 page,
                        QSize logicalSize, double dpr, const QString &transform, double topMs, double bottomMs,
                        const Projection &);
    quint64 rebuildCount() const
    {
        return m_rebuilds;
    }

private:
    QImage m_image;
    QSize m_size;
    QString m_transform;
    quint64 m_page = 0, m_rebuilds = 0;
    double m_dpr = 0;
    QVector<int> m_frames;
};
