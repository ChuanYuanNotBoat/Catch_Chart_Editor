#pragma once
#include "analysis/SpectrumAnalysis.h"
#include "analysis/SpectrumRaster.h"
#include <QString>
#include <QVector>
#include <QThreadPool>
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
        qint64 memoryBytes() const
        {
            if (!spectrum)
                return 0;
            const auto &s = *spectrum;
            qint64 bytes = qint64(s.leftDb.capacity() + s.rightDb.capacity() + s.frequencies.capacity()) * sizeof(float)
                           + qint64(s.frames.capacity()) * sizeof(analysis::SpectrumFrame)
                           + raster.left.sizeInBytes() + raster.right.sizeInBytes();
            for (const auto &image : raster.overlay)
                bytes += image.sizeInBytes();
            return bytes;
        }
    };
    using PageCallback = std::function<void(Page)>;
    struct CacheStats
    {
        quint64 generatedTiles = 0, memoryHits = 0, diskHits = 0, sharedRequests = 0;
    };
    static CacheStats cacheStats();
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
    static void analyzeEnergyAsync(QObject *, const QString &, std::shared_ptr<std::atomic<bool>>, EnergyCallback,
                                   QThreadPool *pool = nullptr);
    static Result analyzeFileRange(const QString &, double startMs, double durationMs,
                                   const std::shared_ptr<std::atomic<bool>> &cancel, double frameStepSeconds = 0);
    // Bounded source-keyed cache shared by AE and the main editor. Call off the
    // GUI thread; returned arrays/images are immutable implicitly shared data.
    static Page prepareFileRange(const QString &, double startMs, double durationMs,
                                 const std::shared_ptr<std::atomic<bool>> &cancel);
    static void analyzeFileRangeAsync(QObject *context, const QString &, double startMs, double durationMs,
                                      std::shared_ptr<std::atomic<bool>>, Callback);
    static void prepareFileRangeAsync(QObject *, const QString &, double startMs, double durationMs,
                                      std::shared_ptr<std::atomic<bool>>, PageCallback);
    // Both views subscribe to the same immutable stereo blocks. Progress is
    // delivered on context's thread; the final page is assembled off-thread.
    static void prepareFileRangeProgressiveAsync(QObject *, const QString &, double startMs, double durationMs,
        std::shared_ptr<std::atomic<bool>>, PageCallback finished, PageCallback progress,
        double focusStartMs = -1, double focusEndMs = -1);
};
