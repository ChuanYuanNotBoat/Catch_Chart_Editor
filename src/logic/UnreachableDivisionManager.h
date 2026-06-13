// src/logic/UnreachableDivisionManager.h
// 基于BPM的不可达分度启用功能 — 核心逻辑类
#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include "model/Note.h"
#include "model/BpmEntry.h"
#include "file/BpmAuxFiles.h"

class ChartController;

/**
 * @brief 前置校验结果
 */
struct TimelineValidationResult
{
    bool valid;
    QString errorMessage;
};

/**
 * @brief Note时间一致性校验结果
 */
struct NoteTimeConsistencyResult
{
    bool consistent;
    QString errorMessage;
};

/**
 * @brief Rain区间冲突检测结果
 */
struct RainConflictResult
{
    bool hasConflict;
    QString conflictDescription;
};

/**
 * @brief BPM插入方案
 */
struct BpmInsertionPlan
{
    BpmEntry adjustBpm;   // 调色BPM
    BpmEntry correctBpm;  // 补正BPM
    BpmEntry restoreBpm;  // 回正BPM（=原歌曲BPM）
    double originalBpm;   // 原始BPM值
    bool valid;
    QString errorMessage;
};

/**
 * @brief 不可达分度管理器
 *
 * 负责：
 * 1. 持久化开关读写
 * 2. 分度可达性判定
 * 3. 前置时间轴一致性校验
 * 4. BPM插入方案计算（基于绝对时间反向求解）
 * 5. Rain区间冲突检测
 * 6. 完整操作执行（含排除项标记）
 */
class UnreachableDivisionManager : public QObject
{
    Q_OBJECT
public:
    explicit UnreachableDivisionManager(QObject *parent = nullptr);
    ~UnreachableDivisionManager();

    // ========== 持久化开关 ==========
    static bool loadShowUnreachable();
    static void saveShowUnreachable(bool enabled);

    // ========== 可达性判定 ==========
    /**
     * @brief 判断某beat是否能精确表示为 [x, y, targetDen]
     */
    static bool isBeatReachableForDenominator(double beatFrac, int targetDen);

    /**
     * @brief 判断指定Note在当前BPM下是否可达
     * 只检查beat本身是否可转换，不涉及BPM可达性
     */
    static bool isNoteReachableForDenominator(const Note &note, int targetDen);

    // ========== 前置校验 ==========

    /**
     * @brief BPM列表结构校验
     * V1: 按beat升序排列
     * V2: 无重复beatPos
     * V3: buildBpmTimeCache成功且无NaN/Inf
     * V4: 第一个BPM entry的beatPos == 0
     */
    static TimelineValidationResult validateTimeline(const QVector<BpmEntry> &bpmList, int offsetMs);

    /**
     * @brief Note时间一致性校验
     * 遍历受影响Note，用MathUtils::beatToMs反算理论时间，
     * 校验单调性、有限性、合理性。
     * @param notes 所有Note
     * @param affectedIndices 受影响的Note索引（选中Note + 所有beat ≥ 选中Note最小beat的Note）
     * @param bpmList 当前BPM列表
     * @param offsetMs 偏移量
     */
    static NoteTimeConsistencyResult validateNoteTimeConsistency(
        const QVector<Note> &notes,
        const QVector<int> &affectedIndices,
        const QVector<BpmEntry> &bpmList,
        int offsetMs);

    /**
     * @brief 找到最近的可达beat（同拍优先，否则顺延下一拍）
     * @param B 原始beat（浮点）
     * @param targetDen 目标分母
     * @param preferForward 是否优先向前顺延
     * @return 最近的可达beat值
     */
    static double findNearestReachableBeat(double B, int targetDen, bool preferForward = true);

    /**
     * @brief 计算BPM插入方案（基于绝对时间反向求解）
     *
     * @param bpmList 当前BPM列表
     * @param noteBeatNum Note的beatNum
     * @param noteNumerator Note的numerator
     * @param noteDenominator Note的denominator
     * @param targetDen 目标分母
     * @param offsetMs 时间偏移量
     * @return BPM插入方案
     */
    static BpmInsertionPlan computeBpmInsertionPlan(
        const QVector<BpmEntry> &bpmList,
        int noteBeatNum, int noteNumerator, int noteDenominator,
        int targetDen,
        int offsetMs);

    // ========== Rain冲突检测 ==========

    /**
     * @brief 检测待插入BPM点是否落入Rain音符区间
     * @param newBpms 待插入的BPM点列表
     * @param notes 所有音符
     * @param existingBpms 已有BPM点（用于构建时间缓存）
     * @param offsetMs 时间偏移
     */
    static RainConflictResult checkRainConflicts(
        const QVector<BpmEntry> &newBpms,
        const QVector<Note> &notes,
        const QVector<BpmEntry> &existingBpms,
        int offsetMs);

    /**
     * @brief 检测BPM点beat位置冲突（C8: 一个时间点一个BPM）
     */
    static bool hasBeatConflict(
        const QVector<BpmEntry> &existingBpms,
        int beatNum, int numerator, int denominator,
        QString *outConflictMsg = nullptr);

    // ========== 完整操作执行 ==========

    /**
     * @brief 收集受影响的Note索引
     * 选中Note + 所有beat >= 选中Note最小beat的Note
     */
    static QVector<int> collectAffectedIndices(
        const QVector<Note> &notes,
        const QVector<int> &targetIndices);

    /**
     * @brief 执行不可达分度应用（原子操作）
     *
     * 完整流程：
     * 1. 前置校验（Timeline + NoteTimeConsistency）
     * 2. 计算BPM插入方案
     * 3. Rain冲突检测
     * 4. beat位置冲突检测
     * 5. 通过ChartController执行原子操作（UnreachableDivisionCommand）
     *
     * @param controller 谱面控制器
     * @param chartPath 谱面文件路径
     * @param targetIndices 选中的Note索引
     * @param targetDen 目标分母
     * @return 成功返回true，失败返回false（错误信息通过errorMessage获取）
     */
    bool applyUnreachableDivision(
        ChartController *controller,
        const QString &chartPath,
        const QVector<int> &targetIndices,
        int targetDen);

    /**
     * @brief 获取最后一次操作的错误信息
     */
    QString lastError() const { return m_lastError; }

private:
    QString m_lastError;

    /**
     * @brief 计算BPM点所在的分母（用于BpmEntry的beatNum/numerator/denominator）
     */
    static double beatToFloat(int beatNum, int numerator, int denominator);

    /**
     * @brief 查找beat对应的当前有效BPM
     */
    static double findEffectiveBpm(const QVector<BpmEntry> &bpmList, double beatFrac);
};