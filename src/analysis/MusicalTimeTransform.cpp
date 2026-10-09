#include "MusicalTimeTransform.h"
#include <algorithm>
#include <cmath>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
namespace analysis
{
void MusicalTimeTransform::setChart(const QVector<MathUtils::BpmCacheEntry> &cache)
{
    m_chart = cache;
    QJsonArray rows;
    for (const auto &entry : cache)
        rows.append(QJsonArray{entry.beatPos, entry.accumulatedMs, entry.bpm});
    m_identity = "chart-time:"
                 + QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(rows).toJson(QJsonDocument::Compact),
                                                                QCryptographicHash::Sha256)
                                           .toHex());
}
double MusicalTimeTransform::audioAtBeat(double beat) const
{
    if (m_chart.isEmpty() || !std::isfinite(beat))
        return 0;
    auto it = std::upper_bound(m_chart.cbegin(), m_chart.cend(), beat, [](double b, const auto &e) {
        return b < e.beatPos;
    });
    if (it != m_chart.cbegin())
        --it;
    return it->bpm > 0 ? it->accumulatedMs + (beat - it->beatPos) * 60000 / it->bpm : it->accumulatedMs;
}
double MusicalTimeTransform::beatAtAudio(double ms) const
{
    if (m_chart.isEmpty() || !std::isfinite(ms))
        return 0;
    auto it = std::upper_bound(m_chart.cbegin(), m_chart.cend(), ms, [](double t, const auto &e) {
        return t < e.accumulatedMs;
    });
    if (it != m_chart.cbegin())
        --it;
    return it->beatPos + (ms - it->accumulatedMs) * it->bpm / 60000;
}
std::optional<double> MusicalTimeTransform::audioAtModelBeat(const AutoTiming2TempoMap &map, double beat)
{
    if (!map.available || !std::isfinite(beat))
        return {};
    for (const auto &s : map.segments)
    {
        if (beat < s.startBeat || beat > s.endBeat || s.endSeconds <= s.startSeconds || s.endBeat <= s.startBeat)
            continue;
        if (!std::isfinite(s.startSeconds) || !std::isfinite(s.endSeconds) || !std::isfinite(s.startBeat)
            || !std::isfinite(s.endBeat) || !std::isfinite(s.phaseLinear) || !std::isfinite(s.phaseQuadratic)
            || !std::isfinite(s.phaseCubic)
            || qAbs(s.phaseLinear + s.phaseQuadratic + s.phaseCubic - (s.endBeat - s.startBeat)) > 1e-6)
            continue;
        auto derivative = [&](double x) {
            return s.phaseLinear + 2 * s.phaseQuadratic * x + 3 * s.phaseCubic * x * x;
        };
        double minimum = qMin(derivative(0), derivative(1));
        if (s.phaseCubic != 0)
        {
            double x = -s.phaseQuadratic / (3 * s.phaseCubic);
            if (x > 0 && x < 1)
                minimum = qMin(minimum, derivative(x));
        }
        if (minimum <= 0)
            continue;
        double lo = 0, hi = 1;
        for (int i = 0; i < 60; ++i)
        {
            double x = (lo + hi) / 2;
            double phase = s.startBeat + x * (s.phaseLinear + x * (s.phaseQuadratic + x * s.phaseCubic));
            if (phase < beat)
                lo = x;
            else
                hi = x;
        }
        return 1000 * (s.startSeconds + (lo + hi) / 2 * (s.endSeconds - s.startSeconds));
    }
    return {};
}
} // namespace analysis
