#pragma once
#include "analysis/SpectrumAnalysis.h"
#include "analysis/SpectrumRaster.h"
#include <QString>
#include <QVector>
#include <functional>
#include <memory>
class QObject;
// Qt audio decoding adapter; no chart or widget dependencies.
class SpectrumService
{
  public:
    using Result = analysis::StereoSpectrum;
    using Callback = std::function<void(Result)>;
    struct Page
    {
        std::shared_ptr<const Result> spectrum;
        analysis::SpectrumRaster raster;
        bool valid() const
        {
            return spectrum && spectrum->valid() && raster.valid();
        }
    };
    using PageCallback = std::function<void(Page)>;
    struct EnergyBin
    {
        double startSeconds = 0, endSeconds = 0, rms = 0, peak = 0;
    };
    struct EnergyEnvelope
    {
        QVector<EnergyBin> bins;
        double durationSeconds = 0;
        QString error;
    };
    using EnergyCallback = std::function<void(EnergyEnvelope)>;
    static EnergyEnvelope analyzeEnergy(const QString &, const std::shared_ptr<std::atomic<bool>> &);
    static void analyzeEnergyAsync(QObject *, const QString &, std::shared_ptr<std::atomic<bool>>, EnergyCallback);
    static Result analyzeFileRange(const QString &, double startMs, double durationMs,
                                   const std::shared_ptr<std::atomic<bool>> &cancel);
    static void analyzeFileRangeAsync(QObject *context, const QString &, double startMs, double durationMs,
                                      std::shared_ptr<std::atomic<bool>>, Callback);
    static void prepareFileRangeAsync(QObject *, const QString &, double startMs, double durationMs,
                                      std::shared_ptr<std::atomic<bool>>, PageCallback);
};
