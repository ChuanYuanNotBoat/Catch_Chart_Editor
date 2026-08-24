// src/utils/MathUtils.cpp
#include "MathUtils.h"
#include "Logger.h"
#include <cmath>
#include <limits>
#include <numeric> // for std::gcd
#include <QDebug>

// 构建 BPM 累计时间缓存（优化1：将 O(N*M) 降为 O(N + M*log M)）
QVector<MathUtils::BpmCacheEntry> MathUtils::buildBpmTimeCache(const QVector<BpmEntry> &bpmList, int offsetMs)
{
    QVector<BpmCacheEntry> cache;
    if (bpmList.isEmpty())
        return cache;

    double accumulated = -offsetMs; // 累计毫秒数（含 offset）
    for (int i = 0; i < bpmList.size(); ++i)
    {
        const BpmEntry &entry = bpmList[i];
        double beatPos = entry.beatNum + static_cast<double>(entry.numerator) / entry.denominator;

        BpmCacheEntry cacheEntry;
        cacheEntry.beatPos = beatPos;
        cacheEntry.accumulatedMs = accumulated;
        cacheEntry.bpm = entry.bpm;
        cache.append(cacheEntry);

        // 计算到下一段的时长，累加到 accumulated（最后一段后面无下一段，无需累加）
        if (i + 1 < bpmList.size())
        {
            const BpmEntry &next = bpmList[i + 1];
            double nextBeatPos = next.beatNum + static_cast<double>(next.numerator) / next.denominator;
            double beatLen = nextBeatPos - beatPos;
            if (entry.bpm > 0)
                accumulated += beatLen * (60000.0 / entry.bpm);
        }
    }
    return cache;
}

// 使用缓存的高效 beatToMs（二分查找）
double MathUtils::beatToMs(int beatNum, int numerator, int denominator,
                           const QVector<BpmCacheEntry> &cache)
{
    if (cache.isEmpty())
        return 0.0;

    double targetBeat = beatNum + static_cast<double>(numerator) / denominator;

    // 二分查找最后一个 beatPos <= targetBeat 的段
    int lo = 0, hi = cache.size() - 1;
    while (lo < hi)
    {
        int mid = (lo + hi + 1) / 2;
        if (cache[mid].beatPos <= targetBeat)
            lo = mid;
        else
            hi = mid - 1;
    }

    const BpmCacheEntry &seg = cache[lo];
    double beatDelta = targetBeat - seg.beatPos;
    if (seg.bpm <= 0)
        return seg.accumulatedMs;

    double ms = seg.accumulatedMs + beatDelta * (60000.0 / seg.bpm);
    return ms;
}

// 原始 beatToMs（保留兼容，内部临时构建缓存）
double MathUtils::beatToMs(int beatNum, int numerator, int denominator,
                           const QVector<BpmEntry> &bpmList, int offsetMs)
{
    try
    {
        if (bpmList.isEmpty())
            return -offsetMs;

        double targetBeat = beatNum + static_cast<double>(numerator) / denominator;

        // 找到第一个 BPM 变化点之前的 BPM
        int idx = 0;
        while (idx + 1 < bpmList.size())
        {
            double nextBeat = bpmList[idx + 1].beatNum +
                              static_cast<double>(bpmList[idx + 1].numerator) / bpmList[idx + 1].denominator;
            if (targetBeat < nextBeat)
                break;
            idx++;
        }

        const BpmEntry &cur = bpmList[idx];
        double curBeat = cur.beatNum + static_cast<double>(cur.numerator) / cur.denominator;

        // 计算从 curBeat 到 targetBeat 的毫秒数
        double beatDelta = targetBeat - curBeat;
        if (cur.bpm <= 0)
        {
            Logger::error(QString("MathUtils::beatToMs - Invalid BPM: %1").arg(cur.bpm));
            return -offsetMs;
        }
        // 每分钟 cur.bpm 拍，所以每拍时长 = 60000 / cur.bpm 毫秒
        double ms = beatDelta * (60000.0 / cur.bpm);

        // 计算之前所有段的累计时间
        double prevMs = -offsetMs;
        for (int i = 0; i < idx; ++i)
        {
            const BpmEntry &prev = bpmList[i];
            double prevBeat = prev.beatNum + static_cast<double>(prev.numerator) / prev.denominator;

            // 下一段的开始beat
            double nextBeat;
            if (i + 1 < bpmList.size())
            {
                const BpmEntry &next = bpmList[i + 1];
                nextBeat = next.beatNum + static_cast<double>(next.numerator) / next.denominator;
            }
            else
            {
                nextBeat = prevBeat; // 不应该发生
            }

            double beatLen = nextBeat - prevBeat;
            if (prev.bpm <= 0)
            {
                Logger::warn(QString("MathUtils::beatToMs - Invalid BPM at index %1: %2").arg(i).arg(prev.bpm));
                continue;
            }
            double segmentMs = beatLen * (60000.0 / prev.bpm);
            prevMs += segmentMs;
        }

        return prevMs + ms;
    }
    catch (const std::exception &e)
    {
        Logger::error(QString("MathUtils::beatToMs - Exception: %1").arg(e.what()));
        return -offsetMs;
    }
    catch (...)
    {
        Logger::error("MathUtils::beatToMs - Unknown exception");
        return -offsetMs;
    }
}

void MathUtils::msToBeat(double ms, const QVector<BpmEntry> &bpmList, int offsetMs,
                         int &outBeatNum, int &outNumerator, int &outDenominator)
{
    if (bpmList.isEmpty())
    {
        outBeatNum = 0;
        outNumerator = 1;
        outDenominator = 1;
        return;
    }

    double elapsed = ms + offsetMs;
    if (elapsed < 0)
    {
        outBeatNum = 0;
        outNumerator = 0;
        outDenominator = 1;
        return;
    }

    int idx = 0;
    double accumulatedMs = 0;
    while (idx < bpmList.size())
    {
        const BpmEntry &cur = bpmList[idx];
        double curBeat = cur.beatNum + static_cast<double>(cur.numerator) / cur.denominator;

        // 当前段时长
        double nextBeat;
        if (idx + 1 < bpmList.size())
        {
            const BpmEntry &next = bpmList[idx + 1];
            nextBeat = next.beatNum + static_cast<double>(next.numerator) / next.denominator;
        }
        else
        {
            nextBeat = curBeat + 1e9; // 无穷大
        }
        double beatLen = nextBeat - curBeat;
        double segmentMs = beatLen * (60000.0 / cur.bpm);

        if (elapsed < accumulatedMs + segmentMs)
        {
            // 落在此段
            double remainMs = elapsed - accumulatedMs;
            double beatOffset = remainMs * (cur.bpm / 60000.0);
            double totalBeat = curBeat + beatOffset;

            // 使用高精度分数近似，最大分母 65536 以保证精度
            floatToBeat(totalBeat, outBeatNum, outNumerator, outDenominator, 65536);
            return;
        }

        accumulatedMs += segmentMs;
        idx++;
    }

    // 超出最后一个 BPM 段（理论上不应发生）
    outBeatNum = bpmList.last().beatNum;
    outNumerator = 1;
    outDenominator = 1;
}

Note MathUtils::snapNoteToTime(const Note &note, int timeDivision)
{
    Note snapped = note;
    // 将时间对齐到 division 的整数倍
    // division 表示 1/division 拍，即每拍分成 division 份
    double beatPos = note.beatNum + static_cast<double>(note.numerator) / note.denominator;
    double divisionBeat = 1.0 / timeDivision;
    double rounded = std::round(beatPos / divisionBeat) * divisionBeat;
    snapped.beatNum = static_cast<int>(rounded);
    double frac = rounded - snapped.beatNum;
    // 近似为最接近的分子/分母
    // 简化：将 frac 转为最简分数，分母不超过 256
    int num = static_cast<int>(std::round(frac * timeDivision));
    int den = timeDivision;
    int gcd = std::gcd(num, den);
    if (gcd > 0)
    {
        num /= gcd;
        den /= gcd;
    }
    snapped.numerator = num;
    snapped.denominator = den;
    if (note.isRain)
    {
        // 同样处理结束时间
        double endBeatPos = note.endBeatNum + static_cast<double>(note.endNumerator) / note.endDenominator;
        double endRounded = std::round(endBeatPos / divisionBeat) * divisionBeat;
        snapped.endBeatNum = static_cast<int>(endRounded);
        double endFrac = endRounded - snapped.endBeatNum;
        int endNum = static_cast<int>(std::round(endFrac * timeDivision));
        int endDen = timeDivision;
        int endGcd = std::gcd(endNum, endDen);
        if (endGcd > 0)
        {
            endNum /= endGcd;
            endDen /= endGcd;
        }
        snapped.endNumerator = endNum;
        snapped.endDenominator = endDen;
    }
    return snapped;
}

double MathUtils::snapTimeToGrid(double timeMs, const QVector<BpmEntry> &bpmList, int offset, int timeDivision)
{
    // 将时间转换为拍号
    int beatNum, numerator, denominator;
    msToBeat(timeMs, bpmList, offset, beatNum, numerator, denominator);
    // 创建临时Note用于吸附
    Note temp;
    temp.beatNum = beatNum;
    temp.numerator = numerator;
    temp.denominator = denominator;
    Note snapped = snapNoteToTime(temp, timeDivision);
    // 转换回时间
    return beatToMs(snapped.beatNum, snapped.numerator, snapped.denominator, bpmList, offset);
}

double MathUtils::beatToFloat(int beatNum, int numerator, int denominator)
{
    if (denominator == 0)
        return static_cast<double>(beatNum); // 防止除零
    return static_cast<double>(beatNum) + static_cast<double>(numerator) / denominator;
}

void MathUtils::floatToBeat(double beat, int &beatNum, int &numerator, int &denominator, int maxDenominator)
{
    beatNum = static_cast<int>(beat);
    double fraction = beat - beatNum;
    if (fraction == 0.0)
    {
        numerator = 0;
        denominator = 1;
        return;
    }
    // 将分数近似为分母不超过maxDenominator的有理数
    double bestError = 1.0;
    int bestNum = 0, bestDen = 1;
    for (int d = 1; d <= maxDenominator; ++d)
    {
        int n = static_cast<int>(std::round(fraction * d));
        if (n > d)
            n = d;
        double err = std::abs(fraction - static_cast<double>(n) / d);
        if (err < bestError)
        {
            bestError = err;
            bestNum = n;
            bestDen = d;
        }
    }
    numerator = bestNum;
    denominator = bestDen;
    // 化简
    int gcd = std::gcd(numerator, denominator);
    if (gcd > 0)
    {
        numerator /= gcd;
        denominator /= gcd;
    }
}

bool MathUtils::isSameBeat(const Note &a, const Note &b, int timeDivision)
{
    // 将两个音符的时间对齐到 timeDivision 后比较
    Note sa = snapNoteToTime(a, timeDivision);
    Note sb = snapNoteToTime(b, timeDivision);
    return sa.beatNum == sb.beatNum &&
           sa.numerator == sb.numerator &&
           sa.denominator == sb.denominator;
}

Note MathUtils::snapNoteToBeat(const Note &note, int timeDivision)
{
    return snapNoteToTime(note, timeDivision);
}

double MathUtils::beatToPixel(double beat, double scrollBeat, double visibleBeatRange, int height)
{
    if (visibleBeatRange <= 0)
        return 0;
    return (beat - scrollBeat) / visibleBeatRange * height;
}

double MathUtils::pixelToBeat(int y, double scrollBeat, double visibleBeatRange, int height)
{
    if (height <= 0)
        return scrollBeat;
    return scrollBeat + (static_cast<double>(y) / height) * visibleBeatRange;
}

int MathUtils::snapXToGrid(int x, int gridDivision)
{
    // gridDivision 是 X 轴分度，将 0-512 分成 gridDivision 格
    double step = 512.0 / gridDivision;
    double rounded = std::round(static_cast<double>(x) / step) * step;
    // 四舍五入到最接近的整数，并限制在 0-512 范围内
    int result = static_cast<int>(std::round(rounded));
    return std::clamp(result, 0, 512);
}

bool MathUtils::isSameTime(const Note &a, const Note &b, int timeDivision)
{
    // 将两个音符的时间对齐到 timeDivision 后比较
    Note sa = snapNoteToTime(a, timeDivision);
    Note sb = snapNoteToTime(b, timeDivision);
    return sa.beatNum == sb.beatNum &&
           sa.numerator == sb.numerator &&
           sa.denominator == sb.denominator;
}

int MathUtils::snapXToBoundary(int x)
{
    // X轴边界吸附：当x接近边界时吸附到0或512
    const int BOUNDARY_THRESHOLD = 20; // 距离边界的阈值
    const int MIN_X = 0;
    const int MAX_X = 512;

    // 先进行边界限制
    if (x < MIN_X)
        return MIN_X;
    if (x > MAX_X)
        return MAX_X;

    // 检查是否接近边界
    if (x < BOUNDARY_THRESHOLD)
    {
        Logger::debug(QString("[MathUtils::snapXToBoundary] x=%1 < threshold %2, snap to 0").arg(x).arg(BOUNDARY_THRESHOLD));
        return MIN_X;
    }
    if (x > MAX_X - BOUNDARY_THRESHOLD)
    {
        Logger::debug(QString("[MathUtils::snapXToBoundary] x=%1 > max-threshold %2, snap to 512").arg(x).arg(MAX_X - BOUNDARY_THRESHOLD));
        return MAX_X;
    }

    Logger::debug(QString("[MathUtils::snapXToBoundary] x=%1 not snapped").arg(x));
    return x; // 不吸附
}

Note MathUtils::snapNoteToTimeWithBoundary(const Note &note, int timeDivision)
{
    // 先进行时间分度吸附
    Note snapped = snapNoteToTime(note, timeDivision);

    // 检查时间边界：时间不能为负
    // 计算总拍数
    double beatPos = snapped.beatNum + static_cast<double>(snapped.numerator) / snapped.denominator;
    if (beatPos < 0)
    {
        // 吸附到最近的正时间分度
        double divisionBeat = 1.0 / timeDivision;
        double rounded = std::ceil(0.0 / divisionBeat) * divisionBeat; // 向上取整到最近的正分度
        snapped.beatNum = static_cast<int>(rounded);
        double frac = rounded - snapped.beatNum;
        int num = static_cast<int>(std::round(frac * timeDivision));
        int den = timeDivision;
        int gcd = std::gcd(num, den);
        if (gcd > 0)
        {
            num /= gcd;
            den /= gcd;
        }
        snapped.numerator = num;
        snapped.denominator = den;
    }

    // 对于Rain音符，还需要检查结束时间边界
    if (note.isRain)
    {
        double endBeatPos = snapped.endBeatNum + static_cast<double>(snapped.endNumerator) / snapped.endDenominator;
        if (endBeatPos < 0)
        {
            // 结束时间不能为负，吸附到最近的正时间分度
            double divisionBeat = 1.0 / timeDivision;
            double endRounded = std::ceil(0.0 / divisionBeat) * divisionBeat;
            snapped.endBeatNum = static_cast<int>(endRounded);
            double endFrac = endRounded - snapped.endBeatNum;
            int endNum = static_cast<int>(std::round(endFrac * timeDivision));
            int endDen = timeDivision;
            int endGcd = std::gcd(endNum, endDen);
            if (endGcd > 0)
            {
                endNum /= endGcd;
                endDen /= endGcd;
            }
            snapped.endNumerator = endNum;
            snapped.endDenominator = endDen;
        }

        // 确保结束时间不小于开始时间
        if (endBeatPos < beatPos)
        {
            // 如果结束时间小于开始时间，将结束时间设置为开始时间
            snapped.endBeatNum = snapped.beatNum;
            snapped.endNumerator = snapped.numerator;
            snapped.endDenominator = snapped.denominator;
        }
    }

    return snapped;
}

double MathUtils::measureBpmFromTime(int startBeatNum, int startNumerator, int startDenominator,
                                     double durationSeconds,
                                     const QVector<BpmEntry> &bpmList, int offsetMs)
{
    if (bpmList.isEmpty() || durationSeconds <= 0)
        return 0.0;

    // 计算起始时间的毫秒数
    double startMs = beatToMs(startBeatNum, startNumerator, startDenominator, bpmList, offsetMs);

    // 计算结束时间的毫秒数
    double endMs = startMs + (durationSeconds * 1000.0);

    // 将结束时间转换为拍号
    int endBeatNum, endNumerator, endDenominator;
    msToBeat(endMs, bpmList, offsetMs, endBeatNum, endNumerator, endDenominator);

    // 计算起始和结束的浮点拍数
    double startBeat = beatToFloat(startBeatNum, startNumerator, startDenominator);
    double endBeat = beatToFloat(endBeatNum, endNumerator, endDenominator);

    // 计算拍数差
    double beatDiff = endBeat - startBeat;

    if (beatDiff <= 0)
        return 0.0;

    // BPM = (拍数 / 时长秒数) * 60
    double bpm = (beatDiff / durationSeconds) * 60.0;

    return bpm;
}

bool MathUtils::quantizeBeatToDivision(double beat, int division,
                                       int &beatNum, int &numerator, int &denominator)
{
    if (!std::isfinite(beat) || division <= 0)
        return false;

    const qint64 ticks = qRound64(beat * static_cast<double>(division));
    qint64 whole = ticks / division;
    qint64 remainder = ticks % division;
    if (remainder < 0)
    {
        remainder += division;
        --whole;
    }
    if (whole < std::numeric_limits<int>::min() || whole > std::numeric_limits<int>::max())
        return false;

    beatNum = static_cast<int>(whole);
    numerator = static_cast<int>(remainder);
    denominator = division;
    return true;
}

bool MathUtils::representBeatWithDivision(double beat, int division,
                                          int &beatNum, int &numerator, int &denominator)
{
    if (!std::isfinite(beat) || division <= 0)
        return false;
    const double scaled = beat * static_cast<double>(division);
    const qint64 nearestTick = qRound64(scaled);
    if (std::abs(scaled - static_cast<double>(nearestTick)) > 1e-7)
        return false;
    return quantizeBeatToDivision(beat, division, beatNum, numerator, denominator);
}
