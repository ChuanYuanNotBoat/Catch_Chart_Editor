#pragma once
#include "utils/MathUtils.h"
#include "audio/AutoTiming2Bridge.h"
#include <optional>
namespace analysis
{
// Chart editing uses this discrete map; candidate models are independent and
// never replace the chart's snap reference.
class MusicalTimeTransform
{
public:
    void setChart(const QVector<MathUtils::BpmCacheEntry> &cache);
    QString identity() const
    {
        return m_identity;
    }
    double audioAtBeat(double beat) const;
    double beatAtAudio(double milliseconds) const;
    static std::optional<double> audioAtModelBeat(const AutoTiming2TempoMap &, double beat);

private:
    QVector<MathUtils::BpmCacheEntry> m_chart;
    QString m_identity;
};
} // namespace analysis
