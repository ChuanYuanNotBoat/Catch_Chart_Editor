#pragma once
#include "analysis/SpectrumAnalysis.h"
#include <QString>
#include <functional>
#include <memory>
class QObject;
// Qt audio decoding adapter; no chart or widget dependencies.
class SpectrumService
{
  public:
    using Result = analysis::StereoSpectrum;
    using Callback = std::function<void(Result)>;
    static Result analyzeFileRange(const QString &, double startMs, double durationMs,
                                   const std::shared_ptr<std::atomic<bool>> &cancel);
    static void analyzeFileRangeAsync(QObject *context, const QString &, double startMs, double durationMs,
                                      std::shared_ptr<std::atomic<bool>>, Callback);
};
