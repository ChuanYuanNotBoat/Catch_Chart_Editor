// src/logic/UnreachableDivisionManager.cpp
// 基于BPM的不可达分度启用功能 — 核心逻辑实现

#include "UnreachableDivisionManager.h"
#include "controller/ChartController.h"
#include "utils/MathUtils.h"
#include "utils/Logger.h"
#include "model/Chart.h"
#include "file/BpmAuxFiles.h"
#include <QSettings>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

// ============================================================
// 常量
// ============================================================
static const char *kSettingsOrg = "MalodyCatchEditor";
static const char *kSettingsApp = "Editor";
static const char *kShowUnreachableKey = "ColorEdit/showUnreachableDivisions";
static constexpr double kEpsilon = 1e-9;
static constexpr double kMaxReasonableTimeMs = 24.0 * 3600.0 * 1000.0; // 24小时
static constexpr double kMaxReasonableBpm = 9999.0;
static constexpr double kMinReasonableBpm = 1.0;

// ============================================================
// 构造/析构
// ============================================================
UnreachableDivisionManager::UnreachableDivisionManager(QObject *parent)
    : QObject(parent)
{
}

UnreachableDivisionManager::~UnreachableDivisionManager() = default;

// ============================================================
// 持久化开关
// ============================================================
bool UnreachableDivisionManager::loadShowUnreachable()
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    return settings.value(kShowUnreachableKey, false).toBool();
}

void UnreachableDivisionManager::saveShowUnreachable(bool enabled)
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.setValue(kShowUnreachableKey, enabled);
}

// ============================================================
// 可达性判定
// ============================================================
bool UnreachableDivisionManager::isBeatReachableForDenominator(double beatFrac, int targetDen)
{
    if (targetDen <= 0)
        return false;
    double ticks = beatFrac * targetDen;
    return std::abs(ticks - std::round(ticks)) < kEpsilon;
}

bool UnreachableDivisionManager::isNoteReachableForDenominator(const Note &note, int targetDen)
{
    if (note.type == NoteType::SOUND)
        return false;
    if (targetDen <= 0)
        return false;

    double beatFrac = static_cast<double>(note.beatNum)
                      + static_cast<double>(note.numerator) / static_cast<double>(note.denominator);
    if (!isBeatReachableForDenominator(beatFrac, targetDen))
        return false;

    if (note.type == NoteType::RAIN)
    {
        double endBeatFrac = static_cast<double>(note.endBeatNum)
                             + static_cast<double>(note.endNumerator) / static_cast<double>(note.endDenominator);
        if (!isBeatReachableForDenominator(endBeatFrac, targetDen))
            return false;
    }

    return true;
}

// ============================================================
// 辅助函数
// ============================================================
double UnreachableDivisionManager::beatToFloat(int beatNum, int numerator, int denominator)
{
    if (denominator <= 0)
        return static_cast<double>(beatNum);
    return static_cast<double>(beatNum) + static_cast<double>(numerator) / static_cast<double>(denominator);
}

double UnreachableDivisionManager::findEffectiveBpm(const QVector<BpmEntry> &bpmList, double beatFrac)
{
    if (bpmList.isEmpty())
        return 120.0; // 默认BPM

    double result = bpmList.first().bpm;
    for (const BpmEntry &entry : bpmList)
    {
        double entryBeat = beatToFloat(entry.beatNum, entry.numerator, entry.denominator);
        if (entryBeat <= beatFrac + kEpsilon)
            result = entry.bpm;
        else
            break;
    }
    return result;
}

// ============================================================
// 前置校验 — BPM列表结构
// ============================================================
TimelineValidationResult UnreachableDivisionManager::validateTimeline(
    const QVector<BpmEntry> &bpmList, int offsetMs)
{
    TimelineValidationResult result;
    result.valid = true;

    // V1: 非空检查
    if (bpmList.isEmpty())
    {
        result.valid = false;
        result.errorMessage = QObject::tr("BPM列表为空，无法执行操作。");
        return result;
    }

    // V2: 第一个BPM entry的beatPos == 0
    {
        double firstBeat = beatToFloat(bpmList.first().beatNum,
                                        bpmList.first().numerator,
                                        bpmList.first().denominator);
        if (std::abs(firstBeat) > kEpsilon)
        {
            result.valid = false;
            result.errorMessage = QObject::tr("第一个BPM点的beat位置不为0（当前值: %1）。").arg(firstBeat, 0, 'f', 6);
            return result;
        }
    }

    // V3: 按beat升序排列 + 无重复beatPos
    for (int i = 1; i < bpmList.size(); ++i)
    {
        double prevBeat = beatToFloat(bpmList[i - 1].beatNum,
                                       bpmList[i - 1].numerator,
                                       bpmList[i - 1].denominator);
        double currBeat = beatToFloat(bpmList[i].beatNum,
                                       bpmList[i].numerator,
                                       bpmList[i].denominator);
        if (currBeat < prevBeat - kEpsilon)
        {
            result.valid = false;
            result.errorMessage = QObject::tr("BPM列表未按beat升序排列（索引%1: beat=%2, 索引%3: beat=%4）。")
                                      .arg(i - 1).arg(prevBeat, 0, 'f', 6)
                                      .arg(i).arg(currBeat, 0, 'f', 6);
            return result;
        }
        if (std::abs(currBeat - prevBeat) < kEpsilon)
        {
            result.valid = false;
            result.errorMessage = QObject::tr("BPM列表存在重复beat位置（beat=%1，索引%2和%3）。")
                                      .arg(currBeat, 0, 'f', 6).arg(i - 1).arg(i);
            return result;
        }
    }

    // V4: buildBpmTimeCache成功且无NaN/Inf
    {
        auto cache = MathUtils::buildBpmTimeCache(bpmList, offsetMs);
        if (cache.isEmpty() && !bpmList.isEmpty())
        {
            result.valid = false;
            result.errorMessage = QObject::tr("BPM时间缓存构建失败。");
            return result;
        }
        for (int i = 0; i < cache.size(); ++i)
        {
            if (std::isnan(cache[i].accumulatedMs) || std::isinf(cache[i].accumulatedMs))
            {
                result.valid = false;
                result.errorMessage = QObject::tr("BPM时间缓存包含NaN/Inf值（索引%1）。").arg(i);
                return result;
            }
            if (std::isnan(cache[i].beatPos) || std::isinf(cache[i].beatPos))
            {
                result.valid = false;
                result.errorMessage = QObject::tr("BPM时间缓存beat位置包含NaN/Inf值（索引%1）。").arg(i);
                return result;
            }
        }
    }

    return result;
}

// ============================================================
// 前置校验 — Note时间一致性
// ============================================================
NoteTimeConsistencyResult UnreachableDivisionManager::validateNoteTimeConsistency(
    const QVector<Note> &notes,
    const QVector<int> &affectedIndices,
    const QVector<BpmEntry> &bpmList,
    int offsetMs)
{
    NoteTimeConsistencyResult result;
    result.consistent = true;

    if (affectedIndices.isEmpty())
        return result;

    auto cache = MathUtils::buildBpmTimeCache(bpmList, offsetMs);
    if (cache.isEmpty())
    {
        result.consistent = false;
        result.errorMessage = QObject::tr("无法构建BPM时间缓存，跳过Note时间一致性校验。");
        return result;
    }

    double prevTimeMs = -std::numeric_limits<double>::max();

    for (int idx : affectedIndices)
    {
        if (idx < 0 || idx >= notes.size())
            continue;

        const Note &note = notes[idx];
        double timeMs = MathUtils::beatToMs(note.beatNum, note.numerator, note.denominator, cache);

        // 有限性检查
        if (std::isnan(timeMs) || std::isinf(timeMs))
        {
            result.consistent = false;
            result.errorMessage = QObject::tr("Note[%1] beat(%2,%3,%4) 计算时间为%5（NaN/Inf）。")
                                      .arg(idx)
                                      .arg(note.beatNum).arg(note.numerator).arg(note.denominator)
                                      .arg(timeMs);
            return result;
        }

        // 合理性检查
        if (timeMs < 0 || timeMs > kMaxReasonableTimeMs)
        {
            result.consistent = false;
            result.errorMessage = QObject::tr("Note[%1] beat(%2,%3,%4) 计算时间%5ms超出合理范围。")
                                      .arg(idx)
                                      .arg(note.beatNum).arg(note.numerator).arg(note.denominator)
                                      .arg(timeMs, 0, 'f', 1);
            return result;
        }

        // 单调性检查
        if (timeMs < prevTimeMs - kEpsilon)
        {
            result.consistent = false;
            result.errorMessage = QObject::tr("Note时间单调性违反：Note[%1]时间%2ms < 前一个Note时间%3ms。")
                                      .arg(idx).arg(timeMs, 0, 'f', 1).arg(prevTimeMs, 0, 'f', 1);
            return result;
        }

        // Rain音符特殊检查
        if (note.type == NoteType::RAIN)
        {
            double endMs = MathUtils::beatToMs(note.endBeatNum, note.endNumerator, note.endDenominator, cache);
            if (std::isnan(endMs) || std::isinf(endMs))
            {
                result.consistent = false;
                result.errorMessage = QObject::tr("Rain Note[%1] 结束时间计算为NaN/Inf。").arg(idx);
                return result;
            }
            if (endMs <= timeMs)
            {
                result.consistent = false;
                result.errorMessage = QObject::tr("Rain Note[%1] 结束时间%2ms <= 起始时间%3ms。")
                                          .arg(idx).arg(endMs, 0, 'f', 1).arg(timeMs, 0, 'f', 1);
                return result;
            }
        }

        prevTimeMs = timeMs;
    }

    return result;
}

// ============================================================
// 最近可达beat查找
// ============================================================
double UnreachableDivisionManager::findNearestReachableBeat(double B, int targetDen, bool preferForward)
{
    int beatNum = static_cast<int>(std::floor(B + kEpsilon));
    if (B < 0 && std::abs(B - beatNum) > kEpsilon)
        beatNum = static_cast<int>(std::floor(B));

    double bestDelta = std::numeric_limits<double>::max();
    double bestBeat = B;

    // 搜索同拍
    for (int y = 0; y < targetDen; ++y)
    {
        double candidate = static_cast<double>(beatNum) + static_cast<double>(y) / static_cast<double>(targetDen);
        double delta = std::abs(candidate - B);
        if (delta < bestDelta - kEpsilon)
        {
            bestDelta = delta;
            bestBeat = candidate;
        }
    }

    // 如果同拍最佳偏差 > 0.5拍，搜索下一拍
    if (bestDelta > 0.5)
    {
        int nextBeat = preferForward ? (beatNum + 1) : (beatNum - 1);
        for (int y = 0; y < targetDen; ++y)
        {
            double candidate = static_cast<double>(nextBeat) + static_cast<double>(y) / static_cast<double>(targetDen);
            double delta = std::abs(candidate - B);
            if (delta < bestDelta - kEpsilon)
            {
                bestDelta = delta;
                bestBeat = candidate;
            }
        }
    }

    return bestBeat;
}

// ============================================================
// BPM插入方案计算
// ============================================================
BpmInsertionPlan UnreachableDivisionManager::computeBpmInsertionPlan(
    const QVector<BpmEntry> &bpmList,
    int noteBeatNum, int noteNumerator, int noteDenominator,
    int targetDen,
    int offsetMs)
{
    BpmInsertionPlan plan;
    plan.valid = false;

    if (bpmList.isEmpty())
    {
        plan.errorMessage = QObject::tr("BPM列表为空。");
        return plan;
    }

    // Note原始beat
    double B = beatToFloat(noteBeatNum, noteNumerator, noteDenominator);

    // 目标beat（最近可达位置）
    double Bp = findNearestReachableBeat(B, targetDen, true);

    // 如果已经是可达的，不需要插入BPM
    if (std::abs(B - Bp) < kEpsilon)
    {
        plan.valid = false;
        plan.errorMessage = QObject::tr("该Note已可达，无需插入BPM。");
        return plan;
    }

    // 原始BPM时间缓存
    auto cache = MathUtils::buildBpmTimeCache(bpmList, offsetMs);
    if (cache.isEmpty())
    {
        plan.errorMessage = QObject::tr("BPM时间缓存构建失败。");
        return plan;
    }

    // T(B): Note原始位置的绝对时间
    double T_B = MathUtils::beatToMs(noteBeatNum, noteNumerator, noteDenominator, cache);

    // T_insert: 调色BPM插入点的beat（floor(B)整数拍）
    int insertBeatNum = static_cast<int>(std::floor(B + kEpsilon));
    if (B < 0 && std::abs(B - insertBeatNum) > kEpsilon)
        insertBeatNum = static_cast<int>(std::floor(B));

    // 检查插入点是否就是第一个BPM（beat=0），如果是，不能在此处插入新BPM
    // 需要用下一整数拍作为插入点
    double insertBeat = static_cast<double>(insertBeatNum);
    if (insertBeatNum == bpmList.first().beatNum
        && bpmList.first().numerator == 0)
    {
        // 插入点与第一个BPM重合，用下一整数拍
        insertBeatNum += 1;
        insertBeat = static_cast<double>(insertBeatNum);
    }

    // T(T_insert): 插入点的绝对时间
    double T_insert = MathUtils::beatToMs(insertBeatNum, 0, 1, cache);

    // 原始BPM在插入点的有效值
    plan.originalBpm = findEffectiveBpm(bpmList, insertBeat);

    // 时间差
    double dt = T_B - T_insert;
    if (std::abs(dt) < kEpsilon)
    {
        plan.errorMessage = QObject::tr("时间差过小，无法计算调色BPM。");
        return plan;
    }

    // 调色BPM公式：BPM_adjust = (B' - T_insert) * 60000 / (T(B) - T(T_insert))
    double bpmAdjust = (Bp - insertBeat) * 60000.0 / dt;

    // 合理性检查
    if (bpmAdjust < kMinReasonableBpm || bpmAdjust > kMaxReasonableBpm)
    {
        plan.errorMessage = QObject::tr("计算出的调色BPM=%1超出合理范围[%2,%3]。")
                                .arg(bpmAdjust, 0, 'f', 2)
                                .arg(kMinReasonableBpm).arg(kMaxReasonableBpm);
        return plan;
    }

    // ====== 补正BPM ======
    // 计算时间漂移 Δt = T(B') - T(B)（用原始BPM计算）
    double T_Bp_original = MathUtils::beatToMs(
        static_cast<int>(std::floor(Bp + kEpsilon)), 0, 1, cache);
    // 更精确：直接用beatToMs计算任意beat的时间
    // beatToMs要求分子/分母形式，对于任意小数我们需要自行计算
    // 使用线性插值：在整数拍之间的线性关系
    int bpBeatNum = static_cast<int>(std::floor(Bp + kEpsilon));
    double bpFrac = Bp - static_cast<double>(bpBeatNum);
    double T_bpBeatStart = MathUtils::beatToMs(bpBeatNum, 0, 1, cache);
    double effBpmForBp = findEffectiveBpm(bpmList, Bp);
    double T_Bp = T_bpBeatStart + bpFrac * 60000.0 / effBpmForBp;

    double deltaTime = T_Bp - T_B;

    // 补正范围：从B'到floor(B')+1
    int fixEndBeat = bpBeatNum + 1;
    double B_fix_end = static_cast<double>(fixEndBeat);

    // 如果漂移过大，扩展到再下一拍
    double T_fixEnd_orig = MathUtils::beatToMs(fixEndBeat, 0, 1, cache);
    double originalSegDuration = T_fixEnd_orig - T_Bp;
    if (std::abs(deltaTime) > originalSegDuration * 0.5 && originalSegDuration > kEpsilon)
    {
        fixEndBeat += 1;
        B_fix_end = static_cast<double>(fixEndBeat);
        T_fixEnd_orig = MathUtils::beatToMs(fixEndBeat, 0, 1, cache);
        originalSegDuration = T_fixEnd_orig - T_Bp;
    }

    // 期望该段时间 = 原始该段时间 - Δt
    double expectedDuration = originalSegDuration - deltaTime;

    if (std::abs(expectedDuration) < kEpsilon)
    {
        plan.errorMessage = QObject::tr("补正段时间差过小，无法计算补正BPM。");
        return plan;
    }

    double bpmCorrect = (B_fix_end - Bp) * 60000.0 / expectedDuration;

    if (bpmCorrect < kMinReasonableBpm || bpmCorrect > kMaxReasonableBpm)
    {
        plan.errorMessage = QObject::tr("计算出的补正BPM=%1超出合理范围[%2,%3]。")
                                .arg(bpmCorrect, 0, 'f', 2)
                                .arg(kMinReasonableBpm).arg(kMaxReasonableBpm);
        return plan;
    }

    // ====== 回正BPM ======
    double bpmRestore = plan.originalBpm;

    // 构建BPM条目
    // 调色BPM
    plan.adjustBpm.beatNum = insertBeatNum;
    plan.adjustBpm.numerator = 0;
    plan.adjustBpm.denominator = 1;
    plan.adjustBpm.bpm = bpmAdjust;

    // 补正BPM（在beat B'处）
    plan.correctBpm.beatNum = bpBeatNum;
    // 将B'的小数部分转换为分子/分母
    plan.correctBpm.numerator = 0;
    plan.correctBpm.denominator = targetDen;
    int roundY = static_cast<int>(std::round(bpFrac * targetDen));
    if (roundY >= targetDen)
        roundY = 0; // 应该进位到下一拍
    if (roundY < 0)
        roundY = 0;
    plan.correctBpm.numerator = roundY;
    plan.correctBpm.denominator = targetDen;
    plan.correctBpm.bpm = bpmCorrect;

    // 回正BPM
    plan.restoreBpm.beatNum = fixEndBeat;
    plan.restoreBpm.numerator = 0;
    plan.restoreBpm.denominator = 1;
    plan.restoreBpm.bpm = bpmRestore;

    plan.valid = true;
    return plan;
}

// ============================================================
// Rain冲突检测
// ============================================================
RainConflictResult UnreachableDivisionManager::checkRainConflicts(
    const QVector<BpmEntry> &newBpms,
    const QVector<Note> &notes,
    const QVector<BpmEntry> &existingBpms,
    int offsetMs)
{
    RainConflictResult result;
    result.hasConflict = false;

    // 构建含新BPM的完整列表用于时间计算
    QVector<BpmEntry> fullBpmList = existingBpms;
    for (const BpmEntry &bpm : newBpms)
        fullBpmList.append(bpm);
    std::sort(fullBpmList.begin(), fullBpmList.end(), [](const BpmEntry &a, const BpmEntry &b) {
        double beatA = static_cast<double>(a.beatNum) + static_cast<double>(a.numerator) / std::max(1, a.denominator);
        double beatB = static_cast<double>(b.beatNum) + static_cast<double>(b.numerator) / std::max(1, b.denominator);
        return beatA < beatB;
    });

    auto cache = MathUtils::buildBpmTimeCache(fullBpmList, offsetMs);

    for (const Note &note : notes)
    {
        if (note.type != NoteType::RAIN)
            continue;

        double rainStartBeat = beatToFloat(note.beatNum, note.numerator, note.denominator);
        double rainEndBeat = beatToFloat(note.endBeatNum, note.endNumerator, note.endDenominator);

        for (const BpmEntry &newBpm : newBpms)
        {
            double newBpmBeat = beatToFloat(newBpm.beatNum, newBpm.numerator, newBpm.denominator);

            // 检查新BPM是否落在Rain区间 [startBeat, endBeat)
            if (newBpmBeat >= rainStartBeat - kEpsilon && newBpmBeat < rainEndBeat - kEpsilon)
            {
                // 检查Rain区间内有效BPM是否为原始BPM
                double originalBpmAtRain = findEffectiveBpm(existingBpms, rainStartBeat);
                if (std::abs(newBpm.bpm - originalBpmAtRain) > kEpsilon)
                {
                    result.hasConflict = true;
                    result.conflictDescription = QObject::tr(
                        "待插入BPM点(beat=%1, bpm=%2)落入Rain音符区间[%3,%4)内，"
                        "且BPM值%5与歌曲原始BPM%6不一致。"
                        "C5约束要求Rain区间内BPM必须为原始BPM。")
                                                     .arg(newBpmBeat, 0, 'f', 6)
                                                     .arg(newBpm.bpm, 0, 'f', 2)
                                                     .arg(rainStartBeat, 0, 'f', 6)
                                                     .arg(rainEndBeat, 0, 'f', 6)
                                                     .arg(newBpm.bpm, 0, 'f', 2)
                                                     .arg(originalBpmAtRain, 0, 'f', 2);
                    return result;
                }
            }
        }
    }

    return result;
}

// ============================================================
// beat位置冲突检测 (C8)
// ============================================================
bool UnreachableDivisionManager::hasBeatConflict(
    const QVector<BpmEntry> &existingBpms,
    int beatNum, int numerator, int denominator,
    QString *outConflictMsg)
{
    for (int i = 0; i < existingBpms.size(); ++i)
    {
        const BpmEntry &entry = existingBpms[i];
        if (entry.beatNum == beatNum
            && entry.numerator == numerator
            && entry.denominator == denominator)
        {
            if (outConflictMsg)
            {
                *outConflictMsg = QObject::tr("beat位置(%1,%2,%3)已存在BPM点（索引%4，bpm=%5），"
                                              "违反C8约束（一个时间点一个BPM）。")
                                      .arg(beatNum).arg(numerator).arg(denominator)
                                      .arg(i).arg(entry.bpm, 0, 'f', 2);
            }
            return true;
        }
    }
    return false;
}

// ============================================================
// 收集受影响的Note索引
// ============================================================
QVector<int> UnreachableDivisionManager::collectAffectedIndices(
    const QVector<Note> &notes,
    const QVector<int> &targetIndices)
{
    if (targetIndices.isEmpty())
        return {};

    // 找到选中Note中最小的beat
    double minBeat = std::numeric_limits<double>::max();
    for (int idx : targetIndices)
    {
        if (idx < 0 || idx >= notes.size())
            continue;
        double beat = beatToFloat(notes[idx].beatNum, notes[idx].numerator, notes[idx].denominator);
        if (beat < minBeat)
            minBeat = beat;
    }

    // 收集所有beat >= minBeat的Note
    QVector<int> affected;
    for (int i = 0; i < notes.size(); ++i)
    {
        if (notes[i].type == NoteType::SOUND)
            continue;
        double beat = beatToFloat(notes[i].beatNum, notes[i].numerator, notes[i].denominator);
        if (beat >= minBeat - kEpsilon)
            affected.append(i);
    }

    // 按beat排序
    std::sort(affected.begin(), affected.end(), [&notes](int a, int b) {
        double beatA = beatToFloat(notes[a].beatNum, notes[a].numerator, notes[a].denominator);
        double beatB = beatToFloat(notes[b].beatNum, notes[b].numerator, notes[b].denominator);
        return beatA < beatB;
    });

    return affected;
}

// ============================================================
// 完整操作执行
// ============================================================
bool UnreachableDivisionManager::applyUnreachableDivision(
    ChartController *controller,
    const QString &chartPath,
    const QVector<int> &targetIndices,
    int targetDen)
{
    m_lastError.clear();

    if (!controller || !controller->chart())
    {
        m_lastError = QObject::tr("ChartController或Chart为空。");
        return false;
    }

    const Chart *chart = controller->chart();
    const QVector<BpmEntry> &bpmList = chart->bpmList();
    const QVector<Note> &notes = chart->notes();
    int offsetMs = chart->meta().offset;

    // ============ Step 1: 前置校验 — Timeline ============
    {
        TimelineValidationResult tv = validateTimeline(bpmList, offsetMs);
        if (!tv.valid)
        {
            m_lastError = QObject::tr("BPM时间轴校验失败：\n%1").arg(tv.errorMessage);
            return false;
        }
    }

    // ============ Step 2: 收集受影响Note ============
    QVector<int> affectedIndices = collectAffectedIndices(notes, targetIndices);
    if (affectedIndices.isEmpty())
    {
        m_lastError = QObject::tr("未找到受影响的Note。");
        return false;
    }

    // ============ Step 3: Note时间一致性校验 ============
    {
        NoteTimeConsistencyResult nc = validateNoteTimeConsistency(notes, affectedIndices, bpmList, offsetMs);
        if (!nc.consistent)
        {
            m_lastError = QObject::tr("Note时间一致性校验失败：\n%1").arg(nc.errorMessage);
            return false;
        }
    }

    // ============ Step 4: 计算BPM插入方案 ============
    // 对每个选中的Note计算BPM方案，取最大的beat（最保守的方案）
    // 实际上只需对第一个选中Note（beat最小的）计算，因为该Note之后的所有Note都在同一调整区间
    int firstTargetIdx = targetIndices.first();
    // 找到beat最小的target
    for (int idx : targetIndices)
    {
        if (idx < 0 || idx >= notes.size())
            continue;
        double beatCur = beatToFloat(notes[firstTargetIdx].beatNum, notes[firstTargetIdx].numerator, notes[firstTargetIdx].denominator);
        double beatNew = beatToFloat(notes[idx].beatNum, notes[idx].numerator, notes[idx].denominator);
        if (beatNew < beatCur)
            firstTargetIdx = idx;
    }

    const Note &targetNote = notes[firstTargetIdx];

    // C5: Rain不可作为目标
    if (targetNote.type == NoteType::RAIN)
    {
        m_lastError = QObject::tr("Rain音符不可作为不可达分度启用的目标（C5约束）。");
        return false;
    }

    BpmInsertionPlan plan = computeBpmInsertionPlan(
        bpmList,
        targetNote.beatNum, targetNote.numerator, targetNote.denominator,
        targetDen,
        offsetMs);

    if (!plan.valid)
    {
        m_lastError = QObject::tr("BPM插入方案计算失败：\n%1").arg(plan.errorMessage);
        return false;
    }

    // ============ Step 5: beat位置冲突检测 (C8) ============
    QVector<BpmEntry> newBpms = {plan.adjustBpm, plan.correctBpm, plan.restoreBpm};

    // 先检查新BPM之间是否有冲突
    for (int i = 0; i < newBpms.size(); ++i)
    {
        for (int j = i + 1; j < newBpms.size(); ++j)
        {
            if (newBpms[i].beatNum == newBpms[j].beatNum
                && newBpms[i].numerator == newBpms[j].numerator
                && newBpms[i].denominator == newBpms[j].denominator)
            {
                m_lastError = QObject::tr("待插入BPM点之间存在beat冲突（均在位置(%1,%2,%3)）。")
                                  .arg(newBpms[i].beatNum)
                                  .arg(newBpms[i].numerator)
                                  .arg(newBpms[i].denominator);
                return false;
            }
        }
    }

    // 检查新BPM与已有BPM的冲突
    for (const BpmEntry &newBpm : newBpms)
    {
        QString conflictMsg;
        if (hasBeatConflict(bpmList, newBpm.beatNum, newBpm.numerator, newBpm.denominator, &conflictMsg))
        {
            m_lastError = QObject::tr("beat位置冲突：\n%1").arg(conflictMsg);
            return false;
        }
    }

    // ============ Step 6: Rain冲突检测 ============
    {
        RainConflictResult rc = checkRainConflicts(newBpms, notes, bpmList, offsetMs);
        if (rc.hasConflict)
        {
            m_lastError = QObject::tr("Rain区间冲突：\n%1").arg(rc.conflictDescription);
            return false;
        }
    }

    // ============ Step 7: 计算Note变化 ============
    // 目标Note的beat变为目标beat
    double originalBeatFrac = beatToFloat(targetNote.beatNum, targetNote.numerator, targetNote.denominator);
    double targetBeatFrac = findNearestReachableBeat(originalBeatFrac, targetDen, true);

    int newBeatNum = static_cast<int>(std::floor(targetBeatFrac + kEpsilon));
    double newFrac = targetBeatFrac - static_cast<double>(newBeatNum);
    int newNumerator = static_cast<int>(std::round(newFrac * targetDen));
    int newDenominator = targetDen;
    if (newNumerator >= newDenominator)
    {
        newNumerator = 0;
        newBeatNum += 1;
    }
    if (newNumerator < 0)
        newNumerator = 0;

    Note newNote = targetNote;
    newNote.beatNum = newBeatNum;
    newNote.numerator = newNumerator;
    newNote.denominator = newDenominator;

    // 对于其他受影响的Note（非目标Note），它们的beat不变——时间线漂移通过BPM补正来修正（C6）

    // ============ Step 8: 构建排除项 ============
    BpmAuxFiles::BpmExcludesData currentExcludes;
    if (!chartPath.isEmpty())
    {
        BpmAuxFiles::loadBpmExcludes(chartPath, currentExcludes);
    }

    BpmAuxFiles::BpmExcludesData newExcludes = currentExcludes;
    // 添加新排除项 — 每个BPM点创建一个零长度排除范围
    for (const BpmEntry &newBpm : newBpms)
    {
        BpmAuxFiles::BpmExcludeRange excl(
            newBpm.beatNum, newBpm.numerator, newBpm.denominator,
            newBpm.beatNum, newBpm.numerator, newBpm.denominator,
            QObject::tr("不可达分度BPM调整"));
        // 检查是否已存在（范围重叠）
        bool exists = false;
        for (const BpmAuxFiles::BpmExcludeRange &existing : newExcludes.excludes)
        {
            if (existing.startBeatNum == excl.startBeatNum
                && existing.startNumerator == excl.startNumerator
                && existing.startDenominator == excl.startDenominator
                && existing.endBeatNum == excl.endBeatNum
                && existing.endNumerator == excl.endNumerator
                && existing.endDenominator == excl.endDenominator)
            {
                exists = true;
                break;
            }
        }
        if (!exists)
            newExcludes.excludes.append(excl);
    }

    // ============ Step 9: 执行原子操作 ============
    // 封装为单个撤销命令（C9）
    controller->applyUnreachableDivisionAtomic(
        newBpms,
        targetNote, newNote,
        currentExcludes, newExcludes,
        chartPath);

    return true;
}