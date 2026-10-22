#pragma once
#include "SpectrumAnalysis.h"
#include <QImage>
#include <QVector>
namespace analysis
{
struct SpectrumRaster
{
    QImage left, right;
    // Indexed, peak-preserving time pyramid. Shared by chart overlays; no
    // waveform and no per-paint recoloring or full viewport rasterization.
    QVector<QImage> overlay;
    bool valid() const
    {
        return !left.isNull() && !right.isNull();
    }
};
// QImage is CPU data and may be prepared in a worker; no QWidget/QPixmap.
SpectrumRaster prepareSpectrumRaster(const StereoSpectrum &, const std::atomic<bool> *cancel = nullptr);
} // namespace analysis
