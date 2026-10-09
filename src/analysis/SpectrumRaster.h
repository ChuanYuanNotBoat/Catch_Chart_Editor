#pragma once
#include "SpectrumAnalysis.h"
#include <QImage>
namespace analysis
{
struct SpectrumRaster
{
    QImage left, right;
    bool valid() const
    {
        return !left.isNull() && !right.isNull();
    }
};
// QImage is CPU data and may be prepared in a worker; no QWidget/QPixmap.
SpectrumRaster prepareSpectrumRaster(const StereoSpectrum &, const std::atomic<bool> *cancel = nullptr);
} // namespace analysis
