#include "model/ChartStatistics.h"

#include "model/Chart.h"
#include "model/MetaData.h"
#include "model/Note.h"
#include "render/RainRewardGenerator.h"

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
    return stats;
}
