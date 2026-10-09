#pragma once
#include "analysis/SpectrumRaster.h"
#include <QString>
#include <QRect>
#include <array>
#include <functional>
std::array<QRect, 2> spectrumChannelRects(int width, int height);
class SpectrumViewport
{
public:
    using Projection = std::function<double(double)>; // logical y -> audio milliseconds
    void clear()
    {
        m_image = {};
        m_page = 0;
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
    double m_dpr = 0, m_top = 0, m_bottom = 0;
};
