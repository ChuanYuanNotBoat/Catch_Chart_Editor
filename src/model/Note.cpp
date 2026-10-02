// src/model/Note.cpp
#include "Note.h"
#include <cmath>
#include <algorithm>

Note::Note()
    : beatNum(0), numerator(1), denominator(1), type(NoteType::NORMAL), x(256), isRain(false),
      endBeatNum(0), endNumerator(1), endDenominator(1), vol(100), offset(0)
{
    id = generateId();
}

Note::Note(int beatNum, int numerator, int denominator, int x)
    : beatNum(beatNum), numerator(numerator), denominator(denominator),
      type(NoteType::NORMAL), x(x), isRain(false),
      endBeatNum(beatNum), endNumerator(numerator), endDenominator(denominator),
      vol(100), offset(0)
{
    id = generateId();
}

Note::Note(int startBeatNum, int startNumerator, int startDenominator,
           int endBeatNum, int endNumerator, int endDenominator, int x)
    : beatNum(startBeatNum), numerator(startNumerator), denominator(startDenominator),
      type(NoteType::RAIN), x(x), isRain(true),
      endBeatNum(endBeatNum), endNumerator(endNumerator), endDenominator(endDenominator),
      vol(100), offset(0)
{
    id = generateId();
}

Note::Note(int beatNum, int numerator, int denominator,
           const QString &sound, int vol, int offset)
    : beatNum(beatNum), numerator(numerator), denominator(denominator),
      type(NoteType::SOUND), x(-1), isRain(false),
      endBeatNum(beatNum), endNumerator(numerator), endDenominator(denominator),
      sound(sound), vol(vol), offset(offset)
{
    id = generateId();
}

bool Note::isValidRain() const
{
    if (type != NoteType::RAIN)
        return false;

    return isTimeValid() && isXValid();
}

bool Note::isValid() const
{
    // Validate the timeline before any type-specific use of the beat triplet.
    if (!isTimeValid())
        return false;

    // 类型特定验证
    switch (type)
    {
    case NoteType::NORMAL:
        return x >= 0 && x <= 512;

    case NoteType::SOUND:
        return !sound.isEmpty() &&
               vol >= 0 && vol <= 100; // offset不需要特别校验范围

    case NoteType::RAIN:
        return isValidRain();

    default:
        return false; // 未知类型
    }
}

double Note::getStartBeat() const
{
    return startPosition().toDouble();
}

double Note::getEndBeat() const
{
    if (type == NoteType::RAIN)
    {
        return endPosition().toDouble();
    }
    // 非rain音符的结束时间等于开始时间
    return getStartBeat();
}

bool Note::operator==(const Note &other) const
{
    // 比较时忽略 id，只比较内容
    if (beatNum != other.beatNum ||
        numerator != other.numerator ||
        denominator != other.denominator ||
        type != other.type ||
        isRain != other.isRain)
    {
        return false;
    }

    // 类型特定比较
    switch (type)
    {
    case NoteType::SOUND:
        // 音效音符比较
        return sound == other.sound &&
               vol == other.vol &&
               offset == other.offset;

    case NoteType::RAIN:
        // Rain 音符比较
        return x == other.x &&
               endBeatNum == other.endBeatNum &&
               endNumerator == other.endNumerator &&
               endDenominator == other.endDenominator;

    case NoteType::NORMAL:
        // 普通音符比较
        return x == other.x;

    default:
        return false;
    }
}

bool Note::operator!=(const Note &other) const
{
    return !(*this == other);
}

bool Note::isXValid() const
{
    if (type == NoteType::SOUND)
    {
        // 音效音符的x可以为-1或其他特殊值
        return true;
    }
    return x >= 0 && x <= 512;
}

bool Note::isTimeValid() const
{
    // 检查分母不为零
    if (denominator == 0)
        return false;

    // 检查分子分母的有效性
    if (numerator < 0 || denominator < 0)
        return false;

    // 对于rain音符，还需要检查结束时间
    if (type == NoteType::RAIN)
    {
        if (endDenominator == 0)
            return false;
        if (endNumerator < 0 || endDenominator < 0)
            return false;

        // 检查结束时间不早于开始时间
        return endPosition() >= startPosition();
    }

    return true;
}

NoteType Note::intToNoteType(int type)
{
    switch (type)
    {
    case 0:
        return NoteType::NORMAL;
    case 1:
        return NoteType::SOUND;
    case 3:
        return NoteType::RAIN;
    default:
        return NoteType::NORMAL; // 默认值
    }
}

int Note::noteTypeToInt(NoteType type)
{
    switch (type)
    {
    case NoteType::NORMAL:
        return 0;
    case NoteType::SOUND:
        return 1;
    case NoteType::RAIN:
        return 3;
    default:
        return 0;
    }
}
