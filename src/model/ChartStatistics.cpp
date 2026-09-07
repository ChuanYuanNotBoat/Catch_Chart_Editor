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
    double playerPosition = 0.0;
    double distanceMoved = 0.0;
    double exactDistanceMoved = 0.0;
    double distanceToHyperdash = 0.0;
    double strainTime = 40.0;
    bool hyperdash = false;
};

QVector<CatchObject> catchObjects(const Chart *chart, int offsetMs)
{
    QVector<CatchObject> objects;
    // osu!catch difficulty uses CS 3.8 and normalizes positions against the
    // catcher half-width before evaluating movement strain.
    constexpr double catchCs = 3.8;
    constexpr double catcherBaseSize = 106.75;
    constexpr double allowedCatchRange = 0.8;
    const double difficultyRange = (catchCs - 5.0) / 5.0;
    const double catcherScale = 1.0 - 0.7 * difficultyRange;
    const double catcherHalfWidth = catcherBaseSize * catcherScale * allowedCatchRange;
    const double positionScale = 41.0 / (catcherHalfWidth * 0.5);
    auto &generator = RainRewardGenerator::instance();
    for (const Note &note : chart->notes())
    {
        if (note.type == NoteType::SOUND)
            continue;
        const double noteTime = MathUtils::beatToMs(note.beatNum, note.numerator, note.denominator,
                                                     chart->bpmList(), offsetMs);
        if (note.type == NoteType::RAIN)
        {
            for (const RainDrop &drop : generator.dropsFor(note))
            {
                int beatNum = 0;
                int numerator = 0;
                int denominator = 1;
                MathUtils::floatToBeat(MathUtils::beatToFloat(note.beatNum, note.numerator, note.denominator) +
                                           drop.beatOffset,
                                       beatNum, numerator, denominator, 1000000);
                objects.append({MathUtils::beatToMs(beatNum, numerator, denominator,
                                                     chart->bpmList(), offsetMs),
                                static_cast<double>(drop.rawX) * positionScale});
            }
        }
        else
        {
            objects.append({noteTime, static_cast<double>(note.x) * positionScale});
        }
    }
    std::stable_sort(objects.begin(), objects.end(), [](const CatchObject &a, const CatchObject &b) {
        return a.timeMs < b.timeMs;
    });
    return objects;
}

void calculateCatchDifficulty(QVector<CatchObject> &objects, ChartStatistics &stats)
{
    if (objects.size() < 2)
        return;

    constexpr double normalizedHalfCatcherWidth = 41.0;
    constexpr double absolutePositioningError = 16.0;
    constexpr double difficultyMultiplier = 4.59;
    constexpr double sectionLength = 750.0;
    constexpr double strainDecayBase = 0.2;
    constexpr double decayWeight = 0.94;
    constexpr double allowedCatchRange = 0.8;
    constexpr double catchCs = 3.8;
    constexpr double catcherBaseSize = 106.75;

    const double halfCatcherWidth = normalizedHalfCatcherWidth / allowedCatchRange;
    const double difficultyRange = (catchCs - 5.0) / 5.0;
    const double catcherScale = 1.0 - 0.7 * difficultyRange;
    const double catcherHalfWidth = catcherBaseSize * catcherScale * allowedCatchRange * 0.5;
    const double positionScale = normalizedHalfCatcherWidth / catcherHalfWidth;
    const double baseDashSpeed = positionScale;
    const double edgeDashThreshold = 20.0 * positionScale;
    int lastDirection = 0;
    double lastExcess = halfCatcherWidth;
    for (int i = 0; i + 1 < objects.size(); ++i)
    {
        const int thisDirection = objects[i + 1].x > objects[i].x ? 1 : -1;
        const double timeToNext = static_cast<int>(objects[i + 1].timeMs) -
                                  static_cast<int>(objects[i].timeMs) - 1000.0 / 60.0 / 4.0;
        const double distanceToNext = std::abs(objects[i + 1].x - objects[i].x) -
                                      (lastDirection == thisDirection ? lastExcess : halfCatcherWidth);
        const double distanceToHyperdash = timeToNext * baseDashSpeed - distanceToNext;
        objects[i].hyperdash = distanceToHyperdash < 0.0;
        if (objects[i].hyperdash)
            lastExcess = halfCatcherWidth;
        else
        {
            objects[i].distanceToHyperdash = distanceToHyperdash;
            lastExcess = std::clamp(distanceToHyperdash, 0.0, halfCatcherWidth);
        }
        lastDirection = thisDirection;
    }
    stats.hyperdashCount = 0;
    for (const CatchObject &object : objects)
        stats.hyperdashCount += object.hyperdash ? 1 : 0;

    QVector<double> sectionPeaks;
    double currentSectionEnd = std::ceil(objects[1].timeMs / sectionLength) * sectionLength;
    double currentStrain = 0.0;
    double maxStrain = 0.0;
    double lastExactDistanceMoved = 0.0;
    double lastStrainTime = 0.0;
    bool inBuzzSection = false;

    for (int i = 1; i < objects.size(); ++i)
    {
        while (objects[i].timeMs > currentSectionEnd)
        {
            sectionPeaks.append(maxStrain);
            maxStrain = currentStrain * std::pow(strainDecayBase,
                                                   (currentSectionEnd - objects[i - 1].timeMs) / 1000.0);
            currentSectionEnd += sectionLength;
        }

        const double strainTime = std::max(40.0, objects[i].timeMs - objects[i - 1].timeMs);
        const double lastPlayerPosition = i == 1 ? objects[i - 1].x : objects[i - 1].playerPosition;
        const double target = std::clamp(objects[i].x, lastPlayerPosition -
                                             (normalizedHalfCatcherWidth - absolutePositioningError),
                                         lastPlayerPosition + (normalizedHalfCatcherWidth - absolutePositioningError));
        objects[i].distanceMoved = target - lastPlayerPosition;
        objects[i].exactDistanceMoved = objects[i].x - lastPlayerPosition;
        objects[i].strainTime = strainTime;
        // osu!catch resets the catcher position after calculating this
        // object's movement, so the dash distance is not counted as strain.
        objects[i].playerPosition = objects[i - 1].hyperdash ? objects[i].x : target;

        const double weightedTime = strainTime + 13.0 + 3.0;
        double distanceAddition = std::pow(std::abs(objects[i].distanceMoved), 1.3) / 510.0;
        if (std::abs(objects[i].distanceMoved) > 0.1)
        {
            if (i >= 2 && std::abs(objects[i - 1].distanceMoved) > 0.1 &&
                (objects[i].distanceMoved > 0.0) != (objects[i - 1].distanceMoved > 0.0))
            {
                distanceAddition += 21.0 / std::sqrt(objects[i - 1].strainTime + 16.0) *
                                     std::min(50.0, std::abs(objects[i - 1].distanceMoved)) / 50.0 *
                                     std::max(std::min(70.0, std::abs(objects[i - 1].distanceMoved)) / 70.0, 0.38) *
                                     std::max(1.0 - std::pow(weightedTime / 1000.0, 3.0), 0.0);
            }
            distanceAddition += 12.5 * std::min(std::abs(objects[i].distanceMoved), normalizedHalfCatcherWidth * 2.0) /
                                (normalizedHalfCatcherWidth * 6.0) / std::sqrt(weightedTime);

            int linearSpacingCount = 0;
            for (int previous = 1; previous <= std::min(i - 1, 10); ++previous)
            {
                const CatchObject &previousObject = objects[i - previous];
                if ((objects[i].distanceMoved > 0.0) != (previousObject.distanceMoved > 0.0) ||
                    objects[i].distanceMoved == 0.0 || previousObject.distanceMoved == 0.0)
                    break;
                const double currentSpacing = std::abs(objects[i].distanceMoved / strainTime);
                const double previousSpacing = std::abs(previousObject.distanceMoved / previousObject.strainTime);
                if (std::abs(currentSpacing / previousSpacing - 1.0) > 0.05)
                    break;
                ++linearSpacingCount;
            }
            distanceAddition *= std::pow(0.7, linearSpacingCount);
        }

        if (i >= 1 && std::abs(objects[i - 1].distanceToHyperdash) <= edgeDashThreshold && !objects[i - 1].hyperdash)
        {
            const double edgeBonus = 5.7 * ((edgeDashThreshold - objects[i - 1].distanceToHyperdash) / edgeDashThreshold) *
                                     std::pow(std::min(objects[i].strainTime, 265.0) / 265.0, 1.5);
            distanceAddition *= 1.0 + edgeBonus;
        }
        if (std::abs(objects[i].exactDistanceMoved) <= normalizedHalfCatcherWidth * 2.0 &&
            objects[i].exactDistanceMoved == -lastExactDistanceMoved &&
            objects[i].strainTime == lastStrainTime)
        {
            if (inBuzzSection)
                distanceAddition = 0.0;
            else
                inBuzzSection = true;
        }
        else
        {
            inBuzzSection = false;
        }

        currentStrain *= std::pow(strainDecayBase, (objects[i].timeMs - objects[i - 1].timeMs) / 1000.0);
        currentStrain += distanceAddition / weightedTime;
        maxStrain = std::max(maxStrain, currentStrain);
        lastExactDistanceMoved = objects[i].exactDistanceMoved;
        lastStrainTime = objects[i].strainTime;
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

    QVector<CatchObject> objects = catchObjects(chart, offsetMs);
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
