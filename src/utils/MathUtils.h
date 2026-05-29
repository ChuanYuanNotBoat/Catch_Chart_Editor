#pragma once

#include <QVector>
#include "model/Note.h"
#include "model/BpmEntry.h"


class MathUtils
{
public:
    // BPM 时间缓存条目
    struct BpmCacheEntry
    {
        double beatPos;       // 该段起始拍数（浮点）
        double accumulatedMs; // 累计到该段起始的毫秒数（含 offset）
        double bpm;           // 该段 BPM
    };

    // 构建 BPM 累计时间查找表（含 offset）
    static QVector<BpmCacheEntry> buildBpmTimeCache(const QVector<BpmEntry> &bpmList, int offsetMs);

    // 原始 beatToMs（内部临时构建缓存，不推荐频繁调用）
    static double beatToMs(int beatNum, int numerator, int denominator,
                           const QVector<BpmEntry> &bpmList, int offsetMs);

    // 使用预计算缓存的高效 beatToMs（O(log N)）
    static double beatToMs(int beatNum, int numerator, int denominator,
                           const QVector<BpmCacheEntry> &cache);

    static void msToBeat(double ms, const QVector<BpmEntry> &bpmList, int offsetMs,
                         int &outBeatNum, int &outNumerator, int &outDenominator);
    static Note snapNoteToTime(const Note &note, int timeDivision);
    static int snapXToGrid(int x, int gridDivision);
    static bool isSameTime(const Note &a, const Note &b, int timeDivision);

    // 分数拍号转换（避免毫秒）
    static double beatToFloat(int beatNum, int numerator, int denominator);
    static void floatToBeat(double beat, int &beatNum, int &numerator, int &denominator, int maxDenominator = 1024);
    static bool isSameBeat(const Note &a, const Note &b, int timeDivision);
    static Note snapNoteToBeat(const Note &note, int timeDivision);

    // 拍号与像素映射（线性）
    static double beatToPixel(double beat, double scrollBeat, double visibleBeatRange, int height);
    static double pixelToBeat(int y, double scrollBeat, double visibleBeatRange, int height);

    // 边界吸附函数
    static int snapXToBoundary(int x);                                          // X轴边界吸附到0或512
    static Note snapNoteToTimeWithBoundary(const Note &note, int timeDivision); // 时间分度吸附带边界检查
    // 时间对齐网格
    static double snapTimeToGrid(double timeMs, const QVector<BpmEntry> &bpmList, int offset, int timeDivision);
    
    // BPM 测量：从指定时间点开始，测量指定时长内的 BPM
    static double measureBpmFromTime(int startBeatNum, int startNumerator, int startDenominator,
                                     double durationSeconds,
                                     const QVector<BpmEntry> &bpmList, int offsetMs);
};