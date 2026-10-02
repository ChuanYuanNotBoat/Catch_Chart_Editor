// src/model/Note.h - 音符数据模型，包含普通音符和雨音符

#pragma once

#include "BeatPosition.h"
#include <QUuid>
#include <QString>
#include <cstdint>

/**
 * @brief 音符类型枚举
 *
 * 使用枚举替代整数类型，提高类型安全性
 * 枚举值与谱面文件中的整数类型保持一致：
 * - NORMAL = 0: 普通音符
 * - SOUND = 1: 音效音符
 * - RAIN = 3: Rain音符
 */
enum class NoteType : int
{
    NORMAL = 0, // 普通音符
    SOUND = 1,  // 音效音符
    RAIN = 3    // Rain音符
};

/**
 * @brief 音符结构体
 *
 * 支持三种类型：
 * - 普通音符：type = NoteType::NORMAL，只有起始拍号
 * - Rain音符：type = NoteType::RAIN，有起始和结束拍号
 * - 音效音符：type = NoteType::SOUND，有声音相关属性
 */
struct Note
{
    // 起始拍号（整数拍 + 分子/分母）
    int beatNum;     // 从0开始的整数拍
    int numerator;   // 分子（1-...）
    int denominator; // 分母（1,2,3,4...）

    QString id; // 唯一标识符

    // 音符类型（使用枚举提高类型安全性）
    NoteType type;

    // 横坐标 0-512（普通音符和 rain 共用；音效音符可以为 -1 或其他值）
    int x;

    // 是否为 rain 音符（冗余字段，可通过 type == NoteType::RAIN 判断）
    bool isRain;

    // rain 专属：结束拍号
    int endBeatNum;
    int endNumerator;
    int endDenominator;

    // 音效音符专属属性
    QString sound; // 音效文件名
    int vol;       // 音量（0-100）
    int offset;    // 偏移量（毫秒）

    /**
     * @brief 默认构造函数，生成无效音符
     */
    Note();

    /**
     * @brief 普通音符构造函数
     */
    Note(int beatNum, int numerator, int denominator, int x);

    /**
     * @brief rain 音符构造函数（优化版，减少冗余初始化）
     */
    Note(int startBeatNum, int startNumerator, int startDenominator,
         int endBeatNum, int endNumerator, int endDenominator,
         int x);

    /**
     * @brief 音效音符构造函数
     */
    Note(int beatNum, int numerator, int denominator,
         const QString &sound, int vol, int offset);

    /**
     * @brief 判断 rain 音符是否有效（结束时间不早于开始时间）
     * 扩展验证逻辑，添加更多约束检查
     */
    bool isValidRain() const;

    /**
     * @brief 判断音符是否有效（通用验证）
     */
    bool isValid() const;

    /**
     * @brief 获取音符的起始时间（以拍为单位）
     */
    double getStartBeat() const;

    /**
     * @brief 获取音符的结束时间（以拍为单位）
     */
    double getEndBeat() const;

    // Exact positions for ordering and logical ranges; doubles remain the
    // projection used by rendering/audio, not the entity comparison key.
    BeatPosition startPosition() const { return {beatNum, numerator, denominator}; }
    BeatPosition endPosition() const
    {
        return isRainNote() ? BeatPosition(endBeatNum, endNumerator, endDenominator)
                            : startPosition();
    }

    /**
     * @brief 判断两个音符是否相等（用于撤销/重做比较）
     */
    bool operator==(const Note &other) const;

    /**
     * @brief 判断两个音符是否不相等
     */
    bool operator!=(const Note &other) const;

    // 生成新 ID
    static QString generateId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

    // 类型检查辅助方法
    bool isNormal() const { return type == NoteType::NORMAL; }
    bool isSound() const { return type == NoteType::SOUND; }
    bool isRainNote() const { return type == NoteType::RAIN; }

    // 边界检查辅助方法
    bool isXValid() const;
    bool isTimeValid() const;

    // 静态转换方法（保持与现有代码的兼容性）
    static NoteType intToNoteType(int type);
    static int noteTypeToInt(NoteType type);
};
