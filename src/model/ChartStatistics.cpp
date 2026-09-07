#include "model/ChartStatistics.h"

#include "model/Chart.h"
#include "model/MetaData.h"
#include "model/Note.h"
#include "render/RainRewardGenerator.h"
#include "utils/MathUtils.h"

#include <algorithm>
#include <cmath>

namespace
{
struct CatchObject
{
    double timeMs = 0.0;
    double x = 0.0;
};

QVector<CatchObject> catchObjects(const Chart *chart, int offsetMs)
{
    QVector<CatchObject> objects;
    for (const Note &note : chart->notes())
    {
        if (note.type == NoteType::SOUND)
            continue;
        objects.append({MathUtils::beatToMs(note.beatNum, note.numerator, note.denominator,
                                             chart->bpmList(), offsetMs), static_cast<double>(note.x)});
    }
    std::sort(objects.begin(), objects.end(), [](const CatchObject &a, const CatchObject &b) {
        return a.timeMs < b.timeMs;
    });
    return objects;
}

void calculateCatchDifficulty(const QVector<CatchObject> &objects, ChartStatistics &stats)
{
    if (objects.size() < 2)
        return;

    constexpr double normalizedHalfCatcherWidth = 41.0;
    constexpr double absolutePositioningError = 16.0;
    constexpr double difficultyMultiplier = 4.59;
    constexpr double sectionLength = 750.0;
    constexpr double strainDecayBase = 0.2;
    constexpr double decayWeight = 0.94;

    QVector<double> movement;
    QVector<double> strainTimes;
    QVector<double> sectionPeaks;
    double playerPosition = objects.first().x;
    double currentSectionEnd = std::ceil(objects[1].timeMs / sectionLength) * sectionLength;
    double currentStrain = 0.0;
    double maxStrain = 0.0;
    int direction = 0;

    for (int i = 1; i < objects.size(); ++i)
    {
        const double strainTime = std::max(40.0, objects[i].timeMs - objects[i - 1].timeMs);
        const double target = std::clamp(objects[i].x, playerPosition -
                                             (normalizedHalfCatcherWidth - absolutePositioningError),
                                         playerPosition + (normalizedHalfCatcherWidth - absolutePositioningError));
        const double moved = target - playerPosition;
        playerPosition = target;
        movement.append(moved);
        strainTimes.append(strainTime);

        const double weightedTime = strainTime + 16.0;
        double distanceAddition = std::pow(std::abs(moved), 1.3) / 510.0;
        if (std::abs(moved) > 0.1)
        {
            const int newDirection = moved > 0.0 ? 1 : -1;
            if (direction != 0 && newDirection != direction)
                distanceAddition += 21.0 / std::sqrt(strainTimes[i - 2] + 16.0) *
                                     std::min(50.0, std::abs(movement[i - 2])) / 50.0 * 0.38 *
                                     std::max(1.0 - std::pow(weightedTime / 1000.0, 3.0), 0.0);
            distanceAddition += 12.5 * std::min(std::abs(moved), normalizedHalfCatcherWidth * 2.0) /
                                (normalizedHalfCatcherWidth * 6.0) / std::sqrt(weightedTime);
            direction = newDirection;
        }
        currentStrain = currentStrain * std::pow(strainDecayBase, strainTime / sectionLength) +
                        distanceAddition / weightedTime;
        if (objects[i].timeMs > currentSectionEnd)
        {
            sectionPeaks.append(maxStrain);
            maxStrain = 0.0;
            currentSectionEnd = std::ceil(objects[i].timeMs / sectionLength) * sectionLength;
        }
        maxStrain = std::max(maxStrain, currentStrain);

        const double timeToNext = static_cast<int>(objects[i].timeMs) - static_cast<int>(objects[i - 1].timeMs) - 1000.0 / 60.0 / 4.0;
        const double distance = std::abs(objects[i].x - objects[i - 1].x) - normalizedHalfCatcherWidth;
        if (timeToNext - distance < 0.0)
            ++stats.hyperdashCount;
    }
    sectionPeaks.append(maxStrain);
    std::sort(sectionPeaks.begin(), sectionPeaks.end(), std::greater<double>());
    double weighted = 0.0;
    double weight = 1.0;
    for (double peak : sectionPeaks)
    {
        weighted += peak * weight;
        weight *= decayWeight;
    }
    stats.starRating = std::sqrt(weighted) * difficultyMultiplier;
}
} // namespace

ChartStatistics ChartStatsCalculator::compute(const Chart *chart, int offsetMs)
{
    ChartStatistics stats;
    if (!chart)
        return stats;

    auto &generator = RainRewardGenerator::instance();
    generator.ensureChart(chart->notes(), chart->bpmList(), offsetMs);

    for (const Note &note : chart->notes())
    {
        switch (note.type)
        {
        case NoteType::NORMAL:
            ++stats.normalCount;
            break;
        case NoteType::RAIN:
            ++stats.rainCount;
            // 与 RainRewardGenerator 预览完全一致的奖励数量。
            stats.rewardCount += static_cast<int>(generator.dropsFor(note).size());
            break;
        case NoteType::SOUND:
            // 音频 note 不包含在任何统计口径里。
            ++stats.soundCount;
            break;
        }
    }

    stats.totalNotes = stats.normalCount + stats.rainCount;
    stats.maxCombo = stats.normalCount + stats.rewardCount;

    const QVector<CatchObject> objects = catchObjects(chart, offsetMs);
    if (objects.size() >= 2)
    {
        const double durationMs = objects.last().timeMs - objects.first().timeMs;
        if (durationMs > 0.0)
        {
            stats.averageDensity = objects.size() * 1000.0 / durationMs;
            int end = 0;
            for (int start = 0; start < objects.size(); ++start)
            {
                while (end < objects.size() && objects[end].timeMs - objects[start].timeMs <= 1000.0)
                    ++end;
                stats.peakDensity = std::max(stats.peakDensity, static_cast<double>(end - start));
            }
        }
        calculateCatchDifficulty(objects, stats);
    }
    return stats;
}
