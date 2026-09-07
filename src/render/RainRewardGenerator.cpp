#include "render/RainRewardGenerator.h"

#include "utils/MathUtils.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace
{
constexpr double kMinimumDurationMs = 1.0e-6;
constexpr double kRewardPeriodMs = 100.0;
constexpr double kEulerGamma = 0.5772156649015329;
constexpr std::uint32_t kSeedXor = 0xCA7CBEA7u;
constexpr std::uint32_t kState0 = 0xF1EA5EEDu;
constexpr std::uint32_t kXScale = 0x201u;
constexpr double kFallbackMsPerBeat = 500.0;

std::uint64_t noteFingerprint(const QVector<Note> &notes)
{
    // QVector storage can be reused after an in-place chart edit. Include the
    // fields used by the official traversal so an edit cannot leave a stale
    // reward cache behind.
    std::uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](std::uint32_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };

    for (const Note &note : notes)
    {
        mix(static_cast<std::uint32_t>(note.beatNum));
        mix(static_cast<std::uint32_t>(note.numerator));
        mix(static_cast<std::uint32_t>(note.denominator));
        mix(static_cast<std::uint32_t>(Note::noteTypeToInt(note.type)));
        mix(static_cast<std::uint32_t>(note.x));
        mix(static_cast<std::uint32_t>(note.endBeatNum));
        mix(static_cast<std::uint32_t>(note.endNumerator));
        mix(static_cast<std::uint32_t>(note.endDenominator));
    }
    return hash;
}

std::uint64_t bpmFingerprint(const QVector<BpmEntry> &bpmList)
{
    std::uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](std::uint32_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };

    for (const BpmEntry &entry : bpmList)
    {
        mix(static_cast<std::uint32_t>(entry.beatNum));
        mix(static_cast<std::uint32_t>(entry.numerator));
        mix(static_cast<std::uint32_t>(entry.denominator));
        std::uint64_t bpmBits = 0;
        static_assert(sizeof bpmBits == sizeof entry.bpm);
        std::memcpy(&bpmBits, &entry.bpm, sizeof bpmBits);
        mix(static_cast<std::uint32_t>(bpmBits));
        mix(static_cast<std::uint32_t>(bpmBits >> 32));
    }
    return hash;
}

int truncateToInt(double value)
{
    if (!std::isfinite(value))
        return 0;

    constexpr double minInt = static_cast<double>(std::numeric_limits<int>::min());
    constexpr double maxInt = static_cast<double>(std::numeric_limits<int>::max());
    if (value <= minInt)
        return std::numeric_limits<int>::min();
    if (value >= maxInt)
        return std::numeric_limits<int>::max();
    return static_cast<int>(value); // cvttsd2si: truncate toward zero
}
}

RainRewardGenerator &RainRewardGenerator::instance()
{
    static RainRewardGenerator generator;
    return generator;
}

std::uint32_t RainRewardGenerator::seedForNoteCount(qsizetype totalInputNoteCount)
{
    const std::uint32_t count = static_cast<std::uint32_t>(totalInputNoteCount);
    const auto converted = static_cast<std::uint32_t>(
        static_cast<double>(count) + kEulerGamma);
    return (count << 16) ^ converted ^ kSeedXor;
}

std::uint32_t RainRewardGenerator::rotateRight(std::uint32_t value, unsigned bits)
{
    bits &= 31u;
    if (bits == 0)
        return value;
    return (value >> bits) | (value << (32u - bits));
}

void RainRewardGenerator::initializeState(std::uint32_t seed,
                                          std::uint32_t state[4])
{
    state[0] = kState0;
    state[1] = seed;
    state[2] = seed;
    state[3] = seed;

    for (int i = 0; i < 20; ++i)
        advanceState(state);
}

void RainRewardGenerator::advanceState(std::uint32_t state[4])
{
    const std::uint32_t q = state[0] - rotateRight(state[1], 5);
    const std::uint32_t t = rotateRight(state[2], 15) ^ state[1];

    const std::uint32_t next0 = t;
    const std::uint32_t next1 = state[2] + state[3];
    const std::uint32_t next2 = state[3] + q;
    const std::uint32_t next3 = q + t;

    state[0] = next0;
    state[1] = next1;
    state[2] = next2;
    state[3] = next3;
}

std::uint32_t RainRewardGenerator::advanceRainState(std::uint32_t state[4])
{
    // 004C33CC..004C3439 keeps the saved state0 slot at [esp+48h] until the
    // Rain loop finishes, but EDI itself is the working state0.  After each
    // iteration EDI contains t and the next iteration uses that t in q.
    // 004C34FB commits the final EDI value back to [esp+48h].
    const std::uint32_t q = state[0] - rotateRight(state[1], 5);
    const std::uint32_t t = rotateRight(state[2], 15) ^ state[1];

    const std::uint32_t next1 = state[2] + state[3];
    const std::uint32_t next2 = state[3] + q;
    const std::uint32_t next3 = q + t;

    state[0] = t;
    state[1] = next1;
    state[2] = next2;
    state[3] = next3;
    return t;
}

std::uint32_t RainRewardGenerator::rawXFromState(const std::uint32_t state[4])
{
    const std::uint64_t product =
        static_cast<std::uint64_t>(state[3]) * kXScale;
    return static_cast<std::uint32_t>(product >> 32);
}

void RainRewardGenerator::invalidate()
{
    m_cache.clear();
    m_dirty = true;
}

void RainRewardGenerator::ensureChart(const QVector<Note> &notes,
                                      const QVector<BpmEntry> &bpmList,
                                      int offsetMs)
{
    const Note *noteData = notes.constData();
    const BpmEntry *bpmData = bpmList.constData();
    const std::uint64_t fingerprint = noteFingerprint(notes);
    const std::uint64_t bpmFingerprintValue = bpmFingerprint(bpmList);
    if (!m_dirty
        && noteData == m_lastNotesData
        && notes.size() == m_lastNotesSize
        && bpmData == m_lastBpmData
        && bpmList.size() == m_lastBpmSize
        && fingerprint == m_lastNotesFingerprint
        && bpmFingerprintValue == m_lastBpmFingerprint
        && offsetMs == m_lastOffsetMs)
    {
        return;
    }

    m_lastNotesData = noteData;
    m_lastNotesSize = notes.size();
    m_lastBpmData = bpmData;
    m_lastBpmSize = bpmList.size();
    m_lastNotesFingerprint = fingerprint;
    m_lastBpmFingerprint = bpmFingerprintValue;
    m_lastOffsetMs = offsetMs;
    m_bpmList = bpmList;
    m_offsetMs = offsetMs;
    rebuild(notes);
}

void RainRewardGenerator::rebuild(const QVector<Note> &notes)
{
    m_cache.clear();
    // Catch's 004C2CC0 receives the gameplay-note vector, not the editor's
    // complete chart array. SOUND entries are handled by the audio path and
    // are absent from that vector; for Rain Lv.14 this is 858 chart entries
    // versus 857 runtime entries. They therefore must not affect the seed.
    qsizetype runtimeNoteCount = 0;
    for (const Note &note : notes)
    {
        if (note.type != NoteType::SOUND)
            ++runtimeNoteCount;
    }
    initializeState(seedForNoteCount(runtimeNoteCount), m_state);

    // 004C2CC0 traverses every gameplay note. Only the Catch rain branch
    // consumes the reward stream; normal notes still contribute to the seed.
    for (const Note &note : notes)
    {
        if (note.type != NoteType::RAIN)
            continue;

        QVector<RainDrop> drops;
        fillDrops(note, drops);
        m_cache.insert(cacheKey(note), drops);
    }
    m_dirty = false;
}

QString RainRewardGenerator::cacheKey(const Note &rain) const
{
    // Deliberately omit x: mirror preview uses the same generated stream and
    // mirrors the already generated horizontal positions at draw time.
    return QStringLiteral("%1:%2:%3:%4:%5:%6:%7")
        .arg(rain.id)
        .arg(rain.beatNum)
        .arg(rain.numerator)
        .arg(rain.denominator)
        .arg(rain.endBeatNum)
        .arg(rain.endNumerator)
        .arg(rain.endDenominator);
}

QVector<RainDrop> RainRewardGenerator::dropsFor(const Note &rain) const
{
    if (rain.type != NoteType::RAIN)
        return {};

    const auto it = m_cache.constFind(cacheKey(rain));
    return it == m_cache.constEnd() ? QVector<RainDrop>{} : it.value();
}

void RainRewardGenerator::fillDrops(const Note &rain, QVector<RainDrop> &out)
{
    const double startBeat = rain.getStartBeat();
    const double endBeat = rain.getEndBeat();
    if (!(endBeat > startBeat))
        return;

    double startMsValue = 0.0;
    double endMsValue = 0.0;
    if (m_bpmList.isEmpty())
    {
        startMsValue = startBeat * kFallbackMsPerBeat;
        endMsValue = endBeat * kFallbackMsPerBeat;
    }
    else
    {
        startMsValue = MathUtils::beatToMs(rain.beatNum, rain.numerator,
                                           rain.denominator, m_bpmList, m_offsetMs);
        endMsValue = MathUtils::beatToMs(rain.endBeatNum, rain.endNumerator,
                                         rain.endDenominator, m_bpmList, m_offsetMs);
    }

    // The game has already materialised both endpoints as signed 32-bit
    // milliseconds before entering this branch. Convert each endpoint first,
    // then subtract, rather than truncating the floating-point duration.
    const int startMs = truncateToInt(startMsValue);
    const int endMs = truncateToInt(endMsValue);
    const int durationMsInt = endMs - startMs;
    const double durationMs = static_cast<double>(durationMsInt);
    if (durationMs < kMinimumDurationMs)
        return;

    // 004C3381..004C33BE:
    // count = int(round(duration / 100.0)); interval = duration / count.
    const int count = static_cast<int>(std::round(durationMs / kRewardPeriodMs));
    if (count <= 0)
        return;

    out.clear();
    out.reserve(count + 1);

    std::uint32_t lastT = m_state[0];
    for (int i = 0; i <= count; ++i)
    {
        lastT = advanceRainState(m_state);

        RainDrop drop;
        drop.rawX = rawXFromState(m_state);
        // 437 stores the complete generated x field (0..512). The Catch
        // gameplay renderer clamps it to that same lane range and multiplies
        // by 1/512; no byte truncation or /255 conversion occurs here.
        drop.xRatio = static_cast<double>(drop.rawX) / 512.0;

        const double exactMs =
            static_cast<double>(startMs) +
            static_cast<double>(i) * durationMs / static_cast<double>(count);
        const int generatedMs = truncateToInt(exactMs);

        int beatNum = 0;
        int numerator = 0;
        int denominator = 1;
        if (m_bpmList.isEmpty())
        {
            drop.beatOffset =
                static_cast<double>(generatedMs) / kFallbackMsPerBeat - startBeat;
        }
        else
        {
            MathUtils::msToBeat(static_cast<double>(generatedMs), m_bpmList,
                                m_offsetMs, beatNum, numerator, denominator);
            const double generatedBeat =
                MathUtils::beatToFloat(beatNum, numerator, denominator);
            drop.beatOffset = generatedBeat - startBeat;
        }

        out.append(drop);
    }

    // 004C34FB: commit the last t only after all rewards of this Rain have
    // been emitted. This is what makes the second reward differ from a
    // normal advanceState() call while keeping the next Rain deterministic.
    m_state[0] = lastT;
}
