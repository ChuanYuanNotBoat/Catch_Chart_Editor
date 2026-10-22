#pragma once
#include "analysis/TimingInterpolation.h"
#include "model/BpmEntry.h"
#include <QVector>
#include <QString>
#include <atomic>
#include <memory>

namespace analysis
{
struct InterpolationPreviewRequest
{
    InterpolationOptions options;
    std::optional<BeatSpan> span, gap;
    BeatTriplet end;
    QVector<BpmEntry> original;
    int offset = 0, mode = 0;
    double duration = 0;
    bool resume = true;
};
struct InterpolationPreviewResult
{
    InterpolationOptions options;
    TimingInterpolation result;
    QVector<BpmEntry> proposal;
    QVector<double> storedBpms;
    double startMs = 0, cacheError = 0, durationResidual = 0;
    int replaced = 0;
    QString failure;
};
InterpolationPreviewResult buildInterpolationPreview(const InterpolationPreviewRequest &,
                                                    const std::shared_ptr<std::atomic<bool>> &cancel);
}
