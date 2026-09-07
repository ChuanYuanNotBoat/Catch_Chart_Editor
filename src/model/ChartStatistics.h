// src/model/ChartStatistics.h - 谱面统计快照与计算器
#pragma once

#include <QString>
#include <QHash>

class Chart;

/**
 * @brief 谱面统计结果快照。
 *
 * 统计口径：
 * - 音频 note（NoteType::SOUND）不参与任何统计；
 * - 全 note 数 = 常规 note 数 + rain 数量（rain 本体计 1 个）；
 * - 奖励音符数量 = 各 rain 按官方算法生成的奖励 note 总数（与画布预览一致）；
 * - 理论 Max Combo = 常规 note 数 + 奖励音符数量。
 *   rain 本体不重复计入 max combo：rain 在游玩时由奖励 note 构成。
 *   切勿改成 totalNotes + rewardCount。
 */
struct ChartStatistics
{
    int totalNotes = 0;    // 全 note 数（NORMAL + RAIN，不含 SOUND）
    int normalCount = 0;   // 常规 note 数量
    int rainCount = 0;     // rain 数量
    int soundCount = 0;    // 音频 note 数量（仅供参考，不计入任何统计口径）
    int rewardCount = 0;   // rain 奖励音符数量
    int maxCombo = 0;      // 理论 Max Combo = normalCount + rewardCount
    double averageDensity = 0.0; // 可捕获音符/秒
    double peakDensity = 0.0;    // 任意连续 1 秒窗口内的最大数量
    double starRating = 0.0;     // osu!catch Movement difficulty rating
    int hyperdashCount = 0;
    qint64 editTimeMs = 0;
    int editCount = 0;
    int undoCount = 0;
    int redoCount = 0;
    QHash<QString, int> operationCounts;

    bool operator==(const ChartStatistics &other) const
    {
        return totalNotes == other.totalNotes && normalCount == other.normalCount
            && rainCount == other.rainCount && soundCount == other.soundCount
            && rewardCount == other.rewardCount && maxCombo == other.maxCombo
            && qFuzzyCompare(averageDensity + 1.0, other.averageDensity + 1.0)
            && qFuzzyCompare(peakDensity + 1.0, other.peakDensity + 1.0)
            && qFuzzyCompare(starRating + 1.0, other.starRating + 1.0)
            && hyperdashCount == other.hyperdashCount
            && editTimeMs == other.editTimeMs && editCount == other.editCount
            && undoCount == other.undoCount && redoCount == other.redoCount
            && operationCounts == other.operationCounts;
    }

    bool operator!=(const ChartStatistics &other) const { return !(*this == other); }
};

/**
 * @brief 统计计算器：从谱面快照计算 ChartStatistics。
 *
 * 纯查询实现：内部仅调用 RainRewardGenerator::ensureChart() 建立确定性
 * 缓存后读取 dropsFor()，不会破坏官方随机流的确定性与渲染预览一致。
 * 未来新增统计指标时在此扩展 ChartStatistics 字段即可。
 */
class ChartStatsCalculator
{
public:
    // offsetMs 传谱面 meta().offset，保证奖励生成与渲染预览使用同一时基。
    static ChartStatistics compute(const Chart *chart, int offsetMs);
};
